#include "ubertopo/csg/input.h"

#include <algorithm>
#include <cstdlib>

#include "litestl/util/map.h"
#include "ubertopo/topo/face_geom.h"
#include "ubertopo/topo/radial.h"

namespace ubertopo::csg {

namespace {

using geom::Lattice;
using LComplex = topo::Complex<Lattice>;

I3 raw(const geom::Point<Lattice> &p)
{
  return I3(p[0].raw, p[1].raw, p[2].raw);
}

struct Reader {
  Input &in;
  const LComplex &k;
  const GridPlacement &place;
  uint32_t leaf;
  bool mirrored;
  topo::Skeleton<Lattice> sk;
  topo::FacePoints<Lattice> fp;
  litestl::util::Vector<geom::Triangle> tris;
  litestl::util::Map<uint64_t, uint32_t> edge_of; /* by point pair, within the leaf */

  Reader(Input &in_, const Operand &op, uint32_t leaf_)
      : in(in_), k(*op.k), place(op.place), leaf(leaf_), mirrored(op.place.det() < 0), sk(*op.k)
  {
  }

  I3 position(const geom::Point<Lattice> &p) const
  {
    const I3 q = place.apply(raw(p));
    return I3(q[0] + in.shift[0], q[1] + in.shift[1], q[2] + in.shift[2]);
  }

  uint32_t point(const geom::Point<Lattice> &p)
  {
    const I3 q = position(p);
    const uint32_t i = in.points.add(q);
    if (i == in.ipos.size()) {
      in.ipos.append(q);
    }
    return i;
  }

  uint32_t edge(uint32_t a, uint32_t b, uint32_t src)
  {
    const uint64_t key = (uint64_t(std::min(a, b)) << 32) | std::max(a, b);
    if (const uint32_t *e = edge_of.lookup_ptr(key)) {
      return *e;
    }
    const uint32_t e = uint32_t(in.edges.size());
    in.edges.append({leaf, src, std::min(a, b), std::max(a, b)});
    edge_of.add(key, e);
    return e;
  }

  /* box, plane and projection axis; front: three points of the polygon, with
   * the front normal along (p1 - p0) x (p2 - p0) in the operand's frame */
  bool finish(Polygon &g, const geom::Point<Lattice> &p0, const geom::Point<Lattice> &p1,
              const geom::Point<Lattice> &p2)
  {
    const I3 n = cross_small(sub(raw(p1), raw(p0)), sub(raw(p2), raw(p0)));
    Plane pl;
    const int s = plane_from(position(p0), place.apply_dir(n), pl);
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
    return true;
  }

  /* corners [i, n) of one loop, given as indices into fp.pts */
  template <typename IdOf> void add_loop(Polygon &g, uint32_t n, IdOf &&id_of, bool diagonals)
  {
    const uint32_t base = uint32_t(g.pts.size());
    for (uint32_t i = 0; i < n; i++) {
      g.pts.append(point(fp.pts[int(id_of(mirrored ? n - 1 - i : i))]));
    }
    for (uint32_t i = 0; i < n; i++) {
      /* the edge from corner i to the next, in the loop's own order */
      const uint32_t j = mirrored ? (2 * n - 2 - i) % n : i;
      const uint32_t from = id_of(j), to = id_of((j + 1) % n);
      uint32_t src = kNone;
      if (!diagonals || consecutive(from, to)) {
        src = k.edge_of(topo::CoedgeRef(fp.owner[int(from)])).i;
      }
      g.edges.append(edge(g.pts[int(base + i)], g.pts[int(base + (i + 1) % n)], src));
    }
    g.off.append(uint32_t(g.pts.size()));
  }

  /* whether fp's corner b follows a on its loop */
  bool consecutive(uint32_t a, uint32_t b) const
  {
    for (uint32_t l = 0; l + 1 < fp.off.size(); l++) {
      if (a >= fp.off[int(l)] && a < fp.off[int(l) + 1]) {
        return b == (a + 1 < fp.off[int(l) + 1] ? a + 1 : fp.off[int(l)]);
      }
    }
    return false;
  }

  CsgCode face(topo::FaceRef f)
  {
    if (!k.faces.get<".f.surface">()[f.i].none()) {
      return CsgCode::UnsupportedOperand;
    }
    if (!fp.gather(k, f, sk) || fp.pts.size() < 3) {
      return CsgCode::BadOperand;
    }
    Polygon g;
    g.leaf = leaf;
    g.face = f.i;
    g.region[0] = k.region(f, 0).i;
    g.region[1] = k.region(f, 1).i;
    bool degenerate = false;
    if (k.face_proj_axis(f) < 0 && topo::face_points_planar<Lattice>(fp.view(), &degenerate)) {
      g.whole = true;
      g.off.append(0);
      for (uint32_t l = 0; l + 1 < fp.off.size(); l++) {
        const uint32_t b = fp.off[int(l)];
        add_loop(g, fp.off[int(l) + 1] - b, [&](uint32_t i) { return b + i; }, false);
      }
      /* three points in front order: the frame's axis and sign, on a base
       * triple off one line */
      const topo::FaceFrame fr = topo::face_frame<Lattice>(fp.view());
      uint32_t ib = 0, ic = 0;
      for (uint32_t i = 1; i < fp.pts.size() && !ic; i++) {
        if (!ib) {
          ib = geom::lex_compare<Lattice>(fp.pts[int(i)], fp.pts[0]) != 0 ? i : 0;
        }
        else if (fr.axis >= 0 &&
                 geom::orient2d_proj<Lattice>(fp.pts[0], fp.pts[int(ib)], fp.pts[int(i)], fr.axis) != 0)
        {
          ic = i;
        }
      }
      if (!ic || fr.sigma == 0) {
        return CsgCode::BadOperand;
      }
      if (geom::orient2d_proj<Lattice>(fp.pts[0], fp.pts[int(ib)], fp.pts[int(ic)], fr.axis) != fr.sigma) {
        std::swap(ib, ic);
      }
      if (!finish(g, fp.pts[0], fp.pts[int(ib)], fp.pts[int(ic)])) {
        return CsgCode::BadOperand;
      }
      in.polygons.append(std::move(g));
      return CsgCode::Ok;
    }
    tris.clear();
    if (!topo::face_triangles<Lattice>(k, f, tris, &sk) || tris.size() == 0) {
      return CsgCode::BadOperand;
    }
    Outline o;
    o.first = uint32_t(in.polygons.size());
    o.count = uint32_t(tris.size());
    o.off.append(0);
    for (uint32_t l = 0; l + 1 < fp.off.size(); l++) {
      const uint32_t b = fp.off[int(l)], n = fp.off[int(l) + 1] - b;
      for (uint32_t i = 0; i < n; i++) {
        o.pts.append(point(fp.pts[int(b + (mirrored ? n - 1 - i : i))]));
      }
      o.off.append(uint32_t(o.pts.size()));
    }
    g.outline = uint32_t(in.outlines.size());
    in.outlines.append(std::move(o));
    for (const geom::Triangle &t : tris) {
      Polygon h = g;
      h.whole = false;
      h.off.append(0);
      add_loop(h, 3, [&](uint32_t i) { return t[int(i)]; }, true);
      if (!finish(h, fp.pts[int(t[0])], fp.pts[int(t[1])], fp.pts[int(t[2])])) {
        return CsgCode::BadOperand;
      }
      in.polygons.append(std::move(h));
    }
    return CsgCode::Ok;
  }
};

bool has_wires_or_inclusions(const LComplex &k)
{
  bool any = false;
  k.verts.for_each_live([&](uint32_t v) {
    any = any || !k.vert_edge(topo::VertRef(v));
  });
  k.edges.for_each_live([&](uint32_t e) { any = any || !topo::CoedgeRef(k.edges.get<".e.c">()[e]); });
  return any;
}

} // namespace

CsgCode read_operands(std::span<const Operand> leaves, Input &out, uint32_t *bad_leaf)
{
  auto fail = [&](CsgCode c, uint32_t leaf) {
    if (bad_leaf) {
      *bad_leaf = leaf;
    }
    return c;
  };
  /* the box of all operands, moved to the origin */
  bool any = false;
  I3 lo(0, 0, 0), hi(0, 0, 0);
  for (uint32_t l = 0; l < leaves.size(); l++) {
    const Operand &op = leaves[l];
    if (!op.k || op.place.det() == 0 || op.k->regions_dirty) {
      return fail(CsgCode::BadOperand, l);
    }
    if (op.k->frame.quantum_log2 != leaves[0].k->frame.quantum_log2) {
      return fail(CsgCode::MixedFrames, l);
    }
    if (has_wires_or_inclusions(*op.k)) {
      return fail(CsgCode::UnsupportedOperand, l);
    }
    bool ok = true;
    op.k->verts.for_each_live([&](uint32_t v) {
      const I3 q = op.place.apply(raw(op.k->pos(topo::VertRef(v))));
      for (int i = 0; i < 3; i++) {
        ok = ok && std::abs(op.place.offset[i]) <= (int64_t(1) << 40);
        lo[i] = any ? std::min(lo[i], q[i]) : q[i];
        hi[i] = any ? std::max(hi[i], q[i]) : q[i];
      }
      any = true;
    });
    if (!ok) {
      return fail(CsgCode::OutOfRange, l);
    }
  }
  for (int i = 0; i < 3; i++) {
    out.shift[i] = -(lo[i] + (hi[i] - lo[i]) / 2);
  }
  /* input points take the first indices of the table */
  for (uint32_t l = 0; l < leaves.size(); l++) {
    out.leaf_begin.append(uint32_t(out.polygons.size()));
    Reader r(out, leaves[l], l);
    CsgCode c = CsgCode::Ok;
    leaves[l].k->faces.for_each_live([&](uint32_t f) {
      if (c == CsgCode::Ok) {
        c = r.face(topo::FaceRef(f));
      }
    });
    if (c != CsgCode::Ok) {
      return fail(c, l);
    }
    for (const I3 &p : out.ipos) {
      if (!in_range(p)) {
        return fail(CsgCode::OutOfRange, l);
      }
    }
  }
  out.leaf_begin.append(uint32_t(out.polygons.size()));
  return CsgCode::Ok;
}

} // namespace ubertopo::csg
