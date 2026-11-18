#pragma once

/* The editor's derived indices (plan P3b W4, §3.4, C10, C13).
 *
 * Kept per complex while an Editor is attached, so no primitive scans the
 * whole complex:
 *  - persistent IDs of vertices, edges and faces, with a count per ID, so
 *    duplicates are visible (check-on-insert);
 *  - inclusions: container (face or region) -> sorted packed elements
 *    ((index << 1) | is_edge);
 *  - container users per hint (hint_in_use);
 *  - side carriers per hint (hint_carried), and per-region votes for
 *    build_regions' winning hint.
 *
 * Maintenance is by dirty rows, not by write helpers: Editor::touch() marks
 * every row it is about to write, restore() marks every row it puts back or
 * drops, and the index re-reads the marked rows before answering a query.
 * Each row's indexed keys are remembered, so a flush first removes the keys
 * of every marked row and then adds their current keys; a row freed and an
 * ID re-used by another row in the same transaction never look like a
 * duplicate. Barriers (snapshot restores) invalidate everything; the next
 * query rebuilds in O(N). */

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
  /** build_regions' winning hint of region @p r (most sides; ties to the
   * smaller hint), or kNoId. The unbounded hint never wins here. */
  PersistentId winning_hint(uint32_t r) const
  {
    auto it = votes_.find(r);
    PersistentId best{};
    uint32_t n = 0;
    if (it != votes_.end()) {
      for (auto &[h, c] : it->second) {
        if (c > n) {
          n = c;
          best = h;
        }
      }
    }
    return best == ident::unbounded_region_id() ? PersistentId{} : best;
  }
  /** True if a region other than @p r and region 0 also wins @p h. */
  bool other_winner(uint32_t r, PersistentId h) const
  {
    auto it = hint_regions_.find(h);
    if (it == hint_regions_.end()) {
      return false;
    }
    for (auto &[r2, n] : it->second) {
      if (r2 != r && r2 != 0 && n > 0 && winning_hint(r2) == h) {
        return true;
      }
    }
    return false;
  }

  /** Same contents as @p o (tests: an index maintained by dirty rows must
   * equal one rebuilt from scratch). Both must be flushed. */
  bool same_as(const EditIndex &o) const
  {
    return ids_[0] == o.ids_[0] && ids_[1] == o.ids_[1] && ids_[2] == o.ids_[2] && dups_ == o.dups_ &&
           incl_ == o.incl_ && users_ == o.users_ && carriers_ == o.carriers_ && votes_ == o.votes_ &&
           hint_regions_ == o.hint_regions_;
  }

private:
  struct Dirty {
    uint8_t kind; /* 0 vertex, 1 edge, 2 face */
    uint32_t row;
  };
  struct ElemKey { /* vertices and edges */
    bool live = false;
    PersistentId id{};
    ident::ContainerRef c{};
    PersistentId hint{};
  };
  struct FaceKey {
    bool live = false;
    PersistentId id{};
    uint32_t region[2] = {NONE, NONE};
    PersistentId hint[2] = {};
  };


  ElemKey vkey(const Complex<C> &k, uint32_t v) const
  {
    ElemKey x;
    if (v < k.verts.capacity() && k.verts.live(v)) {
      x.live = true;
      x.id = k.verts.template get<".id">()[v];
      x.c = k.verts.template get<".v.container">()[v];
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
      x.c = k.edges.template get<".e.container">()[e];
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
      const U32x2 r = k.faces.template get<".f.region">()[f];
      const auto &sh = k.faces.sides().template get<".fs.hint">();
      for (uint32_t s = 0; s < 2; s++) {
        x.region[s] = r[s];
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
    if (!x.c.is_none()) {
      if (delta > 0) {
        incl_[x.c.bits].insert(packed);
      }
      else {
        auto it = incl_.find(x.c.bits);
        it->second.erase(packed);
        if (it->second.empty()) {
          incl_.erase(it);
        }
      }
    }
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
      const PersistentId h = x.hint[s];
      if (h == ident::kNoId) {
        continue;
      }
      bump(carriers_, h, delta);
      if (x.region[s] != NONE) {
        auto &v = votes_[x.region[s]];
        bump(v, h, delta);
        if (v.empty()) {
          votes_.erase(x.region[s]);
        }
        auto &hr = hint_regions_[h];
        uint32_t &n = hr[x.region[s]];
        n = uint32_t(int(n) + delta);
        if (n == 0) {
          hr.erase(x.region[s]);
          if (hr.empty()) {
            hint_regions_.erase(h);
          }
        }
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
    votes_.clear();
    hint_regions_.clear();
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
  std::map<uint32_t, std::map<PersistentId, uint32_t>> votes_; /* region -> hint -> sides */
  std::map<PersistentId, std::map<uint32_t, uint32_t>> hint_regions_;
};

} // namespace ubertopo::topo
