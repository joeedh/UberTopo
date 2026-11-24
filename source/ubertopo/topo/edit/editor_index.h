#pragma once

/* The editor's derived indices (plan P3b W4, §3.4, C10, C13).
 *
 * Kept per complex while an Editor is attached, so no primitive scans the
 * whole complex:
 *  - persistent IDs of vertices, edges and faces, with a count per ID, so
 *    duplicates are visible (check-on-insert);
 *  - inclusions: container face -> sorted packed elements
 *    ((index << 1) | is_edge);
 *  - container users per hint (hint_in_use);
 *  - side carriers per hint (hint_carried).
 * Nothing here depends on region indices, which most region rebuilds shift
 * (plan 2026-11-24). Regions' winning hints are a separate cache,
 * editor_region_hints.h.
 *
 * Maintenance is by dirty rows, not by write helpers: Editor::touch() marks
 * every row it is about to write, restore() marks every row it puts back or
 * drops, and the index re-reads the marked rows before answering a query.
 * Each row's indexed keys are remembered, so a flush first removes the keys
 * of every marked row and then adds their current keys; a row freed and an
 * ID re-used by another row in the same transaction never look like a
 * duplicate. A compact barrier (and its undo) renumbers rows, so it
 * rebuilds everything in O(N log N); rebuild_regions and its undo change
 * only regions and resolved containers (refresh_containers). */

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "litestl/util/vector.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/edit/edit_types.h"

namespace ubertopo::topo {

template <geom::CoordType C> class EditIndex {
public:
  using PersistentId = ident::PersistentId;

  void invalidate()
  {
    valid_ = false;
    dirty_.clear();
  }
  bool valid() const
  {
    return valid_;
  }
  /** Marks a row about to change (face sides mark their face). */
  void mark(Grp g, uint32_t row)
  {
    if (!valid_) {
      return;
    }
    switch (g) {
      case Grp::Verts:
        dirty_.push_back({0, row});
        break;
      case Grp::Edges:
        dirty_.push_back({1, row});
        break;
      case Grp::Faces:
        dirty_.push_back({2, row});
        break;
      case Grp::Sides:
        dirty_.push_back({2, row / 2});
        break;
      default:
        break;
    }
  }

  /** Brings the index up to date with @p k. */
  void flush(const Complex<C> &k)
  {
    if (!valid_) {
      rebuild(k);
      return;
    }
    if (dirty_.empty()) {
      return;
    }
    for (const Dirty &d : dirty_) {
      remove_row(d.kind, d.row);
    }
    for (const Dirty &d : dirty_) {
      add_row(k, d.kind, d.row);
    }
    dirty_.clear();
  }
  /** Brings the index up to date after build_regions, or after restoring
   * the state before one (plans 2026-11-22 §2.2, 2026-11-24). Of what the
   * index holds, build_regions changes only containers of hinted elements:
   * it resolves them to regions, so a hinted element whose container was a
   * face (the editor makes none, but a loaded complex may hold one) leaves
   * that face's inclusions. */
  void refresh_containers(const Complex<C> &k)
  {
    if (!valid_) {
      rebuild(k);
      return;
    }
    flush(k);
    if (users_.empty()) {
      return; /* no hinted element */
    }
    auto elems = [&](std::vector<ElemKey> &keys, const auto &col, uint32_t tag) {
      for (uint32_t i = 0; i < uint32_t(keys.size()); i++) {
        ElemKey &x = keys[i];
        if (!x.live || x.hint == ident::kNoId) {
          continue;
        }
        const ident::ContainerRef c = face_only(col[i]);
        if (!(x.c == c)) {
          container(x.c, (i << 1) | tag, -1);
          x.c = c;
          container(x.c, (i << 1) | tag, +1);
        }
      }
    };
    elems(vkeys_, k.verts.template get<".v.container">(), 0);
    elems(ekeys_, k.edges.template get<".e.container">(), 1);
  }

  /* ---- queries (flush first) ---- */
  uint32_t id_count(uint32_t slot, PersistentId id) const
  {
    auto it = ids_[slot].find(id);
    return it == ids_[slot].end() ? 0 : it->second;
  }
  /** Number of IDs held by more than one live element. */
  uint32_t duplicates() const
  {
    return dups_;
  }
  void inclusions(ident::ContainerRef c, litestl::util::Vector<uint32_t, 16> &out) const
  {
    out.clear();
    auto it = incl_.find(c.bits);
    if (it != incl_.end()) {
      for (uint32_t x : it->second) {
        out.append(x);
      }
    }
  }
  bool hint_in_use(PersistentId h) const
  {
    auto it = users_.find(h);
    return it != users_.end() && it->second > 0;
  }
  uint32_t carriers(PersistentId h) const
  {
    auto it = carriers_.find(h);
    return it == carriers_.end() ? 0 : it->second;
  }

  /** Same contents as @p o (tests: an index maintained by dirty rows must
   * equal one rebuilt from scratch). Both must be flushed. */
  bool same_as(const EditIndex &o) const
  {
    return ids_[0] == o.ids_[0] && ids_[1] == o.ids_[1] && ids_[2] == o.ids_[2] && dups_ == o.dups_ &&
           incl_ == o.incl_ && users_ == o.users_ && carriers_ == o.carriers_;
  }

private:
  struct Dirty {
    uint8_t kind; /* 0 vertex, 1 edge, 2 face */
    uint32_t row;
  };
  struct ElemKey { /* vertices and edges */
    bool live = false;
    PersistentId id{};
    ident::ContainerRef c{}; /* face_only */
    PersistentId hint{};
  };
  struct FaceKey {
    bool live = false;
    PersistentId id{};
    PersistentId hint[2] = {};
  };

  /* the part of a container the index holds: a face */
  static ident::ContainerRef face_only(ident::ContainerRef c)
  {
    return c.kind() == ident::ContainerKind::Face ? c : ident::ContainerRef{};
  }
  ElemKey vkey(const Complex<C> &k, uint32_t v) const
  {
    ElemKey x;
    if (v < k.verts.capacity() && k.verts.live(v)) {
      x.live = true;
      x.id = k.verts.template get<".id">()[v];
      x.c = face_only(k.verts.template get<".v.container">()[v]);
      x.hint = k.verts.template get<".v.container_hint">()[v];
    }
    return x;
  }
  ElemKey ekey(const Complex<C> &k, uint32_t e) const
  {
    ElemKey x;
    if (e < k.edges.capacity() && k.edges.live(e)) {
      x.live = true;
      x.id = k.edges.template get<".id">()[e];
      x.c = face_only(k.edges.template get<".e.container">()[e]);
      x.hint = k.edges.template get<".e.container_hint">()[e];
    }
    return x;
  }
  FaceKey fkey(const Complex<C> &k, uint32_t f) const
  {
    FaceKey x;
    if (f < k.faces.capacity() && k.faces.live(f)) {
      x.live = true;
      x.id = k.faces.template get<".id">()[f];
      const auto &sh = k.faces.sides().template get<".fs.hint">();
      for (uint32_t s = 0; s < 2; s++) {
        x.hint[s] = sh[2 * f + s];
      }
    }
    return x;
  }

  void count_id(uint32_t slot, PersistentId id, int delta)
  {
    uint32_t &n = ids_[slot][id];
    if (delta > 0) {
      if (++n == 2) {
        dups_++;
      }
    }
    else {
      if (n-- == 2) {
        dups_--;
      }
      if (n == 0) {
        ids_[slot].erase(id);
      }
    }
  }
  /* an element's entry in its container face's inclusion list */
  void container(ident::ContainerRef c, uint32_t packed, int delta)
  {
    if (c.is_none()) {
      return;
    }
    if (delta > 0) {
      incl_[c.bits].insert(packed);
    }
    else {
      auto it = incl_.find(c.bits);
      it->second.erase(packed);
      if (it->second.empty()) {
        incl_.erase(it);
      }
    }
  }
  static void bump(std::map<PersistentId, uint32_t> &m, PersistentId h, int delta)
  {
    uint32_t &n = m[h];
    n = uint32_t(int(n) + delta);
    if (n == 0) {
      m.erase(h);
    }
  }
  void elem(const ElemKey &x, uint32_t slot, uint32_t packed, int delta)
  {
    if (!x.live) {
      return;
    }
    count_id(slot, x.id, delta);
    container(x.c, packed, delta);
    if (!(x.hint == ident::kNoId)) {
      bump(users_, x.hint, delta);
    }
  }
  void face(const FaceKey &x, int delta)
  {
    if (!x.live) {
      return;
    }
    count_id(2, x.id, delta);
    for (uint32_t s = 0; s < 2; s++) {
      if (!(x.hint[s] == ident::kNoId)) {
        bump(carriers_, x.hint[s], delta);
      }
    }
  }
  void remove_row(uint8_t kind, uint32_t row)
  {
    if (kind == 0) {
      if (row < vkeys_.size()) {
        elem(vkeys_[row], 0, row << 1, -1);
        vkeys_[row] = ElemKey{};
      }
    }
    else if (kind == 1) {
      if (row < ekeys_.size()) {
        elem(ekeys_[row], 1, (row << 1) | 1, -1);
        ekeys_[row] = ElemKey{};
      }
    }
    else if (row < fkeys_.size()) {
      face(fkeys_[row], -1);
      fkeys_[row] = FaceKey{};
    }
  }
  void add_row(const Complex<C> &k, uint8_t kind, uint32_t row)
  {
    if (kind == 0) {
      ElemKey x = vkey(k, row);
      if (row >= vkeys_.size()) {
        vkeys_.resize(row + 1);
      }
      if (vkeys_[row].live) {
        return; /* marked twice: already added in this flush */
      }
      elem(x, 0, row << 1, +1);
      vkeys_[row] = x;
    }
    else if (kind == 1) {
      ElemKey x = ekey(k, row);
      if (row >= ekeys_.size()) {
        ekeys_.resize(row + 1);
      }
      if (ekeys_[row].live) {
        return;
      }
      elem(x, 1, (row << 1) | 1, +1);
      ekeys_[row] = x;
    }
    else {
      FaceKey x = fkey(k, row);
      if (row >= fkeys_.size()) {
        fkeys_.resize(row + 1);
      }
      if (fkeys_[row].live) {
        return;
      }
      face(x, +1);
      fkeys_[row] = x;
    }
  }

  void rebuild(const Complex<C> &k)
  {
    for (auto &m : ids_) {
      m.clear();
    }
    dups_ = 0;
    incl_.clear();
    users_.clear();
    carriers_.clear();
    vkeys_.assign(k.verts.capacity(), ElemKey{});
    ekeys_.assign(k.edges.capacity(), ElemKey{});
    fkeys_.assign(k.faces.capacity(), FaceKey{});
    valid_ = true;
    dirty_.clear();
    k.verts.for_each_live([&](uint32_t v) { add_row(k, 0, v); });
    k.edges.for_each_live([&](uint32_t e) { add_row(k, 1, e); });
    k.faces.for_each_live([&](uint32_t f) { add_row(k, 2, f); });
  }

  bool valid_ = false;
  std::vector<Dirty> dirty_;
  std::vector<ElemKey> vkeys_, ekeys_;
  std::vector<FaceKey> fkeys_;
  std::map<PersistentId, uint32_t> ids_[3];
  uint32_t dups_ = 0;
  std::map<uint32_t, std::set<uint32_t>> incl_; /* container bits -> packed */
  std::map<PersistentId, uint32_t> users_, carriers_;
};

} // namespace ubertopo::topo
