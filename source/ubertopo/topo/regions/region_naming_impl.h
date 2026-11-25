#pragma once

/* RegionBuilder: regions, their IDs, and container hints. Included only by
 * regions.cc. */

#include <algorithm>
#include <cassert>

#include "ubertopo/topo/regions/region_builder.h"

namespace ubertopo::topo::region_detail {

template <geom::CoordType C> void RegionBuilder<C>::name_regions()
{
  const uint32_t S = uint32_t(shell_.size());
  shell_preg_.resize(S);
  R_ = 1;
  for (uint32_t sh = 0; sh < S; sh++) {
    shell_preg_[sh] = shell_[sh].kind == Outer ? R_++ : 0;
  }
  for (uint32_t sh = 0; sh < S; sh++) {
    if (shell_[sh].kind != Outer && shell_[sh].parent != NONE) {
      shell_preg_[sh] = shell_preg_[shell_[sh].parent];
    }
  }
  const PersistentId unb = ident::unbounded_region_id();
  util::Vector<PersistentId> preg_id, preg_hint, preg_min;
  preg_id.resize(R_);
  preg_hint.resize(R_);
  preg_min.resize(R_);
  {
    util::KeyGroups preg_shells;
    util::group_by_key(S, R_, [&](uint32_t sh) { return shell_preg_[sh]; }, preg_shells);
    HintVotes votes; /* unb votes like any hint */
    for (uint32_t r = 0; r < R_; r++) {
      votes.clear();
      PersistentId min_side{};
      bool have_min = false;
      for (uint32_t sh : preg_shells[r]) {
        /* shells are in label order: the first has the smallest side */
        if (!have_min) {
          min_side = shell_[sh].label;
          have_min = true;
        }
        for (const HintVotes::Vote &v : shell_[sh].hints) {
          votes.add(v.h, v.n);
        }
      }
      const PersistentId hint = votes.winner();
      const size_t nhints = votes.votes().size();
      if (nhints > 1 || (r == 0 && nhints > 0 && !(hint == unb)) || (r != 0 && hint == unb)) {
        report_.add(RegionCode::HintConflict, r);
      }
      preg_hint[r] = (r == 0 || hint == unb) ? PersistentId{} : hint;
      preg_min[r] = min_side;
      if (r != 0) {
        for (const HintVotes::Vote &v : votes.votes()) {
          if (v.h == hint || v.h == unb) {
            continue;
          }
          HintInfo &hi = info_[v.h];
          if (!hi.has_carrier) {
            hi.has_carrier = true;
            hi.carrier = r;
          }
          else {
            hi.carrier = NONE; /* a second region (a region's hints are distinct) */
          }
        }
      }
    }
  }
  /* IDs (DEC-017): a winning hint no other region wins names the region on
   * its own; shared winners keep the minimal side to stay unique */
  for (uint32_t r = 1; r < R_; r++) {
    if (!(preg_hint[r] == ident::kNoId)) {
      info_[preg_hint[r]].winners++;
    }
  }
  for (uint32_t r = 0; r < R_; r++) {
    if (r == 0) {
      preg_id[r] = unb;
    }
    else if (preg_hint[r] == ident::kNoId) {
      preg_id[r] = ident::region_id_from_sides(preg_min[r]);
    }
    else if (info_.lookup(preg_hint[r]).winners == 1) {
      preg_id[r] = ident::region_id_from_hint(preg_hint[r]);
    }
    else {
      preg_id[r] = ident::region_id(preg_hint[r], preg_min[r]);
    }
  }
  /* final order: region 0 first, then by ID (unique barring a hash
   * collision) */
  util::Vector<uint32_t> rorder;
  for (uint32_t r = 1; r < R_; r++) {
    rorder.append(r);
  }
  {
    const bool unique = util::sort_unique_keys(util::span<uint32_t>(rorder.data(), rorder.size()),
                                               [&](uint32_t a, uint32_t b) { return preg_id[a] < preg_id[b]; });
    assert(unique);
    (void)unique;
  }
  preg_final_.resize(R_);
  preg_final_[0] = 0;
  for (uint32_t i = 0; i < rorder.size(); i++) {
    preg_final_[rorder[i]] = i + 1;
  }
  util::Vector<PersistentId> reg_hint;
  reg_id_.resize(R_);
  reg_hint.resize(R_);
  for (uint32_t r = 0; r < R_; r++) {
    reg_id_[preg_final_[r]] = preg_id[r];
    reg_hint[preg_final_[r]] = preg_hint[r];
  }
  /* in final order: the second winner of a hint is reported (only it), and
   * a hint's sole winner is recorded */
  for (uint32_t r = 1; r < R_; r++) {
    if (reg_hint[r] == ident::kNoId) {
      continue;
    }
    HintInfo &hi = info_.lookup(reg_hint[r]);
    if (hi.seen++ == 1) {
      report_.add(RegionCode::DuplicateHint, r);
    }
    hi.win = r;
  }
}

template <geom::CoordType C> bool RegionBuilder<C>::resolve_containers()
{
  /* unb, then the winning region (ambiguous if two win), then the minority
   * carrier (ambiguous if two carry it), else unresolved */
  const PersistentId unb = ident::unbounded_region_id();
  auto resolve = [&](PersistentId h, uint32_t index, ident::ContainerRef &out) {
    if (h == unb) {
      return ident::ContainerRef::region(RegionRef(0), out);
    }
    const HintInfo *hi = info_.lookup_ptr(h);
    if (hi == nullptr || hi->winners == 0) {
      if (hi == nullptr || !hi->has_carrier) {
        report_.add(RegionCode::UnresolvedContainerHint, index);
        return false;
      }
      if (hi->carrier == NONE) {
        report_.add(RegionCode::AmbiguousContainerHint, index);
        return false;
      }
      return ident::ContainerRef::region(RegionRef(preg_final_[hi->carrier]), out);
    }
    if (hi->winners > 1) {
      report_.add(RegionCode::AmbiguousContainerHint, index);
      return false;
    }
    return ident::ContainerRef::region(RegionRef(hi->win), out);
  };
  const auto &vh = k_.verts.template get<".v.container_hint">();
  for (uint32_t v = 0; v < k_.verts.capacity(); v++) {
    ident::ContainerRef cr;
    if (k_.verts.live(v) && !(vh[v] == ident::kNoId) && resolve(vh[v], v, cr)) {
      vcont_.append({v, cr});
    }
  }
  const auto &eh = k_.edges.template get<".e.container_hint">();
  for (uint32_t e = 0; e < k_.edges.capacity(); e++) {
    ident::ContainerRef cr;
    if (k_.edges.live(e) && !(eh[e] == ident::kNoId) && resolve(eh[e], e, cr)) {
      econt_.append({e, cr});
    }
  }
  return report_.ok();
}

} // namespace ubertopo::topo::region_detail
