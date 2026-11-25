#include "ubertopo/csg/names.h"

#include <algorithm>

#include "litestl/util/map.h"
#include "ubertopo/csg/planar.h"

namespace ubertopo::csg {

namespace {

using ident::derive;
using ident::kNoId;
using litestl::util::Vector;
namespace tag = ident::tag;

/* kinds of names */
enum : uint32_t { kOwn, kEdgeEdge, kEdgeFace, kFaces, kWeak, kOrdinal, kSkeleton };
/* kinds of carriers */
enum : uint32_t { kSourceEdge, kCut, kDiagonal };

/* a source element at a vertex */
struct Carrier {
  uint32_t leaf;
  PersistentId id;
  I3 v; /* of an edge its direction, of a face its normal */

  /* tree order, then by ID */
  bool operator<(const Carrier &b) const
  {
    return leaf != b.leaf ? leaf < b.leaf : id < b.id;
  }
};

bool parallel(const I3 &a, const I3 &b)
{
  return cross(a, b).is_zero();
}

/* the order of a name's parts: by ID, so that it doesn't depend on the tree's */
struct Part {
  PersistentId leaf, id;

  bool operator<(const Part &b) const
  {
    return leaf == b.leaf ? id < b.id : leaf < b.leaf;
  }
};

/* the smallest of a piece's coedges */
struct CoedgeKey {
  PersistentId start, carrier;

  bool operator==(const CoedgeKey &b) const = default;
  bool operator<(const CoedgeKey &b) const
  {
    return start == b.start ? carrier < b.carrier : start < b.start;
  }
};

struct Namer {
  const Arrangement &a;
  const Expression &e;
  const Vector<Selected> &sel;
  const Result &res;
  const NameSource &src;
  Names &out;

  litestl::util::Map<uint64_t, uint32_t> src_vert; /* (leaf, point): in Input::verts */
  litestl::util::Map<uint64_t, uint32_t> edge_of;  /* result edge, by its points */
  Vector<Vector<uint32_t, 6>> faces_at, edges_at;  /* by point */
  Vector<uint32_t> rank;                           /* by point: lexicographic */

  static uint64_t pair(uint32_t p, uint32_t q)
  {
    return (uint64_t(std::min(p, q)) << 32) | std::max(p, q);
  }
  /* i: in Input::verts */
  PersistentId vert_id(uint32_t i) const
  {
    const SrcVert &v = a.in.verts[int(i)];
    if (src.previous) {
      return src.previous->vertex[v.vert];
    }
    const topo::Complex<geom::Lattice> &k = *src.operands[v.leaf].k;
    return v.vert != kNone ? k.vert_id(topo::VertRef(v.vert))
                           : derive(tag::BoolVertex, uint32_t(kSkeleton), k.edge_id(topo::EdgeRef(v.edge)), v.index);
  }
  PersistentId face_id(const Polygon &g) const
  {
    return src.previous ? src.previous->face[g.face] : src.operands[g.leaf].k->face_id(topo::FaceRef(g.face));
  }
  PersistentId primary(const Polygon &g) const
  {
    return src.previous ? face_id(g) : derive(tag::BoolFace, src.leaf_ids[g.leaf], face_id(g));
  }
  /* p is a vertex of the face that g is of */
  const uint32_t *own_corner(const Polygon &g, uint32_t p) const
  {
    if (src.previous) {
      const auto &pts = src.previous->result.faces[int(g.face)].pts;
      if (std::none_of(pts.begin(), pts.end(), [&](uint32_t q) { return src.previous->point_of[q] == p; })) {
        return nullptr;
      }
    }
    return own(g.leaf, p);
  }
  const uint32_t *own(uint32_t leaf, uint32_t p) const
  {
    return src_vert.lookup_ptr((uint64_t(leaf) << 32) | p);
  }
  /* a diagonal of T(f) is named as an edge between its vertices would be */
  PersistentId edge_id(const SrcEdge &x) const
  {
    if (x.edge != kNone && !src.previous) {
      return src.operands[x.leaf].k->edge_id(topo::EdgeRef(x.edge));
    }
    const uint32_t *va = own(x.leaf, x.a), *vb = own(x.leaf, x.b);
    return va && vb ? ident::edge_id(vert_id(*va), vert_id(*vb)) : kNoId;
  }

  void index()
  {
    for (uint32_t i = 0; i < a.in.verts.size(); i++) {
      const SrcVert &v = a.in.verts[int(i)];
      src_vert.add((uint64_t(v.leaf) << 32) | v.point, i);
    }
    const uint32_t np = a.in.points.size();
    Vector<uint32_t> order;
    for (uint32_t p = 0; p < np; p++) {
      faces_at.append({});
      edges_at.append({});
      rank.append(kNone);
      out.vertex.append(kNoId);
      out.vertex_src.append(kNone);
    }
    for (uint32_t f = 0; f < res.faces.size(); f++) {
      for (uint32_t p : res.faces[int(f)].pts) {
        if (faces_at[int(p)].size() == 0) {
          order.append(p);
        }
        faces_at[int(p)].append(f);
      }
    }
    for (uint32_t x = 0; x < res.edges.size(); x++) {
      const ResultEdge &re = res.edges[int(x)];
      edges_at[int(re.a)].append(x);
      edges_at[int(re.b)].append(x);
      edge_of.add(pair(re.a, re.b), x);
    }
    std::sort(order.begin(), order.end(),
              [&](uint32_t p, uint32_t q) { return lex_compare(a.in.points[p], a.in.points[q]) < 0; });
    for (uint32_t i = 0; i < order.size(); i++) {
      rank[int(order[int(i)])] = i;
    }
    points = std::move(order);
  }
  Vector<uint32_t> points; /* of the result, in lexicographic order */

  /* ---- vertices ---- */

  /* corner or on the segment */
  bool on(const PlanarGraph &g, const SrcEdge &x, uint32_t p) const
  {
    return p == x.a || p == x.b || (g.turn(x.a, x.b, p) == 0 && g.between(x.a, x.b, p));
  }

  static void add(Vector<Carrier, 8> &v, const Carrier &c)
  {
    for (const Carrier &x : v) {
      if (x.leaf == c.leaf && x.id == c.id && x.v[0] == c.v[0] && x.v[1] == c.v[1] && x.v[2] == c.v[2]) {
        return;
      }
    }
    v.append(c);
  }

  /* faces that cut a result edge at one end, and that the result doesn't have */
  void cutters(uint32_t arr, Vector<Carrier, 8> &v) const
  {
    const ArrEdge &x = a.edges[int(arr)];
    uint32_t best = kNone;
    for (const EdgeUse &u : x.uses) {
      const uint32_t plane = a.facets[int(u.facet)].plane;
      const bool here = std::any_of(x.uses.begin(), x.uses.end(), [&](const EdgeUse &y) {
        return sel[int(y.facet)].kept && a.facets[int(y.facet)].plane == plane;
      });
      if (here) {
        continue;
      }
      for (uint32_t s : a.facets[int(u.facet)].sources) {
        if (best == kNone || before(s, best)) {
          best = s;
        }
      }
    }
    if (best != kNone) {
      const Polygon &g = a.in.polygons[int(best)];
      add(v, {g.leaf, face_id(g), a.in.planes[g.plane].n});
    }
  }

  /* polygons: tree order, then by face */
  bool before(uint32_t s, uint32_t t) const
  {
    const Polygon &g = a.in.polygons[int(s)], &h = a.in.polygons[int(t)];
    return g.leaf != h.leaf ? g.leaf < h.leaf : face_id(g) < face_id(h);
  }

  PersistentId vertex_name(uint32_t p)
  {
    uint32_t leaf = kNone;
    Vector<Carrier, 8> edges, faces;
    for (uint32_t f : faces_at[int(p)]) {
      const Polygon &g = a.in.polygons[int(res.faces[int(f)].source)];
      if (own(g.leaf, p)) {
        leaf = std::min(leaf, g.leaf);
        continue;
      }
      PlanarGraph pg(a.in, g.plane, g.axis);
      for (uint32_t x : g.edges) {
        const SrcEdge &se = a.in.edges[int(x)];
        if (on(pg, se, p)) {
          add(edges, {g.leaf, edge_id(se), sub(a.in.ipos[int(se.b)], a.in.ipos[int(se.a)])});
        }
      }
      add(faces, {g.leaf, face_id(g), a.in.planes[g.plane].n});
    }
    if (leaf != kNone) {
      const uint32_t v = *own(leaf, p);
      out.vertex_src[int(p)] = a.in.verts[int(v)].vert != kNone ? v : kNone;
      if (src.previous) {
        return vert_id(v);
      }
      return derive(tag::BoolVertex, uint32_t(kOwn), src.leaf_ids[leaf], vert_id(*own(leaf, p)));
    }
    std::sort(edges.begin(), edges.end());
    std::sort(faces.begin(), faces.end());
    if (edges.size() > 0) {
      for (int i = 1; i < int(edges.size()); i++) {
        if (!parallel(edges[0].v, edges[i].v)) {
          return name(kEdgeEdge, {&edges[0], &edges[i]});
        }
      }
      for (const Carrier &f : faces) {
        if (!dot(f.v, edges[0].v).is_zero()) {
          return name(kEdgeFace, {&edges[0], &f});
        }
      }
    }
    /* faces that only cut, for what is missing */
    Vector<Carrier, 8> more;
    for (uint32_t x : edges_at[int(p)]) {
      const ResultEdge &re = res.edges[int(x)];
      const uint32_t arr = re.arr[re.a == p ? 0 : 1];
      if (arr != kNone) {
        cutters(arr, more);
      }
    }
    std::sort(more.begin(), more.end());
    if (edges.size() > 0) {
      for (const Carrier &f : more) {
        if (!dot(f.v, edges[0].v).is_zero()) {
          return name(kEdgeFace, {&edges[0], &f});
        }
      }
    }
    for (const Carrier &f : more) {
      add(faces, f);
    }
    const Carrier *pick[3] = {nullptr, nullptr, nullptr};
    for (const Carrier &f : faces) {
      if (!pick[0]) {
        pick[0] = &f;
      }
      else if (!pick[1]) {
        pick[1] = parallel(pick[0]->v, f.v) ? nullptr : &f;
      }
      else if (triple(pick[0]->v, pick[1]->v, f.v).sign() != 0) {
        return name(kFaces, {pick[0], pick[1], &f});
      }
    }
    out.weak++;
    return name(kWeak, {edges.size() > 0 ? &edges[0] : nullptr, pick[0], pick[1]});
  }

  PersistentId name(uint32_t kind, std::initializer_list<const Carrier *> parts) const
  {
    Vector<Part, 3> v;
    for (const Carrier *c : parts) {
      if (c) {
        v.append({src.leaf_ids[c->leaf], c->id});
      }
    }
    std::sort(v.begin(), v.end());
    ident::Hasher128 h(tag::BoolVertex);
    h.u32(kind);
    for (const Part &x : v) {
      h.h128(x.leaf).h128(x.id);
    }
    return h.finish();
  }

  /* the first of equal names keeps it */
  struct Uniquer {
    litestl::util::Map<PersistentId, uint32_t> seen;

    PersistentId operator()(PersistentId id, ident::DomainTag domain, uint32_t &count)
    {
      const PersistentId base = id;
      uint32_t *n = seen.lookup_ptr(base);
      if (!n) {
        seen.add(base, 1);
        return id;
      }
      count++;
      id = derive(domain, uint32_t(kOrdinal), base, (*n)++);
      return id;
    }
  };

  void name_vertices()
  {
    Uniquer unique;
    for (uint32_t p : points) {
      out.vertex[int(p)] = unique(vertex_name(p), tag::BoolVertex, out.ordinals);
    }
  }

  /* ---- faces ---- */

  /* what the coedge of a face from p to q is on (seen from the source's front) */
  PersistentId carrier(const ResultFace &r, uint32_t p, uint32_t q, uint32_t &kind) const
  {
    const Polygon &g = a.in.polygons[int(r.source)];
    if (!r.whole && !r.joined) {
      PlanarGraph pg(a.in, g.plane, g.axis);
      for (uint32_t x : g.edges) {
        const SrcEdge &se = a.in.edges[int(x)];
        if (on(pg, se, p) && on(pg, se, q)) {
          kind = kSourceEdge;
          return edge_id(se);
        }
      }
    }
    else {
      kind = kSourceEdge;
      return kNoId; /* (a whole face is the only piece) */
    }
    const ResultEdge &re = res.edges[int(*edge_of.lookup_ptr(pair(p, q)))];
    const uint32_t arr = re.arr[re.a == p ? 0 : 1];
    if (arr == kNone) {
      kind = kDiagonal;
      return out.vertex[int(q)];
    }
    kind = kCut;
    /* the first face across it: one of the result, else one that cuts, else
     * one in its plane */
    uint32_t best[3] = {kNone, kNone, kNone};
    for (const EdgeUse &u : a.edges[int(arr)].uses) {
      const Facet &f = a.facets[int(u.facet)];
      const Selected &s = sel[int(u.facet)];
      const bool across = f.plane != g.plane;
      auto take = [&](uint32_t which, uint32_t polygon) {
        const Polygon &h = a.in.polygons[int(polygon)];
        if ((h.leaf != g.leaf || h.face != g.face) && (best[which] == kNone || before(polygon, best[which]))) {
          best[which] = polygon;
        }
      };
      if (across && s.kept) {
        take(0, s.source);
      }
      for (uint32_t x : f.sources) {
        take(across ? 1 : 2, x);
      }
    }
    const uint32_t c = best[0] != kNone ? best[0] : best[1] != kNone ? best[1] : best[2];
    if (c == kNone) {
      return kNoId;
    }
    const Polygon &h = a.in.polygons[int(c)];
    return derive(tag::BoolFace, uint32_t(kCut), src.leaf_ids[h.leaf], face_id(h), direction(g, h, p, q));
  }

  /* whether p -> q is along the cross product of the faces' front normals */
  bool direction(const Polygon &g, const Polygon &h, uint32_t p, uint32_t q) const
  {
    const Dir d = cross(a.in.planes[g.plane].n, a.in.planes[h.plane].n);
    for (int i = 0; i < 3; i++) {
      const int s = compare(a.in.points[q], a.in.points[p], i) * d.v[i].sign() * g.sign * h.sign;
      if (s != 0) {
        return s > 0;
      }
    }
    return false;
  }

  struct Piece {
    uint32_t face;
    bool has_own = false;
    PersistentId vertex; /* the smallest own one */
    bool has_key = false;
    CoedgeKey key;

    bool operator<(const Piece &b) const
    {
      if (has_own != b.has_own) {
        return has_own;
      }
      if (has_own && !(vertex == b.vertex)) {
        return vertex < b.vertex;
      }
      return has_key && b.has_key && !(key == b.key) ? key < b.key : has_key && !b.has_key;
    }
  };

  /* calls f(start, end) for the coedges, the source's front up */
  template <typename F> static void for_each_coedge(const ResultFace &r, F &&f)
  {
    for (int l = 0; l + 1 < int(r.off.size()); l++) {
      const uint32_t b = r.off[l], n = r.off[l + 1] - b;
      for (uint32_t i = 0; i < n; i++) {
        const uint32_t p = r.pts[int(b + i)], q = r.pts[int(b + (i + 1) % n)];
        r.reversed ? f(q, p) : f(p, q);
      }
    }
  }

  Piece piece(uint32_t face) const
  {
    const ResultFace &r = res.faces[int(face)];
    const Polygon &src_polygon = a.in.polygons[int(r.source)];
    Piece x;
    x.face = face;
    uint32_t at = kNone;
    for (uint32_t p : r.pts) {
      if (const uint32_t *v = own_corner(src_polygon, p)) {
        const PersistentId id = vert_id(*v);
        if (!x.has_own || id < x.vertex) {
          x.has_own = true;
          x.vertex = id;
          at = p;
        }
      }
    }
    bool on_source = false;
    for_each_coedge(r, [&](uint32_t p, uint32_t q) {
      if (x.has_own && p != at) {
        return;
      }
      uint32_t kind = 0;
      const PersistentId c = carrier(r, p, q, kind);
      const bool better_kind = !x.has_own && kind == kSourceEdge && !on_source;
      if (!x.has_own && kind != kSourceEdge && on_source) {
        return;
      }
      const CoedgeKey key{out.vertex[int(p)], derive(tag::BoolFace, kind, c)};
      if (!x.has_key || better_kind || key < x.key) {
        x.has_key = true;
        x.key = key;
        on_source = on_source || kind == kSourceEdge;
      }
    });
    return x;
  }

  /* for what has one name all the same */
  bool before_by_points(uint32_t f, uint32_t g) const
  {
    Vector<uint32_t, 16> x, y;
    for (uint32_t p : res.faces[int(f)].pts) {
      x.append(rank[int(p)]);
    }
    for (uint32_t p : res.faces[int(g)].pts) {
      y.append(rank[int(p)]);
    }
    std::sort(x.begin(), x.end());
    std::sort(y.begin(), y.end());
    return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
  }

  void name_faces()
  {
    litestl::util::Map<uint64_t, uint32_t> group_of; /* (leaf, face) */
    Vector<Vector<uint32_t, 2>> groups;
    for (uint32_t f = 0; f < res.faces.size(); f++) {
      const Polygon &g = a.in.polygons[int(res.faces[int(f)].source)];
      const uint64_t key = (uint64_t(g.leaf) << 32) | g.face;
      const uint32_t *i = group_of.lookup_ptr(key);
      if (!i) {
        group_of.add(key, uint32_t(groups.size()));
        groups.append({});
        groups[int(groups.size()) - 1].append(f);
      }
      else {
        groups[int(*i)].append(f);
      }
      out.face.append(kNoId);
    }
    Uniquer unique;
    for (const auto &group : groups) {
      const Polygon &g = a.in.polygons[int(res.faces[int(group[0])].source)];
      const PersistentId leaf = src.leaf_ids[g.leaf], face = face_id(g);
      if (group.size() == 1) {
        out.face[int(group[0])] = unique(primary(g), tag::BoolFace, out.collisions);
        continue;
      }
      Vector<Piece> pieces;
      for (uint32_t f : group) {
        pieces.append(piece(f));
      }
      std::sort(pieces.begin(), pieces.end(), [&](const Piece &x, const Piece &y) {
        return x < y || (!(y < x) && before_by_points(x.face, y.face));
      });
      for (int i = 0; i < int(pieces.size()); i++) {
        const Piece &x = pieces[i];
        PersistentId id = primary(g);
        if (i > 0) {
          const bool shared = std::any_of(pieces.begin(), pieces.end(), [&](const Piece &y) {
            return y.face != x.face && y.has_own && y.vertex == x.vertex;
          });
          id = x.has_own && !shared ? derive(tag::BoolFace, leaf, face, x.vertex)
                                    : derive(tag::BoolFace, leaf, face, x.key.start, x.key.carrier);
        }
        out.face[int(x.face)] = unique(id, tag::BoolFace, out.collisions);
      }
    }
  }

  /* pieces of operands' edges */
  void find_edges()
  {
    for (const ResultEdge &re : res.edges) {
      uint32_t best = kNone;
      for (const EdgeUse &u : re.uses) {
        const ResultFace &r = res.faces[int(u.facet)];
        const Polygon &g0 = a.in.polygons[int(r.source)];
        uint32_t begin = r.whole ? a.in.outlines[int(g0.outline)].first : r.source;
        uint32_t end = begin + (r.whole ? a.in.outlines[int(g0.outline)].count : 1);
        if (r.joined) {
          /* (a joined face's pieces may be of several faces: names don't need
           * more than its first one's) */
          for (; begin > 0 && same_face(begin - 1, r.source); begin--) {
          }
          for (; end < a.in.polygons.size() && same_face(end, r.source); end++) {
          }
        }
        for (uint32_t s = begin; s < end; s++) {
          const Polygon &g = a.in.polygons[int(s)];
          if (r.joined && (classify(a.in.points[re.a], a.in.planes[g.plane]) != 0 ||
                           classify(a.in.points[re.b], a.in.planes[g.plane]) != 0))
          {
            continue;
          }
          const PlanarGraph pg(a.in, g.plane, g.axis);
          for (uint32_t x : g.edges) {
            const SrcEdge &se = a.in.edges[int(x)];
            if (se.edge == kNone || (best != kNone && !edge_before(x, best))) {
              continue;
            }
            /* (p is in the plane of each triangle that it is a corner of only) */
            if (r.whole ? (re.a == se.a && re.b == se.b) : (on(pg, se, re.a) && on(pg, se, re.b))) {
              best = x;
            }
          }
        }
      }
      out.edge_src.append(best);
    }
  }
  bool same_face(uint32_t s, uint32_t t) const
  {
    const Polygon &g = a.in.polygons[int(s)], &h = a.in.polygons[int(t)];
    return g.leaf == h.leaf && g.face == h.face;
  }
  bool edge_before(uint32_t x, uint32_t y) const
  {
    const SrcEdge &s = a.in.edges[int(x)], &t = a.in.edges[int(y)];
    return s.leaf != t.leaf ? s.leaf < t.leaf : edge_id(s) < edge_id(t);
  }

  void run()
  {
    index();
    name_vertices();
    name_faces();
    find_edges();
  }
};

} // namespace

void name_elements(const Arrangement &a, const Expression &e, const Vector<Selected> &selected, const Result &result,
                   const NameSource &source, Names &out)
{
  out = Names();
  Namer n{a, e, selected, result, source, out};
  n.run();
}

void name_regions(std::span<const RegionSides> regions, const NameSource &source, Vector<PersistentId> &hints)
{
  hints.clear();
  struct Named {
    PersistentId name, min_face;
    uint32_t region;
  };
  Vector<Named> named;
  for (uint32_t i = 0; i < regions.size(); i++) {
    const RegionSides &r = regions[i];
    const uint32_t label = r.solid != kVoid ? r.solid : r.air;
    hints.append(kNoId);
    if (i != 0 && label != kVoid) {
      named.append({source.labels[label], r.min_face, i});
    }
  }
  std::sort(named.begin(), named.end(), [](const Named &x, const Named &y) {
    return x.name == y.name ? x.min_face < y.min_face : x.name < y.name;
  });
  for (int i = 0; i < int(named.size()); i++) {
    const Named &n = named[i];
    const bool first = i == 0 || !(named[i - 1].name == n.name);
    hints[int(n.region)] = first ? n.name : derive(tag::BoolRegion, n.name, n.min_face);
  }
}

} // namespace ubertopo::csg
