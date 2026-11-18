#pragma once

/* Canonical exact constrained Delaunay triangulation of a polygon with holes
 * in an axis-aligned projection (plan P2 amendment A1 §12.2).
 *
 * The result depends only on the projected point set and the loop edges:
 * cocircular ties are broken by Simulation of Simplicity with lexicographic
 * ranks, so it is unique, independent of insertion order, loop heads and loop
 * order, and identical in double and Lattice for the same geometry.
 *
 * cdt_proj is also the projectability check: it succeeds only if every loop
 * is simple, loops neither cross nor touch, the outer loop contains every
 * hole, no hole contains another, and holes wind opposite to the outer loop.
 *
 * Output: triangles of stored corner indices (positions in PolyView::pts), in
 * the face's front winding (the outer loop's orientation), each rotated to
 * start at its smallest index, sorted. */

#include <array>
#include <cstdint>
#include <span>

#include "litestl/util/vector.h"
#include "ubertopo/geom/cdt/constrain.h"
#include "ubertopo/geom/cdt/triangulation.h"
#include "ubertopo/geom/predicates.h"

namespace ubertopo::geom {

enum class CdtStatus : uint8_t {
  Ok,
  NotProjectable,  /* duplicate projected points, crossing/touching/nested loops, ... */
  HoleOrientation, /* a hole winds like the outer loop */
};

using Triangle = std::array<uint32_t, 3>;

namespace cdt::detail {

/* @p vertex_order / @p constraint_order: optional insertion orders (test
 * hook; empty = canonical). The result must not depend on them. */
template <CoordType C>
CdtStatus cdt_proj_ordered(const PolyView<C> &f,
                           int axis,
                           std::span<const uint32_t> vertex_order,
                           std::span<const uint32_t> constraint_order,
                           util::Vector<Triangle> &out)
{
  out.clear();
  const uint32_t n = uint32_t(f.pts.size());
  if (n < 3 || f.loop_count() == 0) {
    return CdtStatus::NotProjectable;
  }
  /* loop orientations first: winding errors get their own code */
  const uint32_t outer = outer_loop_proj<C>(f, axis);
  const int so = loop_orientation_proj<C>(f.loop(outer), axis);
  if (so == 0) {
    return CdtStatus::NotProjectable;
  }
  for (uint32_t l = 0; l < f.loop_count(); l++) {
    const int o = loop_orientation_proj<C>(f.loop(l), axis);
    if (o == 0) {
      return CdtStatus::NotProjectable;
    }
    if (l != outer && o == so) {
      return CdtStatus::HoleOrientation;
    }
  }

  util::Vector<Point2<C>> pts;
  for (uint32_t i = 0; i < n; i++) {
    pts.append(drop_axis<C>(f.pts[i], axis));
  }
  util::Vector<uint32_t> lexo;
  for (uint32_t i = 0; i < n; i++) {
    lexo.append(i);
  }
  auto lexless = [&](uint32_t a, uint32_t b) {
    const auto &p = pts[a], &q = pts[b];
    return p[0] < q[0] || (!(q[0] < p[0]) && p[1] < q[1]);
  };
  std::sort(lexo.begin(), lexo.end(), lexless);
  util::Vector<uint32_t> rank;
  rank.resize(n);
  for (uint32_t i = 0; i < n; i++) {
    rank[lexo[i]] = i;
    if (i > 0 && !lexless(lexo[i - 1], lexo[i])) {
      return CdtStatus::NotProjectable; /* two corners project to one point */
    }
  }
  const std::span<const uint32_t> order =
      vertex_order.empty() ? std::span<const uint32_t>(lexo.data(), lexo.size()) : vertex_order;

  Triangulation<C> tr(std::span<const Point2<C>>(pts.data(), pts.size()), std::span<const uint32_t>(rank.data(), n));
  const uint32_t m = tr.init(order);
  if (m == 0) {
    return CdtStatus::NotProjectable; /* all points collinear */
  }
  for (uint32_t k = m; k < order.size(); k++) {
    if (!tr.insert(order[k], order[k - 1])) {
      return CdtStatus::NotProjectable;
    }
  }

  /* constraints: every loop edge, canonically by (min rank, max rank) */
  util::Vector<std::array<uint32_t, 2>> cons;
  for (uint32_t l = 0; l < f.loop_count(); l++) {
    const uint32_t b = f.off[l], m2 = f.off[l + 1] - b;
    for (uint32_t j = 0; j < m2; j++) {
      cons.append({b + j, b + (j + 1) % m2});
    }
  }
  util::Vector<uint32_t> corder;
  if (constraint_order.empty()) {
    for (uint32_t i = 0; i < cons.size(); i++) {
      corder.append(i);
    }
    auto key = [&](uint32_t i) {
      const uint32_t ra = rank[cons[i][0]], rb = rank[cons[i][1]];
      return std::array<uint32_t, 2>{std::min(ra, rb), std::max(ra, rb)};
    };
    std::sort(corder.begin(), corder.end(), [&](uint32_t x, uint32_t y) { return key(x) < key(y); });
  }
  else {
    for (uint32_t i : constraint_order) {
      corder.append(i);
    }
  }
  cdt::Constrainer<C> cn(tr);
  for (uint32_t ci : corder) {
    if (cn.insert(cons[ci][0], cons[ci][1]) != ConstrainResult::Ok) {
      return CdtStatus::NotProjectable;
    }
  }

  /* inside: triangles left of the directed loop edges, with the outer loop
   * normalised to counter-clockwise; flood fill without crossing constraints */
  const uint32_t T = uint32_t(tr.tris.size());
  util::Vector<uint8_t> keep;
  keep.resize(T);
  for (uint32_t i = 0; i < T; i++) {
    keep[i] = 0;
  }
  util::Vector<uint32_t> stack, twins;
  for (const auto &e : cons) {
    const uint32_t u = so > 0 ? e[0] : e[1], v = so > 0 ? e[1] : e[0];
    uint32_t s;
    const uint32_t t = cn.find_edge(u, v, &s);
    if (t == kNone || tr.is_ghost(tr.tris[t])) {
      return CdtStatus::NotProjectable;
    }
    if (!keep[t]) {
      keep[t] = 1;
      stack.append(t);
    }
    twins.append(tr.tris[t].n[(s + 2) % 3]);
  }
  while (stack.size() > 0) {
    const uint32_t t = stack.pop_back();
    for (uint32_t i = 0; i < 3; i++) {
      if (tr.tris[t].cons & (1u << i)) {
        continue;
      }
      const uint32_t nb = tr.tris[t].n[i];
      if (nb == kNone || tr.is_ghost(tr.tris[nb])) {
        return CdtStatus::NotProjectable; /* the kept region reaches the hull: not enclosed */
      }
      if (!keep[nb]) {
        keep[nb] = 1;
        stack.append(nb);
      }
    }
  }
  for (uint32_t t : twins) {
    if (t != kNone && keep[t]) {
      return CdtStatus::NotProjectable; /* nested hole, hole outside, ... */
    }
  }

  for (uint32_t t = 0; t < T; t++) {
    if (!keep[t] || tr.tris[t].dead) {
      continue;
    }
    Triangle x = {tr.tris[t].v[0], tr.tris[t].v[1], tr.tris[t].v[2]};
    if (so < 0) {
      std::swap(x[1], x[2]); /* front winding */
    }
    while (x[0] > x[1] || x[0] > x[2]) {
      x = {x[1], x[2], x[0]};
    }
    out.append(x);
  }
  std::sort(out.begin(), out.end());
  return CdtStatus::Ok;
}

} // namespace cdt::detail

/** Canonical CDT of @p f projected along @p axis (see the header comment). */
template <CoordType C> CdtStatus cdt_proj(const PolyView<C> &f, int axis, litestl::util::Vector<Triangle> &out)
{
  return cdt::detail::cdt_proj_ordered<C>(f, axis, {}, {}, out);
}

} // namespace ubertopo::geom
