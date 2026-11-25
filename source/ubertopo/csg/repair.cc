#include "ubertopo/csg/repair.h"

#include <algorithm>
#include <cstdlib>

#include "litestl/util/map.h"
#include "ubertopo/csg/triangulate.h"
#include "ubertopo/topo/face_geom.h"
#include "ubertopo/topo/radial.h"

namespace ubertopo::csg {

namespace {

using geom::Lattice;
using litestl::util::Vector;

bool before(const I3 &p, const I3 &q)
{
  return p[0] != q[0] ? p[0] < q[0] : p[1] != q[1] ? p[1] < q[1] : p[2] < q[2];
}

struct Reader {
  const Rounded &r;
  bool exact;
  Input &in;
  Vector<uint32_t> &point_of;
  RepairCounts &counts;

  Vector<uint8_t> shared;                 /* by point of in: several points of r */
  litestl::util::Map<uint64_t, uint32_t> real, diagonal; /* edges, by point pair */

  static uint64_t pair(uint32_t p, uint32_t q)
  {
    return (uint64_t(std::min(p, q)) << 32) | std::max(p, q);
  }

  /* which of the points that came together goes on */
  bool better(uint32_t p, uint32_t q) const
  {
    if (r.a.in.is_input(p) != r.a.in.is_input(q)) {
      return r.a.in.is_input(p); /* (it was there before) */
    }
    const auto &v = r.provenance.verts;
    if (v.size() > 0 && (v[int(p)].leaf != kNone) != (v[int(q)].leaf != kNone)) {
      return v[int(p)].leaf != kNone;
    }
    return r.names.vertex.size() > 0 ? r.names.vertex[int(p)] < r.names.vertex[int(q)] : p < q;
  }

  void points()
  {
    Vector<uint32_t> order;
    for (uint32_t p = 0; p < r.used.size(); p++) {
      point_of.append(kNone);
      if (r.used[p]) {
        order.append(p);
      }
    }
    std::sort(order.begin(), order.end(), [&](uint32_t p, uint32_t q) {
      const I3 &x = r.positions[p], &y = r.positions[q];
      return before(x, y) || (!before(y, x) && better(p, q));
    });
    for (uint32_t p : order) {
      const uint32_t i = in.points.add(r.positions[p]);
      point_of[int(p)] = i;
      if (i == in.ipos.size()) {
        in.ipos.append(r.positions[p]);
        in.verts.append({0, i, p, kNone, 0});
        shared.append(0);
      }
      else {
        shared[int(i)] = 1;
        counts.merged++;
      }
    }
  }

  uint32_t edge(litestl::util::Map<uint64_t, uint32_t> &m, uint32_t p, uint32_t q, uint32_t src)
  {
    const uint64_t k = pair(p, q);
    if (const uint32_t *e = m.lookup_ptr(k)) {
      return *e;
    }
    const uint32_t e = uint32_t(in.edges.size());
    in.edges.append({0, src, std::min(p, q), std::max(p, q)});
    m.add(k, e);
    return e;
  }

  void edges()
  {
    for (uint32_t x = 0; x < r.result.edges.size(); x++) {
      const ResultEdge &re = r.result.edges[int(x)];
      const uint32_t p = point_of[int(re.a)], q = point_of[int(re.b)];
      if (p == q) {
        continue;
      }
      SrcEdge &e = in.edges[int(edge(real, p, q, x))];
      /* of several, one that is an operand's, and the first such */
      const auto &from = r.provenance.edges;
      if (e.edge != x && from.size() > 0 && from[int(x)].leaf != kNone) {
        const Provenance::Edge &s = from[int(x)], &t = from[int(e.edge)];
        if (s.leaf < t.leaf || (s.leaf == t.leaf && s.edge < t.edge)) {
          e.edge = x;
        }
      }
    }
  }

  /* plane, box and axis; n: the front normal */
  bool add(Polygon &g, const I3 &n)
  {
    Plane pl;
    const int s = plane_from(in.ipos[int(g.pts[0])], n, pl);
    if (s == 0) {
      return false;
    }
    g.plane = in.planes.add(pl);
    g.sign = int8_t(s);
    g.axis = 0;
    for (int i = 1; i < 3; i++) {
      if (std::abs(pl.n[i]) > std::abs(pl.n[g.axis])) {
        g.axis = int8_t(i);
      }
    }
    g.lo = g.hi = in.ipos[int(g.pts[0])];
    for (uint32_t p : g.pts) {
      const I3 &q = in.ipos[int(p)];
      for (int i = 0; i < 3; i++) {
        g.lo[i] = std::min(g.lo[i], q[i]);
        g.hi[i] = std::max(g.hi[i], q[i]);
      }
    }
    in.polygons.append(std::move(g));
    return true;
  }

  I3 normal(uint32_t p, uint32_t q, uint32_t s) const
  {
    const I3 &o = in.ipos[int(p)];
    return cross_small(sub(in.ipos[int(q)], o), sub(in.ipos[int(s)], o));
  }

  CsgCode face(uint32_t fi)
  {
    const ResultFace &f = r.result.faces[int(fi)];
    /* the loops, without repeated points */
    Polygon g;
    g.face = fi;
    g.leaf = 0;
    g.region[0] = f.solid[0];
    g.region[1] = f.solid[1];
    g.off.append(0);
    bool touched = false;
    litestl::util::Map<uint64_t, uint32_t> sides; /* the face's own edges */
    for (int l = 0; l + 1 < int(f.off.size()); l++) {
      const uint32_t b = uint32_t(g.pts.size());
      for (uint32_t i = f.off[l]; i < f.off[l + 1]; i++) {
        const uint32_t p = point_of[int(f.pts[int(i)])];
        touched = touched || shared[int(p)];
        if (g.pts.size() == b || g.pts[int(g.pts.size()) - 1] != p) {
          g.pts.append(p);
        }
      }
      while (g.pts.size() > b + 1 && g.pts[int(g.pts.size()) - 1] == g.pts[int(b)]) {
        g.pts.resize(g.pts.size() - 1);
      }
      if (g.pts.size() < b + 3) {
        g.pts.resize(b);
        continue;
      }
      for (uint32_t i = b; i < g.pts.size(); i++) {
        const uint32_t p = g.pts[int(i)], q = g.pts[int(i + 1 < g.pts.size() ? i + 1 : b)];
        sides.add_overwrite(pair(p, q), 0);
      }
      g.off.append(uint32_t(g.pts.size()));
    }
    Vector<geom::Point<Lattice>> pts;
    for (uint32_t p : g.pts) {
      const I3 &q = in.ipos[int(p)];
      pts.append(geom::Point<Lattice>(Lattice(q[0]), Lattice(q[1]), Lattice(q[2])));
    }
    const geom::PolyView<Lattice> view{{pts.data(), size_t(pts.size())}, {g.off.data(), size_t(g.off.size())}};
    int8_t axis = -1;
    Vector<geom::Triangle> tris;
    bool ok = g.pts.size() > 0 && topo::classify_face<Lattice>(view, true, axis, tris) == topo::FaceGeom::Ok;
    if (ok && axis < 0) {
      /* (of a planar face it doesn't ask whether loops cross) */
      ok = geom::cdt_proj<Lattice>(view, geom::project_axis<Lattice>(view), tris) == geom::CdtStatus::Ok;
    }
    if (exact && (touched || (axis >= 0 && !f.whole))) {
      ok = false;
    }
    if (ok && axis < 0) {
      const topo::FaceFrame fr = topo::face_frame<Lattice>(view);
      uint32_t ic = 0;
      for (uint32_t i = 2; i < pts.size() && !ic; i++) {
        ic = geom::orient2d_proj<Lattice>(pts[0], pts[1], pts[int(i)], fr.axis) != 0 ? i : 0;
      }
      const bool ccw = geom::orient2d_proj<Lattice>(pts[0], pts[1], pts[int(ic)], fr.axis) == fr.sigma;
      const I3 n = ccw ? normal(g.pts[0], g.pts[1], g.pts[int(ic)]) : normal(g.pts[0], g.pts[int(ic)], g.pts[1]);
      g.whole = true;
      g.for_each_edge([&](uint32_t i, uint32_t j) {
        g.edges.append(edge(real, g.pts[int(i)], g.pts[int(j)], kNone));
      });
      return add(g, n) ? CsgCode::Ok : CsgCode::Inconsistent;
    }
    Vector<PointTriangle> moved;
    if (ok) {
      for (const geom::Triangle &t : tris) {
        moved.append({g.pts[int(t[0])], g.pts[int(t[1])], g.pts[int(t[2])]});
      }
    }
    else {
      counts.broken++;
      if (triangulate_face(r.a, f, moved) != CsgCode::Ok) {
        return CsgCode::Inconsistent;
      }
      for (PointTriangle &t : moved) {
        t = {point_of[int(t[0])], point_of[int(t[1])], point_of[int(t[2])]};
      }
    }
    const uint32_t first = uint32_t(in.polygons.size());
    const uint32_t outline = ok ? uint32_t(in.outlines.size()) : kNone;
    for (const PointTriangle &t : moved) {
      const I3 n = normal(t[0], t[1], t[2]);
      if (n[0] == 0 && n[1] == 0 && n[2] == 0) {
        continue;
      }
      Polygon h;
      h.face = fi;
      h.leaf = 0;
      h.region[0] = g.region[0];
      h.region[1] = g.region[1];
      h.outline = outline;
      h.off.append(0);
      h.off.append(3);
      for (int i = 0; i < 3; i++) {
        h.pts.append(t[i]);
      }
      for (int i = 0; i < 3; i++) {
        const uint32_t p = t[i], q = t[(i + 1) % 3];
        /* a diagonal isn't the edge that some other face may have there */
        const bool own = sides.contains(pair(p, q)) && real.contains(pair(p, q));
        h.edges.append(edge(own ? real : diagonal, p, q, kNone));
      }
      add(h, n);
    }
    if (ok) {
      in.outlines.append({g.pts, g.off, first, uint32_t(in.polygons.size()) - first});
    }
    return CsgCode::Ok;
  }
};

} // namespace

CsgCode read_rounded(const Rounded &r, bool exact, Input &in, Vector<uint32_t> &point_of, RepairCounts &counts)
{
  Reader x{r, exact, in, point_of, counts, {}, {}, {}};
  x.points();
  x.edges();
  in.self = true;
  in.leaf_begin.append(0);
  for (uint32_t f = 0; f < r.result.faces.size(); f++) {
    const CsgCode c = x.face(f);
    if (c != CsgCode::Ok) {
      return c;
    }
  }
  in.leaf_begin.append(uint32_t(in.polygons.size()));
  return CsgCode::Ok;
}

PersistentId polygons_hash(const Input &in)
{
  Vector<PersistentId> each;
  for (const Polygon &g : in.polygons) {
    ident::Hasher128 h(ident::tag::BoolFace);
    h.u32(g.region[0]).u32(g.region[1]).u32(uint32_t(g.off.size()));
    for (int l = 0; l + 1 < int(g.off.size()); l++) {
      const uint32_t b = g.off[l], n = g.off[l + 1] - b;
      uint32_t first = 0;
      for (uint32_t i = 1; i < n; i++) {
        if (before(in.ipos[int(g.pts[int(b + i)])], in.ipos[int(g.pts[int(b + first)])])) {
          first = i;
        }
      }
      for (uint32_t i = 0; i < n; i++) {
        const I3 &p = in.ipos[int(g.pts[int(b + (first + i) % n)])];
        h.u64(uint64_t(p[0])).u64(uint64_t(p[1])).u64(uint64_t(p[2]));
      }
    }
    each.append(h.finish());
  }
  std::sort(each.begin(), each.end());
  ident::Hasher128 h(ident::tag::BoolFace);
  for (const PersistentId &x : each) {
    h.h128(x);
  }
  return h.finish();
}

void select_repaired(const Arrangement &a, const Vector<uint32_t> &solid, const Rounded &r, const Expression &e,
                     Vector<Selected> &out)
{
  out.clear();
  for (uint32_t f = 0; f < a.facets.size(); f++) {
    Selected s;
    const uint32_t want[2] = {solid[int(2 * f)], solid[int(2 * f + 1)]};
    auto label = [&](uint32_t p, uint32_t side) {
      const Polygon &g = a.in.polygons[int(p)];
      return g.region[(g.sign > 0) == (side == 0) ? 0 : 1];
    };
    auto thin = [&](uint32_t p) { return label(p, 0) == label(p, 1); };
    /* 0: it separates the two labels, 1: it separates some; 2: any */
    auto rank = [&](uint32_t p) {
      return label(p, 0) == want[0] && label(p, 1) == want[1] ? 0 : !thin(p) ? 1 : 2;
    };
    auto better = [&](uint32_t p, uint32_t q) {
      const uint32_t x = a.in.polygons[int(p)].face, y = a.in.polygons[int(q)].face;
      if (rank(p) != rank(q)) {
        return rank(p) < rank(q);
      }
      if (x != y && r.names.face.size() > 0) {
        return r.names.face[int(x)] < r.names.face[int(y)];
      }
      return p < q;
    };
    for (uint32_t p : a.facets[int(f)].sources) {
      if ((want[0] != want[1] || thin(p)) && (s.source == kNone || better(p, s.source))) {
        s.source = p;
      }
    }
    s.kept = s.source != kNone;
    for (uint32_t side = 0; side < 2 && s.kept; side++) {
      s.label[side] = want[side];
      if (want[side] != kVoid) {
        continue;
      }
      /* the air of the source there, else of another one */
      auto air = [&](uint32_t p) {
        const Polygon &g = a.in.polygons[int(p)];
        const uint32_t l = r.result.faces[int(g.face)].label[(g.sign > 0) == (side == 0) ? 0 : 1];
        return e.solid(l) ? kVoid : l;
      };
      s.label[side] = air(s.source);
      for (uint32_t p : a.facets[int(f)].sources) {
        s.label[side] = s.label[side] == kVoid ? air(p) : s.label[side];
      }
    }
    out.append(s);
  }
}

/* ---- joining ---- */

namespace {

struct Joiner {
  const Result &res;
  std::span<const I3> positions;
  std::span<const uint32_t> root;
  Result &out;

  Vector<uint32_t> parent;                      /* of faces */
  litestl::util::Map<uint64_t, uint32_t> inner; /* directed sides at which faces are joined */

  uint32_t find(uint32_t f)
  {
    while (parent[int(f)] != f) {
      parent[int(f)] = parent[int(parent[int(f)])];
      f = parent[int(f)];
    }
    return f;
  }
  bool alike(uint32_t f, uint32_t g) const
  {
    const ResultFace &x = res.faces[int(f)], &y = res.faces[int(g)];
    return root[f] == root[g] && x.reversed == y.reversed && x.label[0] == y.label[0] && x.label[1] == y.label[1] &&
           x.solid[0] == y.solid[0] && x.solid[1] == y.solid[1];
  }
  bool joins(const ResultEdge &x) const
  {
    return x.uses.size() == 2 && x.uses[0].facet != x.uses[1].facet && x.uses[0].dir != x.uses[1].dir &&
           alike(x.uses[0].facet, x.uses[1].facet);
  }
  void gather(Vector<Vector<uint32_t, 2>> &groups)
  {
    groups.clear();
    groups.resize(res.faces.size());
    for (uint32_t f = 0; f < res.faces.size(); f++) {
      groups[int(find(f))].append(f);
    }
  }

  static constexpr uint32_t kMaxPartly = 200;

  static uint64_t directed(uint32_t p, uint32_t q)
  {
    return (uint64_t(p) << 32) | q;
  }

  /* the loops around the faces of a group; false: not a face */
  bool outline(const Vector<uint32_t, 2> &group, ResultFace &f) const
  {
    litestl::util::Map<uint64_t, uint32_t> halves; /* directed sides of the group */
    litestl::util::Map<uint32_t, uint32_t> points;
    for (uint32_t g : group) {
      for_each_side(res.faces[int(g)], [&](uint32_t p, uint32_t q) {
        halves.add_overwrite(directed(p, q), 0);
        points.add_overwrite(p, 0);
      });
    }
    litestl::util::Map<uint32_t, uint32_t> next;
    Vector<uint32_t> starts;
    bool ok = true;
    for (uint32_t g : group) {
      for_each_side(res.faces[int(g)], [&](uint32_t p, uint32_t q) {
        if (halves.contains(directed(q, p))) {
          ok = ok && inner.contains(directed(p, q)); /* (else something else is there) */
          return;
        }
        ok = ok && !next.contains(p); /* (the outline touches itself) */
        next.add_overwrite(p, q);
        starts.append(p);
      });
    }
    /* a point in the middle would be lost */
    ok = ok && next.size() == points.size();
    f.pts.clear();
    f.off.clear();
    f.off.append(0);
    litestl::util::Map<uint32_t, uint32_t> done;
    for (uint32_t s : starts) {
      if (!ok || done.contains(s)) {
        continue;
      }
      for (uint32_t p = s; !done.contains(p) && ok;) {
        done.add(p, 0);
        f.pts.append(p);
        const uint32_t *n = next.lookup_ptr(p);
        ok = n != nullptr;
        p = ok ? *n : p;
      }
      f.off.append(uint32_t(f.pts.size()));
    }
    if (!ok) {
      return false;
    }
    Vector<geom::Point<Lattice>> pts;
    for (uint32_t p : f.pts) {
      const I3 &q = positions[p];
      pts.append(geom::Point<Lattice>(Lattice(q[0]), Lattice(q[1]), Lattice(q[2])));
    }
    const geom::PolyView<Lattice> view{{pts.data(), size_t(pts.size())}, {f.off.data(), size_t(f.off.size())}};
    int8_t axis = -1;
    Vector<geom::Triangle> tris;
    return topo::classify_face<Lattice>(view, true, axis, tris) == topo::FaceGeom::Ok;
  }

  template <typename F> static void for_each_side(const ResultFace &f, F &&fn)
  {
    for (int l = 0; l + 1 < int(f.off.size()); l++) {
      for (uint32_t i = f.off[l]; i < f.off[l + 1]; i++) {
        fn(f.pts[int(i)], f.pts[int(i + 1 < f.off[l + 1] ? i + 1 : f.off[l])]);
      }
    }
  }

  bool run(Vector<Vector<uint32_t, 2>> &members)
  {
    for (uint32_t f = 0; f < res.faces.size(); f++) {
      parent.append(f);
    }
    for (const ResultEdge &x : res.edges) {
      if (joins(x)) {
        parent[int(find(x.uses[0].facet))] = find(x.uses[1].facet);
        inner.add(directed(x.a, x.b), 0);
        inner.add(directed(x.b, x.a), 0);
      }
    }
    Vector<Vector<uint32_t, 2>> groups;
    gather(groups);
    /* a group that isn't a face: as much of it as is, an edge at a time */
    Vector<uint8_t> partly;
    ResultFace tmp;
    for (const auto &g : groups) {
      partly.append(g.size() > 2 && g.size() <= kMaxPartly && !outline(g, tmp));
      if (partly[int(partly.size()) - 1]) {
        for (uint32_t f : g) {
          parent[int(f)] = f;
        }
      }
    }
    Vector<uint32_t> of; /* by face: its group above */
    of.resize(res.faces.size());
    for (uint32_t g = 0; g < groups.size(); g++) {
      for (uint32_t f : groups[int(g)]) {
        of[int(f)] = g;
      }
    }
    Vector<Vector<uint32_t, 2>> parts = groups;
    for (uint32_t f = 0; f < res.faces.size(); f++) {
      if (partly[int(of[int(f)])]) {
        parts[int(f)].clear();
        parts[int(f)].append(f);
      }
    }
    for (const ResultEdge &x : res.edges) {
      if (!joins(x) || !partly[int(of[int(x.uses[0].facet)])]) {
        continue;
      }
      const uint32_t g = find(x.uses[0].facet), h = find(x.uses[1].facet);
      if (g == h) {
        continue;
      }
      Vector<uint32_t, 2> both = parts[int(g)];
      for (uint32_t f : parts[int(h)]) {
        both.append(f);
      }
      if (outline(both, tmp)) {
        parent[int(g)] = h;
        parts[int(h)] = both;
      }
    }
    gather(groups);
    out = Result();
    out.stats = res.stats;
    members.clear();
    Vector<uint32_t> face_of;
    face_of.resize(res.faces.size());
    for (uint32_t &x : face_of) {
      x = kNone;
    }
    bool any = false;
    for (uint32_t f = 0; f < res.faces.size(); f++) {
      if (face_of[int(f)] != kNone) {
        continue;
      }
      const auto &group = groups[int(find(f))];
      ResultFace joined = res.faces[int(f)];
      if (group.size() > 1 && outline(group, joined)) {
        any = true;
        joined.whole = false;
        joined.joined = true;
        members.append(group);
        for (uint32_t g : group) {
          face_of[int(g)] = uint32_t(out.faces.size());
        }
        out.faces.append(std::move(joined));
        continue;
      }
      face_of[int(f)] = uint32_t(out.faces.size());
      members.append({});
      members[int(members.size()) - 1].append(f);
      out.faces.append(res.faces[int(f)]);
    }
    for (const ResultEdge &x : res.edges) {
      if (joins(x) && face_of[int(x.uses[0].facet)] == face_of[int(x.uses[1].facet)]) {
        continue;
      }
      ResultEdge y = x;
      for (EdgeUse &u : y.uses) {
        u.facet = face_of[int(u.facet)];
      }
      out.edges.append(std::move(y));
    }
    return any;
  }
};

} // namespace

bool join_faces(const Result &result, std::span<const I3> positions,
                std::span<const uint32_t> root, Result &out, Vector<Vector<uint32_t, 2>> &members)
{
  Joiner j{result, positions, root, out, {}, {}};
  return j.run(members);
}

} // namespace ubertopo::csg
