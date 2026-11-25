#include "ubertopo/csg/output.h"

#include <algorithm>
#include <cmath>

#include "ubertopo/topo/builder.h"
#include "ubertopo/topo/face_geom.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::csg {

using geom::Lattice;
using litestl::util::Vector;
using LComplex = topo::Complex<Lattice>;

bool ExactPlacement::set(const RigidPlacement &p)
{
  constexpr double tol = 0x1p-40;
  for (int i = 0; i < 3; i++) {
    for (int j = i; j < 3; j++) {
      const double d = p.r[i][0] * p.r[j][0] + p.r[i][1] * p.r[j][1] + p.r[i][2] * p.r[j][2];
      if (!(std::abs(d - (i == j ? 1.0 : 0.0)) <= tol)) {
        return false;
      }
    }
  }
  const double det = p.r[0][0] * (p.r[1][1] * p.r[2][2] - p.r[1][2] * p.r[2][1]) -
                     p.r[0][1] * (p.r[1][0] * p.r[2][2] - p.r[1][2] * p.r[2][0]) +
                     p.r[0][2] * (p.r[1][0] * p.r[2][1] - p.r[1][1] * p.r[2][0]);
  if (!(det > 0)) {
    return false;
  }
  for (int i = 0; i < 3; i++) {
    if (!(std::abs(p.t[i]) <= 0x1p40)) {
      return false;
    }
    t_[i] = int64_t(std::floor(std::ldexp(p.t[i], kTransBits) + 0.5));
    for (int j = 0; j < 3; j++) {
      r_[i][j] = int64_t(std::ldexp(p.r[i][j], kRotBits));
    }
  }
  return true;
}

I3 ExactPlacement::round(const Input &in, const HPoint &p, int shift) const
{
  /* the operands' coordinates, over w */
  Int512 x[3];
  const Int512 w = Int512::from(p.w);
  for (int j = 0; j < 3; j++) {
    x[j] = Int512::from(p.x[j]) - Int512::from(in.shift[j]) * w;
  }
  I3 out;
  const Int512 one = w << kRotBits;
  for (int i = 0; i < 3; i++) {
    Int512 s = Int512::from(t_[i]) * (w << (kRotBits - kTransBits));
    for (int j = 0; j < 3; j++) {
      s += Int512::from(r_[i][j]) * x[j];
    }
    /* floor(s 2^shift / one + 1/2) */
    out[i] = floor_div((s << (shift + 1)) + one, one << 1);
  }
  return out;
}

namespace {

geom::Point<Lattice> lattice(const I3 &p)
{
  return geom::Point<Lattice>(Lattice(p[0]), Lattice(p[1]), Lattice(p[2]));
}

struct Writer {
  const Arrangement &a;
  const Expression &e;
  const Vector<Selected> &sel;
  const OutputOptions &options;
  OutputReport &rep;
  ExactPlacement place;
  Vector<uint32_t> key;    /* by point: builder key */
  Vector<uint8_t> used;    /* by point */

  /* outlines whose triangulation is another one after rounding */
  bool check_whole(Vector<uint8_t> &split)
  {
    bool any = false;
    split.resize(a.in.outlines.size());
    for (uint8_t &s : split) {
      s = 0;
    }
    Vector<geom::Point<Lattice>> pts;
    Vector<geom::Triangle> tris;
    for (const ResultFace &f : rep.result.faces) {
      if (!f.whole) {
        continue;
      }
      const uint32_t outline = a.in.polygons[int(f.source)].outline;
      const Outline &o = a.in.outlines[int(outline)];
      pts.clear();
      for (uint32_t p : f.pts) {
        pts.append(lattice(rep.positions[int(p)]));
      }
      const geom::PolyView<Lattice> view{{pts.data(), size_t(pts.size())}, {f.off.data(), size_t(f.off.size())}};
      int8_t axis = -1;
      bool same = topo::classify_face<Lattice>(view, true, axis, tris) == topo::FaceGeom::Ok;
      if (same && axis >= 0) {
        /* (a planar face has one surface) */
        same = tris.size() == o.count;
        for (uint32_t i = 0; i < o.count && same; i++) {
          const Polygon &t = a.in.polygons[int(o.first + i)];
          bool found = false;
          for (const geom::Triangle &u : tris) {
            int n = 0;
            for (int x = 0; x < 3; x++) {
              n += std::find(t.pts.begin(), t.pts.end(), f.pts[int(u[x])]) != t.pts.end();
            }
            found = found || n == 3;
          }
          same = found;
        }
      }
      if (!same) {
        split[int(outline)] = 1;
        rep.split_whole++;
        any = true;
      }
    }
    return any;
  }

  CsgCode round_points()
  {
    used.resize(a.in.points.size());
    rep.positions.resize(a.in.points.size());
    for (uint32_t p = 0; p < a.in.points.size(); p++) {
      used[int(p)] = 0;
      rep.positions[int(p)] = I3(0, 0, 0);
    }
    for (const ResultFace &f : rep.result.faces) {
      for (uint32_t p : f.pts) {
        used[int(p)] = 1;
      }
    }
    for (uint32_t p = 0; p < a.in.points.size(); p++) {
      if (used[int(p)]) {
        rep.positions[int(p)] = place.round(a.in, a.in.points[p], options.quantum_shift);
        if (!in_range(rep.positions[int(p)])) {
          return CsgCode::OutOfRange;
        }
      }
    }
    return CsgCode::Ok;
  }

  CsgCode fail(RoundCode c)
  {
    rep.round = c;
    return CsgCode::RoundingFailed;
  }

  CsgCode write(LComplex &k)
  {
    Vector<uint32_t> order;
    for (uint32_t p = 0; p < used.size(); p++) {
      key.append(kNone);
      if (used[int(p)]) {
        order.append(p);
      }
    }
    auto less = [&](uint32_t x, uint32_t y) {
      const I3 &p = rep.positions[int(x)], &q = rep.positions[int(y)];
      return p[0] != q[0] ? p[0] < q[0] : p[1] != q[1] ? p[1] < q[1] : p[2] < q[2];
    };
    std::sort(order.begin(), order.end(), less);
    for (int i = 1; i < int(order.size()); i++) {
      if (!less(order[i - 1], order[i])) {
        return fail(RoundCode::SamePosition);
      }
    }
    topo::Builder<Lattice> b;
    for (uint32_t i = 0; i < order.size(); i++) {
      key[int(order[int(i)])] =
          b.vertex(ident::derive(ident::tag::OpDerived, uint32_t(0), i), lattice(rep.positions[int(order[int(i)])]));
    }
    Vector<uint32_t> keys, sizes;
    for (uint32_t i = 0; i < rep.result.faces.size(); i++) {
      const ResultFace &f = rep.result.faces[int(i)];
      keys.clear();
      sizes.clear();
      for (uint32_t p : f.pts) {
        keys.append(key[int(p)]);
      }
      for (int l = 0; l + 1 < int(f.off.size()); l++) {
        sizes.append(f.off[l + 1] - f.off[l]);
      }
      b.face(ident::derive(ident::tag::OpDerived, uint32_t(1), i), {keys.data(), size_t(keys.size())},
             {sizes.data(), size_t(sizes.size())});
    }
    topo::BuildPolicy policy;
    policy.allow_projectable_faces = true;
    topo::BuildReport br;
    if (!b.build(k, br, policy)) {
      return fail(RoundCode::Build);
    }
    rep.projected = br.projected_faces;
    topo::RegionReport rr;
    if (!topo::build_regions(k, rr)) {
      return fail(RoundCode::Regions);
    }
    return CsgCode::Ok;
  }

  /* the rounded faces are arranged as the exact ones */
  CsgCode check(const LComplex &k)
  {
    litestl::util::Map<ident::PersistentId, uint32_t> vert, face;
    k.verts.for_each_live([&](uint32_t v) { vert.add(k.vert_id(topo::VertRef(v)), v); });
    k.faces.for_each_live([&](uint32_t f) { face.add(k.face_id(topo::FaceRef(f)), f); });
    rep.face_of.clear();
    for (uint32_t i = 0; i < rep.result.faces.size(); i++) {
      rep.face_of.append(*face.lookup_ptr(ident::derive(ident::tag::OpDerived, uint32_t(1), i)));
    }
    Vector<uint32_t> vert_of; /* by key */
    for (uint32_t i = 0; i < k.verts.capacity(); i++) {
      const uint32_t *v = vert.lookup_ptr(ident::derive(ident::tag::OpDerived, uint32_t(0), i));
      vert_of.append(v ? *v : kNone);
    }
    litestl::util::Map<uint64_t, uint32_t> edge;
    auto pair = [](uint32_t x, uint32_t y) { return (uint64_t(std::min(x, y)) << 32) | std::max(x, y); };
    k.edges.for_each_live([&](uint32_t x) {
      const auto vs = k.edge_verts(topo::EdgeRef(x));
      edge.add(pair(vs[0].i, vs[1].i), x);
    });
    if (edge.size() != rep.result.edges.size()) {
      return CsgCode::Inconsistent;
    }
    Vector<std::pair<uint32_t, bool>, 8> got, want;
    for (const ResultEdge &x : rep.result.edges) {
      const uint32_t va = vert_of[int(key[int(x.a)])], vb = vert_of[int(key[int(x.b)])];
      const uint32_t *ke = edge.lookup_ptr(pair(va, vb));
      if (!ke) {
        return CsgCode::Inconsistent;
      }
      const topo::EdgeRef er(*ke);
      const bool along = k.edge_verts(er)[0].i == va; /* vs[0] -> vs[1] is a -> b */
      want.clear();
      got.clear();
      for (int i = 0; i < int(x.uses.size()); i++) {
        const EdgeUse &u = x.uses[along ? i : int(x.uses.size()) - 1 - i];
        want.append({rep.face_of[int(u.facet)], (u.dir > 0) == along});
      }
      const topo::CoedgeRef first = k.edge_coedge(er);
      topo::CoedgeRef c = first;
      do {
        got.append({k.face_of(c).i, k.sense(c)});
        c = k.radial_next(c);
      } while (c.i != first.i && got.size() <= want.size());
      if (got.size() != want.size()) {
        return CsgCode::Inconsistent;
      }
      bool same = false;
      for (int s = 0; s < int(want.size()) && !same; s++) {
        same = true;
        for (int i = 0; i < int(want.size()) && same; i++) {
          same = got[i] == want[(i + s) % int(want.size())];
        }
      }
      if (!same) {
        return fail(RoundCode::Radial);
      }
    }
    Vector<uint32_t> label;
    for (uint32_t i = 0; i < k.regions.capacity(); i++) {
      label.append(i == 0 ? kVoid : kNone);
    }
    for (uint32_t i = 0; i < rep.result.faces.size(); i++) {
      for (uint32_t side = 0; side < 2; side++) {
        const uint32_t s = rep.result.faces[int(i)].solid[side];
        uint32_t &l = label[int(k.region(topo::FaceRef(rep.face_of[int(i)]), side).i)];
        l = l == kNone ? s : l;
        if (l != s) {
          return fail(RoundCode::Labels);
        }
      }
    }
    if (options.validate && !topo::validate(k, topo::ValidateLevel::Global).ok()) {
      return fail(RoundCode::Invalid);
    }
    return CsgCode::Ok;
  }

  CsgCode merge(std::span<const uint8_t> split)
  {
    rep.result = Result();
    const CsgCode c = merge_facets(a, e, sel, split, rep.result);
    return c == CsgCode::Ok ? round_points() : c;
  }

  CsgCode run(LComplex &out)
  {
    if (!place.set(options.placement)) {
      return CsgCode::BadPlacement;
    }
    CsgCode c = merge({});
    Vector<uint8_t> split;
    if (c == CsgCode::Ok && check_whole(split)) {
      c = merge({split.data(), size_t(split.size())});
    }
    LComplex k;
    c = c == CsgCode::Ok ? write(k) : c;
    c = c == CsgCode::Ok ? check(k) : c;
    if (c == CsgCode::Ok) {
      out = std::move(k);
    }
    return c;
  }
};

} // namespace

CsgCode build_output(const Arrangement &a, const Expression &e, const Vector<Selected> &selected,
                     const OutputOptions &options, LComplex &out, OutputReport &report)
{
  Writer w{a, e, selected, options, report, {}, {}, {}};
  return w.run(out);
}

} // namespace ubertopo::csg
