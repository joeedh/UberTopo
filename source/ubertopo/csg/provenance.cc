#include "ubertopo/csg/provenance.h"

#include <cmath>

#include "litestl/util/map.h"

namespace ubertopo::csg {

namespace {

/* exact coordinates cut off at 2^-20, so that measures don't depend on the
 * compiler */
double coord(const Arrangement &a, uint32_t p, int axis)
{
  return double(floor_at(a.in.points[p], axis, 20));
}

double length(const Arrangement &a, uint32_t p, uint32_t q)
{
  double s = 0;
  for (int i = 0; i < 3; i++) {
    const double d = coord(a, q, i) - coord(a, p, i);
    s += d * d;
  }
  return std::sqrt(s);
}

template <typename P, typename O> double area(const Arrangement &a, const P &pts, const O &off)
{
  double n[3] = {0, 0, 0};
  for (int l = 0; l + 1 < int(off.size()); l++) {
    for (uint32_t i = off[l]; i < off[l + 1]; i++) {
      const uint32_t o = pts[0], p = pts[int(i)], q = pts[int(i + 1 < off[l + 1] ? i + 1 : off[l])];
      for (int c = 0; c < 3; c++) {
        const int u = (c + 1) % 3, v = (c + 2) % 3;
        n[c] += (coord(a, p, u) - coord(a, o, u)) * (coord(a, q, v) - coord(a, o, v)) -
                (coord(a, p, v) - coord(a, o, v)) * (coord(a, q, u) - coord(a, o, u));
      }
    }
  }
  return std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
}

/* sums of measures by source */
struct Totals {
  litestl::util::Map<uint64_t, double> sum;

  static uint64_t key(uint32_t leaf, uint32_t slot)
  {
    return (uint64_t(leaf) << 32) | slot;
  }
  void add(uint64_t k, double x)
  {
    double *t = sum.lookup_ptr(k);
    t ? void(*t += x) : void(sum.add(k, x));
  }
  double share(uint64_t k, double x) const
  {
    return x / *sum.lookup_ptr(k);
  }
};

} // namespace

void find_provenance(const Arrangement &a, const Result &result, const Names &names, Provenance &out)
{
  out = Provenance();
  for (uint32_t i : names.vertex_src) {
    out.verts.append({});
    if (i != kNone) {
      const SrcVert &v = a.in.verts[int(i)];
      out.verts[int(out.verts.size()) - 1] = {v.leaf, v.vert};
    }
  }
  if (names.edge_src.size() > 0) {
    /* a source edge is as long as its pieces */
    Totals total;
    for (const SrcEdge &e : a.in.edges) {
      if (e.edge != kNone) {
        total.add(Totals::key(e.leaf, e.edge), length(a, e.a, e.b));
      }
    }
    for (uint32_t x = 0; x < names.edge_src.size(); x++) {
      const uint32_t i = names.edge_src[int(x)];
      out.edges.append({});
      if (i != kNone) {
        const SrcEdge &e = a.in.edges[int(i)];
        const ResultEdge &re = result.edges[int(x)];
        out.edges[int(x)] = {e.leaf, e.edge, total.share(Totals::key(e.leaf, e.edge), length(a, re.a, re.b))};
      }
    }
  }
  Totals total;
  for (const Polygon &g : a.in.polygons) {
    total.add(Totals::key(g.leaf, g.face), area(a, g.pts, g.off));
  }
  for (const ResultFace &r : result.faces) {
    const Polygon &g = a.in.polygons[int(r.source)];
    out.faces.append({g.leaf, g.face, r.reversed,
                      r.whole ? 1.0 : total.share(Totals::key(g.leaf, g.face), area(a, r.pts, r.off))});
  }
}

void compose_provenance(const Provenance &before, const Provenance &first, std::span<const uint32_t> root,
                        const Arrangement &a, const Result &result, const Names &names, Provenance &out)
{
  out = Provenance();
  for (uint32_t i : names.vertex_src) {
    out.verts.append(i == kNone ? Provenance::Vertex() : before.verts[int(a.in.verts[int(i)].vert)]);
  }
  if (names.edge_src.size() > 0) {
    Totals total;
    auto source = [&](uint32_t x) {
      const uint32_t i = names.edge_src[int(x)];
      return i == kNone ? kNone : a.in.edges[int(i)].edge;
    };
    auto measure = [&](uint32_t x) { return length(a, result.edges[int(x)].a, result.edges[int(x)].b); };
    for (uint32_t x = 0; x < names.edge_src.size(); x++) {
      total.add(source(x), measure(x));
    }
    for (uint32_t x = 0; x < names.edge_src.size(); x++) {
      const uint32_t s = source(x);
      Provenance::Edge e;
      if (s != kNone) {
        e = before.edges[int(s)];
        e.part *= total.share(s, measure(x));
      }
      out.edges.append(e);
    }
  }
  Totals total;
  for (uint32_t i = 0; i < result.faces.size(); i++) {
    total.add(root[i], area(a, result.faces[int(i)].pts, result.faces[int(i)].off));
  }
  for (uint32_t i = 0; i < result.faces.size(); i++) {
    const ResultFace &r = result.faces[int(i)];
    Provenance::Face f = first.faces[int(root[i])];
    f.part *= total.share(root[i], area(a, r.pts, r.off));
    out.faces.append(f);
  }
}

} // namespace ubertopo::csg
