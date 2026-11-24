#pragma once

/* Editor<C>: journaled, invertible editing of a Complex<C> (plan P3a,
 * docs/plans/2026-11-18-p3-editing.md; design 01 §7, 03 §6, 04 §6).
 *
 * Transactions. Every primitive runs in its own transaction; begin()/commit()
 * group primitives into one undo unit, and nested begin() is a savepoint.
 * Before the first write to a row in a savepoint the editor captures all of
 * that row's column bytes; rollback() restores them, reverses the allocator
 * events and shrinks the groups, so a failed primitive or a rolled-back
 * transaction leaves the complex with a bit-identical content hash.
 *
 * Primitives check their preconditions without writing, change topology
 * through the editor's write helpers only (never column.set directly),
 * propagate attributes (propagate.h), validate and record lineage. A
 * primitive that would leave an invalid complex rolls back and returns
 * EditCode::Invalid with the validator's report.
 *
 * Regions are derived (plan §2): build_regions() is their only writer, run
 * through rebuild_regions() (a barrier transaction). Edits that change faces
 * set regions_dirty.
 *
 * Undoing a barrier (rebuild_regions) restores a snapshot of the whole
 * complex: pointers to dynamic columns taken before it must be fetched again.
 *
 * Curves (plan P4a §5.2): intern_geom (intern_curve) adds definitions to the
 * complex's geometry store without journaling; set_edge_curve, split_edge_at,
 * set_face_surface and set_coedge_pcurve are journaled (plan P4b §5.1). The editor never renumbers the live store: undoing a barrier
 * restores the groups and keeps the store, and the journal roots every ref
 * it holds (JournalRoots), so collects (collect_geometry, and every barrier
 * after it commits) never free an entry undo or redo could bring back.
 *
 * Writes from outside the editor are allowed between transactions, but they
 * truncate the journal (undo can't cross them). A frozen complex is refused;
 * thaw it first. Columns may not be added or removed inside a transaction. */

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "litestl/io/bytes.h"
#include "litestl/util/vector.h"
#include "ubertopo/curves/curve.h"
#include "ubertopo/curves/surface.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/geom_store.h"
#include "ubertopo/topo/edit/edit_types.h"
#include "ubertopo/topo/edit/editor_grid.h"
#include "ubertopo/topo/edit/editor_index.h"
#include "ubertopo/topo/edit/editor_region_hints.h"
#include "ubertopo/topo/edit/journal.h"
#include "ubertopo/topo/edit/propagate.h"

namespace ubertopo::topo {

namespace edit_detail {
template <geom::CoordType C> struct HalfPlanes;
}

enum class ValidateMode : uint8_t {
  Full,     /* validate(k, Global) after every primitive (the oracle) */
  Local,    /* validate_local on the elements each primitive touched */
  None,     /* no validation: the caller guarantees validity (tests, bulk tools) */
  OnCommit, /* validate_local once, on everything the outermost transaction
             * touched; intermediate states may be invalid (plan P3b §3.5) */
};

struct EditorOptions {
  ValidateMode validate = ValidateMode::Full;
  /** Redo re-executes without validating; with this set it compares the
   * result's content hash with the one stored at commit and fails with
   * JournalStale on a mismatch (plan P3b C11). Costs O(N) per commit. */
#ifdef NDEBUG
  bool verify_redo = false;
#else
  bool verify_redo = true;
#endif
};

/** A container for embed(): a face, a region (by index; needs current
 * regions), or none. */
struct EmbedTarget {
  enum class Kind : uint8_t { None, Face, Region };
  Kind kind = Kind::None;
  uint32_t index = NONE;
  static EmbedTarget face(FaceRef f)
  {
    return {Kind::Face, f.i};
  }
  static EmbedTarget region(RegionRef r)
  {
    return {Kind::Region, r.i};
  }
};

template <geom::CoordType C> class Editor {
public:
  using Point = geom::Point<C>;

  explicit Editor(Complex<C> &k,
                  Journal<C> *journal = nullptr,
                  const PropagationRules *rules = nullptr,
                  EditorOptions opt = {});
  ~Editor();
  Editor(const Editor &) = delete;
  Editor &operator=(const Editor &) = delete;

  Complex<C> &complex()
  {
    return k_;
  }
  const EditReport &report() const
  {
    return rep_;
  }
  EditorOptions &options()
  {
    return opt_;
  }

  /* ---- transactions ---- */
  EditCode begin(std::string_view label = {});
  EditCode commit();
  EditCode rollback();
  uint32_t depth() const
  {
    return uint32_t(sp_.size());
  }

  /* ---- journal (needs a journal and no open transaction) ---- */
  EditCode undo();
  EditCode redo();

  /* ---- primitives (P3a) ---- */
  EditCode make_vertex(const Point &p, ident::PersistentId id, VertRef *out = nullptr);
  EditCode kill_vertex(VertRef v);
  /** A wire from @p a to @p b; @p id defaults to edge_id of the vertex IDs.
   * @p in embeds it on creation: a wire lying in a face must be made
   * embedded in it, or the validator reports WireCrossesFace. */
  EditCode make_edge(VertRef a,
                     VertRef b,
                     ident::PersistentId id = ident::kNoId,
                     EdgeRef *out = nullptr,
                     EmbedTarget in = {});
  EditCode kill_edge(EdgeRef e);
  EditCode set_position(VertRef v, const Point &p);
  /** Embeds an isolated vertex or a wire (EmbedTarget::Kind::None unembeds). */
  EditCode embed_vertex(VertRef v, EmbedTarget t);
  EditCode embed_edge(EdgeRef e, EmbedTarget t);
  EditCode split_edge(EdgeRef e, const Point &p, VertRef *v_out = nullptr, EdgeRef *e_out = nullptr);
  EditCode join_edge(VertRef v, EdgeRef *survivor = nullptr);
  /** A face over existing edges: @p verts holds the loops back to back. */
  EditCode make_face(std::span<const VertRef> verts,
                     std::span<const uint32_t> loop_sizes,
                     ident::PersistentId id,
                     uint8_t flags = 0,
                     ident::PersistentId front_hint = ident::kNoId,
                     ident::PersistentId back_hint = ident::kNoId,
                     FaceRef *out = nullptr);
  EditCode kill_face(FaceRef f);
  /** Splits @p f by a chord from @p a to @p b (vertices of its outer loop).
   * The child containing the chord coedge a -> b keeps f's slot. */
  EditCode split_face(FaceRef f, VertRef a, VertRef b, FaceRef *other = nullptr, EdgeRef *chord = nullptr);
  /** Joins the two faces on either side of @p e and removes e. */
  EditCode join_faces(EdgeRef e, FaceRef *survivor = nullptr);
  EditCode flip_face(FaceRef f);
  /* ---- primitives (P3b W6) ---- */
  /** Turns a chain of wires embedded in @p f (edges in path order) into face
   * topology: a path between two outer-loop vertices splits f (as
   * split_face: the child holding the chain coedges a -> b keeps f's slot);
   * a closed cycle becomes a hole of f filled by a new face, *other. */
  EditCode imprint(FaceRef f, std::span<const EdgeRef> chain, FaceRef *other = nullptr);
  /** Inverts imprint from one edge of the chain between two faces: the
   * faces merge and the chain becomes wires embedded in the survivor. */
  EditCode unimprint(EdgeRef e, FaceRef *survivor = nullptr);
  /** A closed chain of wires embedded in @p f becomes a hole (an opening). */
  EditCode add_hole(FaceRef f, std::span<const EdgeRef> chain);
  /** A hole of @p f is filled; its edges and vertices become inclusions. */
  EditCode remove_hole(FaceRef f, LoopRef hole);
  /* ---- primitives (P3b W7) ---- */
  /** Merges each pair of coincident vertices into the one with the smaller
   * ID and fuses the parallel edges that result (smaller edge ID survives;
   * radial cycles re-sorted). One call may go from an invalid state
   * (coincident distinct vertices) to a valid one (plan P3b C3). */
  EditCode weld(std::span<const std::pair<VertRef, VertRef>> pairs);
  EditCode set_side_hint(FaceRef f, uint32_t side, ident::PersistentId hint);
  /** Writes one non-Topo attribute value (raw bytes of column @p column). */
  EditCode set_attr(Grp g, uint32_t row, uint32_t column, std::span<const uint8_t> bytes);
  /** Barrier: build_regions(), then validation; rolls back on failure. */
  EditCode rebuild_regions();
  /** Barrier: drops every tombstone (compact.h); outside transactions only.
   * Indices of every domain may change; Ok without a journal entry if there
   * is nothing to drop. */
  EditCode compact();

  /* ---- curves (P4a W6; editor_impl_curves.h) ---- */
  /** Interns @p def, a definition of @p kind (T is the kind's value type),
   * into the complex's store. It is re-validated by its kind's write/read
   * round trip (read re-runs the kind's checks and refuses non-canonical or
   * non-finite definitions), its inner refs must be live and of kinds the
   * holder accepts, and its evaluator must build. Not journaled and not a
   * write: version() is unchanged, and an entry nothing references never
   * changes a hash. Refused on a frozen complex. A surface kind's evaluator
   * is its SurfaceEval (plan P4b §5.1). */
  template <typename T>
  EditCode intern_geom(const curves::KindOps &kind, const T &def, litestl::attr::PoolRef *out);
  /** intern_geom under its P4a name. */
  template <typename T>
  EditCode intern_curve(const curves::KindOps &kind, const T &def, litestl::attr::PoolRef *out)
  {
    return intern_geom(kind, def, out);
  }
  /** Sets the carrier of @p e: vertex vs[0] at carrier(t0), vs[1] at
   * carrier(t1) (plan P4a §5.1). @p ref NONE makes e straight, with range
   * (0, 0). Checks the kind and the range here; the endpoint rule is the
   * validator's (CurveEndpoint). */
  EditCode set_edge_curve(EdgeRef e, litestl::attr::PoolRef ref, double t0, double t1);
  /** Splits a curved edge at carrier parameter @p t, strictly inside its
   * range: the new vertex at round_C(carrier(t)), the two halves sharing
   * the carrier with ranges (t0, t) and (t, t1). IDs and slots as in
   * split_edge; the chord skeleton changes, so incident planar faces must
   * stay planar (NonPlanarFace) and regions become dirty. */
  EditCode split_edge_at(EdgeRef e, double t, VertRef *v_out = nullptr, EdgeRef *e_out = nullptr);
  /* ---- surfaces (P4b W5; editor_impl_curves.h) ---- */
  /** Sets the surface of @p f, its Reversed flag, and the pcurves of all of
   * its coedges in one step, so every validation mode sees a whole surfaced
   * face: each listed coedge (of f, once) gets its pcurve, every other coedge
   * of f gets NONE. @p ref NONE clears the surface, the pcurves and Reversed;
   * it then takes reversed false and no pcurves. Refs must be NONE or live;
   * the kind, range and fit rules are the validator's (plan P4b §4.2). */
  EditCode set_face_surface(FaceRef f,
                            litestl::attr::PoolRef ref,
                            bool reversed,
                            std::span<const std::pair<CoedgeRef, litestl::attr::PoolRef>> pcurves = {});
  /** Sets one coedge's pcurve (NONE or live; rules as set_face_surface). */
  EditCode set_coedge_pcurve(CoedgeRef c, litestl::attr::PoolRef ref);
  /** Frees the store entries that neither the complex, the journal nor
   * @p extra reach. Outside transactions only; a journal invalidated by an
   * outside write is dropped first. */
  EditCode collect_geometry(std::span<litestl::attr::RefRoot *const> extra = {});

  /** Test hook: the maintained index equals one rebuilt from scratch. */
  bool index_consistent() const
  {
    EditIndex<C> fresh;
    fresh.flush(k_);
    idx_.flush(k_);
    return !idx_.valid() || idx_.same_as(fresh);
  }
  /** Test hook: the kept face-box grid equals one rebuilt from scratch. */
  bool grid_consistent() const
  {
    grid_.flush(k_);
    return grid_.same_as_fresh(k_);
  }
  /** Test hook: the cached winning hints of regions, if current, equal fresh
   * ones. Leaves the cache current. */
  bool region_hints_consistent() const
  {
    return region_hints_.consistent(k_);
  }
  /** The edge joining @p a and @p b, if any. */
  EdgeRef find_edge(VertRef a, VertRef b) const;

private:
  struct Savepoint {
    uint32_t rows, bytes, alloc, ops, lineage, epoch;
    uint32_t size[kGroupCount];
    bool regions_dirty;
  };

  /* RAII scope of one public primitive: a savepoint, failure reporting,
   * validation and op recording (editor.cc). */
  struct Prim {
    Editor &ed;
    OpRecord<C> op;
    EditCode code = EditCode::Ok;
    bool outer = false, done = false;
    Prim(Editor &e, OpKind kind);
    ~Prim();
    EditCode fail(EditCode c, ident::ElemKind kind = ident::ElemKind::Vertex, uint32_t index = NONE, const char *detail = nullptr);
    EditCode finish();
  };

  /* ---- transaction internals (editor.cc) ---- */
  EditCode begin_internal(std::string_view label);
  EditCode commit_internal(bool from_redo);
  void restore(TxnLog &log, uint32_t rows_mark, uint32_t alloc_mark, const uint32_t *sizes, bool regions_dirty);
  void rebuild_captured();
  EditCode apply_op(const OpRecord<C> &op);
  /* ---- P3b W6 cores (editor_impl_imprint.h); they set rep_ and return a
   * code, and the calling primitive fails with it ---- */
  EditCode fail_report(EditCode c, ident::ElemKind kind, uint32_t index, const char *detail = nullptr);
  EditCode read_chain(FaceRef f,
                      std::span<const EdgeRef> chain,
                      litestl::util::Vector<uint32_t, 32> &vs,
                      bool &closed);
  EditCode split_core(FaceRef f,
                      std::span<const uint32_t> chain_e,
                      std::span<const uint32_t> chain_v,
                      bool make_chord,
                      FaceRef *other,
                      EdgeRef *chord);
  EditCode island_core(FaceRef f,
                       std::span<const uint32_t> chain_e,
                       std::span<const uint32_t> chain_v,
                       bool make_face,
                       FaceRef *island);
  void drop_hole(FaceRef f, LoopRef hole);
  /** rebuild_regions() / compact(): a snapshot transaction of its own. */
  EditCode run_barrier(OpKind kind);
  /** k_ = snapshot, keeping the live geometry store (plan P4a §5.2). */
  void restore_snapshot(Complex<C> &&snapshot);
  /** The collect behind collect_geometry and barriers. */
  bool collect_internal(std::span<litestl::attr::RefRoot *const> extra, const char **err);
  /** split_edge's body after its checks: @p frac is the propagation
   * fraction from vs[0]; @p t non-null for a curved split at *t. */
  EditCode split_impl(Prim &pr, EdgeRef e, const Point &p, double frac, const double *tc, VertRef *v_out, EdgeRef *e_out);
  EditCode validate_now(const litestl::util::Vector<uint32_t> *touched);
  /* the faces the edits of plan P4b §5.2 refuse (SurfacedFace) */
  bool surfaced(FaceRef f) const
  {
    return !k_.faces.template get<".f.surface">()[f.i].none();
  }
  bool edge_surfaced(EdgeRef e) const;
  bool vert_surfaced(VertRef v) const;
  void touched_filter(ValidateFilter &flt, const litestl::util::Vector<uint32_t> *touched) const;

  /* ---- write helpers: capture first, then write (editor.cc) ---- */
  void touch(Grp g, uint32_t row);
  uint32_t alloc(Grp g);
  void free(Grp g, uint32_t i);
  template <litestl::util::StrLiteral N, typename D, typename V> void put(D &d, Grp g, uint32_t row, const V &v)
  {
    touch(g, row);
    d.template get<N>().set(row, v);
  }
  template <litestl::util::StrLiteral N, typename V> void setV(VertRef v, const V &x)
  {
    put<N>(k_.verts, Grp::Verts, v.i, x);
  }
  template <litestl::util::StrLiteral N, typename V> void setE(EdgeRef e, const V &x)
  {
    put<N>(k_.edges, Grp::Edges, e.i, x);
  }
  template <litestl::util::StrLiteral N, typename V> void setC(CoedgeRef c, const V &x)
  {
    put<N>(k_.coedges, Grp::Coedges, c.i, x);
  }
  template <litestl::util::StrLiteral N, typename V> void setL(LoopRef l, const V &x)
  {
    put<N>(k_.loops, Grp::Loops, l.i, x);
  }
  template <litestl::util::StrLiteral N, typename V> void setF(FaceRef f, const V &x)
  {
    put<N>(k_.faces, Grp::Faces, f.i, x);
  }
  template <litestl::util::StrLiteral N, typename V> void setS(uint32_t side_row, const V &x)
  {
    put<N>(k_.faces.sides(), Grp::Sides, side_row, x);
  }
  void set_disk(EdgeRef e, uint32_t entry, uint32_t packed);
  void set_dirty();

  /* ---- propagation (editor.cc) ---- */
  bool propagate(Grp g, PropEvent ev, const MixBuild &mb);

  /* ---- cycles (cycles.cc) ---- */
  void disk_insert(EdgeRef e, uint32_t slot);
  void disk_remove(EdgeRef e, uint32_t slot);
  void disk_replace(EdgeRef old_e, uint32_t old_slot, EdgeRef new_e, uint32_t new_slot);
  /** Re-sorts the radial cycle of @p e from its current members plus @p extra.
   * @p hp, if given, is a half-plane cache shared across calls: valid while
   * no position, loop or carrier changes, and needed by callers that re-sort
   * many edges of one face, since a projected face's cache entry costs its
   * T(f). */
  EditCode resort_radial(EdgeRef e, std::span<const uint32_t> extra = {}, edit_detail::HalfPlanes<C> *hp = nullptr);
  void radial_remove(CoedgeRef c);
  void refresh_ec(EdgeRef e);
  void link_radial(EdgeRef e, std::span<const uint32_t> order);
  EditCode resort_face_edges(FaceRef f);
  /** Clears the projection axis of a projected face that is now planar (C5). */
  void fix_projection(FaceRef f);

  /* ---- queries (editor.cc) ---- */
  bool id_used(Grp g, ident::PersistentId id);
  bool live(Grp g, uint32_t i) const;
  void inclusions(FaceRef f, litestl::util::Vector<uint32_t, 16> &out) const; /* (index << 1) | is_edge */
  bool hint_in_use(ident::PersistentId h) const;
  bool hint_carried(ident::PersistentId h, uint32_t skip_side_a, uint32_t skip_side_b) const;
  uint32_t edge_slot(EdgeRef e, VertRef v) const;
  litestl::util::Vector<CoedgeRef, 8> radial_members(EdgeRef e) const;

  /* ---- primitive bodies ---- */
  EditCode embed_impl(bool is_edge, uint32_t index, EmbedTarget t);

  Complex<C> &k_;
  Journal<C> *journal_;
  const PropagationRules *rules_;
  EditorOptions opt_;
  EditReport rep_;

  /* current transaction */
  TxnLog log_;
  std::vector<OpRecord<C>> ops_;
  std::vector<Lineage> lineage_;
  std::string label_;
  std::vector<Savepoint> sp_;
  std::unique_ptr<Complex<C>> barrier_snapshot_;
  OpKind barrier_kind_ = OpKind::Compact; /* the barrier barrier_snapshot_ belongs to */
  /** rebuild_regions' validation in Local and OnCommit modes (plan
   * 2026-11-22 §2.1, §8): the combinatorial checks that read regions
   * (validate_regions), then the embedding layer on the elements with
   * region containers. */
  ValidationReport validate_region_barrier();
  uint64_t start_schema_[kGroupCount] = {};
  std::unordered_map<uint64_t, uint32_t> captured_; /* (g << 32 | row) -> epoch */
  uint32_t epoch_ = 0;
  bool in_prim_ = false;
  bool redoing_ = false;
  /* rows written or allocated by the current outer primitive (Local), and
   * by the whole outermost transaction (OnCommit; rows freed and re-used
   * stay in it) */
  litestl::util::Vector<uint32_t> touched_[kGroupCount];
  litestl::util::Vector<uint32_t> txn_touched_[kGroupCount];
  /* the face-box grid (editor_grid.h), kept like the index */
  mutable EditGrid<C> grid_;
  /* regions' winning hints (editor_region_hints.h), validated by column
   * versions: nothing marks or invalidates it */
  mutable EditRegionHints<C> region_hints_;
  /** The index as an IdOracle for validate_local. */
  struct IndexIds final : IdOracle {
    const EditIndex<C> *idx = nullptr;
    uint32_t count(ident::ElemKind kind, ident::PersistentId id) const override
    {
      const uint32_t slot = kind == ident::ElemKind::Vertex ? 0 : kind == ident::ElemKind::Edge ? 1 : 2;
      return idx->id_count(slot, id);
    }
  };

  /* derived indices (editor_index.h); idx_stamp_ is the complex version
   * they were last consistent with, apart from rows marked dirty */
  mutable EditIndex<C> idx_;
  uint64_t idx_stamp_ = 0;
  /** Marks the index consistent with the current version (after any editor
   * operation; plan P3b C10). */
  void sync_index_stamp()
  {
    idx_stamp_ = k_.version();
  }
  /** Invalidates the index if something wrote outside the editor. */
  void check_index_stamp()
  {
    if (k_.version() != idx_stamp_) {
      idx_.invalidate();
      grid_.invalidate();
    }
  }
};

template <geom::CoordType C>
template <typename T>
EditCode Editor<C>::intern_geom(const curves::KindOps &kind, const T &def, litestl::attr::PoolRef *out)
{
  namespace attr = litestl::attr;
  rep_ = EditReport{};
  auto fail = [&](EditCode c, const char *d) {
    rep_.code = c;
    rep_.detail = d;
    return c;
  };
  if (k_.frozen()) {
    return fail(EditCode::Frozen, nullptr);
  }
  using P = attr::InternPool<T>;
  const uint32_t pool = curves::pool_id(kind);
  if (pool == 0 || pool > k_.geom.pool_count() || k_.geom.pool_any(pool).class_tag() != P::static_tag()) {
    return fail(EditCode::BadArgument, "the definition's type is not the kind's");
  }
  bool inner_ok = true;
  attr::PoolTraits<T>::for_each_ref(def, [&](attr::PoolRef r) {
    attr::RefSite site;
    site.kind = attr::RefSite::Inner;
    site.a = pool;
    if (!attr::well_formed(r) || (!r.none() && (!k_.geom.live(r) || !accept_geom_ref(k_.geom, site, r)))) {
      inner_ok = false;
    }
  });
  if (!inner_ok) {
    return fail(EditCode::BadArgument, "an inner ref is dead or of a kind the holder refuses");
  }
  litestl::io::ByteWriter w;
  def.write(w);
  litestl::io::ByteReader r(w.data(), w.size());
  T v;
  if (!T::read(r, v, attr::PoolReadLimits{})) { /* every kind's read re-runs make: canonical and valid */
    return fail(EditCode::BadArgument, "the definition is not canonical or not valid");
  }
  const attr::PoolRef ref = k_.geom.template intern<P>(pool, std::move(v));
  const bool builds = kind.as_surface()                    ? bool(curves::surface(k_.geom, ref))
                      : curves::curve_dim(k_.geom, ref) == 3 ? bool(curves::curve3(k_.geom, ref))
                                                             : bool(curves::curve2(k_.geom, ref));
  if (!builds) {
    /* the entry stays until the next collect; nothing references it */
    return fail(EditCode::BadArgument, "the definition's evaluator does not build");
  }
  if (out) {
    *out = ref;
  }
  return EditCode::Ok;
}

extern template class Editor<double>;
extern template class Editor<float>;
extern template class Editor<geom::Lattice>;

} // namespace ubertopo::topo
