#pragma once

/* Validator internals shared by the full and the filtered geometric layers
 * (validate_geom.cc, validate_grid.cc). Not a public API. */

#include <algorithm>
#include <array>
#include <cmath>

#include "litestl/util/vector.h"
#include "ubertopo/geom/cdt/cdt.h"
#include "ubertopo/geom/predicates.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/face_geom.h"
#include "ubertopo/topo/radial.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::topo::vdetail {

namespace util = litestl::util;

template <geom::CoordType C> bool face_is_planar(const geom::PolyView<C> &v)
{
  const auto &p = v.pts;
  int ib = -1, ic = -1;
  for (int i = 1; i < int(p.size()) && ib < 0; i++) {
    if (geom::lex_compare<C>(p[i], p[0]) != 0) {
      ib = i;
    }
  }
  for (int i = 1; ib >= 0 && i < int(p.size()) && ic < 0; i++) {
    for (int a = 0; a < 3; a++) {
      if (geom::orient2d_proj<C>(p[0], p[ib], p[i], a) != 0) {
        ic = i;
        break;
      }
    }
  }
  if (ic < 0) {
    return false;
  }
  for (size_t i = 0; i < p.size(); i++) {
    if (geom::vol3<C>(p[0], p[ib], p[ic], p[i]) != 0) {
      return false;
    }
  }
  return true;
}

/** The axis a face's geometry uses: the stored axis of a projected face, else
 * the dominant Newell axis. */
template <geom::CoordType C> int face_axis(const Complex<C> &k, FaceRef f, const geom::PolyView<C> &v)
{
  const int a = k.face_proj_axis(f);
  return a >= 0 ? a : geom::project_axis<C>(v);
}

/** Front-winding triangles of face @p f: the loop fan for a planar face
 * (signed; correct for winding and volumes), T(f) for a projected one. */
template <geom::CoordType C> void face_fan(const Complex<C> &k, FaceRef f, util::Vector<geom::Point<C>> &out)
{
  out.clear();
  FacePoints<C> fp;
  fp.gather(k, f);
  if (k.face_proj_axis(f) >= 0) {
    util::Vector<geom::Triangle> t;
    if (geom::cdt_proj<C>(fp.view(), k.face_proj_axis(f), t) == geom::CdtStatus::Ok) {
      for (auto &x : t) {
        out.append(fp.pts[x[0]]);
        out.append(fp.pts[x[1]]);
        out.append(fp.pts[x[2]]);
      }
    }
    return;
  }
  const auto v = fp.view();
  for (uint32_t l = 0; l < v.loop_count(); l++) {
    const auto lp = v.loop(l);
    for (size_t i = 1; i + 1 < lp.size(); i++) {
      out.append(lp[0]);
      out.append(lp[i]);
      out.append(lp[i + 1]);
    }
  }
}

/** Appends the triangles of face side @p fs (2 f + s) oriented away from the
 * side's region: front winding for back sides. */
template <geom::CoordType C>
void append_side_tris(const util::Vector<geom::Point<C>> &fan, uint32_t fs, util::Vector<geom::Point<C>> &tris)
{
  for (uint32_t i = 0; i + 2 < fan.size(); i += 3) {
    tris.append(fan[i]);
    tris.append((fs & 1u) ? fan[i + 1] : fan[i + 2]);
    tris.append((fs & 1u) ? fan[i + 2] : fan[i + 1]);
  }
}

/** Region winding of point p: the shells of region r, oriented away from r.
 * @p fan_of(face index) returns the face's front-winding triangles. */
template <geom::CoordType C, typename FanOf>
geom::RayResult region_winding(const Complex<C> &k, RegionRef r, const geom::Point<C> &p, FanOf &&fan_of)
{
  util::Vector<geom::Point<C>> tris;
  const auto &sn = k.faces.sides().template get<".fs.shell_next">();
  for (uint32_t s = k.regions.template get<".r.shell">()[r.i]; s != NONE; s = k.shells.template get<".s.next">()[s]) {
    for (uint32_t fs = k.shells.template get<".s.first">()[s]; fs != NONE; fs = sn[fs]) {
      append_side_tris<C>(fan_of(fs / 2), fs, tris);
    }
  }
  return geom::ray_winding<C>(p, std::span<const geom::Point<C>>(tris.data(), tris.size()));
}

/** p in the closed triangle (a, b, c), all coplanar: projected sign tests. */
template <geom::CoordType C>
bool in_closed_triangle(const geom::Point<C> &p, const geom::Point<C> &a, const geom::Point<C> &b, const geom::Point<C> &c)
{
  for (int ax = 0; ax < 3; ax++) {
    const int o = geom::orient2d_proj<C>(a, b, c, ax);
    if (o != 0) {
      const int s1 = geom::orient2d_proj<C>(a, b, p, ax) * o, s2 = geom::orient2d_proj<C>(b, c, p, ax) * o,
                s3 = geom::orient2d_proj<C>(c, a, p, ax) * o;
      return s1 >= 0 && s2 >= 0 && s3 >= 0;
    }
  }
  return false;
}

/** Does segment (p, q) of one face (or a wire) meet triangle t of another
 * face other than at shared vertices (@p shared_v) and along shared edges
 * (@p shared_e, only when (p, q) is a boundary edge)? */
template <geom::CoordType C, typename SharedV, typename SharedE>
bool bad_contact(const Complex<C> &k,
                 uint32_t p,
                 uint32_t q,
                 bool pq_boundary,
                 const std::array<uint32_t, 3> &t,
                 SharedV &&shared_v,
                 SharedE &&shared_e)
{
  using P = geom::Point<C>;
  auto pos = [&](uint32_t v) -> const P & { return k.pos(VertRef(v)); };
  const P &P0 = pos(p), &Q0 = pos(q), &A = pos(t[0]), &B = pos(t[1]), &Cc = pos(t[2]);
  const int op = geom::vol3<C>(A, B, Cc, P0), oq = geom::vol3<C>(A, B, Cc, Q0);
  if (op * oq > 0) {
    return false;
  }
  if (op == 0 && oq == 0) {
    /* coplanar: 2D in a projection where t is non-degenerate */
    int ax = -1;
    for (int a = 0; a < 3 && ax < 0; a++) {
      if (geom::orient2d_proj<C>(A, B, Cc, a) != 0) {
        ax = a;
      }
    }
    if (ax < 0) {
      return false;
    }
    for (const uint32_t e : {p, q}) {
      if (in_closed_triangle<C>(pos(e), A, B, Cc) && !shared_v(e)) {
        return true; /* a non-shared endpoint on or in t */
      }
    }
    const uint32_t te[3][2] = {{t[0], t[1]}, {t[1], t[2]}, {t[2], t[0]}};
    for (auto &e : te) {
      const geom::SegIsect s = geom::segments_intersect_proj<C>(P0, Q0, pos(e[0]), pos(e[1]), ax);
      if (s == geom::SegIsect::Proper) {
        return true;
      }
      if (s == geom::SegIsect::Overlap) {
        const bool same = (std::min(p, q) == std::min(e[0], e[1]) && std::max(p, q) == std::max(e[0], e[1]));
        if (!(same && pq_boundary && shared_e(p, q))) {
          return true;
        }
      }
      if (s == geom::SegIsect::Touch) {
        /* touching at a t vertex inside (p, q), or at p / q */
        for (const uint32_t tv : e) {
          if (tv != p && tv != q && geom::orient2d_proj<C>(P0, Q0, pos(tv), ax) == 0 &&
              geom::on_segment_collinear_proj<C>(pos(tv), P0, Q0, ax) && !shared_v(tv))
          {
            return true;
          }
        }
      }
    }
    return false;
  }
  if (op == 0 || oq == 0) {
    const uint32_t on = op == 0 ? p : q;
    return in_closed_triangle<C>(pos(on), A, B, Cc) && !shared_v(on);
  }
  /* strict crossing of t's plane at an interior point of (p, q) */
  const int e1 = geom::vol3<C>(P0, Q0, A, B), e2 = geom::vol3<C>(P0, Q0, B, Cc), e3 = geom::vol3<C>(P0, Q0, Cc, A);
  const bool pos_ = e1 > 0 || e2 > 0 || e3 > 0, neg = e1 < 0 || e2 < 0 || e3 < 0;
  return !(pos_ && neg); /* the crossing point lies in the closed triangle */
}

/** Loop checks of face @p f (with @p checks: simple, disjoint, holes inside
 * the outer loop) and its triangulation T(f) as vertex-index triples. False
 * if the face fails a check or doesn't triangulate. */
template <geom::CoordType C>
bool face_checks_and_tris(const Complex<C> &k,
                          uint32_t f,
                          bool checks,
                          ValidationReport &rep,
                          util::Vector<std::array<uint32_t, 3>> &tris)
{
  using ident::ElemKind;
  using VC = ValidateCode;
  tris.clear();
  FacePoints<C> fp;
  fp.gather(k, FaceRef(f));
  const auto v = fp.view();
  const int axis = face_axis<C>(k, FaceRef(f), v);
  if (checks) {
    const uint32_t outer = geom::outer_loop_proj<C>(v, axis);
    bool loops_ok = true;
    for (uint32_t l = 0; l < v.loop_count() && loops_ok; l++) {
      const auto L1 = v.loop(l);
      for (uint32_t m = l; m < v.loop_count() && loops_ok; m++) {
        const auto L2 = v.loop(m);
        for (size_t i = 0; i < L1.size() && loops_ok; i++) {
          for (size_t j = (l == m ? i + 1 : 0); j < L2.size() && loops_ok; j++) {
            const bool adjacent = l == m && (j == i + 1 || (i == 0 && j == L1.size() - 1));
            const geom::SegIsect s = geom::segments_intersect_proj<C>(L1[i], L1[(i + 1) % L1.size()], L2[j],
                                                                      L2[(j + 1) % L2.size()], axis);
            if (adjacent ? (s == geom::SegIsect::Overlap || s == geom::SegIsect::Proper) : s != geom::SegIsect::None) {
              loops_ok = false;
            }
          }
        }
      }
    }
    if (!loops_ok) {
      rep.add(VC::LoopIntersection, ElemKind::Face, f);
      return false;
    }
    for (uint32_t l = 0; l < v.loop_count(); l++) {
      if (l == outer) {
        continue;
      }
      const uint32_t off[2] = {0, uint32_t(v.loop(outer).size())};
      const geom::PolyView<C> ov2{v.loop(outer), std::span<const uint32_t>(off, 2)};
      if (geom::point_in_poly_proj<C>(v.loop(l)[0], ov2, axis) != geom::PointClass::Inside) {
        rep.add(VC::HoleOutside, ElemKind::Face, f);
      }
    }
  }
  util::Vector<geom::Triangle> t;
  if (geom::cdt_proj<C>(v, axis, t) != geom::CdtStatus::Ok) {
    if (checks) {
      rep.add(VC::LoopIntersection, ElemKind::Face, f);
    }
    return false;
  }
  util::Vector<uint32_t, 32> corner_v;
  k.for_each_loop(FaceRef(f), [&](LoopRef l) { k.for_each_coedge(l, [&](CoedgeRef c) { corner_v.append(k.start_vert(c).i); }); });
  for (auto &x : t) {
    tris.append({corner_v[x[0]], corner_v[x[1]], corner_v[x[2]]});
  }
  return true;
}

/** True if vertex @p v is a corner of face @p f (a disk and radial walk:
 * independent of the face's size). */
template <geom::CoordType C> bool vertex_on_face(const Complex<C> &k, uint32_t v, uint32_t f)
{
  bool on = false;
  k.for_each_edge_at(VertRef(v), [&](EdgeRef e) {
    k.for_each_radial(e, [&](CoedgeRef c) { on = on || k.face_of(c).i == f; });
  });
  return on;
}

/** True if an edge (a, b) bounds face @p f. */
template <geom::CoordType C> bool edge_on_face(const Complex<C> &k, uint32_t a, uint32_t b, uint32_t f)
{
  bool on = false;
  k.for_each_edge_at(VertRef(a), [&](EdgeRef e) {
    const auto vs = k.edge_verts(e);
    if ((vs[0].i == a && vs[1].i == b) || (vs[0].i == b && vs[1].i == a)) {
      k.for_each_radial(e, [&](CoedgeRef c) { on = on || k.face_of(c).i == f; });
    }
  });
  return on;
}

/** The embedded-wire rule (plan P3b C5): a wire (p, q) embedded in face f
 * crosses no loop edge of f, overlaps none, passes through no corner of f,
 * and has its exact midpoint strictly inside f. */
template <geom::CoordType C> bool embedded_wire_ok(const Complex<C> &k, uint32_t p, uint32_t q, uint32_t f)
{
  using P = geom::Point<C>;
  auto pos = [&](uint32_t v) -> const P & { return k.pos(VertRef(v)); };
  FacePoints<C> fp;
  fp.gather(k, FaceRef(f));
  const auto v = fp.view();
  const int axis = face_axis<C>(k, FaceRef(f), v);
  for (uint32_t l = 0; l < v.loop_count(); l++) {
    const auto L = v.loop(l);
    for (size_t i = 0; i < L.size(); i++) {
      const P &u = L[i], &t = L[(i + 1) % L.size()];
      const geom::SegIsect s = geom::segments_intersect_proj<C>(pos(p), pos(q), u, t, axis);
      if (s == geom::SegIsect::Proper || s == geom::SegIsect::Overlap) {
        return false;
      }
      if (geom::lex_compare<C>(u, pos(p)) != 0 && geom::lex_compare<C>(u, pos(q)) != 0 &&
          geom::orient2d_proj<C>(pos(p), pos(q), u, axis) == 0 &&
          geom::on_segment_collinear_proj<C>(u, pos(p), pos(q), axis))
      {
        return false; /* a corner of f on the wire */
      }
    }
  }
  util::Vector<P, 32> twice;
  for (const P &x : v.pts) {
    twice.append(P{x[0] + x[0], x[1] + x[1], x[2] + x[2]});
  }
  const geom::PolyView<C> v2{std::span<const P>(twice.data(), twice.size()), v.off};
  const P mid2{pos(p)[0] + pos(q)[0], pos(p)[1] + pos(q)[1], pos(p)[2] + pos(q)[2]};
  return geom::point_in_poly_proj<C>(mid2, v2, axis) == geom::PointClass::Inside;
}

/** Stored regions agree with a fresh build_regions on a copy: O(N), for the
 * full layer and for barriers. */
template <geom::CoordType C> void check_regions_fresh(const Complex<C> &k, ValidationReport &rep)
{
  using ident::ElemKind;
  Complex<C> copy = k;
  RegionReport rr;
  if (!build_regions(copy, rr)) {
    rep.add(ValidateCode::RegionsStale, ElemKind::Region, 0);
    return;
  }
  bool same = copy.regions.capacity() == k.regions.capacity() && copy.shells.capacity() == k.shells.capacity();
  for (uint32_t f = 0; same && f < k.faces.capacity(); f++) {
    if (!k.faces.live(f)) {
      continue;
    }
    for (uint32_t s = 0; s < 2 && same; s++) {
      const RegionRef r0 = k.region(FaceRef(f), s), r1 = copy.region(FaceRef(f), s);
      same = r0.i < k.regions.capacity() && r1.i < copy.regions.capacity() &&
             k.regions.template get<".id">()[r0.i] == copy.regions.template get<".id">()[r1.i] &&
             k.shell(FaceRef(f), s) == copy.shell(FaceRef(f), s);
    }
  }
  if (!same) {
    rep.add(ValidateCode::RegionsStale, ElemKind::Region, 0);
  }
}

} // namespace ubertopo::topo::vdetail
