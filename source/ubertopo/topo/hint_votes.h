#pragma once

/* The vote for a region's hint (design 01 §6, DEC-017): the hint of most of
 * its hinted sides, ties going to the smaller hint. The unbounded hint votes
 * like any other; what its win means is the caller's business. Used by
 * build_regions and by the editor's region embeds, so the two can't differ. */

#include <cstdint>

#include "litestl/util/map.h"
#include "litestl/util/vector.h"
#include "ubertopo/ident/id.h"

namespace ubertopo::topo {

class HintVotes {
public:
  struct Vote {
    ident::PersistentId h;
    uint32_t n;
  };

  void clear()
  {
    votes_.clear();
    if (use_map_) {
      pos_ = litestl::util::Map<ident::PersistentId, uint32_t>();
      use_map_ = false;
    }
  }
  /** One side's hint. */
  void add(const ident::PersistentId &h)
  {
    if (!use_map_) {
      for (Vote &v : votes_) {
        if (v.h == h) {
          v.n++;
          return;
        }
      }
      votes_.append({h, 1});
      if (votes_.size() > kScan) {
        use_map_ = true;
        for (uint32_t i = 0; i < votes_.size(); i++) {
          pos_.add(votes_[i].h, i);
        }
      }
      return;
    }
    if (uint32_t *p = pos_.lookup_ptr(h)) {
      votes_[*p].n++;
    }
    else {
      pos_.add(h, uint32_t(votes_.size()));
      votes_.append({h, 1});
    }
  }
  /** The distinct hints, in the order they first voted. */
  const litestl::util::Vector<Vote> &votes() const
  {
    return votes_;
  }
  /** The winning hint; kNoId if nothing voted. */
  ident::PersistentId winner() const
  {
    ident::PersistentId best{};
    uint32_t n = 0;
    for (const Vote &v : votes_) {
      if (v.n > n || (v.n == n && v.h < best)) {
        n = v.n;
        best = v.h;
      }
    }
    return best;
  }

private:
  /* distinct hints scanned linearly; past this many a map indexes them
   * (region 0 has about one hint over many sides) */
  static constexpr uint32_t kScan = 8;
  litestl::util::Vector<Vote> votes_;
  litestl::util::Map<ident::PersistentId, uint32_t> pos_;
  bool use_map_ = false;
};

} // namespace ubertopo::topo
