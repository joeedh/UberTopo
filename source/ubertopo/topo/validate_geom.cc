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
#include "ubertopo/topo/face_grid.h"
#include "ubertopo/topo/validate.h"
#include "ubertopo/topo/validate_detail.h"

namespace ubertopo::topo {

namespace util = litestl::util;
using ident::ElemKind;
using VC = ValidateCode;

using vdetail::bad_contact;
using vdetail::face_axis;
using vdetail::in_closed_triangle;

/* fn(i) for each index in @p list (a filter list), or for every row below
 * @p n without a filter */
template <typename F> void for_rows(const ValidateFilter *flt, const util::Vector<uint32_t> *list, uint32_t n, F &&fn)
{
  if (flt) {
    for (uint32_t i : *list) {
      if (i < n) {
        fn(i);
      }
    }
    return;
  }
  for (uint32_t i = 0; i < n; i++) {
    fn(i);
  }
}

template <geom::CoordType C>
void validate_embedding(const Complex<C> &k, ValidationReport &rep, const ValidateFilter *flt)
{
  /* per-call stores are sparse: a filter touches only the faces it lists and
   * their neighbours (plan P3b C13) */
  std::unordered_map<uint32_t, FaceFrame> frames;
  validate_curves(k, rep, flt);
  validate_surfaces(k, rep, flt);
  /* one skeleton table for the pass (plan P4b §4.3): planarity reads the
   * stored vertices, everything else the refined skeleton */
  Skeleton<C> sk(k);
  for_rows(flt, flt ? &flt->faces : nullptr, k.faces.capacity(), [&](uint32_t f) {
    if (!k.faces.live(f)) {
      return;
    }
    const FaceRef fr(f);
    FacePoints<C> fv, fp;
    fv.gather(k, fr);
    fp.gather(k, fr, sk);
    const auto v = fp.view();
    bool degenerate = false;
    const bool planar = face_planar_rule<C>(fv.view(), v, &degenerate) && !degenerate;
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
      return;
    }
    if (!planar) {
      rep.add(VC::NonPlanarFace, ElemKind::Face, f);
      return;
    }
    const FaceFrame &fm = frames[f] = face_frame<C>(v);
    const int axis = fm.axis;
    if (axis >= 0) {
      const uint32_t outer = geom::outer_loop_proj<C>(v, axis);
      const int so = geom::loop_orientation_proj<C>(v.loop(outer), axis);
      for (uint32_t l = 0; l < v.loop_count(); l++) {
        if (l != outer && geom::loop_orientation_proj<C>(v.loop(l), axis) == so) {
          rep.add(VC::HoleWinding, ElemKind::Face, f);
        }
      }
    }
  });
  /* curved edges that don't flatten (plan P4b §4.3): read as straight by
   * every test above, so reported here */
  {
    const auto &cv = k.edges.template get<".e.curve">();
    for_rows(flt, flt ? &flt->edges : nullptr, k.edges.capacity(), [&](uint32_t e) {
      if (k.edges.live(e) && !cv[e].none() && sk.edge_failed(EdgeRef(e))) {
        rep.add(VC::SkeletonDegenerate, ElemKind::Edge, e);
      }
    });
  }
  if (!rep.ok()) {
    return;
  }
  /* planar frames and T(f) third vertices, computed on first use (a filter
   * needs them only for the faces around the listed edges and containers) */
  auto frame_of = [&](uint32_t f) -> const FaceFrame & {
    auto it = frames.find(f);
    if (it == frames.end()) {
      FacePoints<C> fq;
      fq.gather(k, FaceRef(f), sk);
      it = frames.emplace(f, face_frame<C>(fq.view())).first;
    }
    return it->second;
  };
  /* radial order: re-derive from geometry */
  std::unordered_map<uint32_t, geom::Point<C>> third; /* coedge -> T(f) third point (projected faces) */
  std::unordered_map<uint32_t, bool> third_done;
  auto half_plane = [&](CoedgeRef c, geom::HalfPlane<C> &h) {
    const FaceRef f = k.face_of(c);
    if (k.face_proj_axis(f) >= 0) {
      if (!third_done.count(f.i)) {
        third_done[f.i] = true;
        util::Vector<std::pair<CoedgeRef, geom::Point<C>>> th;
        projected_third_vertices<C>(k, f, th, &sk);
        for (auto &x : th) {
          third[x.first.i] = x.second;
        }
      }
      auto it = third.find(c.i);
      if (it == third.end()) {
        return false;
      }
      h.q = it->second;
      h.s = 1;
      return true;
    }
    return coedge_half_plane<C>(k, c, frame_of(f.i), h, &sk);
  };
  util::Vector<uint32_t, 8> in, order;
  for_rows(flt, flt ? &flt->edges : nullptr, k.edges.capacity(), [&](uint32_t e) {
    if (!k.edges.live(e) || !k.edge_coedge(EdgeRef(e))) {
      return;
    }
    in.clear();
    k.for_each_radial(EdgeRef(e), [&](CoedgeRef c) { in.append(c.i); });
    bool tie = false;
    if (!radial_order<C>(k, EdgeRef(e), std::span<const uint32_t>(in.data(), in.size()), half_plane, order, &tie, &sk) ||
        order.size() != in.size())
    {
      rep.add(VC::RadialOrder, ElemKind::Edge, e);
      return;
    }
    /* same cyclic sequence, starting at .e.c */
    bool same = order[0] == k.edge_coedge(EdgeRef(e)).i;
    for (uint32_t i = 0; same && i < in.size(); i++) {
      same = in[i] == order[i];
    }
    if (!same) {
      rep.add(VC::RadialOrder, ElemKind::Edge, e);
    }
  });
  /* containers: geometry */
  const bool regions_ok = !k.regions_dirty;
  std::unordered_map<uint32_t, util::Vector<geom::Point<C>>> fans; /* face -> front triangles, on first use */
  auto fan_of = [&](uint32_t f) -> const util::Vector<geom::Point<C>> & {
    auto it = fans.find(f);
    if (it == fans.end()) {
      it = fans.emplace(f, util::Vector<geom::Point<C>>()).first;
      vdetail::face_fan<C>(k, FaceRef(f), it->second, sk);
    }
    return it->second;
  };
  auto point_in_container = [&](const geom::Point<C> &p, ident::ContainerRef cr) {
    if (cr.kind() == ident::ContainerKind::Face) {
      const FaceRef f = cr.as_face();
      FacePoints<C> fv, fp;
      fv.gather(k, f);
      fp.gather(k, f, sk);
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
      const auto &q = fv.pts; /* stored vertices: the skeleton is only near the plane */
      /* coplanar with the face, and inside or on it in projection */
      for (size_t i = 2; i < q.size(); i++) {
        if (geom::vol3<C>(q[0], q[1], q[i], p) != 0) {
          return false;
        }
      }
      return geom::point_in_poly_proj<C>(p, v, frame_of(f.i).axis) != geom::PointClass::Outside;
    }
    if (cr.kind() == ident::ContainerKind::Region && regions_ok) {
      const RegionRef r = cr.as_region();
      /* region 0 by one ray through the grid (C13); others by their own
       * boundary, O(region boundary) */
      const geom::RayResult rr = r.i == 0 && flt && flt->grid ? region0_winding_grid<C>(k, p, *flt->grid)
                                                              : vdetail::region_winding<C>(k, r, p, fan_of);
      return rr.status == geom::RayStatus::Ok && rr.winding == (r.i == 0 ? 0 : 1);
    }
    return true;
  };
  for_rows(flt, flt ? &flt->verts : nullptr, k.verts.capacity(), [&](uint32_t v) {
    const ident::ContainerRef cr = k.vert_container(VertRef(v));
    if (k.verts.live(v) && !cr.is_none() && !point_in_container(k.pos(VertRef(v)), cr)) {
      rep.add(VC::ContainerGeometry, ElemKind::Vertex, v);
    }
  });
  for_rows(flt, flt ? &flt->edges : nullptr, k.edges.capacity(), [&](uint32_t e) {
    const ident::ContainerRef cr = k.edges.template get<".e.container">()[e];
    if (!k.edges.live(e) || cr.is_none()) {
      return;
    }
    const auto vs = k.edge_verts(EdgeRef(e));
    if (!point_in_container(k.pos(vs[0]), cr) || !point_in_container(k.pos(vs[1]), cr)) {
      rep.add(VC::ContainerGeometry, ElemKind::Edge, e);
    }
  });
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
  if (flt) {
    /* filtered: through the face-box grid (validate_grid.cc); a caller
     * without one (not the editor) pays for a grid of its own */
    if (flt->grid) {
      validate_global_grid<C>(k, rep, *flt, *flt->grid);
    }
    else {
      FaceGrid g;
      g.build(k);
      validate_global_grid<C>(k, rep, *flt, g);
    }
    return;
  }
  using P = geom::Point<C>;
  const uint32_t F = k.faces.capacity();
  Skeleton<C> sk(k); /* one table: faces sharing a curved edge share its ids */
  util::Vector<FaceMesh<C>> mesh;
  util::Vector<uint8_t> built; /* 0: not yet, 1: built, 2: failed */
  mesh.resize(F);
  built.resize(F);
  for (uint32_t f = 0; f < F; f++) {
    built[f] = 0;
  }
  /* loop checks (with checks set) and the triangulation of face f */
  auto build = [&](uint32_t f, bool checks) -> bool {
      FaceMesh<C> &fm = mesh[f];
      if (!vdetail::face_checks_and_tris<C>(k, f, checks, rep, fm.tris, sk)) {
        return false;
      }
      /* skeleton ids: a curved edge's segments are shared like edges */
      FacePoints<C> fp;
      fp.gather(k, FaceRef(f), sk);
      const auto &corner_v = fp.ids;
      const auto &off = fp.off;
      for (uint32_t cv : corner_v) {
        fm.verts.append(cv);
      }
      std::sort(fm.verts.begin(), fm.verts.end());
      for (uint32_t l = 0; l + 1 < off.size(); l++) {
        const uint32_t b = off[l], m = off[l + 1] - b;
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
        const auto d = geom::to_double3<C>(sk.pos(cv));
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
    FacePoints<C> fp;
    fp.gather(k, FaceRef(f), sk);
    for (const P &x : fp.pts) {
      const auto d = geom::to_double3<C>(x);
      for (int i = 0; i < 3; i++) {
        fm.lo[i] = std::min(fm.lo[i], d[i]);
        fm.hi[i] = std::max(fm.hi[i], d[i]);
      }
    }
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
  auto pos = [&](uint32_t v) -> const P & { return sk.pos(v); };

  /* does segment (p, q) of face f meet triangle t of face g other than at
   * shared vertices and along shared edges? */
  auto bad_contact = [&](uint32_t p, uint32_t q, bool pq_boundary, const std::array<uint32_t, 3> &t, const Shared &sh) {
    return vdetail::bad_contact<C>(
        sk, p, q, pq_boundary, t, [&](uint32_t v) { return sh.vert(v); }, [&](uint32_t a, uint32_t b) { return sh.edge(a, b); });
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
      /* the wire's skeleton chain p, interior points, q (plan P4b §12 E) */
      util::Vector<uint32_t, 16> chain;
      chain.append(p);
      for (uint32_t id : sk.interior(EdgeRef(w))) {
        chain.append(id);
      }
      chain.append(q);
      double lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
      for (uint32_t id : chain) {
        const auto a = geom::to_double3<C>(pos(id));
        for (int i = 0; i < 3; i++) {
          lo[i] = std::min(lo[i], a[i]);
          hi[i] = std::max(hi[i], a[i]);
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
          for (uint32_t i = 0; i + 1 < chain.size() && !bad; i++) {
            bad = !vdetail::embedded_wire_ok<C>(k, chain[i], chain[i + 1], f, sk);
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
          std::sort(sh.verts.begin(), sh.verts.end());
          for (uint32_t i = 0; i + 1 < chain.size() && !bad; i++) {
            for (uint32_t j = 0; j < m.tris.size() && !bad; j++) {
              bad = bad_contact(chain[i], chain[i + 1], false, m.tris[j], sh);
            }
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
  if (!k.regions_dirty) {
    vdetail::check_regions_fresh<C>(k, rep);
  }
}

template void validate_embedding<double>(const Complex<double> &, ValidationReport &, const ValidateFilter *);
template void validate_embedding<float>(const Complex<float> &, ValidationReport &, const ValidateFilter *);
template void validate_embedding<geom::Lattice>(const Complex<geom::Lattice> &, ValidationReport &, const ValidateFilter *);
template void validate_global<double>(const Complex<double> &, ValidationReport &, const ValidateFilter *);
template void validate_global<float>(const Complex<float> &, ValidationReport &, const ValidateFilter *);
template void validate_global<geom::Lattice>(const Complex<geom::Lattice> &, ValidationReport &, const ValidateFilter *);

} // namespace ubertopo::topo
