#pragma once

/* The edit journal (design 04 §6; plan P3 §3.2, C9, C12, C13).
 *
 * A committed transaction keeps
 *  - its physical undo log (TxnLog): the pre-transaction bytes of every row it
 *    wrote (all columns of the row), its allocator events and the group sizes
 *    and regions_dirty at its start. Undo replays the log in reverse and is
 *    exact: the content hash returns to its pre-transaction value;
 *  - a full copy-on-write snapshot instead, for barrier transactions
 *    (rebuild_regions), which replace whole domains;
 *  - its operation records: redo re-executes them through a fresh editor
 *    (the primitives are deterministic, and undo restored the exact
 *    pre-state, allocator included);
 *  - its ID lineage.
 *
 * Undo and redo require that the complex wasn't changed outside the editor
 * since (checked with the version stamp), and that no group's column set
 * changed. A new transaction after undos drops the redo tail.
 *
 * JournalRoots registers everything the journal holds with a PoolStore
 * (DEC-015): captured row bytes, barrier snapshots and SetAttr payloads. */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "litestl/attr/pool_store.h"
#include "litestl/util/vector.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/edit/edit_types.h"

namespace ubertopo::topo {

struct RowCapture {
  uint32_t row;
  uint32_t off; /* into TxnLog::bytes */
  uint32_t epoch;
  uint8_t g; /* Grp */
};

enum class AllocKind : uint8_t { FromFree, Append, Free };
struct AllocEvent {
  uint32_t row;
  uint8_t g;
  AllocKind kind;
};

/** Physical undo log of one transaction. */
struct TxnLog {
  litestl::util::Vector<RowCapture> rows;
  litestl::util::Vector<uint8_t> bytes;
  litestl::util::Vector<AllocEvent> alloc;
  uint32_t start_size[kGroupCount] = {};
  bool regions_dirty = true;
};

template <geom::CoordType C> struct Txn {
  std::string label;
  std::vector<OpRecord<C>> ops;
  std::vector<Lineage> lineage;
  TxnLog log;
  std::unique_ptr<Complex<C>> snapshot; /* barrier: the complex before */
  uint64_t schema_ver[kGroupCount] = {}; /* column sets after the transaction */
};

template <geom::CoordType C> class Journal {
public:
  uint32_t undo_count() const
  {
    return uint32_t(done_.size());
  }
  uint32_t redo_count() const
  {
    return uint32_t(undone_.size());
  }
  /** Applied transactions, oldest first. */
  const Txn<C> &txn(uint32_t i) const
  {
    return done_[i];
  }
  void clear()
  {
    done_.clear();
    undone_.clear();
  }

  /** IDs derived from @p id by the applied transactions (direct children). */
  std::vector<ident::PersistentId> forward(ident::PersistentId id) const
  {
    std::vector<ident::PersistentId> out;
    for (const Txn<C> &t : done_) {
      for (const Lineage &l : t.lineage) {
        if (l.parent == id && !(l.child == id)) {
          out.push_back(l.child);
        }
      }
    }
    return out;
  }
  /** IDs @p id was derived from (direct parents). */
  std::vector<ident::PersistentId> backward(ident::PersistentId id) const
  {
    std::vector<ident::PersistentId> out;
    for (const Txn<C> &t : done_) {
      for (const Lineage &l : t.lineage) {
        if (l.child == id && !(l.parent == id)) {
          out.push_back(l.parent);
        }
      }
    }
    return out;
  }

private:
  template <geom::CoordType> friend class Editor;
  template <geom::CoordType> friend class JournalRoots;
  std::vector<Txn<C>> done_;
  std::vector<Txn<C>> undone_; /* redo stack: last undone at the back */
  /* complex version after the last commit / undo / redo through the journal */
  uint64_t expected_version_ = 0;
};

/** The groups of a complex in Grp order. */
template <geom::CoordType C> inline litestl::attr::AttrGroup &group_of(Complex<C> &k, Grp g)
{
  switch (g) {
    case Grp::Verts:
      return k.verts.group();
    case Grp::Edges:
      return k.edges.group();
    case Grp::Coedges:
      return k.coedges.group();
    case Grp::Loops:
      return k.loops.group();
    case Grp::Faces:
      return k.faces.group();
    case Grp::Sides:
      return k.faces.sides().group();
    case Grp::Shells:
      return k.shells.group();
    case Grp::Regions:
      return k.regions.group();
    case Grp::Global:
      break;
  }
  return k.global.group();
}
template <geom::CoordType C> inline const litestl::attr::AttrGroup &group_of(const Complex<C> &k, Grp g)
{
  return group_of(const_cast<Complex<C> &>(k), g);
}

/** PoolStore root over a journal's captured bytes, barrier snapshots and
 * SetAttr payloads (plan P3 C9). Captured bytes are interpreted with the
 * columns of @p k, which the journal's schema check keeps compatible;
 * transactions whose recorded column sets no longer match are skipped (they
 * can't be undone either). */
template <geom::CoordType C> class JournalRoots final : public litestl::attr::RefRoot {
public:
  JournalRoots(Journal<C> &j, Complex<C> &k) : j_(j), k_(k) {}

  void visit_refs(litestl::attr::RefVisitor &v) const override
  {
    each([&](const litestl::attr::ColumnBase &col, uint8_t *bytes) { col.visit_elem_refs(bytes, v); },
         [&](litestl::attr::ColumnBase &col) { col.visit_refs(v); });
  }
  void remap_refs(const litestl::attr::RefRemap &m) override
  {
    each([&](const litestl::attr::ColumnBase &col, uint8_t *bytes) { col.remap_elem_refs(bytes, m); },
         [&](litestl::attr::ColumnBase &col) { col.remap_refs(m); });
  }

private:
  template <typename ElemFn, typename ColFn> void each(ElemFn &&elem, ColFn &&whole) const
  {
    for (auto *list : {&j_.done_, &j_.undone_}) {
      for (Txn<C> &t : *list) {
        if (!compatible(t)) {
          continue;
        }
        for (const RowCapture &rc : t.log.rows) {
          litestl::attr::AttrGroup &g = group_of(k_, Grp(rc.g));
          uint8_t *p = t.log.bytes.data() + rc.off;
          for (uint32_t c = 0; c < g.column_count(); c++) {
            const litestl::attr::ColumnBase &col = g.column(c);
            if (col.holds_refs()) {
              elem(col, p);
            }
            p += col.elem_size();
          }
        }
        for (OpRecord<C> &op : t.ops) {
          if (op.kind == OpKind::SetAttr && op.u.size() >= 3) {
            const litestl::attr::ColumnBase &col = group_of(k_, Grp(op.u[0])).column(op.u[2]);
            if (col.holds_refs() && op.bytes.size() == col.elem_size()) {
              elem(col, op.bytes.data());
            }
          }
        }
        if (t.snapshot) {
          for (uint32_t gi = 0; gi < kGroupCount; gi++) {
            litestl::attr::AttrGroup &g = group_of(*t.snapshot, Grp(gi));
            for (uint32_t c = 0; c < g.column_count(); c++) {
              if (g.column(c).holds_refs()) {
                whole(g.column(c));
              }
            }
          }
        }
      }
    }
  }
  bool compatible(const Txn<C> &t) const
  {
    for (uint32_t gi = 0; gi < kGroupCount; gi++) {
      if (t.schema_ver[gi] != group_of(k_, Grp(gi)).schema_version()) {
        return false;
      }
    }
    return true;
  }

  Journal<C> &j_;
  Complex<C> &k_;
};

} // namespace ubertopo::topo
