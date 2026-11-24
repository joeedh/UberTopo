#pragma once

/* Editor<C>: transactions, captures, rollback, journal, propagation and
 * queries. Template definitions, included only by editor.cc (which
 * instantiates Editor for every coordinate type). */

#include <algorithm>
#include <cstring>
#include <unordered_set>

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::topo {

namespace edit_detail {

inline uint64_t cap_key(uint8_t g, uint32_t row)
{
  return (uint64_t(g) << 32) | row;
}

/** f(domain) for the allocating domain of @p g (FaceDomain for faces). */
template <geom::CoordType C, typename F> void with_domain(Complex<C> &k, Grp g, F &&f)
{
  switch (g) {
    case Grp::Verts:
      f(k.verts);
      break;
    case Grp::Edges:
      f(k.edges);
      break;
    case Grp::Coedges:
      f(k.coedges);
      break;
    case Grp::Loops:
      f(k.loops);
      break;
    case Grp::Faces:
      f(k.faces);
      break;
    case Grp::Shells:
      f(k.shells);
      break;
    case Grp::Regions:
      f(k.regions);
      break;
    case Grp::Sides:
    case Grp::Global:
      break;
  }
}

inline uint32_t row_bytes(const litestl::attr::AttrGroup &g)
{
  uint32_t n = 0;
  for (uint32_t c = 0; c < g.column_count(); c++) {
    n += g.column(c).elem_size();
  }
  return n;
}

inline int id_slot(Grp g)
{
  return g == Grp::Verts ? 0 : g == Grp::Edges ? 1 : g == Grp::Faces ? 2 : -1;
}

} // namespace edit_detail

/* ------------------------------------------------------------------------ */
/* construction, transactions                                               */
/* ------------------------------------------------------------------------ */

template <geom::CoordType C>
Editor<C>::Editor(Complex<C> &k, Journal<C> *journal, const PropagationRules *rules, EditorOptions opt)
    : k_(k), journal_(journal), rules_(rules), opt_(opt)
{
}

template <geom::CoordType C> Editor<C>::~Editor()
{
  while (!sp_.empty()) {
    rollback();
  }
}

template <geom::CoordType C> EditCode Editor<C>::begin(std::string_view label)
{
  const EditCode c = begin_internal(label);
  rep_ = EditReport{};
  rep_.code = c;
  return c;
}

template <geom::CoordType C> EditCode Editor<C>::begin_internal(std::string_view label)
{
  if (sp_.empty()) {
    if (k_.frozen()) {
      return EditCode::Frozen;
    }
    check_index_stamp();
    /* writes outside the editor since the last journaled step: the journal
     * can no longer undo exactly across them */
    if (journal_ && (!journal_->done_.empty() || !journal_->undone_.empty()) &&
        k_.version() != journal_->expected_version_)
    {
      journal_->clear();
    }
    if (journal_) {
      journal_->expected_version_ = k_.version();
    }
    log_ = TxnLog{};
    ops_.clear();
    lineage_.clear();
    label_ = std::string(label);
    captured_.clear();
    for (auto &t : txn_touched_) {
      t.clear();
    }
    for (uint32_t g = 0; g < kGroupCount; g++) {
      const litestl::attr::AttrGroup &gr = group_of(k_, Grp(g));
      log_.start_size[g] = gr.size();
      start_schema_[g] = gr.schema_version();
    }
    log_.regions_dirty = k_.regions_dirty;
  }
  Savepoint s;
  s.rows = uint32_t(log_.rows.size());
  s.bytes = uint32_t(log_.bytes.size());
  s.alloc = uint32_t(log_.alloc.size());
  s.ops = uint32_t(ops_.size());
  s.lineage = uint32_t(lineage_.size());
  s.epoch = ++epoch_;
  for (uint32_t g = 0; g < kGroupCount; g++) {
    s.size[g] = group_of(k_, Grp(g)).size();
  }
  s.regions_dirty = k_.regions_dirty;
  sp_.push_back(s);
  return EditCode::Ok;
}

template <geom::CoordType C> EditCode Editor<C>::commit()
{
  if (sp_.empty()) {
    rep_.code = EditCode::NoTransaction;
    return EditCode::NoTransaction;
  }
  if (sp_.size() > 1) {
    sp_.pop_back();
    return EditCode::Ok;
  }
  if (opt_.validate == ValidateMode::OnCommit && !redoing_ && !barrier_snapshot_) {
    const EditCode v = validate_now(txn_touched_);
    if (v != EditCode::Ok) {
      const EditReport keep = rep_;
      rollback();
      rep_ = keep;
      return v;
    }
  }
  return commit_internal(redoing_);
}

template <geom::CoordType C> EditCode Editor<C>::commit_internal(bool from_redo)
{
  if (!barrier_snapshot_) {
    for (uint32_t g = 0; g < kGroupCount; g++) {
      if (group_of(k_, Grp(g)).schema_version() != start_schema_[g]) {
        rollback();
        rep_.code = EditCode::SchemaChanged;
        return EditCode::SchemaChanged;
      }
    }
  }
  sp_.pop_back();
  if (journal_ && (!ops_.empty() || log_.rows.size() || log_.alloc.size() || barrier_snapshot_)) {
    Txn<C> t;
    t.label = std::move(label_);
    t.ops = std::move(ops_);
    t.lineage = std::move(lineage_);
    /* keep the first (oldest) capture of each row */
    std::unordered_set<uint64_t> seen;
    t.log.alloc = std::move(log_.alloc);
    for (uint32_t g = 0; g < kGroupCount; g++) {
      t.log.start_size[g] = log_.start_size[g];
    }
    t.log.regions_dirty = log_.regions_dirty;
    for (const RowCapture &rc : log_.rows) {
      if (!seen.insert(edit_detail::cap_key(rc.g, rc.row)).second) {
        continue;
      }
      const uint32_t n = edit_detail::row_bytes(group_of(k_, Grp(rc.g)));
      RowCapture out = rc;
      out.off = uint32_t(t.log.bytes.size());
      t.log.rows.append(out);
      for (uint32_t i = 0; i < n; i++) {
        t.log.bytes.append(log_.bytes[int(rc.off + i)]);
      }
    }
    t.snapshot = std::move(barrier_snapshot_);
    if (opt_.verify_redo) {
      t.post_hash = content_hash(k_);
    }
    for (uint32_t g = 0; g < kGroupCount; g++) {
      const litestl::attr::AttrGroup &gr = group_of(k_, Grp(g));
      t.schema_ver[g] = gr.schema_version();
      uint32_t off = 0;
      for (uint32_t c = 0; c < gr.column_count(); c++) {
        if (gr.column(c).holds_refs()) {
          t.ref_cols[g].push_back(RefCol{c, off, std::string(gr.name(c)), &gr.column(c).type()});
        }
        off += gr.column(c).elem_size();
      }
    }
    journal_->done_.push_back(std::move(t));
    if (!from_redo) {
      journal_->undone_.clear();
    }
  }
  if (journal_) {
    journal_->expected_version_ = k_.version();
  }
  log_ = TxnLog{};
  ops_.clear();
  lineage_.clear();
  captured_.clear();
  barrier_snapshot_.reset();
  sync_index_stamp();
  return EditCode::Ok;
}

template <geom::CoordType C> EditCode Editor<C>::rollback()
{
  if (sp_.empty()) {
    return EditCode::NoTransaction;
  }
  const Savepoint s = sp_.back();
  sp_.pop_back();
  restore(log_, s.rows, s.alloc, s.size, s.regions_dirty);
  log_.bytes.resize(int(s.bytes));
  ops_.erase(ops_.begin() + s.ops, ops_.end());
  lineage_.erase(lineage_.begin() + s.lineage, lineage_.end());
  if (sp_.empty()) {
    if (barrier_snapshot_) {
      restore_snapshot(std::move(*barrier_snapshot_));
      barrier_snapshot_.reset();
      /* a failed rebuild_regions rolls back before it touches the index or
       * the grid (plan 2026-11-22 §2.2): the snapshot alone restores it */
      if (barrier_kind_ != OpKind::RebuildRegions) {
        idx_.invalidate();
        idx_.flush(k_); /* eagerly, inside the barrier (C13) */
        grid_.rebuild_if_used(k_);
      }
    }
    log_ = TxnLog{};
    captured_.clear();
    /* the restored state is the journal's state again (begin_internal synced it) */
    if (journal_) {
      journal_->expected_version_ = k_.version();
    }
  }
  else {
    rebuild_captured();
  }
  sync_index_stamp();
  return EditCode::Ok;
}

template <geom::CoordType C>
void Editor<C>::restore(TxnLog &log, uint32_t rows_mark, uint32_t alloc_mark, const uint32_t *sizes, bool regions_dirty)
{
  for (uint32_t i = uint32_t(log.rows.size()); i-- > rows_mark;) {
    const RowCapture &rc = log.rows[int(i)];
    litestl::attr::AttrGroup &g = group_of(k_, Grp(rc.g));
    idx_.mark(Grp(rc.g), rc.row);
    grid_.mark(Grp(rc.g), rc.row);
    const uint8_t *p = log.bytes.data() + rc.off;
    for (uint32_t c = 0; c < g.column_count(); c++) {
      litestl::attr::ColumnBase &col = g.column(c);
      col.set_bytes(rc.row, p);
      p += col.elem_size();
    }
  }
  for (uint32_t i = uint32_t(log.alloc.size()); i-- > alloc_mark;) {
    const AllocEvent ev = log.alloc[int(i)];
    edit_detail::with_domain(k_, Grp(ev.g), [&](auto &d) {
      if (ev.kind == AllocKind::Free) {
        d.undo_free(ev.row);
      }
      else {
        d.undo_alloc(ev.row, ev.kind == AllocKind::FromFree);
      }
    });
  }
  for (uint32_t g = 0; g < kGroupCount; g++) {
    litestl::attr::AttrGroup &gr = group_of(k_, Grp(g));
    for (uint32_t r = sizes[g]; r < gr.size(); r++) {
      idx_.mark(Grp(g), r); /* appended rows, dropped */
      grid_.mark(Grp(g), r);
    }
    if (gr.size() != sizes[g]) {
      gr.resize(sizes[g]);
    }
  }
  k_.regions_dirty = regions_dirty;
  log.rows.resize(int(rows_mark));
  log.alloc.resize(int(alloc_mark));
}

template <geom::CoordType C> void Editor<C>::rebuild_captured()
{
  captured_.clear();
  for (const RowCapture &rc : log_.rows) {
    uint32_t &e = captured_[edit_detail::cap_key(rc.g, rc.row)];
    e = std::max(e, rc.epoch);
  }
}

/* ------------------------------------------------------------------------ */
/* journal                                                                  */
/* ------------------------------------------------------------------------ */

template <geom::CoordType C> EditCode Editor<C>::undo()
{
  rep_ = EditReport{};
  auto ret = [&](EditCode c) {
    rep_.code = c;
    return c;
  };
  if (!journal_ || journal_->done_.empty()) {
    return ret(EditCode::NothingToUndo);
  }
  if (!sp_.empty()) {
    return ret(EditCode::OpenTransaction);
  }
  Txn<C> &t = journal_->done_.back();
  bool stale = k_.version() != journal_->expected_version_;
  for (uint32_t g = 0; g < kGroupCount && !stale; g++) {
    stale = group_of(k_, Grp(g)).schema_version() != t.schema_ver[g];
  }
  if (stale) {
    journal_->clear();
    return ret(EditCode::JournalStale);
  }
  check_index_stamp();
  if (t.snapshot) {
    restore_snapshot(std::move(*t.snapshot));
    if (!t.ops.empty() && t.ops.front().kind == OpKind::RebuildRegions) {
      /* the state before build_regions differs only in regions and resolved
       * containers; the grid holds geometry only */
      idx_.refresh_containers(k_);
    }
    else {
      idx_.invalidate();
      idx_.flush(k_);
      grid_.rebuild_if_used(k_);
    }
  }
  else {
    restore(t.log, 0, 0, t.log.start_size, t.log.regions_dirty);
  }
  Txn<C> u;
  u.label = std::move(t.label);
  u.ops = std::move(t.ops);
  u.post_hash = t.post_hash;
  for (uint32_t g = 0; g < kGroupCount; g++) {
    u.ref_cols[g] = std::move(t.ref_cols[g]); /* SetAttr payloads stay roots */
  }
  journal_->done_.pop_back();
  journal_->undone_.push_back(std::move(u));
  journal_->expected_version_ = k_.version();
  sync_index_stamp();
  return EditCode::Ok;
}

template <geom::CoordType C> EditCode Editor<C>::redo()
{
  rep_ = EditReport{};
  if (!journal_ || journal_->undone_.empty()) {
    rep_.code = EditCode::NothingToRedo;
    return rep_.code;
  }
  if (!sp_.empty()) {
    rep_.code = EditCode::OpenTransaction;
    return rep_.code;
  }
  if (k_.version() != journal_->expected_version_) {
    journal_->clear();
    rep_.code = EditCode::JournalStale;
    return rep_.code;
  }
  Txn<C> t = std::move(journal_->undone_.back());
  journal_->undone_.pop_back();
  redoing_ = true;
  EditCode c;
  const bool verify = opt_.verify_redo && !(t.post_hash == ident::PersistentId{});
  bool mismatch = false;
  if (t.ops.size() == 1 && (t.ops[0].kind == OpKind::RebuildRegions || t.ops[0].kind == OpKind::Compact)) {
    c = run_barrier(t.ops[0].kind);
    if (c == EditCode::Ok && verify && !(content_hash(k_) == t.post_hash)) {
      redoing_ = false;
      undo();
      mismatch = true;
    }
  }
  else {
    c = begin_internal(t.label);
    for (size_t i = 0; i < t.ops.size() && c == EditCode::Ok; i++) {
      c = apply_op(t.ops[i]);
    }
    if (c == EditCode::Ok && verify && !(content_hash(k_) == t.post_hash)) {
      rollback();
      mismatch = true;
    }
    else if (c == EditCode::Ok) {
      c = commit_internal(true);
    }
    else {
      const EditReport keep = rep_;
      rollback();
      rep_ = keep;
    }
  }
  redoing_ = false;
  if (mismatch) {
    journal_->clear();
    rep_ = EditReport{};
    rep_.code = EditCode::JournalStale;
    rep_.detail = "redo produced a different state";
    return EditCode::JournalStale;
  }
  if (c != EditCode::Ok) {
    journal_->undone_.push_back(std::move(t));
  }
  return c;
}

template <geom::CoordType C> EditCode Editor<C>::apply_op(const OpRecord<C> &op)
{
  const auto &u = op.u;
  switch (op.kind) {
    case OpKind::MakeVertex:
      return make_vertex(op.pts[0], op.ids[0]);
    case OpKind::KillVertex:
      return kill_vertex(VertRef(u[0]));
    case OpKind::MakeEdge:
      return make_edge(VertRef(u[0]), VertRef(u[1]), op.ids[0], nullptr, EmbedTarget{EmbedTarget::Kind(u[2]), u[3]});
    case OpKind::KillEdge:
      return kill_edge(EdgeRef(u[0]));
    case OpKind::SetPosition:
      return set_position(VertRef(u[0]), op.pts[0]);
    case OpKind::EmbedVertex:
      return embed_vertex(VertRef(u[0]), EmbedTarget{EmbedTarget::Kind(u[1]), u[2]});
    case OpKind::EmbedEdge:
      return embed_edge(EdgeRef(u[0]), EmbedTarget{EmbedTarget::Kind(u[1]), u[2]});
    case OpKind::SplitEdge:
      return split_edge(EdgeRef(u[0]), op.pts[0]);
    case OpKind::JoinEdge:
      return join_edge(VertRef(u[0]));
    case OpKind::MakeFace: {
      /* u: flags, nloops, sizes..., verts... ; ids: face, front, back */
      const uint32_t nl = u[1];
      litestl::util::Vector<VertRef, 32> vs;
      for (uint32_t i = 2 + nl; i < uint32_t(u.size()); i++) {
        vs.append(VertRef(u[i]));
      }
      return make_face(std::span<const VertRef>(vs.data(), vs.size()),
                       std::span<const uint32_t>(u.data() + 2, nl),
                       op.ids[0],
                       uint8_t(u[0]),
                       op.ids[1],
                       op.ids[2]);
    }
    case OpKind::KillFace:
      return kill_face(FaceRef(u[0]));
    case OpKind::SplitFace:
      return split_face(FaceRef(u[0]), VertRef(u[1]), VertRef(u[2]));
    case OpKind::JoinFaces:
      return join_faces(EdgeRef(u[0]));
    case OpKind::FlipFace:
      return flip_face(FaceRef(u[0]));
    case OpKind::SetSideHint:
      return set_side_hint(FaceRef(u[0]), u[1], op.ids[0]);
    case OpKind::SetAttr:
      return set_attr(Grp(u[0]), u[1], u[2], std::span<const uint8_t>(op.bytes.data(), op.bytes.size()));
    case OpKind::Imprint:
    case OpKind::AddHole: {
      litestl::util::Vector<EdgeRef, 32> es;
      for (uint32_t i = 0; i < u[1]; i++) {
        es.append(EdgeRef(u[2 + i]));
      }
      const std::span<const EdgeRef> chain(es.data(), es.size());
      return op.kind == OpKind::Imprint ? imprint(FaceRef(u[0]), chain) : add_hole(FaceRef(u[0]), chain);
    }
    case OpKind::Unimprint:
      return unimprint(EdgeRef(u[0]));
    case OpKind::RemoveHole:
      return remove_hole(FaceRef(u[0]), LoopRef(u[1]));
    case OpKind::Weld: {
      litestl::util::Vector<std::pair<VertRef, VertRef>, 8> ps;
      for (uint32_t i = 0; i < u[0]; i++) {
        ps.append({VertRef(u[1 + 2 * i]), VertRef(u[2 + 2 * i])});
      }
      return weld(std::span<const std::pair<VertRef, VertRef>>(ps.data(), ps.size()));
    }
    case OpKind::SetEdgeCurve:
      return set_edge_curve(EdgeRef(u[0]),
                            litestl::attr::PoolRef{u[1], u[2]},
                            edit_detail::get_f64(u.data() + 3),
                            edit_detail::get_f64(u.data() + 5));
    case OpKind::SplitEdgeAt:
      return split_edge_at(EdgeRef(u[0]), edit_detail::get_f64(u.data() + 1));
    case OpKind::SetFaceSurface: {
      litestl::util::Vector<std::pair<CoedgeRef, litestl::attr::PoolRef>, 16> pcs;
      for (uint32_t i = 4; i + 2 < uint32_t(u.size()); i += 3) {
        pcs.append({CoedgeRef(u[i]), litestl::attr::PoolRef{u[i + 1], u[i + 2]}});
      }
      return set_face_surface(FaceRef(u[0]),
                              litestl::attr::PoolRef{u[1], u[2]},
                              u[3] != 0,
                              std::span<const std::pair<CoedgeRef, litestl::attr::PoolRef>>(pcs.data(), pcs.size()));
    }
    case OpKind::SetCoedgePcurve:
      return set_coedge_pcurve(CoedgeRef(u[0]), litestl::attr::PoolRef{u[1], u[2]});
    case OpKind::RebuildRegions:
    case OpKind::Compact:
      return EditCode::OpenTransaction; /* barriers are replayed on their own (redo) */
  }
  return EditCode::BadArgument;
}

/* ------------------------------------------------------------------------ */
/* primitive scope                                                          */
/* ------------------------------------------------------------------------ */

template <geom::CoordType C> Editor<C>::Prim::Prim(Editor &e, OpKind kind) : ed(e)
{
  op.kind = kind;
  outer = !ed.in_prim_;
  if (outer) {
    ed.rep_ = EditReport{};
  }
  code = ed.begin_internal({});
  if (code != EditCode::Ok) {
    ed.rep_.code = code;
    done = true;
    return;
  }
  if (outer) {
    ed.in_prim_ = true;
    for (auto &t : ed.touched_) {
      t.clear();
    }
    (void)0; /* the index stamp is checked by begin_internal at depth 0 */
  }
}

template <geom::CoordType C> Editor<C>::Prim::~Prim()
{
  if (!done) {
    ed.rollback();
    if (outer) {
      ed.in_prim_ = false;
    }
  }
}

template <geom::CoordType C>
EditCode Editor<C>::Prim::fail(EditCode c, ident::ElemKind kind, uint32_t index, const char *detail)
{
  ed.rep_.code = c;
  ed.rep_.kind = kind;
  ed.rep_.index = index;
  if (detail) {
    ed.rep_.detail = detail;
  }
  ed.rollback();
  done = true;
  if (outer) {
    ed.in_prim_ = false;
  }
  code = c;
  return c;
}

template <geom::CoordType C> EditCode Editor<C>::Prim::finish()
{
  if (outer && !ed.redoing_ && (ed.opt_.validate == ValidateMode::Full || ed.opt_.validate == ValidateMode::Local)) {
    const EditCode v = ed.validate_now(ed.touched_);
    if (v != EditCode::Ok) {
      return fail(v, ed.rep_.kind, ed.rep_.index);
    }
  }
  if (outer) {
    ed.ops_.push_back(std::move(op));
  }
  const EditCode c = ed.commit();
  done = true;
  if (outer) {
    ed.in_prim_ = false;
  }
  if (c == EditCode::Ok) {
    ed.sync_index_stamp();
  }
  code = c;
  return c;
}

template <geom::CoordType C>
void Editor<C>::touched_filter(ValidateFilter &flt, const litestl::util::Vector<uint32_t> *touched_) const
{
  /* the elements this primitive wrote, plus (C14) the edges of touched faces
   * and coedges, and the inclusions of touched faces (C3) */
  auto add = [](litestl::util::Vector<uint32_t> &v, uint32_t i) { v.append(i); };
  auto face_ok = [&](uint32_t f) { return f < k_.faces.capacity() && k_.faces.live(f); };
  for (uint32_t f : touched_[uint32_t(Grp::Faces)]) {
    if (face_ok(f)) {
      add(flt.faces, f);
    }
  }
  for (uint32_t r : touched_[uint32_t(Grp::Sides)]) {
    if (face_ok(r / 2)) {
      add(flt.faces, r / 2);
    }
  }
  for (uint32_t l : touched_[uint32_t(Grp::Loops)]) {
    if (l < k_.loops.capacity() && k_.loops.live(l)) {
      add(flt.faces, k_.face_of(LoopRef(l)).i);
    }
  }
  for (uint32_t c : touched_[uint32_t(Grp::Coedges)]) {
    if (c < k_.coedges.capacity() && k_.coedges.live(c)) {
      add(flt.faces, k_.face_of(CoedgeRef(c)).i);
      add(flt.edges, k_.edge_of(CoedgeRef(c)).i);
    }
    else if (c < k_.coedges.capacity()) {
      /* a freed coedge still names its edge: that radial cycle changed */
      const EdgeRef e = k_.edge_of(CoedgeRef(c));
      if (e.i < k_.edges.capacity() && k_.edges.live(e.i)) {
        add(flt.edges, e.i);
      }
    }
  }
  for (uint32_t e : touched_[uint32_t(Grp::Edges)]) {
    if (e < k_.edges.capacity() && k_.edges.live(e)) {
      add(flt.edges, e);
    }
    else if (e < k_.edges.capacity()) {
      /* a freed edge still names its vertices: their disks changed */
      for (const VertRef v : k_.edge_verts(EdgeRef(e))) {
        if (v.i < k_.verts.capacity() && k_.verts.live(v.i)) {
          add(flt.verts, v.i);
        }
      }
    }
  }
  for (uint32_t v : touched_[uint32_t(Grp::Verts)]) {
    if (v < k_.verts.capacity() && k_.verts.live(v)) {
      add(flt.verts, v);
      /* a moved vertex: its edges' radial orders, wires' containers, and
       * the faces around it (their geometry) */
      k_.for_each_edge_at(VertRef(v), [&](EdgeRef e) {
        add(flt.edges, e.i);
        k_.for_each_radial(e, [&](CoedgeRef c) { add(flt.faces, k_.face_of(c).i); });
      });
    }
  }
  auto uniq = [](litestl::util::Vector<uint32_t> &v) {
    std::sort(v.begin(), v.end());
    v.resize(int(std::unique(v.begin(), v.end()) - v.begin()));
  };
  uniq(flt.faces);
  for (uint32_t f : flt.faces) {
    k_.for_each_loop(FaceRef(f), [&](LoopRef l) {
      k_.for_each_coedge(l, [&](CoedgeRef c) { add(flt.edges, k_.edge_of(c).i); });
    });
    litestl::util::Vector<uint32_t, 16> incl;
    inclusions(FaceRef(f), incl);
    for (uint32_t x : incl) {
      add((x & 1) ? flt.edges : flt.verts, x >> 1);
    }
  }
  uniq(flt.edges);
  uniq(flt.verts);
  flt.regions = false; /* regions are dirty, or the primitive kept them by construction */
}

template <geom::CoordType C> EditCode Editor<C>::validate_now(const litestl::util::Vector<uint32_t> *touched)
{
  ValidationReport r;
  if (opt_.validate == ValidateMode::Local || opt_.validate == ValidateMode::OnCommit) {
    ValidateFilter flt;
    touched_filter(flt, touched);
    idx_.flush(k_);
    grid_.flush(k_);
    IndexIds ids;
    ids.idx = &idx_;
    flt.ids = &ids;
    flt.grid = &grid_.grid();
    r = validate_local(k_, flt);
  }
  else {
    r = validate(k_, ValidateLevel::Global);
  }
  if (r.ok()) {
    return EditCode::Ok;
  }
  rep_.validation = r;
  if (r.issues.size()) {
    rep_.kind = r.issues[0].kind;
    rep_.index = r.issues[0].index;
  }
  rep_.code = EditCode::Invalid;
  return EditCode::Invalid;
}

/* ------------------------------------------------------------------------ */
/* writes                                                                   */
/* ------------------------------------------------------------------------ */

template <geom::CoordType C> void Editor<C>::touch(Grp g, uint32_t row)
{
  if (sp_.empty()) {
    fprintf(stderr, "ubertopo::Editor: write outside a transaction\n");
    abort();
  }
  touched_[uint32_t(g)].append(row);
  txn_touched_[uint32_t(g)].append(row);
  idx_.mark(g, row);
  grid_.mark(g, row);
  const Savepoint &s = sp_.back();
  if (row >= s.size[uint32_t(g)]) {
    return; /* appended inside this savepoint: dropped by rollback */
  }
  const uint64_t key = edit_detail::cap_key(uint8_t(g), row);
  auto it = captured_.find(key);
  if (it != captured_.end() && it->second >= s.epoch) {
    return;
  }
  litestl::attr::AttrGroup &gr = group_of(k_, g);
  RowCapture rc{row, uint32_t(log_.bytes.size()), s.epoch, uint8_t(g)};
  const uint32_t n = edit_detail::row_bytes(gr);
  log_.bytes.resize(log_.bytes.size() + int(n));
  uint8_t *p = log_.bytes.data() + rc.off;
  for (uint32_t c = 0; c < gr.column_count(); c++) {
    const litestl::attr::ColumnBase &col = gr.column(c);
    col.get_bytes(row, p);
    p += col.elem_size();
  }
  log_.rows.append(rc);
  captured_[key] = s.epoch;
}

template <geom::CoordType C> uint32_t Editor<C>::alloc(Grp g)
{
  uint32_t out = NONE;
  edit_detail::with_domain(k_, g, [&](auto &d) {
    const uint32_t i = d.peek_alloc();
    const bool from_free = i < d.capacity();
    if (from_free) {
      touch(g, i);
      if (g == Grp::Faces) {
        touch(Grp::Sides, 2 * i);
        touch(Grp::Sides, 2 * i + 1);
      }
    }
    else {
      touched_[uint32_t(g)].append(i);
      txn_touched_[uint32_t(g)].append(i);
      idx_.mark(g, i);
      grid_.mark(g, i);
    }
    out = d.alloc();
    log_.alloc.append(AllocEvent{out, uint8_t(g), from_free ? AllocKind::FromFree : AllocKind::Append});
  });
  return out;
}

template <geom::CoordType C> void Editor<C>::free(Grp g, uint32_t i)
{
  touch(g, i);
  if (g == Grp::Faces) {
    touch(Grp::Sides, 2 * i);
    touch(Grp::Sides, 2 * i + 1);
  }
  edit_detail::with_domain(k_, g, [&](auto &d) { d.free(i); });
  log_.alloc.append(AllocEvent{i, uint8_t(g), AllocKind::Free});
}

template <geom::CoordType C> void Editor<C>::set_disk(EdgeRef e, uint32_t entry, uint32_t packed)
{
  U32x4 d = k_.edges.template get<".e.disk">()[e.i];
  d[entry] = packed;
  setE<".e.disk">(e, d);
}

template <geom::CoordType C> void Editor<C>::set_dirty()
{
  k_.regions_dirty = true; /* restored from the savepoint on rollback */
}

/* ------------------------------------------------------------------------ */
/* propagation                                                              */
/* ------------------------------------------------------------------------ */

template <geom::CoordType C> bool Editor<C>::propagate(Grp g, PropEvent ev, const MixBuild &mb)
{
  using litestl::attr::ColumnBase;
  using litestl::attr::ColumnFlags;
  using litestl::attr::MixPolicy;
  if (mb.empty()) {
    return true;
  }
  for (uint32_t t : mb.targets) {
    touch(g, t);
  }
  litestl::attr::AttrGroup &gr = group_of(k_, g);
  bool bad_rule = false;
  litestl::util::Vector<uint8_t, 64> a, b;
  auto sources_equal = [&](const ColumnBase &col) {
    const uint32_t n = col.elem_size();
    a.resize(int(n));
    b.resize(int(n));
    for (uint32_t k = 0; k + 1 < uint32_t(mb.offsets.size()); k++) {
      const uint32_t s0 = mb.offsets[int(k)], s1 = mb.offsets[int(k + 1)];
      if (s1 == s0) {
        continue;
      }
      col.get_bytes(mb.sources[int(s0)], a.data());
      for (uint32_t j = s0 + 1; j < s1; j++) {
        col.get_bytes(mb.sources[int(j)], b.data());
        if (memcmp(a.data(), b.data(), n) != 0) {
          return false;
        }
      }
    }
    return true;
  };
  auto policy = [&](std::string_view name, uint32_t, const ColumnBase &col) -> MixPolicy {
    if (g == Grp::Verts && name == "pos") {
      return MixPolicy::Skip; /* C2: primitives write positions */
    }
    if (has_flag(col.flags(), ColumnFlags::Temp) || has_flag(col.flags(), ColumnFlags::Derived)) {
      return MixPolicy::SetDefault;
    }
    const ColumnRule *r = rules_ ? rules_->find(g, name) : nullptr;
    const bool is_float = col.default_policy() == MixPolicy::Interpolate;
    switch (ev) {
      case PropEvent::Split:
        if (r && r->split) {
          if ((*r->split == MixPolicy::Interpolate || *r->split == MixPolicy::WeightedMean) && !is_float) {
            bad_rule = true; /* C10: split by measure is for float columns */
            return MixPolicy::CopyFirst;
          }
          return *r->split;
        }
        return MixPolicy::CopyFirst;
      case PropEvent::Merge:
        if (r && r->merge) {
          return *r->merge;
        }
        if (sources_equal(col)) {
          return MixPolicy::CopyFirst;
        }
        return is_float ? MixPolicy::WeightedMean : MixPolicy::CopyFirst;
      case PropEvent::Interp:
        return r && r->interp ? *r->interp : col.default_policy();
      case PropEvent::Reverse:
        return r && r->reverse ? *r->reverse : MixPolicy::CopyFirst;
    }
    return MixPolicy::CopyFirst;
  };
  const litestl::attr::AttrGroup::PolicyFn pf = policy;
  const PropEventInfo info{ev, g};
  const char *err = nullptr;
  if (!gr.mix(mb.batch(), &pf, &info, &err)) {
    rep_.detail = err;
    return false;
  }
  if (bad_rule) {
    rep_.detail = "split-by-measure rule on a non-float column";
    return false;
  }
  return true;
}

/* ------------------------------------------------------------------------ */
/* queries                                                                  */
/* ------------------------------------------------------------------------ */

template <geom::CoordType C> bool Editor<C>::live(Grp g, uint32_t i) const
{
  switch (g) {
    case Grp::Verts:
      return k_.verts.live(i);
    case Grp::Edges:
      return k_.edges.live(i);
    case Grp::Coedges:
      return k_.coedges.live(i);
    case Grp::Loops:
      return k_.loops.live(i);
    case Grp::Faces:
      return k_.faces.live(i);
    case Grp::Shells:
      return k_.shells.live(i);
    case Grp::Regions:
      return k_.regions.live(i);
    default:
      return false;
  }
}

template <geom::CoordType C> bool Editor<C>::id_used(Grp g, ident::PersistentId id)
{
  idx_.flush(k_);
  return idx_.id_count(uint32_t(edit_detail::id_slot(g)), id) != 0;
}

template <geom::CoordType C> EdgeRef Editor<C>::find_edge(VertRef a, VertRef b) const
{
  EdgeRef found;
  k_.for_each_edge_at(a, [&](EdgeRef e) {
    const auto vs = k_.edge_verts(e);
    if ((vs[0] == a && vs[1] == b) || (vs[0] == b && vs[1] == a)) {
      found = e;
    }
  });
  return found;
}

template <geom::CoordType C> uint32_t Editor<C>::edge_slot(EdgeRef e, VertRef v) const
{
  const auto vs = k_.edge_verts(e);
  return vs[0] == v ? 0 : vs[1] == v ? 1 : 2;
}

template <geom::CoordType C> litestl::util::Vector<CoedgeRef, 8> Editor<C>::radial_members(EdgeRef e) const
{
  litestl::util::Vector<CoedgeRef, 8> out;
  k_.for_each_radial(e, [&](CoedgeRef c) { out.append(c); });
  return out;
}

template <geom::CoordType C>
void Editor<C>::inclusions(FaceRef f, litestl::util::Vector<uint32_t, 16> &out) const
{
  out.clear();
  ident::ContainerRef target;
  if (!ident::ContainerRef::face(f, target)) {
    return;
  }
  idx_.flush(k_);
  idx_.inclusions(target, out);
}

template <geom::CoordType C> bool Editor<C>::hint_in_use(ident::PersistentId h) const
{
  if (h == ident::kNoId) {
    return false;
  }
  idx_.flush(k_);
  return idx_.hint_in_use(h);
}

template <geom::CoordType C>
bool Editor<C>::hint_carried(ident::PersistentId h, uint32_t skip_side_a, uint32_t skip_side_b) const
{
  idx_.flush(k_);
  uint32_t n = idx_.carriers(h);
  const auto &sh = k_.faces.sides().template get<".fs.hint">();
  auto skip = [&](uint32_t row) {
    if (row != NONE && row < sh.size() && k_.faces.live(row / 2) && sh[row] == h && n > 0) {
      n--;
    }
  };
  skip(skip_side_a);
  if (skip_side_b != skip_side_a) {
    skip(skip_side_b);
  }
  return n > 0;
}

} // namespace ubertopo::topo
