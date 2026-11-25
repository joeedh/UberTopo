#pragma once

/* Editor primitives, P3b W6: imprint, island imprint and add_hole,
 * remove_hole (plan P3b §3.6, C14); the split core is in editor_impl_split.h,
 * unimprint in editor_impl_unimprint.h. Included only by editor.cc. */

#include <algorithm>
#include <cmath>

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/face_geom.h"

namespace ubertopo::topo {

namespace edit_detail {
inline uint32_t vertex_degree(const auto &k, VertRef v)
{
  uint32_t n = 0;
  k.for_each_edge_at(v, [&](EdgeRef) { n++; });
  return n;
}
} // namespace edit_detail

/* ---- the chain of an imprint: edges in path order -> vertices V0..Vn ---- */

template <geom::CoordType C>
EditCode Editor<C>::read_chain(FaceRef f,
                               std::span<const EdgeRef> chain,
                               litestl::util::Vector<uint32_t, 32> &vs,
                               bool &closed)
{
  using ident::ElemKind;
  vs.clear();
  closed = false;
  if (chain.empty()) {
    return fail_report(EditCode::BadArgument, ElemKind::Face, f.i, "empty chain");
  }
  ident::ContainerRef cf;
  ident::ContainerRef::face(f, cf);
  for (size_t i = 0; i < chain.size(); i++) {
    const EdgeRef e = chain[i];
    if (!k_.edges.live(e.i)) {
      return fail_report(EditCode::DeadElement, ElemKind::Edge, e.i);
    }
    if (k_.edges.template get<".e.c">()[e.i] != NONE) {
      return fail_report(EditCode::NotWire, ElemKind::Edge, e.i);
    }
    if (!(k_.edges.template get<".e.container">()[e.i] == cf)) {
      return fail_report(EditCode::NotEmbedded, ElemKind::Edge, e.i, "a chain edge is not embedded in the face");
    }
    for (size_t j = 0; j < i; j++) {
      if (chain[j] == e) {
        return fail_report(EditCode::ChainNotSimple, ElemKind::Edge, e.i, "an edge repeats");
      }
    }
  }
  /* V0: for one edge its vs[0]; otherwise the end of edge 0 not on edge 1 */
  const auto e0 = k_.edge_verts(chain[0]);
  VertRef v = e0[0];
  if (chain.size() > 1) {
    const auto e1 = k_.edge_verts(chain[1]);
    v = (e0[0] == e1[0] || e0[0] == e1[1]) ? e0[1] : e0[0];
  }
  vs.append(v.i);
  for (const EdgeRef e : chain) {
    const auto ev = k_.edge_verts(e);
    if (ev[0] != v && ev[1] != v) {
      return fail_report(EditCode::ChainNotSimple, ElemKind::Edge, e.i, "the chain is not connected");
    }
    v = ev[0] == v ? ev[1] : ev[0];
    vs.append(v.i);
  }
  closed = chain.size() > 2 && vs[0] == vs.last();
  const int n_unique = closed ? vs.size() - 1 : vs.size();
  for (int i = 0; i < n_unique; i++) {
    for (int j = 0; j < i; j++) {
      if (vs[i] == vs[j]) {
        return fail_report(EditCode::ChainNotSimple, ElemKind::Vertex, vs[i], "a vertex repeats");
      }
    }
  }
  /* interior vertices (all of them for a cycle): embedded in f, degree 2 */
  const int lo = closed ? 0 : 1, hi = closed ? n_unique : n_unique - 1;
  for (int i = lo; i < hi; i++) {
    const VertRef w(vs[i]);
    if (!(k_.verts.template get<".v.container">()[w.i] == cf)) {
      return fail_report(EditCode::NotEmbedded, ElemKind::Vertex, w.i, "a chain vertex is not embedded in the face");
    }
    if (edit_detail::vertex_degree(k_, w) != 2) {
      return fail_report(EditCode::ChainNotSimple, ElemKind::Vertex, w.i, "a chain vertex has other edges");
    }
  }
  return EditCode::Ok;
}

template <geom::CoordType C>
EditCode Editor<C>::fail_report(EditCode c, ident::ElemKind kind, uint32_t index, const char *detail)
{
  rep_.code = c;
  rep_.kind = kind;
  rep_.index = index;
  rep_.detail = detail;
  return c;
}

/* ---- island core: a closed chain becomes a hole of f, and (make_face) the
 * outer loop of a new face filling it ---- */

template <geom::CoordType C>
EditCode Editor<C>::island_core(FaceRef f,
                                std::span<const uint32_t> chain_e,
                                std::span<const uint32_t> chain_v, /* V0..Vn, Vn == V0 */
                                bool make_face,
                                FaceRef *island)
{
  using ident::ElemKind;
  const uint32_t n = uint32_t(chain_e.size());
  const int axis = edit_detail::face_axis<C>(k_, f);
  Skeleton<C> sk(k_);
  FacePoints<C> fp;
  if (axis < 0 || !fp.gather(k_, f, sk)) {
    return fail_report(EditCode::DegenerateLoop, ElemKind::Face, f.i);
  }
  litestl::util::Vector<uint32_t, 8> loops;
  k_.for_each_loop(f, [&](LoopRef l) { loops.append(l.i); });
  const uint32_t outer = loops.size() > 1 ? geom::outer_loop_proj<C>(fp.view(), axis) : 0;
  /* the cycle's polygon and orientations, on the refined skeleton (plan P4b
   * §4.3): each chain vertex, then its edge's flattening points toward the
   * next */
  litestl::util::Vector<Point, 32> cyc, cyc_v;
  for (uint32_t i = 0; i < n; i++) {
    cyc.append(k_.pos(VertRef(chain_v[i])));
    cyc_v.append(k_.pos(VertRef(chain_v[i])));
    const EdgeRef e(chain_e[i]);
    const auto in = sk.interior(e);
    const bool fwd = k_.edge_verts(e)[0].i == chain_v[i];
    for (size_t j = 0; j < in.size(); j++) {
      cyc.append(sk.pos(in[fwd ? j : in.size() - 1 - j]));
    }
  }
  const uint32_t cyc_off[2] = {0, uint32_t(cyc.size())}, cyc_voff[2] = {0, n};
  const geom::PolyView<C> cyc_view{std::span<const Point>(cyc.data(), cyc.size()), std::span<const uint32_t>(cyc_off, 2)};
  {
    bool deg = false;
    face_planar_rule<C>(geom::PolyView<C>{std::span<const Point>(cyc_v.data(), cyc_v.size()),
                                          std::span<const uint32_t>(cyc_voff, 2)},
                        cyc_view,
                        &deg);
    if (deg) {
      return fail_report(EditCode::DegenerateLoop, ElemKind::Face, f.i, "the cycle is degenerate");
    }
  }
  const auto outer_pts = fp.view().loop(outer);
  const int o_outer = geom::loop_orientation_proj<C>(outer_pts, axis);
  const int o_cyc = geom::loop_orientation_proj<C>(std::span<const Point>(cyc.data(), cyc.size()), axis);
  /* the new face's loop runs like f's outer loop; the hole the other way */
  const bool island_fwd = o_cyc == o_outer;
  auto inside = [&](const Point &p) { return geom::point_in_poly_proj<C>(p, cyc_view, axis); };
  for (int i = 0; i < loops.size(); i++) {
    if (uint32_t(i) == outer) {
      continue;
    }
    if (inside(k_.pos(k_.start_vert(k_.first_coedge(LoopRef(loops[i]))))) != geom::PointClass::Outside) {
      return fail_report(EditCode::HoleInside, ElemKind::Loop, loops[i], "a hole of the face lies inside the cycle");
    }
  }
  litestl::util::Vector<uint32_t, 32> chain_packed;
  for (uint32_t e : chain_e) {
    chain_packed.append((e << 1) | 1);
  }
  for (uint32_t i = 0; i < n; i++) {
    chain_packed.append(chain_v[i] << 1);
  }
  std::sort(chain_packed.begin(), chain_packed.end());
  litestl::util::Vector<uint32_t, 16> incl, incl_in;
  inclusions(f, incl);
  for (uint32_t x : incl) {
    if (std::binary_search(chain_packed.begin(), chain_packed.end(), x)) {
      continue;
    }
    geom::PointClass pc;
    if (x & 1) {
      const auto evs = k_.edge_verts(EdgeRef(x >> 1));
      pc = inside(k_.pos(evs[0]));
      if (inside(k_.pos(evs[1])) != pc) {
        pc = geom::PointClass::Boundary;
      }
    }
    else {
      pc = inside(k_.pos(VertRef(x >> 1)));
    }
    if (pc == geom::PointClass::Boundary) {
      return fail_report(EditCode::InclusionOnChord, (x & 1) ? ElemKind::Edge : ElemKind::Vertex, x >> 1);
    }
    if (pc == geom::PointClass::Inside) {
      if (!make_face) {
        return fail_report(EditCode::ContainedElement, (x & 1) ? ElemKind::Edge : ElemKind::Vertex, x >> 1,
                           "an inclusion would float in the opening");
      }
      incl_in.append(x);
    }
  }
  const ident::PersistentId fid = k_.face_id(f);
  ident::PersistentId gid{};
  if (make_face) {
    ident::PersistentId min_e = k_.edge_id(EdgeRef(chain_e[0]));
    for (uint32_t e : chain_e) {
      min_e = std::min(min_e, k_.edge_id(EdgeRef(e)));
    }
    gid = ident::derive(ident::tag::OpDerived, fid, std::string_view("imprint.island"), min_e);
    if (id_used(Grp::Faces, gid)) {
      return fail_report(EditCode::DuplicateId, ElemKind::Face, f.i);
    }
  }
  const bool was_projected = k_.face_proj_axis(f) >= 0;
  const double area0 = edit_detail::face_area<C>(k_, f);

  /* hole loop of f: the cycle against f's outer orientation */
  litestl::util::Vector<uint32_t, 32> hc, gc;
  for (uint32_t i = 0; i < n; i++) {
    hc.append(alloc(Grp::Coedges));
    if (make_face) {
      gc.append(alloc(Grp::Coedges));
    }
  }
  const LoopRef lh(alloc(Grp::Loops));
  FaceRef g;
  LoopRef lg;
  if (make_face) {
    g = FaceRef(alloc(Grp::Faces));
    lg = LoopRef(alloc(Grp::Loops));
  }
  /* coedge i of a loop running "forward" goes V(i) -> V(i+1); backward
   * coedge i goes V(i+1) -> V(i), and the loop order is reversed */
  auto build = [&](const litestl::util::Vector<uint32_t, 32> &cs, LoopRef l, bool fwd_loop) {
    for (uint32_t i = 0; i < n; i++) {
      const CoedgeRef c(cs[i]);
      const EdgeRef e(chain_e[i]);
      const VertRef from(fwd_loop ? chain_v[i] : chain_v[i + 1]);
      setC<".c.e">(c, e.i);
      setC<".c.v">(c, from.i);
      setC<".c.flags">(c, uint8_t(k_.edge_verts(e)[0] == from ? coedge_flags::Sense : 0));
      setC<".c.l">(c, l.i);
      const uint32_t nx = fwd_loop ? cs[(i + 1) % n] : cs[(i + n - 1) % n];
      const uint32_t pv = fwd_loop ? cs[(i + n - 1) % n] : cs[(i + 1) % n];
      setC<".c.next">(c, nx);
      setC<".c.prev">(c, pv);
    }
    setL<".l.c">(l, cs[0]);
    setL<".l.size">(l, n);
  };
  build(hc, lh, !island_fwd);
  /* append the hole to f's loop chain */
  {
    const LoopRef last(loops.last());
    setL<".l.next">(last, lh.i);
    setL<".l.next">(lh, NONE);
    setL<".l.f">(lh, f.i);
    setL<".id">(lh, ident::loop_id(fid, uint32_t(loops.size())));
  }
  const ident::ContainerRef none{};
  for (uint32_t e : chain_e) {
    setE<".e.container">(EdgeRef(e), none);
    setE<".e.container_hint">(EdgeRef(e), ident::kNoId);
  }
  for (uint32_t i = 0; i < n; i++) {
    setV<".v.container">(VertRef(chain_v[i]), none);
    setV<".v.container_hint">(VertRef(chain_v[i]), ident::kNoId);
  }
  if (make_face) {
    build(gc, lg, island_fwd);
    setL<".l.next">(lg, NONE);
    setL<".l.f">(lg, g.i);
    setL<".id">(lg, ident::loop_id(gid, 0));
    setF<".f.l">(g, lg.i);
    setF<".id">(g, gid);
    setF<".f.flags">(g, k_.face_flags(f));
    setF<".f.surface">(g, k_.faces.template get<".f.surface">()[f.i]);
    setS<".fs.hint">(2 * g.i, k_.side_hint(f, 0));
    setS<".fs.hint">(2 * g.i + 1, k_.side_hint(f, 1));
    ident::ContainerRef cg;
    ident::ContainerRef::face(g, cg);
    for (uint32_t x : incl_in) {
      if (x & 1) {
        setE<".e.container">(EdgeRef(x >> 1), cg);
      }
      else {
        setV<".v.container">(VertRef(x >> 1), cg);
      }
    }
  }
  /* radial cycles: the smaller face ID first */
  for (uint32_t i = 0; i < n; i++) {
    if (make_face) {
      const bool g_first = gid < fid;
      const uint32_t order[2] = {g_first ? gc[i] : hc[i], g_first ? hc[i] : gc[i]};
      link_radial(EdgeRef(chain_e[i]), std::span<const uint32_t>(order, 2));
    }
    else {
      const uint32_t order[1] = {hc[i]};
      link_radial(EdgeRef(chain_e[i]), std::span<const uint32_t>(order, 1));
    }
  }
  fix_projection(f);
  if (make_face) {
    fix_projection(g);
  }
  if (was_projected) {
    for (const FaceRef face : {f, g}) {
      if (!face) {
        continue;
      }
      const EditCode rc = resort_face_edges(face);
      if (rc != EditCode::Ok) {
        return rc;
      }
    }
  }
  set_dirty();
  /* propagation */
  MixBuild lm;
  const LoopRef L_outer(loops[int(outer)]);
  lm.add(lh.i, {{L_outer.i, 1.0}});
  if (make_face) {
    lm.add(lg.i, {{L_outer.i, 1.0}});
    const double ag = edit_detail::face_area<C>(k_, g);
    const double w_g = area0 > 0 ? std::min(1.0, ag / area0) : 0.5, w_f = 1.0 - w_g;
    MixBuild fm, sm;
    fm.add(f.i, {{f.i, w_f}});
    fm.add(g.i, {{f.i, w_g}});
    for (uint32_t s = 0; s < 2; s++) {
      sm.add(2 * f.i + s, {{2 * f.i + s, w_f}});
      sm.add(2 * g.i + s, {{2 * f.i + s, w_g}});
    }
    if (!propagate(Grp::Faces, PropEvent::Split, fm) || !propagate(Grp::Sides, PropEvent::Split, sm)) {
      return fail_report(EditCode::PropagationFailed, ElemKind::Face, f.i);
    }
    lineage_.push_back({fid, gid, ElemKind::Face, LineageKind::Split});
  }
  if (!propagate(Grp::Loops, PropEvent::Split, lm)) {
    return fail_report(EditCode::PropagationFailed, ElemKind::Face, f.i);
  }
  if (island) {
    *island = g;
  }
  return EditCode::Ok;
}

/* ---- drop a hole: its coedges and loop go, its edges and vertices become
 * inclusions of f; later holes are renumbered ---- */

template <geom::CoordType C> void Editor<C>::drop_hole(FaceRef f, LoopRef lh)
{
  const ident::PersistentId fid = k_.face_id(f);
  litestl::util::Vector<uint32_t, 32> cs;
  k_.for_each_coedge(lh, [&](CoedgeRef c) { cs.append(c.i); });
  ident::ContainerRef cf;
  ident::ContainerRef::face(f, cf);
  for (uint32_t c : cs) {
    radial_remove(CoedgeRef(c));
  }
  /* then the edges and vertices left without face edges become inclusions */
  for (uint32_t c : cs) {
    const EdgeRef e = k_.edge_of(CoedgeRef(c));
    if (k_.edges.template get<".e.c">()[e.i] == NONE) {
      setE<".e.container">(e, cf);
    }
    const VertRef v = k_.start_vert(CoedgeRef(c));
    bool face_edges = false;
    k_.for_each_edge_at(v, [&](EdgeRef x) { face_edges = face_edges || k_.edges.template get<".e.c">()[x.i] != NONE; });
    if (!face_edges) {
      setV<".v.container">(v, cf);
    }
  }
  for (uint32_t c : cs) {
    free(Grp::Coedges, c);
  }
  /* unlink from the loop chain and renumber */
  litestl::util::Vector<uint32_t, 8> keep;
  k_.for_each_loop(f, [&](LoopRef l) {
    if (l != lh) {
      keep.append(l.i);
    }
  });
  if (k_.first_loop(f) == lh) {
    setF<".f.l">(f, keep[0]); /* f has an outer loop too */
  }
  for (int i = 0; i < keep.size(); i++) {
    const LoopRef l(keep[i]);
    setL<".l.next">(l, i + 1 < keep.size() ? keep[i + 1] : NONE);
    const ident::PersistentId want = ident::loop_id(fid, uint32_t(i));
    if (!(k_.loops.template get<".id">()[l.i] == want)) {
      setL<".id">(l, want);
    }
  }
  free(Grp::Loops, lh.i);
}

/* ---- public primitives ---- */

template <geom::CoordType C>
EditCode Editor<C>::imprint(FaceRef f, std::span<const EdgeRef> chain, FaceRef *other)
{
  Prim pr(*this, OpKind::Imprint);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.faces.live(f.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Face, f.i);
  }
  if (surfaced(f)) {
    return pr.fail(EditCode::SurfacedFace, ident::ElemKind::Face, f.i);
  }
  litestl::util::Vector<uint32_t, 32> vs;
  bool closed = false;
  EditCode c = read_chain(f, chain, vs, closed);
  litestl::util::Vector<uint32_t, 32> es;
  for (const EdgeRef e : chain) {
    es.append(e.i);
  }
  if (c == EditCode::Ok) {
    if (closed) {
      c = island_core(f, std::span<const uint32_t>(es.data(), es.size()), std::span<const uint32_t>(vs.data(), vs.size()),
                      true, other);
    }
    else {
      const CutPiece piece{std::span<const uint32_t>(es.data(), es.size()),
                           std::span<const uint32_t>(vs.data(), vs.size())};
      c = split_core(f, std::span(&piece, 1), {}, false, "split_face", other, nullptr);
    }
  }
  if (c != EditCode::Ok) {
    return pr.fail(c, rep_.kind, rep_.index, rep_.detail);
  }
  pr.op.u.append(f.i);
  pr.op.u.append(uint32_t(es.size()));
  for (uint32_t e : es) {
    pr.op.u.append(e);
  }
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::add_hole(FaceRef f, std::span<const EdgeRef> chain)
{
  Prim pr(*this, OpKind::AddHole);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.faces.live(f.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Face, f.i);
  }
  if (surfaced(f)) {
    return pr.fail(EditCode::SurfacedFace, ident::ElemKind::Face, f.i);
  }
  litestl::util::Vector<uint32_t, 32> vs;
  bool closed = false;
  EditCode c = read_chain(f, chain, vs, closed);
  if (c == EditCode::Ok && !closed) {
    c = fail_report(EditCode::ChainNotSimple, ident::ElemKind::Face, f.i, "a hole needs a closed chain");
  }
  litestl::util::Vector<uint32_t, 32> es;
  for (const EdgeRef e : chain) {
    es.append(e.i);
  }
  if (c == EditCode::Ok) {
    c = island_core(f, std::span<const uint32_t>(es.data(), es.size()), std::span<const uint32_t>(vs.data(), vs.size()),
                    false, nullptr);
  }
  if (c != EditCode::Ok) {
    return pr.fail(c, rep_.kind, rep_.index, rep_.detail);
  }
  pr.op.u.append(f.i);
  pr.op.u.append(uint32_t(es.size()));
  for (uint32_t e : es) {
    pr.op.u.append(e);
  }
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::remove_hole(FaceRef f, LoopRef l)
{
  Prim pr(*this, OpKind::RemoveHole);
  if (pr.done) {
    return pr.code;
  }
  using ident::ElemKind;
  if (!k_.faces.live(f.i) || !k_.loops.live(l.i) || k_.face_of(l) != f) {
    return pr.fail(EditCode::BadArgument, ElemKind::Loop, l.i);
  }
  if (surfaced(f)) {
    return pr.fail(EditCode::SurfacedFace, ElemKind::Face, f.i);
  }
  const int axis = edit_detail::face_axis<C>(k_, f);
  Skeleton<C> sk(k_);
  FacePoints<C> fp;
  if (axis < 0 || !fp.gather(k_, f, sk)) {
    return pr.fail(EditCode::DegenerateLoop, ElemKind::Face, f.i);
  }
  uint32_t ordinal = 0, mine = NONE, count = 0;
  k_.for_each_loop(f, [&](LoopRef x) {
    if (x == l) {
      mine = ordinal;
    }
    ordinal++;
    count++;
  });
  if (count < 2 || geom::outer_loop_proj<C>(fp.view(), axis) == mine) {
    return pr.fail(EditCode::NotOuterLoop, ElemKind::Loop, l.i, "the loop is the face's outer loop");
  }
  /* the hole's edges carry no other coedges; its vertices no other face edges */
  bool shared = false;
  k_.for_each_coedge(l, [&](CoedgeRef c) {
    shared = shared || radial_members(k_.edge_of(c)).size() != 1;
    k_.for_each_edge_at(k_.start_vert(c), [&](EdgeRef x) {
      bool on_loop = false;
      k_.for_each_radial(x, [&](CoedgeRef y) { on_loop = on_loop || k_.loop_of(y) == l; });
      shared = shared || (!on_loop && k_.edges.template get<".e.c">()[x.i] != NONE);
    });
  });
  if (shared) {
    return pr.fail(EditCode::EdgeShared, ElemKind::Loop, l.i);
  }
  drop_hole(f, l);
  const bool was_projected = k_.face_proj_axis(f) >= 0;
  fix_projection(f);
  if (was_projected) {
    const EditCode rc = resort_face_edges(f);
    if (rc != EditCode::Ok) {
      return pr.fail(rc, rep_.kind, rep_.index);
    }
  }
  set_dirty();
  pr.op.u.append(f.i);
  pr.op.u.append(l.i);
  return pr.finish();
}

} // namespace ubertopo::topo
