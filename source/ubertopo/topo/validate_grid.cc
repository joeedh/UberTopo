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

template <geom::CoordType C> Box tri_box(const Skeleton<C> &k, const Tri &t)
{
  Box b;
  for (int i = 0; i < 3; i++) {
    b.lo[i] = INFINITY;
    b.hi[i] = -INFINITY;
  }
  for (uint32_t v : t) {
    const auto d = geom::to_double3<C>(k.pos(v));
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
  /* 1. the listed faces: loop checks and T(f); triangles in skeleton ids of
   * this pass's table (plan P4b §4.3) */
  Skeleton<C> sk(k);
  std::unordered_map<uint32_t, util::Vector<Tri>> tris;
  std::unordered_map<uint32_t, bool> fails; /* faces that don't triangulate */
  for (uint32_t f : flt.faces) {
    if (!k.faces.live(f)) {
      continue;
    }
    if (!vdetail::face_checks_and_tris<C>(k, f, true, rep, tris[f], sk)) {
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
    util::Vector<Tri> &slot = tris[g]; /* map nodes don't move: the pointer stays valid */
    if (!vdetail::face_checks_and_tris<C>(k, g, false, dummy, slot, sk)) {
      tris.erase(g);
      fails[g] = true; /* an unlisted face that doesn't triangulate isn't this edit's problem */
      return nullptr;
    }
    return &slot;
  };
  /* triangle ti of a large face's grid record, in this pass's ids: a curved
   * face's record holds another table's ids, so it is read by index from
   * this pass's T(g) (the same canonical CDT, so the same order) */
  auto large_tri = [&](uint32_t g, const FaceGrid::Rec &r, uint32_t ti) -> const Tri * {
    if (!r.curved) {
      return &r.tris[ti];
    }
    const util::Vector<Tri> *t = tris_of(g);
    return t && ti < uint32_t(t->size()) ? &(*t)[ti] : nullptr;
  };
  /* does triangle tf of face f meet triangle tg of face g away from shared
   * vertices and edges? (each triangle's edges against the other) */
  auto tri_pair_bad = [&](uint32_t f, const Tri &tf, uint32_t g, const Tri &tg) {
    auto sv = [&](uint32_t v) {
      return vdetail::skel_point_on_face<C>(k, sk, v, f) && vdetail::skel_point_on_face<C>(k, sk, v, g);
    };
    auto se = [&](uint32_t a, uint32_t b) {
      return vdetail::skel_segment_on_face<C>(k, sk, a, b, f) && vdetail::skel_segment_on_face<C>(k, sk, a, b, g);
    };
    for (int dir = 0; dir < 2; dir++) {
      const Tri &x = dir ? tg : tf, &y = dir ? tf : tg;
      const uint32_t xf = dir ? g : f;
      for (int s = 0; s < 3; s++) {
        const uint32_t p = x[s], q = x[(s + 1) % 3];
        if (vdetail::bad_contact<C>(sk, p, q, vdetail::skel_segment_on_face<C>(k, sk, p, q, xf), y, sv, se)) {
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
      const Box b = tri_box<C>(sk, tf);
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
          if (b.overlaps(tri_box<C>(sk, tg)) && tri_pair_bad(f, tf, g, tg)) {
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
        const Tri *tg = large_tri(g, *r, ti);
        if (tg && b.overlaps(r->tri_boxes[ti]) && tri_pair_bad(f, tf, g, *tg)) {
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
    /* the wire's skeleton chain (plan P4b §12 E): each segment is queried
     * and tested; only the true endpoints may be shared */
    const auto chain = wire_chain<C>(k, sk, w);
    const ident::ContainerRef wc = econt[w];
    bool bad = false;
    for (int si = 0; si + 1 < chain.size() && !bad; si++) {
      const uint32_t a = chain[si], b = chain[si + 1];
      const auto pd3 = geom::to_double3<C>(sk.pos(a)), qd3 = geom::to_double3<C>(sk.pos(b));
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
          bad = !vdetail::embedded_wire_ok<C>(k, a, b, g, sk);
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
          return wb.overlaps(tb) && vdetail::bad_contact<C>(sk, a, b, false, t, sv, se);
        };
        if (large) {
          for (const auto &[g2, ti] : cand.tris) {
            if (g2 != g || ti >= uint32_t(r->tris.size())) {
              continue;
            }
            const Tri *t = large_tri(g, *r, ti);
            if (t && test(*t, r->tri_boxes[ti])) {
              bad = true;
              break;
            }
          }
        }
        else {
          for (const Tri &t : *tg_list) {
            if (test(t, tri_box<C>(sk, t))) {
              bad = true;
              break;
            }
          }
        }
      }
    }
    if (bad) {
      rep.add(VC::WireCrossesFace, ElemKind::Edge, w);
    }
  }

  /* 4. stored regions against a fresh build, O(N), when a caller sets
   * flt.regions (no editor path does: barriers build them fresh) */
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
  Skeleton<C> sk(k);
  auto fan = [&](uint32_t g) -> const util::Vector<geom::Point<C>> & {
    auto it = fans.find(g);
    if (it == fans.end()) {
      it = fans.emplace(g, util::Vector<geom::Point<C>>()).first;
      vdetail::face_fan<C>(k, FaceRef(g), it->second, sk);
    }
    return it->second;
  };
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
      for (uint32_t s = 0; s < 2; s++) {
        if (k.region(FaceRef(g), s).i == 0) {
          vdetail::append_side_tris<C>(fan(g), 2 * g + s, tris);
        }
      }
    }
    util::Vector<uint32_t, 8> whole; /* curved large faces: their whole fan, once */
    for (const auto &[g, ti] : cand.tris) {
      const FaceGrid::Rec *r = grid.face_rec(g);
      if (!r || ti >= uint32_t(r->tris.size()) || !k.faces.live(g)) {
        continue;
      }
      if (r->curved) {
        /* its record's ids belong to another table (FaceGrid::Rec) */
        if (std::find(whole.begin(), whole.end(), g) == whole.end()) {
          whole.append(g);
          for (uint32_t s = 0; s < 2; s++) {
            if (k.region(FaceRef(g), s).i == 0) {
              vdetail::append_side_tris<C>(fan(g), 2 * g + s, tris);
            }
          }
        }
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
