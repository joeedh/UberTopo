/* Validator: embedding and global layers (design 01 §8, plan P2 W8 and
 * amendment A1 W8). Exact predicates on stored vertices only. */

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "ubertopo/geom/cdt/cdt.h"
#include "ubertopo/geom/predicates.h"
#include "ubertopo/topo/radial.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::topo {

namespace util = litestl::util;
using ident::ElemKind;
using VC = ValidateCode;

namespace {

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

/** Region winding of point p: the shells of region r, oriented away from r. */
template <geom::CoordType C>
geom::RayResult region_winding(const Complex<C> &k,
                               RegionRef r,
                               const geom::Point<C> &p,
                               const util::Vector<util::Vector<geom::Point<C>>> &ftri)
{
  util::Vector<geom::Point<C>> tris;
  const auto &sn = k.faces.sides().template get<".fs.shell_next">();
  for (uint32_t s = k.regions.template get<".r.shell">()[r.i]; s != NONE; s = k.shells.template get<".s.next">()[s]) {
    for (uint32_t fs = k.shells.template get<".s.first">()[s]; fs != NONE; fs = sn[fs]) {
      const auto &t = ftri[fs / 2];
      for (uint32_t i = 0; i + 2 < t.size(); i += 3) {
        tris.append(t[i]);
        /* away from the side's region: front winding for BACK sides */
        tris.append((fs & 1u) ? t[i + 1] : t[i + 2]);
        tris.append((fs & 1u) ? t[i + 2] : t[i + 1]);
      }
    }
  }
  return geom::ray_winding<C>(p, std::span<const geom::Point<C>>(tris.data(), tris.size()));
}

/** Front-winding triangles of every live face: loop fans for planar faces
 * (signed; correct for winding and volumes), T(f) for projected ones. */
template <geom::CoordType C> util::Vector<util::Vector<geom::Point<C>>> face_fans(const Complex<C> &k)
{
  util::Vector<util::Vector<geom::Point<C>>> ftri;
  ftri.resize(k.faces.capacity());
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    if (!k.faces.live(f)) {
      continue;
    }
    FacePoints<C> fp;
    fp.gather(k, FaceRef(f));
    if (k.face_proj_axis(FaceRef(f)) >= 0) {
      util::Vector<geom::Triangle> t;
      if (geom::cdt_proj<C>(fp.view(), k.face_proj_axis(FaceRef(f)), t) == geom::CdtStatus::Ok) {
        for (auto &x : t) {
          ftri[f].append(fp.pts[x[0]]);
          ftri[f].append(fp.pts[x[1]]);
          ftri[f].append(fp.pts[x[2]]);
        }
      }
      continue;
    }
    const auto v = fp.view();
    for (uint32_t l = 0; l < v.loop_count(); l++) {
      const auto lp = v.loop(l);
      for (size_t i = 1; i + 1 < lp.size(); i++) {
        ftri[f].append(lp[0]);
        ftri[f].append(lp[i]);
        ftri[f].append(lp[i + 1]);
      }
    }
  }
  return ftri;
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

} // namespace

template <geom::CoordType C>
void validate_embedding(const Complex<C> &k, ValidationReport &rep, const ValidateFilter *flt)
{
  const uint32_t F = k.faces.capacity();
  util::Vector<FaceFrame> frames;
  util::Vector<uint8_t> frame_done;
  frames.resize(F);
  frame_done.resize(F);
  for (uint32_t f = 0; f < F; f++) {
    frame_done[f] = 0;
  }
  for (uint32_t f = 0; f < F; f++) {
    if (!k.faces.live(f) || (flt && !flt->face(f))) {
      continue;
    }
    const FaceRef fr(f);
    FacePoints<C> fp;
    fp.gather(k, fr);
    const auto v = fp.view();
    const bool planar = face_is_planar<C>(v);
    const int pa = k.face_proj_axis(fr);
    if (pa >= 0) {
      if (pa > 2 || planar) {
        rep.add(VC::SpuriousProjection, ElemKind::Face, f);
      }
      if (!k.allows_projectable()) {
        rep.add(VC::ProjectionNotAllowed, ElemKind::Face, f);
      }
      util::Vector<geom::Triangle> t;
      if (pa <= 2 && geom::cdt_proj<C>(v, pa, t) != geom::CdtStatus::Ok) {
        rep.add(VC::NotProjectable, ElemKind::Face, f);
      }
    }
    else {
      if (!planar) {
        rep.add(VC::NonPlanarFace, ElemKind::Face, f);
        continue;
      }
      frames[f] = face_frame<C>(v);
      frame_done[f] = 1;
      const int axis = frames[f].axis;
      if (axis >= 0) {
        const uint32_t outer = geom::outer_loop_proj<C>(v, axis);
        const int so = geom::loop_orientation_proj<C>(v.loop(outer), axis);
        for (uint32_t l = 0; l < v.loop_count(); l++) {
          if (l != outer && geom::loop_orientation_proj<C>(v.loop(l), axis) == so) {
            rep.add(VC::HoleWinding, ElemKind::Face, f);
          }
        }
      }
    }
  }
  if (!rep.ok()) {
    return;
  }
  /* planar frames and T(f) third vertices, computed on first use (a filter
   * needs them only for the faces around the listed edges and containers) */
  auto frame_of = [&](uint32_t f) -> const FaceFrame & {
    if (!frame_done[f]) {
      frames[f] = face_frame<C>(k, FaceRef(f));
      frame_done[f] = 1;
    }
    return frames[f];
  };
  /* radial order: re-derive from geometry */
  std::unordered_map<uint32_t, uint32_t> third; /* coedge -> T(f) third vertex (projected faces) */
  util::Vector<uint8_t> third_done;
  third_done.resize(F);
  for (uint32_t f = 0; f < F; f++) {
    third_done[f] = 0;
  }
  auto half_plane = [&](CoedgeRef c, geom::HalfPlane<C> &h) {
    const FaceRef f = k.face_of(c);
    if (k.face_proj_axis(f) >= 0) {
      if (!third_done[f.i]) {
        third_done[f.i] = 1;
        util::Vector<std::pair<CoedgeRef, VertRef>> th;
        projected_third_vertices<C>(k, f, th);
        for (auto &x : th) {
          third[x.first.i] = x.second.i;
        }
      }
      auto it = third.find(c.i);
      if (it == third.end()) {
        return false;
      }
      h.q = k.pos(VertRef(it->second));
      h.s = 1;
      return true;
    }
    return coedge_half_plane<C>(k, c, frame_of(f.i), h);
  };
  util::Vector<uint32_t, 8> in, order;
  for (uint32_t e = 0; e < k.edges.capacity(); e++) {
    if (!k.edges.live(e) || !k.edge_coedge(EdgeRef(e)) || (flt && !flt->edge(e))) {
      continue;
    }
    in.clear();
    k.for_each_radial(EdgeRef(e), [&](CoedgeRef c) { in.append(c.i); });
    bool tie = false;
    if (!radial_order<C>(k, EdgeRef(e), std::span<const uint32_t>(in.data(), in.size()), half_plane, order, &tie) ||
        order.size() != in.size())
    {
      rep.add(VC::RadialOrder, ElemKind::Edge, e);
      continue;
    }
    /* same cyclic sequence, starting at .e.c */
    bool same = order[0] == k.edge_coedge(EdgeRef(e)).i;
    for (uint32_t i = 0; same && i < in.size(); i++) {
      same = in[i] == order[i];
    }
    if (!same) {
      rep.add(VC::RadialOrder, ElemKind::Edge, e);
    }
  }
  /* containers: geometry */
  const bool regions_ok = !k.regions_dirty;
  util::Vector<util::Vector<geom::Point<C>>> ftri;
  bool ftri_done = false;
  auto point_in_container = [&](const geom::Point<C> &p, ident::ContainerRef cr) {
    if (cr.kind() == ident::ContainerKind::Face) {
      const FaceRef f = cr.as_face();
      FacePoints<C> fp;
      fp.gather(k, f);
      const auto v = fp.view();
      const int pa = k.face_proj_axis(f);
      if (pa >= 0) {
        util::Vector<geom::Triangle> t;
        if (geom::cdt_proj<C>(v, pa, t) != geom::CdtStatus::Ok) {
          return false;
        }
        for (auto &x : t) {
          const auto &a = fp.pts[x[0]], &b = fp.pts[x[1]], &c = fp.pts[x[2]];
          if (geom::vol3<C>(a, b, c, p) == 0 && in_closed_triangle<C>(p, a, b, c)) {
            return true;
          }
        }
        return false;
      }
      const auto &q = v.pts;
      /* coplanar with the face, and inside or on it in projection */
      for (size_t i = 2; i < q.size(); i++) {
        if (geom::vol3<C>(q[0], q[1], q[i], p) != 0) {
          return false;
        }
      }
      return geom::point_in_poly_proj<C>(p, v, frame_of(f.i).axis) != geom::PointClass::Outside;
    }
    if (cr.kind() == ident::ContainerKind::Region && regions_ok) {
      if (!ftri_done) {
        ftri = face_fans<C>(k);
        ftri_done = true;
      }
      const geom::RayResult rr = region_winding<C>(k, cr.as_region(), p, ftri);
      return rr.status == geom::RayStatus::Ok && rr.winding == (cr.as_region().i == 0 ? 0 : 1);
    }
    return true;
  };
  for (uint32_t v = 0; v < k.verts.capacity(); v++) {
    const ident::ContainerRef cr = k.vert_container(VertRef(v));
    if (flt && !flt->vert(v)) {
      continue;
    }
    if (k.verts.live(v) && !cr.is_none() && !point_in_container(k.pos(VertRef(v)), cr)) {
      rep.add(VC::ContainerGeometry, ElemKind::Vertex, v);
    }
  }
  for (uint32_t e = 0; e < k.edges.capacity(); e++) {
    const ident::ContainerRef cr = k.edges.template get<".e.container">()[e];
    if (!k.edges.live(e) || cr.is_none() || (flt && !flt->edge(e))) {
      continue;
    }
    const auto vs = k.edge_verts(EdgeRef(e));
    if (!point_in_container(k.pos(vs[0]), cr) || !point_in_container(k.pos(vs[1]), cr)) {
      rep.add(VC::ContainerGeometry, ElemKind::Edge, e);
    }
  }
}

namespace {

/* Allowed contact between faces f and g: shared vertices, shared edges. */
struct Shared {
  util::Vector<uint32_t, 16> verts;          /* sorted */
  util::Vector<std::pair<uint32_t, uint32_t>, 16> edges; /* sorted vertex pairs (min, max) */
  bool vert(uint32_t v) const
  {
    return std::binary_search(verts.begin(), verts.end(), v);
  }
  bool edge(uint32_t a, uint32_t b) const
  {
    const std::pair<uint32_t, uint32_t> e = {std::min(a, b), std::max(a, b)};
    return std::binary_search(edges.begin(), edges.end(), e);
  }
};

/* T(f) of every face for the intersection test (planar: CDT in its dominant
 * projection; projected: along the stored axis), as vertex-index triples. */
template <geom::CoordType C> struct FaceMesh {
  util::Vector<std::array<uint32_t, 3>> tris; /* vertex indices */
  util::Vector<uint32_t, 16> verts;           /* sorted */
  util::Vector<std::pair<uint32_t, uint32_t>, 16> edges; /* boundary edges, sorted */
  double lo[3], hi[3];
};

} // namespace

template <geom::CoordType C>
void validate_global(const Complex<C> &k, ValidationReport &rep, const ValidateFilter *flt)
{
  using P = geom::Point<C>;
  const uint32_t F = k.faces.capacity();
  util::Vector<FaceMesh<C>> mesh;
  util::Vector<uint8_t> built; /* 0: not yet, 1: built, 2: failed */
  mesh.resize(F);
  built.resize(F);
  for (uint32_t f = 0; f < F; f++) {
    built[f] = 0;
  }
  /* loop checks (with checks set) and the triangulation of face f */
  auto build = [&](uint32_t f, bool checks) -> bool {
      FacePoints<C> fp;
      fp.gather(k, FaceRef(f));
      const auto v = fp.view();
      const int axis = face_axis<C>(k, FaceRef(f), v);
      /* loops: simple, disjoint, holes inside the outer loop */
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
        util::Vector<uint32_t, 2> off;
        off.append(0);
        off.append(uint32_t(v.loop(outer).size()));
        const geom::PolyView<C> ov2{v.loop(outer), std::span<const uint32_t>(off.data(), off.size())};
        if (geom::point_in_poly_proj<C>(v.loop(l)[0], ov2, axis) != geom::PointClass::Inside) {
          rep.add(VC::HoleOutside, ElemKind::Face, f);
        }
      }
      }
      /* triangulation for the face-face test */
      util::Vector<geom::Triangle> t;
      if (geom::cdt_proj<C>(v, axis, t) != geom::CdtStatus::Ok) {
        if (checks) {
          rep.add(VC::LoopIntersection, ElemKind::Face, f);
        }
        return false;
      }
      util::Vector<uint32_t, 32> corner_v;
      k.for_each_loop(FaceRef(f), [&](LoopRef l) {
        k.for_each_coedge(l, [&](CoedgeRef c) { corner_v.append(k.start_vert(c).i); });
      });
      FaceMesh<C> &fm = mesh[f];
      for (auto &x : t) {
        fm.tris.append({corner_v[x[0]], corner_v[x[1]], corner_v[x[2]]});
      }
      for (uint32_t cv : corner_v) {
        fm.verts.append(cv);
      }
      std::sort(fm.verts.begin(), fm.verts.end());
      for (uint32_t l = 0; l + 1 < fp.off.size(); l++) {
        const uint32_t b = fp.off[l], m = fp.off[l + 1] - b;
        for (uint32_t j = 0; j < m; j++) {
          const uint32_t a = corner_v[b + j], c = corner_v[b + (j + 1) % m];
          fm.edges.append({std::min(a, c), std::max(a, c)});
        }
      }
      std::sort(fm.edges.begin(), fm.edges.end());
      for (int i = 0; i < 3; i++) {
        fm.lo[i] = INFINITY;
        fm.hi[i] = -INFINITY;
      }
      for (uint32_t cv : corner_v) {
        const auto d = geom::to_double3<C>(k.pos(VertRef(cv)));
        for (int i = 0; i < 3; i++) {
          fm.lo[i] = std::min(fm.lo[i], d[i]);
          fm.hi[i] = std::max(fm.hi[i], d[i]);
        }
      }
    return true;
  };
  auto ensure = [&](uint32_t f, bool checks) {
    if (!built[f]) {
      built[f] = build(f, checks) ? 1 : 2;
    }
    return built[f] == 1;
  };
  for (uint32_t f = 0; f < F; f++) {
    if (k.faces.live(f) && (!flt || flt->face(f))) {
      ensure(f, true);
    }
  }
  if (!rep.ok()) {
    return;
  }
  /* bounding boxes of the other faces: a filter triangulates them only when
   * a listed face comes near */
  for (uint32_t f = 0; f < F; f++) {
    if (!k.faces.live(f) || built[f] == 1) {
      continue;
    }
    FaceMesh<C> &fm = mesh[f];
    for (int i = 0; i < 3; i++) {
      fm.lo[i] = INFINITY;
      fm.hi[i] = -INFINITY;
    }
    k.for_each_loop(FaceRef(f), [&](LoopRef l) {
      k.for_each_coedge(l, [&](CoedgeRef c) {
        const auto d = geom::to_double3<C>(k.pos(k.start_vert(c)));
        for (int i = 0; i < 3; i++) {
          fm.lo[i] = std::min(fm.lo[i], d[i]);
          fm.hi[i] = std::max(fm.hi[i], d[i]);
        }
      });
    });
  }

  /* candidate pairs: uniform grid over face bounding boxes */
  double glo[3] = {INFINITY, INFINITY, INFINITY}, ghi[3] = {-INFINITY, -INFINITY, -INFINITY}, ext_sum = 0;
  uint32_t nf = 0;
  for (uint32_t f = 0; f < F; f++) {
    if (!k.faces.live(f)) {
      continue;
    }
    nf++;
    for (int i = 0; i < 3; i++) {
      glo[i] = std::min(glo[i], mesh[f].lo[i]);
      ghi[i] = std::max(ghi[i], mesh[f].hi[i]);
      ext_sum += mesh[f].hi[i] - mesh[f].lo[i];
    }
  }
  if (nf == 0) {
    return;
  }
  const double cell = std::max(ext_sum / (3.0 * nf), 1e-300);
  auto cell_of = [&](double x, int i) { return int64_t(std::floor((x - glo[i]) / cell)); };
  std::map<std::array<int64_t, 3>, util::Vector<uint32_t>> grid;
  for (uint32_t f = 0; f < F; f++) {
    if (!k.faces.live(f)) {
      continue;
    }
    const FaceMesh<C> &m = mesh[f];
    for (int64_t x = cell_of(m.lo[0], 0); x <= cell_of(m.hi[0], 0); x++) {
      for (int64_t y = cell_of(m.lo[1], 1); y <= cell_of(m.hi[1], 1); y++) {
        for (int64_t z = cell_of(m.lo[2], 2); z <= cell_of(m.hi[2], 2); z++) {
          grid[{x, y, z}].append(f);
        }
      }
    }
  }
  util::Vector<std::pair<uint32_t, uint32_t>> pairs;
  for (auto &[c, fs] : grid) {
    for (uint32_t i = 0; i < fs.size(); i++) {
      for (uint32_t j = i + 1; j < fs.size(); j++) {
        const FaceMesh<C> &a = mesh[fs[i]], &b = mesh[fs[j]];
        bool overlap = true;
        for (int d = 0; d < 3; d++) {
          overlap = overlap && a.lo[d] <= b.hi[d] && b.lo[d] <= a.hi[d];
        }
        if (overlap && (!flt || flt->face(fs[i]) || flt->face(fs[j]))) {
          pairs.append({std::min(fs[i], fs[j]), std::max(fs[i], fs[j])});
        }
      }
    }
  }
  std::sort(pairs.begin(), pairs.end());
  auto pos = [&](uint32_t v) -> const P & { return k.pos(VertRef(v)); };

  /* does segment (p, q) of face f meet triangle t of face g other than at
   * shared vertices and along shared edges? */
  auto bad_contact = [&](uint32_t p, uint32_t q, bool pq_boundary, const std::array<uint32_t, 3> &t, const Shared &sh) {
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
        if (in_closed_triangle<C>(pos(e), A, B, Cc) && !sh.vert(e)) {
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
          if (!(same && pq_boundary && sh.edge(p, q))) {
            return true;
          }
        }
        if (s == geom::SegIsect::Touch) {
          /* touching at a t vertex inside (p, q), or at p / q */
          for (const uint32_t tv : e) {
            if (tv != p && tv != q && geom::orient2d_proj<C>(P0, Q0, pos(tv), ax) == 0 &&
                geom::on_segment_collinear_proj<C>(pos(tv), P0, Q0, ax) && !sh.vert(tv))
            {
              return true;
            }
          }
        }
      }
      /* t's vertices strictly inside segment (p, q) (collinear) and not shared */
      return false;
    }
    if (op == 0 || oq == 0) {
      const uint32_t on = op == 0 ? p : q;
      return in_closed_triangle<C>(pos(on), A, B, Cc) && !sh.vert(on);
    }
    /* strict crossing of t's plane at an interior point of (p, q) */
    const int e1 = geom::vol3<C>(P0, Q0, A, B), e2 = geom::vol3<C>(P0, Q0, B, Cc), e3 = geom::vol3<C>(P0, Q0, Cc, A);
    const bool pos_ = e1 > 0 || e2 > 0 || e3 > 0, neg = e1 < 0 || e2 < 0 || e3 < 0;
    return !(pos_ && neg); /* the crossing point lies in the closed triangle */
  };

  for (auto &[f, g] : pairs) {
    if (!ensure(f, false) || !ensure(g, false)) {
      continue; /* an unlisted face that doesn't triangulate isn't this edit's problem */
    }
    const FaceMesh<C> &a = mesh[f], &b = mesh[g];
    Shared sh;
    for (uint32_t v : a.verts) {
      if (std::binary_search(b.verts.begin(), b.verts.end(), v)) {
        sh.verts.append(v);
      }
    }
    for (const auto &e : a.edges) {
      if (std::binary_search(b.edges.begin(), b.edges.end(), e)) {
        sh.edges.append(e);
      }
    }
    bool bad = false;
    for (int dir = 0; dir < 2 && !bad; dir++) {
      const FaceMesh<C> &x = dir ? b : a, &y = dir ? a : b;
      for (uint32_t i = 0; i < x.tris.size() && !bad; i++) {
        for (int s = 0; s < 3 && !bad; s++) {
          const uint32_t p = x.tris[i][s], q = x.tris[i][(s + 1) % 3];
          const std::pair<uint32_t, uint32_t> pe = {std::min(p, q), std::max(p, q)};
          const bool boundary = std::binary_search(x.edges.begin(), x.edges.end(), pe);
          for (uint32_t j = 0; j < y.tris.size() && !bad; j++) {
            bad = bad_contact(p, q, boundary, y.tris[j], sh);
          }
        }
      }
    }
    if (bad) {
      rep.add(VC::FaceIntersection, ElemKind::Face, f);
    }
  }

  /* wires against faces (plan P3b W8, C5) */
  {
    const auto &ec = k.edges.template get<".e.c">();
    const auto &econt = k.edges.template get<".e.container">();
    const auto &vcont = k.verts.template get<".v.container">();
    util::Vector<uint32_t, 16> cand;
    for (uint32_t w = 0; w < k.edges.capacity(); w++) {
      if (!k.edges.live(w) || ec[w] != NONE) {
        continue;
      }
      const auto wv = k.edge_verts(EdgeRef(w));
      const uint32_t p = wv[0].i, q = wv[1].i;
      double lo[3], hi[3];
      {
        const auto a = geom::to_double3<C>(pos(p)), b = geom::to_double3<C>(pos(q));
        for (int i = 0; i < 3; i++) {
          lo[i] = std::min(a[i], b[i]);
          hi[i] = std::max(a[i], b[i]);
        }
      }
      /* candidate faces: the grid cells the wire's box covers */
      cand.clear();
      for (int64_t x = cell_of(lo[0], 0); x <= cell_of(hi[0], 0); x++) {
        for (int64_t y = cell_of(lo[1], 1); y <= cell_of(hi[1], 1); y++) {
          for (int64_t z = cell_of(lo[2], 2); z <= cell_of(hi[2], 2); z++) {
            auto it = grid.find({x, y, z});
            if (it == grid.end()) {
              continue;
            }
            for (uint32_t f : it->second) {
              cand.append(f);
            }
          }
        }
      }
      std::sort(cand.begin(), cand.end());
      cand.resize(int(std::unique(cand.begin(), cand.end()) - cand.begin()));
      const ident::ContainerRef wc = econt[w];
      for (uint32_t f : cand) {
        if (flt && !flt->edge(w) && !flt->face(f)) {
          continue;
        }
        const FaceMesh<C> &m0 = mesh[f];
        bool overlap = true;
        for (int d = 0; d < 3; d++) {
          overlap = overlap && m0.lo[d] <= hi[d] && lo[d] <= m0.hi[d];
        }
        if (!overlap || !ensure(f, false)) {
          continue;
        }
        const FaceMesh<C> &m = mesh[f];
        bool bad = false;
        if (wc.kind() == ident::ContainerKind::Face && wc.index() == f) {
          /* embedded in f: inside f, touching its boundary at most at the
           * wire's endpoints (no crossing, no overlap, no corner on the
           * wire), with the exact midpoint strictly inside */
          FacePoints<C> fp;
          fp.gather(k, FaceRef(f));
          const auto v = fp.view();
          const int axis = face_axis<C>(k, FaceRef(f), v);
          for (uint32_t l = 0; l < v.loop_count() && !bad; l++) {
            const auto L = v.loop(l);
            for (size_t i = 0; i < L.size() && !bad; i++) {
              const P &u = L[i], &t = L[(i + 1) % L.size()];
              const geom::SegIsect s = geom::segments_intersect_proj<C>(pos(p), pos(q), u, t, axis);
              if (s == geom::SegIsect::Proper || s == geom::SegIsect::Overlap) {
                bad = true;
              }
              if (geom::lex_compare<C>(u, pos(p)) != 0 && geom::lex_compare<C>(u, pos(q)) != 0 &&
                  geom::orient2d_proj<C>(pos(p), pos(q), u, axis) == 0 &&
                  geom::on_segment_collinear_proj<C>(u, pos(p), pos(q), axis))
              {
                bad = true; /* a corner of f on the wire */
              }
            }
          }
          if (!bad) {
            util::Vector<P, 32> twice;
            for (const P &x : v.pts) {
              twice.append(P{x[0] + x[0], x[1] + x[1], x[2] + x[2]});
            }
            const geom::PolyView<C> v2{std::span<const P>(twice.data(), twice.size()), v.off};
            const P mid2{pos(p)[0] + pos(q)[0], pos(p)[1] + pos(q)[1], pos(p)[2] + pos(q)[2]};
            bad = geom::point_in_poly_proj<C>(mid2, v2, axis) != geom::PointClass::Inside;
          }
        }
        else {
          /* elsewhere: only shared endpoints, or endpoints embedded in f */
          Shared sh;
          for (const uint32_t e : {std::min(p, q), std::max(p, q)}) {
            const ident::ContainerRef c = vcont[e];
            if (std::binary_search(m.verts.begin(), m.verts.end(), e) ||
                (c.kind() == ident::ContainerKind::Face && c.index() == f))
            {
              sh.verts.append(e);
            }
          }
          for (uint32_t j = 0; j < m.tris.size() && !bad; j++) {
            bad = bad_contact(p, q, false, m.tris[j], sh);
          }
        }
        if (bad) {
          rep.add(VC::WireCrossesFace, ElemKind::Edge, w);
          break;
        }
      }
    }
  }

  /* stored regions agree with a fresh build */
  if (!k.regions_dirty && (!flt || flt->regions)) {
    Complex<C> copy = k;
    RegionReport rr;
    if (!build_regions(copy, rr)) {
      rep.add(VC::RegionsStale, ElemKind::Region, 0);
      return;
    }
    bool same = copy.regions.capacity() == k.regions.capacity() && copy.shells.capacity() == k.shells.capacity();
    for (uint32_t f = 0; same && f < F; f++) {
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
      rep.add(VC::RegionsStale, ElemKind::Region, 0);
    }
  }
}

template void validate_embedding<double>(const Complex<double> &, ValidationReport &, const ValidateFilter *);
template void validate_embedding<float>(const Complex<float> &, ValidationReport &, const ValidateFilter *);
template void validate_embedding<geom::Lattice>(const Complex<geom::Lattice> &, ValidationReport &, const ValidateFilter *);
template void validate_global<double>(const Complex<double> &, ValidationReport &, const ValidateFilter *);
template void validate_global<float>(const Complex<float> &, ValidationReport &, const ValidateFilter *);
template void validate_global<geom::Lattice>(const Complex<geom::Lattice> &, ValidationReport &, const ValidateFilter *);

} // namespace ubertopo::topo
