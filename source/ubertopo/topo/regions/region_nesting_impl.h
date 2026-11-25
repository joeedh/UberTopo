#pragma once

/* RegionBuilder: nesting. Included only by regions.cc. */

#include <algorithm>

#include "ubertopo/topo/regions/region_builder.h"

namespace ubertopo::topo::region_detail {

template <geom::CoordType C>
int RegionBuilder<C>::test_point(uint32_t a,
                                 std::span<const uint32_t> va,
                                 std::span<const uint32_t> vb,
                                 Point &p)
{
  for (uint32_t v : va) {
    if (!std::binary_search(vb.begin(), vb.end(), v)) {
      p = k_.pos(VertRef(v));
      return 0;
    }
  }
  FaceTriangles<C> ft;
  for (uint32_t s : shell_sides_[a]) {
    ft.gather(k_, FaceRef(s / 2), sk_); /* not a face of B: candidates share none */
    const auto &t = ft.tris;
    for (uint32_t i = 0; i + 2 < t.size(); i += 3) {
      if (geom::orient2d_proj<C>(t[i], t[i + 1], t[i + 2], 0) == 0 &&
          geom::orient2d_proj<C>(t[i], t[i + 1], t[i + 2], 1) == 0 &&
          geom::orient2d_proj<C>(t[i], t[i + 1], t[i + 2], 2) == 0)
      {
        continue; /* degenerate fan triangle */
      }
      if (geom::interior_point4<C>(t[i], t[i + 1], t[i + 2], p)) {
        return 2;
      }
    }
  }
  return -1;
}

/* Innermost containing outer shell of each cavity and sheet shell A. */
template <geom::CoordType C> bool RegionBuilder<C>::nest()
{
  const uint32_t S = uint32_t(shell_.size());
  util::Vector<uint32_t> outers, inner;
  for (uint32_t sh = 0; sh < S; sh++) {
    (shell_[sh].kind == Outer ? outers : inner).append(sh);
  }
  const int n = int(inner.size());
  /* the outer shells each A is tested against, in shell order */
  util::Vector<util::Vector<uint32_t>> cand;
  cand.resize(size_t(n));
  task::parallel_for_chunks(util::IndexRange(0, n), 16, [&](int, util::IndexRange r) {
    for (int i : r) {
      const uint32_t a = inner[i];
      for (uint32_t b : outers) {
        if (!shell_[b].box.contains(shell_[a].box)) {
          continue;
        }
        /* (a) sharing a face on opposite sides: not inside */
        bool shared = false;
        for (uint32_t s : shell_sides_[a]) {
          if (side_shell_[s ^ 1u] == b) {
            shared = true;
            break;
          }
        }
        if (!shared) {
          cand[i].append(b);
        }
      }
    }
  });
  /* what the tests read of each outer shell, once per shell */
  struct Prep {
    util::Vector<uint32_t> verts;
    util::Vector<Point> tris; /* outward from its enclosed volume */
  };
  util::Vector<uint32_t> needed, prep_of;
  prep_of.resize(S);
  for (uint32_t sh = 0; sh < S; sh++) {
    prep_of[sh] = NONE;
  }
  for (int i = 0; i < n; i++) {
    for (uint32_t b : cand[i]) {
      if (prep_of[b] == NONE) {
        prep_of[b] = uint32_t(needed.size());
        needed.append(b);
      }
    }
  }
  util::Vector<Prep> prep;
  prep.resize(needed.size());
  task::parallel_for_chunks(util::IndexRange(0, int(needed.size())), 1, [&](int, util::IndexRange r) {
    FaceTriangles<C> ft;
    for (int i : r) {
      shell_verts(needed[i], prep[i].verts);
      auto &out = prep[i].tris;
      for (uint32_t s : shell_sides_[needed[i]]) {
        ft.gather(k_, FaceRef(s / 2), sk_);
        const auto &t = ft.tris;
        for (uint32_t j = 0; j + 2 < t.size(); j += 3) {
          out.append(t[j]);
          out.append(t[(s & 1u) ? j + 1 : j + 2]);
          out.append(t[(s & 1u) ? j + 2 : j + 1]);
        }
      }
    }
  });
  struct Result {
    int32_t err = -1;
    uint32_t tests = 0, retries = 0;
  };
  util::Vector<Result> result;
  result.resize(size_t(n));
  task::parallel_for_chunks(util::IndexRange(0, n), 16, [&](int, util::IndexRange r) {
    util::Vector<uint32_t> va;
    for (int i : r) {
      const uint32_t a = inner[i];
      uint32_t &parent = shell_[a].parent;
      parent = NONE;
      if (cand[i].size() == 0) {
        continue;
      }
      shell_verts(a, va);
      for (uint32_t b : cand[i]) {
        const Prep &pb = prep[prep_of[b]];
        /* (b) a test point of A off B */
        Point p;
        const int scale = test_point(a, std::span<const uint32_t>(va.data(), va.size()),
                                     std::span<const uint32_t>(pb.verts.data(), pb.verts.size()), p);
        if (scale < 0) {
          result[i].err = int32_t(RegionCode::NoTestPoint);
          break;
        }
        const geom::RayResult rr =
            geom::ray_winding<C>(p, std::span<const Point>(pb.tris.data(), pb.tris.size()), scale);
        result[i].tests++;
        result[i].retries += uint32_t(rr.retries);
        if (rr.status == geom::RayStatus::Boundary) {
          result[i].err = int32_t(RegionCode::TouchingShell);
          break;
        }
        if (rr.status != geom::RayStatus::Ok) {
          result[i].err = int32_t(RegionCode::NestingDegenerate);
          break;
        }
        if (rr.winding != 0 && (parent == NONE || shell_[b].vol.smaller_than(shell_[parent].vol))) {
          parent = b;
        }
      }
    }
  });
  for (int i = 0; i < n; i++) {
    report_.ray_tests += result[i].tests;
    report_.ray_retries += result[i].retries;
    if (result[i].err >= 0) {
      report_.add(RegionCode(result[i].err), inner[i]);
    }
  }
  return report_.ok();
}

} // namespace ubertopo::topo::region_detail
