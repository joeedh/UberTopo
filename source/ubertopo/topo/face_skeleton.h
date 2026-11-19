#pragma once

/* The refined skeleton (plan P4b §4.3, §12).
 *
 * The exact geometric tests (projection, T(f), radial order, contacts,
 * containers, regions, face boxes) read a face as loops of points. A straight
 * edge contributes its end vertices; a curved edge also contributes the
 * interior points of its certified flattening at .g.skeleton_tol, rounded to
 * C. The skeleton lies within skeleton_tol plus rounding of the carrier.
 *
 * Rules:
 *  - each curved edge is flattened once, in edge order over (range[0],
 *    range[1]) (flatten runs on the sorted range, then reversed if needed);
 *    a coedge of opposite sense reads the same points reversed, so every face
 *    naming the edge sees the same points and segments;
 *  - a rounded point equal to its predecessor, to the start vertex or to the
 *    end vertex is dropped;
 *  - points are numbered from the vertex count, in one table per Skeleton
 *    (one per validation pass or editor operation), so ids index positions:
 *    below the vertex count a stored vertex, above it a flattening point;
 *  - a complex with no curved edges gives exactly the stored vertices.
 * A curved edge whose carrier doesn't flatten (no evaluator, tolerance below
 * the curve's evaluation error, rounding out of range) gets no interior
 * points and is reported by failed(). */

#include <algorithm>
#include <span>
#include <unordered_map>
#include <utility>

#include "litestl/util/vector.h"
#include "ubertopo/curves/approx.h"
#include "ubertopo/curves/curve.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/curve_geom.h"

namespace ubertopo::topo {

template <geom::CoordType C> class Skeleton {
public:
  using Point = geom::Point<C>;

  explicit Skeleton(const Complex<C> &k)
      : k_(k), nv_(k.verts.capacity()), tol_(k.global.template get<".g.skeleton_tol">()[0])
  {
  }
  Skeleton(const Skeleton &) = delete;
  Skeleton &operator=(const Skeleton &) = delete;

  const Complex<C> &complex() const
  {
    return k_;
  }
  uint32_t vertex_count() const
  {
    return nv_;
  }
  bool is_vertex(uint32_t id) const
  {
    return id < nv_;
  }
  const Point &pos(uint32_t id) const
  {
    return id < nv_ ? k_.pos(VertRef(id)) : extra_[id - nv_];
  }

  /** The interior points of edge @p e in edge order (vs[0] -> vs[1]), as ids;
   * empty for a straight edge or one that failed. */
  std::span<const uint32_t> interior(EdgeRef e)
  {
    if (k_.edges.template get<".e.curve">()[e.i].none()) {
      return {}; /* straight: no table entry, so reads after warm() never insert */
    }
    auto it = span_.find(e.i);
    if (it == span_.end()) {
      it = span_.emplace(e.i, flatten_edge(e)).first;
    }
    const auto [off, n] = it->second;
    return std::span<const uint32_t>(ids_.data() + off, n);
  }
  /** The edge a flattening point lies on (@p id >= vertex_count()). */
  EdgeRef edge_of(uint32_t id) const
  {
    return EdgeRef(edge_of_[id - nv_]);
  }
  /** True if ids a and b are consecutive on one edge's chain (vs[0],
   * interior points, vs[1]), at least one being a flattening point; *e is
   * that edge. Two vertices are never consecutive this way: an edge joining
   * them with interior points has no segment between them. */
  bool consecutive(uint32_t a, uint32_t b, EdgeRef *e)
  {
    const uint32_t x = !is_vertex(a) ? a : b;
    if (is_vertex(x)) {
      return false;
    }
    const EdgeRef ed = edge_of(x);
    const auto in = interior(ed);
    const auto vs = k_.edge_verts(ed);
    /* the position of an id in the chain, or -1 */
    auto at = [&](uint32_t id) -> int64_t {
      if (id == vs[0].i) {
        return 0;
      }
      if (id == vs[1].i) {
        return int64_t(in.size()) + 1;
      }
      for (size_t j = 0; j < in.size(); j++) {
        if (in[j] == id) {
          return int64_t(j) + 1;
        }
      }
      return -1;
    };
    const int64_t ia = at(a), ib = at(b);
    if (ia < 0 || ib < 0 || (ia - ib != 1 && ib - ia != 1)) {
      return false;
    }
    *e = ed;
    return true;
  }
  /** Flattens every live curved edge now, so later reads (interior,
   * gather) never insert: safe from several threads at once. */
  void warm()
  {
    const auto &cv = k_.edges.template get<".e.curve">();
    for (uint32_t e = 0; e < k_.edges.capacity(); e++) {
      if (k_.edges.live(e) && !cv[e].none()) {
        (void)interior(EdgeRef(e));
      }
    }
  }
  /** True if a curved edge seen so far failed to flatten. */
  bool failed() const
  {
    return failed_;
  }
  /** True if curved edge @p e fails to flatten (flattens it if needed). */
  bool edge_failed(EdgeRef e)
  {
    (void)interior(e);
    return std::find(failed_edges_.begin(), failed_edges_.end(), e.i) != failed_edges_.end();
  }
  uint32_t extra_count() const
  {
    return uint32_t(extra_.size());
  }

private:
  std::pair<uint32_t, uint32_t> flatten_edge(EdgeRef e)
  {
    const uint32_t off = uint32_t(ids_.size());
    const litestl::attr::PoolRef ref = k_.edges.template get<".e.curve">()[e.i];
    if (ref.none()) {
      return {off, 0};
    }
    const auto rg = k_.edges.template get<".e.range">()[e.i];
    const auto c = curves::curve3(k_.geom, ref);
    const double lo = std::min(rg[0], rg[1]), hi = std::max(rg[0], rg[1]);
    litestl::util::Vector<double> ts;
    if (!c || !(lo < hi) || !curves::flatten<3>(*c, lo, hi, tol_, ts) || ts.size() < 2) {
      failed_ = true;
      failed_edges_.append(e.i);
      return {off, 0};
    }
    if (rg[0] > rg[1]) {
      std::reverse(ts.begin(), ts.end());
    }
    const auto vs = k_.edge_verts(e);
    const Point &a = k_.pos(vs[0]), &b = k_.pos(vs[1]);
    Point prev = a;
    litestl::util::Vector<Point> pts;
    for (int i = 1; i + 1 < ts.size(); i++) {
      curves::Vec<3> out[1];
      c->eval(ts[i], 0, out, curves::Side::Above);
      Point p;
      if (!round_point<C>(litestl::math::double3(out[0][0], out[0][1], out[0][2]), p)) {
        failed_ = true;
        failed_edges_.append(e.i);
        return {off, 0};
      }
      if (geom::lex_compare<C>(p, prev) == 0 || geom::lex_compare<C>(p, a) == 0) {
        continue;
      }
      pts.append(p);
      prev = p;
    }
    while (pts.size() > 0 && geom::lex_compare<C>(pts[pts.size() - 1], b) == 0) {
      pts.pop_back();
    }
    for (const Point &p : pts) {
      ids_.append(nv_ + uint32_t(extra_.size()));
      extra_.append(p);
      edge_of_.append(e.i);
    }
    return {off, uint32_t(pts.size())};
  }

  const Complex<C> &k_;
  uint32_t nv_;
  double tol_;
  bool failed_ = false;
  litestl::util::Vector<Point> extra_;
  litestl::util::Vector<uint32_t> edge_of_; /* parallel to extra_ */
  litestl::util::Vector<uint32_t> ids_;
  litestl::util::Vector<uint32_t> failed_edges_; /* few: a linear search */
  std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> span_; /* edge -> (offset, count) in ids_ */
};

/** A wire's skeleton chain: vs[0], its flattening points, vs[1]. */
template <geom::CoordType C>
litestl::util::Vector<uint32_t, 16> wire_chain(const Complex<C> &k, Skeleton<C> &sk, uint32_t e)
{
  litestl::util::Vector<uint32_t, 16> chain;
  const auto vs = k.edge_verts(EdgeRef(e));
  chain.append(vs[0].i);
  for (uint32_t id : sk.interior(EdgeRef(e))) {
    chain.append(id);
  }
  chain.append(vs[1].i);
  return chain;
}

} // namespace ubertopo::topo
