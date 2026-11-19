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

#include "litestl/util/vector.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/edit/edit_types.h"
#include "ubertopo/topo/edit/editor_grid.h"
#include "ubertopo/topo/edit/editor_index.h"
#include "ubertopo/topo/edit/journal.h"
#include "ubertopo/topo/edit/propagate.h"

namespace ubertopo::topo {

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
  EditCode validate_now(const litestl::util::Vector<uint32_t> *touched);
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
  /** Re-sorts the radial cycle of @p e from its current members plus @p extra. */
  EditCode resort_radial(EdgeRef e, std::span<const uint32_t> extra = {});
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
  /** build_regions' winning hint of region @p r (kNoId if none). */
  ident::PersistentId region_winning_hint(uint32_t r) const;
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

extern template class Editor<double>;
extern template class Editor<float>;
extern template class Editor<geom::Lattice>;

} // namespace ubertopo::topo
