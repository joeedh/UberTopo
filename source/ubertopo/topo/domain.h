#pragma once

/* A topological domain: a TypedGroup over a compile-time schema plus a slot
 * allocator (after Sculptcore's ElemData; plan P2 W4).
 *
 *  - Slots are live or free (".live"). free(i) resets every column of slot i
 *    to its default *except ".gen"*, then bumps ".gen" and adds i to the
 *    free set, so a stale Handle never resolves to a reused slot.
 *  - alloc() takes the *lowest* free slot (an ordered set; plan P3 C15 and
 *    P3b §3.4: insert, erase and min in O(log n)), so the
 *    allocation sequence is a function of ".live" alone: continuing in memory
 *    and continuing after read() / permute() allocate identically.
 *  - alloc_n(n) appends a dense range and ignores the freelist (bulk build).
 *  - The generation floor (plan P3b §3.1): no handle with gen >= gen_floor()
 *    was issued for an index at or beyond capacity(). Slots appended past
 *    the capacity start at the floor; permute() raises it over the indices
 *    it drops off the end, and replace_rows() over replaced rows. Rollback
 *    resizes don't touch it.
 *  - Copying a Domain is a snapshot (columns are copy-on-write). */

#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>

#include "litestl/attr/schema.h"
#include "litestl/util/span.h"
#include "litestl/util/vector.h"
#include "ubertopo/ident/ref.h"
#include "ubertopo/topo/schema.h"

namespace ubertopo::topo {

template <typename S> class Domain {
public:
  using schema = S;
  using Group = attr::TypedGroup<S>;

  explicit Domain(int page_shift = attr::kDefaultPageShift) : g_(page_shift) {}

  template <litestl::util::StrLiteral N> auto &get()
  {
    return g_.template get<N>();
  }
  template <litestl::util::StrLiteral N> const auto &get() const
  {
    return g_.template get<N>();
  }
  template <attr::AttrDefinition D> auto &get()
  {
    return g_.template get<D>();
  }
  template <attr::AttrDefinition D> const auto &get() const
  {
    return g_.template get<D>();
  }

  attr::AttrGroup &group()
  {
    return g_.group();
  }
  const attr::AttrGroup &group() const
  {
    return g_.group();
  }
  Group &typed()
  {
    return g_;
  }
  const Group &typed() const
  {
    return g_;
  }

  uint32_t capacity() const
  {
    return g_.size();
  }
  uint32_t live_count() const
  {
    return live_count_;
  }
  uint32_t free_count() const
  {
    return uint32_t(free_.size());
  }
  bool live(uint32_t i) const
  {
    return i < capacity() && attr::get_bool(g_.template get<".live">(), i);
  }
  uint32_t gen(uint32_t i) const
  {
    return g_.template get<".gen">()[i];
  }

  /** The slot the next alloc() returns: the lowest free one, else capacity(). */
  uint32_t peek_alloc() const
  {
    return free_.empty() ? capacity() : *free_.begin();
  }

  /** One slot: the lowest free one, else a new one at the end. */
  uint32_t alloc()
  {
    uint32_t i;
    if (!free_.empty()) {
      i = *free_.begin();
      free_.erase(free_.begin());
    }
    else {
      i = capacity();
      g_.resize(i + 1);
      if (gen_floor_ != 0) {
        g_.template get<".gen">().set(i, gen_floor_);
      }
    }
    attr::set_bool(g_.template get<".live">(), i, true);
    live_count_++;
    return i;
  }

  /** A dense range [first, first + n) appended at the end; returns first. */
  uint32_t alloc_n(uint32_t n)
  {
    const uint32_t first = capacity();
    g_.resize(first + n);
    g_.template get<".live">().fill(first, n, uint8_t(1));
    if (gen_floor_ != 0) {
      g_.template get<".gen">().fill(first, n, gen_floor_);
    }
    live_count_ += n;
    return first;
  }

  /** Frees a live slot (see the header comment). */
  void free(uint32_t i)
  {
    attr::AttrGroup &g = g_.group();
    constexpr uint32_t gen_slot = Group::template slot<".gen">();
    const uint32_t one[1] = {i};
    for (uint32_t c = 0; c < g.column_count(); c++) {
      if (c != gen_slot) {
        g.column(c).set_default_list(litestl::util::span<const uint32_t>(one, 1));
      }
    }
    auto &gen = g_.template get<".gen">();
    gen.set(i, gen[i] + 1);
    free_.insert(i);
    live_count_--;
  }

  /* Undo hooks for the editor's rollback (topo/edit): each reverses the
   * allocator bookkeeping of one alloc() / free(); the rows themselves are
   * restored by the caller, and appended rows are dropped by resizing. */
  void undo_alloc(uint32_t i, bool from_free)
  {
    if (from_free) {
      free_.insert(i);
    }
    live_count_--;
  }
  void undo_free(uint32_t i)
  {
    free_.erase(i);
    live_count_++;
  }

  /** f(index) for every live slot, in index order. */
  template <typename F> void for_each_live(F &&f) const
  {
    const auto &lv = g_.template get<".live">();
    for (uint32_t i = 0; i < capacity(); i++) {
      if (lv[i]) {
        f(i);
      }
    }
  }

  template <ident::ElemKind K> ident::Handle<K> handle(ident::Ref<K> r) const
  {
    return {r.i, gen(r.i)};
  }
  /** True if @p h still names the live slot it was taken from. */
  template <ident::ElemKind K> bool resolve(ident::Handle<K> h) const
  {
    return live(h.index) && gen(h.index) == h.gen;
  }

  /** Permutes rows (old_to_new: injective, kDropped for removed slots) and
   * rebuilds the allocator. Index values stored in rows are not rewritten
   * (see remap.h). */
  bool permute(litestl::util::span<const uint32_t> old_to_new, uint32_t new_size)
  {
    uint32_t floor = gen_floor_;
    for (uint32_t i = new_size; i < capacity(); i++) {
      floor = std::max(floor, gen(i) + 1);
    }
    if (!g_.group().permute(old_to_new, new_size)) {
      return false;
    }
    gen_floor_ = floor;
    rebuild_allocator();
    return true;
  }

  uint32_t gen_floor() const
  {
    return gen_floor_;
  }
  void set_gen_floor(uint32_t f)
  {
    gen_floor_ = f;
  }

  /** Generations for a domain rebuilt from scratch in place of @p old (a
   * derived domain: shells, regions). Call on the fresh domain after its
   * rows exist. Row i keeps old's generation if same(i) says the element at
   * i is the same one as before; otherwise it gets max(old gen + 1, floor),
   * or the floor beyond old's capacity. Old rows past the new size raise the
   * floor. An empty old domain leaves everything at 0. */
  template <typename Same> void replace_generations(const Domain &old, Same &&same)
  {
    uint32_t floor = old.gen_floor_;
    for (uint32_t i = capacity(); i < old.capacity(); i++) {
      floor = std::max(floor, old.gen(i) + 1);
    }
    auto &gen = g_.template get<".gen">();
    for (uint32_t i = 0; i < capacity(); i++) {
      uint32_t g = old.gen_floor_;
      if (i < old.capacity()) {
        g = same(i) ? old.gen(i) : std::max(old.gen(i) + 1, old.gen_floor_);
      }
      if (g != gen[i]) {
        gen.set(i, g);
      }
    }
    gen_floor_ = floor;
  }

  /** Rebuilds the free set and the live count from ".live" (after a read or
   * a permutation). */
  void rebuild_allocator()
  {
    free_.clear();
    live_count_ = 0;
    const auto &lv = g_.template get<".live">();
    for (uint32_t i = capacity(); i-- > 0;) {
      if (lv[i]) {
        live_count_++;
      }
      else {
        free_.insert(i);
      }
    }
  }

protected:
  Group g_;
  std::set<uint32_t> free_; /* free slots, ordered */
  uint32_t live_count_ = 0;
  uint32_t gen_floor_ = 0;
};

/** The face domain owns the face-side rows (2 per face, row 2f + side), so
 * faces and face sides can only be allocated, freed or permuted together. */
class FaceDomain : public Domain<FaceSchema> {
public:
  using SideGroup = attr::TypedGroup<FaceSideSchema>;

  uint32_t alloc()
  {
    const uint32_t f = Domain::alloc();
    sync_sides();
    return f;
  }
  uint32_t alloc_n(uint32_t n)
  {
    const uint32_t first = Domain::alloc_n(n);
    sync_sides();
    return first;
  }
  void free(uint32_t f)
  {
    Domain::free(f);
    const uint32_t rows[2] = {2 * f, 2 * f + 1};
    attr::AttrGroup &g = sides_.group();
    for (uint32_t c = 0; c < g.column_count(); c++) {
      g.column(c).set_default_list(litestl::util::span<const uint32_t>(rows, 2));
    }
  }

  /** Permutes faces and their side rows together (row 2f + s follows f). */
  bool permute(litestl::util::span<const uint32_t> old_to_new, uint32_t new_size)
  {
    litestl::util::Vector<uint32_t> side_map;
    for (uint32_t f = 0; f < old_to_new.size(); f++) {
      const uint32_t n = old_to_new[f];
      side_map.append(n == attr::ColumnBase::kDropped ? n : 2 * n);
      side_map.append(n == attr::ColumnBase::kDropped ? n : 2 * n + 1);
    }
    if (!Domain::permute(old_to_new, new_size)) {
      return false;
    }
    return sides_.group().permute(litestl::util::span<const uint32_t>(side_map.data(), side_map.size()), 2 * new_size);
  }

  SideGroup &sides()
  {
    return sides_;
  }
  const SideGroup &sides() const
  {
    return sides_;
  }

private:
  void sync_sides()
  {
    sides_.resize(2 * capacity());
  }
  SideGroup sides_;
};

} // namespace ubertopo::topo
