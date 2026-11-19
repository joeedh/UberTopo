/* Validator: the filtered global layer over the persistent face-box grid,
 * and region-0 containment by one ray through the grid (plan P3b W5, §3.5,
 * C7, C13). The unfiltered layer in validate_geom.cc stays the oracle.
 *
 * Every per-call store is sparse (keyed by the faces met), and shared
 * vertices and edges are found by disk and radial walks, so the cost of a
 * call depends on the listed elements and their neighbours, not on the
 * size of the complex or of a large neighbouring face. */

#include <algorithm>
#include <set>
#include <unordered_map>

#include "ubertopo/topo/face_grid.h"
#include "ubertopo/topo/validate.h"
#include "ubertopo/topo/validate_detail.h"

namespace ubertopo::topo {

namespace util = litestl::util;
using ident::ElemKind;
using VC = ValidateCode;

namespace {

using Tri = std::array<uint32_t, 3>;
using Box = FaceGrid::Box;

template <geom::CoordType C> Box tri_box(const Complex<C> &k, const Tri &t)
{
  Box b;
  for (int i = 0; i < 3; i++) {
    b.lo[i] = INFINITY;
    b.hi[i] = -INFINITY;
  }
  for (uint32_t v : t) {
    const auto d = geom::to_double3<C>(k.pos(VertRef(v)));
    for (int i = 0; i < 3; i++) {
      b.lo[i] = std::min(b.lo[i], d[i]);
      b.hi[i] = std::max(b.hi[i], d[i]);
    }
  }
  return b;
}

/** Candidates from grid entries: whole faces, single triangles of large
 * faces, wires. Sorted and unique after finish(). */
struct Candidates {
  util::Vector<uint32_t, 16> faces, wires;
  util::Vector<std::pair<uint32_t, uint32_t>, 16> tris; /* (face, tri) */
  void add(const FaceGrid::Entry &x)
  {
    switch (x.kind) {
      case FaceGrid::Kind::Face:
        faces.append(x.elem);
        break;
      case FaceGrid::Kind::FaceTri:
        tris.append({x.elem, x.tri});
        break;
      case FaceGrid::Kind::Wire:
        wires.append(x.elem);
        break;
    }
  }
  template <typename V> static void uniq(V &v)
  {
    std::sort(v.begin(), v.end());
    v.resize(int(std::unique(v.begin(), v.end()) - v.begin()));
  }
  void finish()
  {
    uniq(faces);
    uniq(wires);
    uniq(tris);
  }
  void clear()
  {
    faces.clear();
    wires.clear();
    tris.clear();
  }
};

} // namespace

template <geom::CoordType C>
void validate_global_grid(const Complex<C> &k, ValidationReport &rep, const ValidateFilter &flt, const FaceGrid &grid)
{
  /* 1. the listed faces: loop checks and T(f) */
  std::unordered_map<uint32_t, util::Vector<Tri>> tris;
  std::unordered_map<uint32_t, bool> fails; /* faces that don't triangulate */
  for (uint32_t f : flt.faces) {
    if (!k.faces.live(f)) {
      continue;
    }
    if (!vdetail::face_checks_and_tris<C>(k, f, true, rep, tris[f])) {
      fails[f] = true;
    }
  }
  if (!rep.ok()) {
    return;
  }
  auto tris_of = [&](uint32_t g) -> const util::Vector<Tri> * {
    if (fails.count(g)) {
      return nullptr;
    }
    auto it = tris.find(g);
    if (it != tris.end()) {
      return &it->second;
    }
    ValidationReport dummy;
    util::Vector<Tri> t;
    if (!vdetail::face_checks_and_tris<C>(k, g, false, dummy, t)) {
      fails[g] = true; /* an unlisted face that doesn't triangulate isn't this edit's problem */
      return nullptr;
    }
    return &(tris[g] = std::move(t));
  };
  /* does triangle tf of face f meet triangle tg of face g away from shared
   * vertices and edges? (each triangle's edges against the other) */
  auto tri_pair_bad = [&](uint32_t f, const Tri &tf, uint32_t g, const Tri &tg) {
    auto sv = [&](uint32_t v) { return vdetail::vertex_on_face<C>(k, v, f) && vdetail::vertex_on_face<C>(k, v, g); };
    auto se = [&](uint32_t a, uint32_t b) {
      return vdetail::edge_on_face<C>(k, a, b, f) && vdetail::edge_on_face<C>(k, a, b, g);
    };
    for (int dir = 0; dir < 2; dir++) {
      const Tri &x = dir ? tg : tf, &y = dir ? tf : tg;
      const uint32_t xf = dir ? g : f;
      for (int s = 0; s < 3; s++) {
        const uint32_t p = x[s], q = x[(s + 1) % 3];
        if (vdetail::bad_contact<C>(k, p, q, vdetail::edge_on_face<C>(k, p, q, xf), y, sv, se)) {
          return true;
        }
      }
    }
    return false;
  };

  /* 2. face-face contact around each listed face */
  std::set<std::pair<uint32_t, uint32_t>> bad_pairs;
  util::Vector<uint32_t, 16> near_wires;
  Candidates cand;
  for (uint32_t f : flt.faces) {
    if (!k.faces.live(f)) {
      continue;
    }
    const util::Vector<Tri> *tf_list = tris_of(f);
    if (!tf_list) {
      continue;
    }
    for (const Tri &tf : *tf_list) {
      const Box b = tri_box<C>(k, tf);
      cand.clear();
      grid.query(b, [&](const FaceGrid::Entry &x) {
        if (x.kind == FaceGrid::Kind::Wire || x.elem != f) {
          cand.add(x);
        }
      });
      cand.finish();
      for (uint32_t w : cand.wires) {
        near_wires.append(w);
      }
      auto report = [&](uint32_t g) {
        const std::pair<uint32_t, uint32_t> pr{std::min(f, g), std::max(f, g)};
        if (bad_pairs.insert(pr).second) {
          rep.add(VC::FaceIntersection, ElemKind::Face, pr.first);
        }
      };
      auto known_bad = [&](uint32_t g) { return bad_pairs.count({std::min(f, g), std::max(f, g)}) != 0; };
      for (uint32_t g : cand.faces) {
        if (known_bad(g) || !k.faces.live(g)) {
          continue;
        }
        const util::Vector<Tri> *tg_list = tris_of(g);
        if (!tg_list) {
          continue;
        }
        for (const Tri &tg : *tg_list) {
          if (b.overlaps(tri_box<C>(k, tg)) && tri_pair_bad(f, tf, g, tg)) {
            report(g);
            break;
          }
        }
      }
      for (const auto &[g, ti] : cand.tris) {
        const FaceGrid::Rec *r = grid.face_rec(g);
        if (known_bad(g) || !r || ti >= uint32_t(r->tris.size())) {
          continue;
        }
        if (b.overlaps(r->tri_boxes[ti]) && tri_pair_bad(f, tf, g, r->tris[ti])) {
          report(g);
        }
      }
    }
  }

  /* 3. wires: the listed ones and those near a listed face (C5) */
  const auto &econt = k.edges.template get<".e.container">();
  const auto &vcont = k.verts.template get<".v.container">();
  for (uint32_t e : flt.edges) {
    near_wires.append(e);
  }
  Candidates::uniq(near_wires);
  for (uint32_t w : near_wires) {
    if (!k.edges.live(w) || k.edge_coedge(EdgeRef(w))) {
      continue;
    }
    const auto wv = k.edge_verts(EdgeRef(w));
    const uint32_t p = wv[0].i, q = wv[1].i;
    const auto pd3 = geom::to_double3<C>(k.pos(wv[0])), qd3 = geom::to_double3<C>(k.pos(wv[1]));
    const double pd[3] = {pd3[0], pd3[1], pd3[2]}, qd[3] = {qd3[0], qd3[1], qd3[2]};
    Box wb;
    for (int i = 0; i < 3; i++) {
      wb.lo[i] = std::min(pd[i], qd[i]);
      wb.hi[i] = std::max(pd[i], qd[i]);
    }
    cand.clear();
    grid.query_segment(pd, qd, [&](const FaceGrid::Entry &x) {
      if (x.kind != FaceGrid::Kind::Wire) {
        cand.add(x);
      }
    });
    cand.finish();
    /* the faces met, each with the triangles to test (all of T(g) for a
     * whole entry, the listed ones for a large face) */
    util::Vector<uint32_t, 16> gs;
    for (uint32_t g : cand.faces) {
      gs.append(g);
    }
    for (const auto &pr : cand.tris) {
      gs.append(pr.first);
    }
    Candidates::uniq(gs);
    const ident::ContainerRef wc = econt[w];
    bool bad = false;
    for (uint32_t g : gs) {
      if (bad) {
        break;
      }
      if (!k.faces.live(g)) {
        continue;
      }
      const FaceGrid::Rec *r = grid.face_rec(g);
      const bool large = r && r->tris.size() > 0;
      const util::Vector<Tri> *tg_list = large ? nullptr : tris_of(g);
      if (!large && !tg_list) {
        continue;
      }
      if (wc.kind() == ident::ContainerKind::Face && wc.index() == g) {
        bad = !vdetail::embedded_wire_ok<C>(k, p, q, g);
        continue;
      }
      /* elsewhere: only shared endpoints, or endpoints embedded in g */
      auto sv = [&](uint32_t v) {
        if (v != p && v != q) {
          return false;
        }
        const ident::ContainerRef c = vcont[v];
        return (c.kind() == ident::ContainerKind::Face && c.index() == g) || vdetail::vertex_on_face<C>(k, v, g);
      };
      auto se = [](uint32_t, uint32_t) { return false; };
      auto test = [&](const Tri &t, const Box &tb) {
        return wb.overlaps(tb) && vdetail::bad_contact<C>(k, p, q, false, t, sv, se);
      };
      if (large) {
        for (const auto &[g2, ti] : cand.tris) {
          if (g2 == g && ti < uint32_t(r->tris.size()) && test(r->tris[ti], r->tri_boxes[ti])) {
            bad = true;
            break;
          }
        }
      }
      else {
        for (const Tri &t : *tg_list) {
          if (test(t, tri_box<C>(k, t))) {
            bad = true;
            break;
          }
        }
      }
    }
    if (bad) {
      rep.add(VC::WireCrossesFace, ElemKind::Edge, w);
    }
  }

  /* 4. stored regions against a fresh build: only barriers ask (O(N)) */
  if (flt.regions && !k.regions_dirty) {
    vdetail::check_regions_fresh<C>(k, rep);
  }
}

template <geom::CoordType C>
geom::RayResult region0_winding_grid(const Complex<C> &k, const geom::Point<C> &p, const FaceGrid &grid)
{
  geom::RayResult res;
  const Box &bb = grid.bounds();
  const auto pd3 = geom::to_double3<C>(p);
  double lo[3], hi[3];
  for (int i = 0; i < 3; i++) {
    lo[i] = std::min(bb.lo[i], pd3[i]);
    hi[i] = std::max(bb.hi[i], pd3[i]);
  }
  std::unordered_map<uint32_t, util::Vector<geom::Point<C>>> fans;
  util::Vector<geom::Point<C>> tris, one;
  Candidates cand;
  for (int d = 0; d < geom::detail::kRayDirCount; d++) {
    geom::Point<C> q;
    if (!geom::detail::far_point_box<C>(p, lo, hi, geom::detail::kRayDirs[d], q)) {
      res.retries++;
      continue;
    }
    const auto qd3 = geom::to_double3<C>(q);
    const double pd[3] = {pd3[0], pd3[1], pd3[2]}, qd[3] = {qd3[0], qd3[1], qd3[2]};
    cand.clear();
    grid.query_segment(pd, qd, [&](const FaceGrid::Entry &x) {
      if (x.kind != FaceGrid::Kind::Wire) {
        cand.add(x);
      }
    });
    cand.finish();
    /* the sides facing region 0, oriented away from it */
    tris.clear();
    for (uint32_t g : cand.faces) {
      if (!k.faces.live(g)) {
        continue;
      }
      auto it = fans.find(g);
      if (it == fans.end()) {
        it = fans.emplace(g, util::Vector<geom::Point<C>>()).first;
        vdetail::face_fan<C>(k, FaceRef(g), it->second);
      }
      for (uint32_t s = 0; s < 2; s++) {
        if (k.region(FaceRef(g), s).i == 0) {
          vdetail::append_side_tris<C>(it->second, 2 * g + s, tris);
        }
      }
    }
    for (const auto &[g, ti] : cand.tris) {
      const FaceGrid::Rec *r = grid.face_rec(g);
      if (!r || ti >= uint32_t(r->tris.size()) || !k.faces.live(g)) {
        continue;
      }
      one.clear();
      for (uint32_t v : r->tris[ti]) {
        one.append(k.pos(VertRef(v)));
      }
      for (uint32_t s = 0; s < 2; s++) {
        if (k.region(FaceRef(g), s).i == 0) {
          vdetail::append_side_tris<C>(one, 2 * g + s, tris);
        }
      }
    }
    int w = 0;
    const geom::RayStatus st =
        geom::ray_segment_winding<C>(p, q, std::span<const geom::Point<C>>(tris.data(), tris.size()), w);
    if (st == geom::RayStatus::Ok || st == geom::RayStatus::Boundary) {
      res.status = st;
      res.winding = st == geom::RayStatus::Ok ? w : 0;
      return res;
    }
    res.retries++;
  }
  res.status = geom::RayStatus::Degenerate;
  return res;
}

#define UT_VGRID_INST(C)                                                                                              \
  template void validate_global_grid<C>(const Complex<C> &, ValidationReport &, const ValidateFilter &,               \
                                        const FaceGrid &);                                                            \
  template geom::RayResult region0_winding_grid<C>(const Complex<C> &, const geom::Point<C> &, const FaceGrid &);
UT_VGRID_INST(double)
UT_VGRID_INST(float)
UT_VGRID_INST(geom::Lattice)
#undef UT_VGRID_INST

} // namespace ubertopo::topo
