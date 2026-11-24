#pragma once

/* Each region's winning hint, for embeds into a region (plan 2026-11-24;
 * P3b C4): the hint build_regions voted for, and whether another region wins
 * it too.
 *
 * A cache, not an incremental index: it is a function of two columns,
 * .fs.hint and .f.region, and is rebuilt in O(F) when either has changed
 * since (their version stamps differ) and an embed asks. Regions shift on
 * most rebuilds while embeds are rare, and the regions an embed may ask
 * about are always those of the last build (RegionsDirty otherwise), so
 * keeping this up to date through every barrier costs far more than it saves.
 * Face liveness needs no stamp: freeing a face resets its side hints. */

#include <cstdint>

#include "litestl/util/map.h"
#include "litestl/util/sort.h"
#include "litestl/util/vector.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/hint_votes.h"

namespace ubertopo::topo {

template <geom::CoordType C> class EditRegionHints {
public:
  using PersistentId = ident::PersistentId;

  /** build_regions' winning hint of region @p r, which is not region 0, or
   * kNoId (no hinted side, or the unbounded hint wins). */
  PersistentId winner(const Complex<C> &k, uint32_t r)
  {
    update(k);
    return r < winner_.size() ? winner_[r] : PersistentId{};
  }
  /** True if a region other than @p r and region 0 also wins @p h. */
  bool other_winner(const Complex<C> &k, uint32_t r, PersistentId h)
  {
    update(k);
    const uint32_t *n = wins_.lookup_ptr(h);
    const uint32_t own = r < winner_.size() && winner_[r] == h ? 1 : 0;
    return n && *n > own;
  }
  /** Test hook: a cache that claims to be current equals a fresh one. Leaves
   * the cache current. */
  bool consistent(const Complex<C> &k)
  {
    if (!current(k)) {
      update(k);
      return true;
    }
    EditRegionHints fresh;
    fresh.update(k);
    if (winner_.size() != fresh.winner_.size()) {
      return false;
    }
    for (uint32_t r = 0; r < winner_.size(); r++) {
      if (!(winner_[r] == fresh.winner_[r])) {
        return false;
      }
    }
    return true; /* wins_ is a function of winner_ */
  }

private:
  bool current(const Complex<C> &k) const
  {
    return valid_ && hint_version_ == k.faces.sides().template get<".fs.hint">().version() &&
           region_version_ == k.faces.template get<".f.region">().version() && winner_.size() == k.regions.capacity();
  }
  void update(const Complex<C> &k)
  {
    namespace util = litestl::util;
    if (current(k)) {
      return;
    }
    const auto &fr = k.faces.template get<".f.region">();
    const auto &sh = k.faces.sides().template get<".fs.hint">();
    const uint32_t R = k.regions.capacity();
    /* a complex isn't necessarily valid (loaded, or written from outside):
     * regions out of range don't vote */
    util::KeyGroups sides;
    util::group_by_key(
        2 * k.faces.capacity(), R,
        [&](uint32_t s) {
          const uint32_t f = s / 2;
          if (!k.faces.live(f) || sh[s] == ident::kNoId) {
            return util::kNoGroup;
          }
          const uint32_t r = fr[f][s & 1];
          return r != 0 && r < R ? r : util::kNoGroup;
        },
        sides);
    winner_.resize(R);
    wins_ = util::Map<PersistentId, uint32_t>();
    HintVotes votes;
    for (uint32_t r = 0; r < R; r++) {
      votes.clear();
      for (uint32_t s : sides[r]) {
        votes.add(sh[s]);
      }
      PersistentId h = votes.winner();
      if (h == ident::unbounded_region_id()) {
        h = PersistentId{};
      }
      winner_[r] = h;
      if (!(h == ident::kNoId)) {
        wins_[h]++;
      }
    }
    hint_version_ = sh.version();
    region_version_ = fr.version();
    valid_ = true;
  }

  bool valid_ = false;
  uint64_t hint_version_ = 0, region_version_ = 0;
  litestl::util::Vector<PersistentId> winner_; /* by region */
  litestl::util::Map<PersistentId, uint32_t> wins_; /* hint -> regions it wins */
};

} // namespace ubertopo::topo
