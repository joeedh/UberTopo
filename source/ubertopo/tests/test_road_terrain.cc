/* The road-on-terrain acceptance case (plan P4b §6), built by
 * gen/road_terrain.h in double and in Lattice:
 *  - the plan (stations, kill set) and the placement in one transaction;
 *  - validate(Global), regions, the terrain as one sheet shell;
 *  - watertight by sharing: every road edge has radial degree 2;
 *  - bitwise borders: S(s, b_k(s)) equals each border carrier's point;
 *  - surface evaluation, densely, against the carriers' interpolation;
 *  - undo to the terrain, redo to the road;
 *  - edits: split and join of a border edge, split_face refused;
 *  - freeze and I/O;
 *  - double against Lattice: equal coordinate-free structure hashes;
 *  - the skeleton: the fill face's T(f) uses the border flattening points,
 *    and the crossing variant reports FaceIntersection. */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

#include "gen/road_terrain.h"
#include "support/edit_fixture.h"
#include "support/legacy_fixture.h"
#include "ubertopo/topo/freeze.h"
#include "ubertopo/topo/validate_detail.h"
#include "ut_test.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
namespace rt = ubertopo::test::gen::rt;
namespace curves = ubertopo::curves;

namespace {

int scale_div()
{
  const char *e = getenv("LITESTL_TEST_SCALE_DIV");
  const int d = e ? atoi(e) : 1;
  return d > 0 ? d : 1;
}

bool has_code(const ValidationReport &rep, ValidateCode c)
{
  for (const auto &is : rep.issues) {
    if (is.code == c) {
      return true;
    }
  }
  return false;
}

/* The coordinate-free structure hash (plan §6): structure_hash without the
 * coordinate key, the frame, positions, geometry columns (skeleton_tol among
 * them) or the ProjAxis bits of .f.flags, which rounding may change. */
template <geom::CoordType C> PersistentId shape_hash(const Complex<C> &k)
{
  ident::Hasher128 h(ident::DomainTag::Content);
  h.boolean(k.regions_dirty);
  for (uint32_t f : {k.verts.gen_floor(), k.edges.gen_floor(), k.coedges.gen_floor(), k.loops.gen_floor(),
                     k.faces.gen_floor(), k.shells.gen_floor(), k.regions.gen_floor()})
  {
    h.u32(f);
  }
  const auto &flags = k.faces.template get<".f.flags">();
  for (const litestl::attr::AttrGroup *g :
       {&k.verts.group(), &k.edges.group(), &k.coedges.group(), &k.loops.group(), &k.faces.group(),
        &k.faces.sides().group(), &k.shells.group(), &k.regions.group(), &k.global.group()})
  {
    h.u32(g->size());
    for (int i : g->canonical_order()) {
      const auto name = g->name(uint32_t(i));
      const litestl::attr::ColumnBase &c = g->column(uint32_t(i));
      using litestl::attr::ColumnFlags;
      if (test::is_geometry_column(name) || has_flag(c.flags(), ColumnFlags::Temp) ||
          has_flag(c.flags(), ColumnFlags::Derived) || name == "pos")
      {
        continue;
      }
      h.str(name);
      if (g == &k.faces.group() && name == ".f.flags") {
        for (uint32_t r = 0; r < g->size(); r++) {
          h.u32(flags[r] & ~face_flags::ProjAxisMask);
        }
        continue;
      }
      c.hash_into(h);
    }
  }
  return h.finish();
}

/* Pinned goldens (pinned at P4b W6): the terrain sheet and the placed road,
 * per coordinate type. A change means the generator, a curve evaluation or
 * the hash changed; re-pin only with the reason. */
template <geom::CoordType C> void pinned(PersistentId &terrain, PersistentId &road)
{
  if constexpr (std::is_same_v<C, geom::Lattice>) {
    terrain = test::pid(0x4fa19244ad38d7e4ull, 0x6e58203a3dbff0e9ull);
    road = test::pid(0xa5b9cd7c66cd53cbull, 0x0764de8ea7e76939ull);
  }
  else {
    terrain = test::pid(0xef26125e17630834ull, 0xda2e27803105821full);
    road = test::pid(0xb1593a8a3b173e70ull, 0xb2acc08bad884e5cull);
  }
}

struct Result {
  PersistentId shape, terrain, road;
  uint32_t proj_axes = 0; /* faces with a projection axis */
};

template <geom::CoordType C> uint32_t radial_degree(const Complex<C> &k, EdgeRef e)
{
  uint32_t n = 0;
  k.for_each_radial(e, [&](CoedgeRef) { n++; });
  return n;
}

template <geom::CoordType C> uint32_t live_count(const auto &dom)
{
  uint32_t n = 0;
  dom.for_each_live([&](uint32_t) { n++; });
  return n;
}

/* bitwise borders (plan §3.2): S on each link next to border k, from either
 * side, equals the carrier at every station and break */
template <geom::CoordType C> void check_borders(rt::Model<C> &m, const rt::Plan &plan)
{
  const auto ev = curves::surface(m.k.geom, m.rb.road);
  const int nb = int(m.rb.borders.size()), nl = nb - 1;
  std::vector<double> at = m.s;
  for (double b : plan.def.breaks) {
    at.push_back(std::ldexp(b, rt::Model<C>::ul2));
  }
  int bad = 0, n = 0;
  for (int k = 0; k < nb; k++) {
    const auto car = curves::curve3(m.k.geom, m.rb.carriers[k]);
    for (double s : at) {
      for (curves::Side side : {curves::Side::Below, curves::Side::Above}) {
        curves::Vec<3> p;
        car->eval(s, 0, &p, side);
        const double t = m.rb.borders[k].eval(s, 0, side);
        for (int link : {k - 1, k}) {
          if (link < 0 || link >= nl) {
            continue;
          }
          curves::Vec<3> q;
          ev->eval_piece(s, t, 0, &q, link, side);
          n++;
          bad += !(q[0] == p[0] && q[1] == p[1] && q[2] == p[2]);
        }
      }
    }
  }
  printf("  bitwise borders: %d of %d differ\n", bad, n);
  UT_CHECK(bad == 0 && n > 0);
}

/* surface evaluation (plan §6, as amended in §13 W6): on a link, S is the
 * interpolation (1 - w) P_k + w P_(k+1) between the border carriers at the
 * same station, exactly; the carriers are evaluated by different code
 * (alignment3 over offset2), so this checks road3's evaluation within its
 * certified bound at every sample */
template <geom::CoordType C> void check_surface(rt::Model<C> &m)
{
  const auto ev = curves::surface(m.k.geom, m.rb.road);
  const double eS = ev->eval_error(), eps = 0x1p-53;
  const int ns = int(m.s.size()), nl = int(m.rb.borders.size()) - 1;
  const int side = std::max(4, int(std::lround(std::sqrt(10000.0 / scale_div()))));
  double worst = 0;
  long samples = 0;
  for (int k = 0; k < nl; k++) {
    const auto c0 = curves::curve3(m.k.geom, m.rb.carriers[k]), c1 = curves::curve3(m.k.geom, m.rb.carriers[k + 1]);
    const double ec = std::max(c0->eval_error(), c1->eval_error());
    for (int i = 0; i + 1 < ns; i++) {
      for (int a = 0; a < side; a++) {
        const double s = m.s[i] + (a + 0.5) / side * (m.s[i + 1] - m.s[i]);
        const double b0 = m.rb.borders[k].eval(s), b1 = m.rb.borders[k + 1].eval(s);
        curves::Vec<3> P, Q;
        c0->eval(s, 0, &P);
        c1->eval(s, 0, &Q);
        for (int b = 0; b < side; b++) {
          const double t = b0 + (b + 0.5) / side * (b1 - b0);
          curves::Vec<3> S;
          ev->eval_piece(s, t, 0, &S, k);
          const double w = (t - b0) / (b1 - b0);
          double d2 = 0, mag = 0, dmag = 0;
          for (int x = 0; x < 3; x++) {
            const double r = P[x] + w * (Q[x] - P[x]);
            d2 += (S[x] - r) * (S[x] - r);
            mag += std::fabs(P[x]) + std::fabs(Q[x]);
            dmag += std::fabs(Q[x] - P[x]);
          }
          const double tol = eS + ec + 16 * eps * (mag + dmag * (std::fabs(t) + std::fabs(b0) + std::fabs(b1)) / (b1 - b0));
          worst = std::max(worst, std::sqrt(d2) / tol);
          samples++;
        }
      }
    }
  }
  printf("  surface: %ld samples, worst error / bound %.3g\n", samples, worst);
  UT_CHECK(samples > 0 && worst <= 1.0);
}

/* the fill face's T(f) uses the border flattening points (plan §6, Skeleton) */
template <geom::CoordType C> void check_fill_skeleton(rt::Model<C> &m)
{
  Skeleton<C> sk(m.k);
  ValidationReport rep;
  litestl::util::Vector<std::array<uint32_t, 3>> tris;
  UT_CHECK(vdetail::face_checks_and_tris<C>(m.k, m.fill.i, true, rep, tris, sk));
  std::set<uint32_t> used;
  for (const auto &t : tris) {
    used.insert(t.begin(), t.end());
  }
  const int ns = int(m.s.size()), nb = int(m.rb.borders.size());
  size_t points = 0, missing = 0;
  for (int i = 0; i + 1 < ns; i++) {
    for (int k : {0, nb - 1}) {
      for (uint32_t id : sk.interior(m.along[i][k])) {
        points++;
        missing += used.count(id) == 0;
      }
    }
  }
  printf("  fill face: %d triangles, %zu border flattening points, %zu unused\n", int(tris.size()), points, missing);
  UT_CHECK(points > 0 && missing == 0);
}

template <geom::CoordType C> Result test_road(const rt::Plan &plan)
{
  printf("%s:\n", geom::CoordTraits<C>::key);
  Result res;
  rt::Model<C> m;
  UT_CHECK(rt::build_terrain(m));
  UT_CHECK(validate(m.k, ValidateLevel::Global).ok());
  const uint32_t shells0 = live_count<C>(m.k.shells), regions0 = live_count<C>(m.k.regions);
  res.terrain = content_hash(m.k);
  EditorOptions opt;
  opt.validate = ValidateMode::OnCommit;
  Editor<C> ed(m.k, &m.j, nullptr, opt);
  /* the placement */
  EXPECT_EDIT(ed, rt::place_road(ed, m, plan), EditCode::Ok);
  const ValidationReport rep = validate(m.k, ValidateLevel::Global);
  UT_CHECK(rep.ok());
  if (!rep.ok()) {
    rep.print(stderr);
  }
  res.road = content_hash(m.k);
  {
    PersistentId want_terrain, want_road;
    pinned<C>(want_terrain, want_road);
    UT_CHECK(res.terrain == want_terrain && res.road == want_road);
  }
  const int ns = int(m.s.size()), nb = int(m.rb.borders.size()), nl = nb - 1;
  printf("  %d stations x %d borders; %d cells killed; terrain {0x%016llxull, 0x%016llxull} road {0x%016llxull, "
         "0x%016llxull}\n",
         ns, nb, plan.n_killed, (unsigned long long)res.terrain.lo, (unsigned long long)res.terrain.hi,
         (unsigned long long)res.road.lo, (unsigned long long)res.road.hi);
  /* watertight by sharing */
  {
    int bad = 0;
    for (int i = 0; i < ns; i++) {
      for (int k = 0; k < nb; k++) {
        if (i + 1 < ns) {
          bad += radial_degree(m.k, m.along[i][k]) != 2;
        }
        if (k < nl) {
          bad += radial_degree(m.k, m.across[i][k]) != 2;
        }
      }
    }
    UT_CHECK(bad == 0);
  }
  check_borders(m, plan);
  check_surface(m);
  check_fill_skeleton(m);
  /* edits: a border edge split and joined back, then both undone */
  {
    const EdgeRef e = m.along[ns / 2][m.rb.pivot];
    const auto r = m.k.edges.template get<".e.range">()[e.i];
    VertRef v;
    EdgeRef e2;
    EXPECT_EDIT(ed, ed.split_edge_at(e, 0.5 * (r[0] + r[1]), &v, &e2), EditCode::Ok);
    int copied = 0;
    for (EdgeRef x : {e, e2}) {
      m.k.for_each_radial(x, [&](CoedgeRef c) {
        copied += m.k.coedges.template get<".c.pcurve">()[c.i] == m.rb.pcurves[m.rb.pivot];
      });
    }
    UT_CHECK(copied == 4);
    UT_CHECK(validate(m.k, ValidateLevel::Global).ok());
    EXPECT_EDIT(ed, ed.join_edge(v), EditCode::Ok);
    UT_CHECK(validate(m.k, ValidateLevel::Global).ok());
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    UT_CHECK(content_hash(m.k) == res.road);
    const FaceRef f = m.rf[ns / 2][1];
    EXPECT_EDIT(ed, ed.split_face(f, m.rv[ns / 2][1], m.rv[ns / 2 + 1][2]), EditCode::SurfacedFace);
    UT_CHECK(content_hash(m.k) == res.road);
  }
  /* undo restores the terrain bit for bit; redo reproduces the road */
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  UT_CHECK(content_hash(m.k) == res.terrain);
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  UT_CHECK(content_hash(m.k) == res.road);
  /* regions rebuild unchanged: the terrain is still one sheet shell */
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(live_count<C>(m.k.shells) == shells0 && live_count<C>(m.k.regions) == regions0 && shells0 == 1);
  UT_CHECK(validate(m.k, ValidateLevel::Global).ok());
  /* freeze and I/O */
  {
    Complex<C> fz = m.k;
    UT_CHECK(freeze(fz) == FreezeResult::Ok);
    litestl::io::ByteWriter w;
    write(fz, w);
    Complex<C> back;
    litestl::io::ByteReader r(w.data(), w.size());
    UT_CHECK(read(back, r));
    UT_CHECK(content_hash(back) == content_hash(fz) && validate(back, ValidateLevel::Global).ok());
  }
  res.shape = shape_hash(m.k);
  m.k.faces.for_each_live([&](uint32_t f) { res.proj_axes += m.k.face_proj_axis(FaceRef(f)) >= 0; });
  return res;
}

/* the crossing variant reports FaceIntersection (plan §6, Skeleton) */
template <geom::CoordType C> void test_crossing(const rt::Plan &plan)
{
  rt::Model<C> m;
  UT_CHECK(rt::build_terrain(m));
  const PersistentId h0 = content_hash(m.k);
  EditorOptions opt;
  opt.validate = ValidateMode::OnCommit;
  Editor<C> ed(m.k, &m.j, nullptr, opt);
  rt::PlaceOptions po;
  po.cross = true;
  EXPECT_EDIT(ed, rt::place_road(ed, m, plan, po), EditCode::Invalid);
  UT_CHECK(has_code(ed.report().validation, ValidateCode::FaceIntersection));
  UT_CHECK(content_hash(m.k) == h0);
}

} // namespace

int main()
{
  rt::Plan plan;
  const char *e = rt::plan_road(plan);
  UT_CHECK(e == nullptr);
  if (e) {
    fprintf(stderr, "plan_road: %s\n", e);
    return ut_test_end();
  }
  printf("plan: %zu stations, %d cells killed, a buffer of %zu points\n", plan.stations.size(), plan.n_killed,
         plan.buffer.size());
  /* 25 steps of 50 m, plus the breaks at 322, 380, 820 and 878 m */
  UT_CHECK(plan.stations.size() == 29 && plan.n_killed == 50);
  const Result d = test_road<double>(plan);
  const Result l = test_road<geom::Lattice>(plan);
  printf("shape %016llx / %016llx; projected faces %u / %u\n", (unsigned long long)d.shape.lo,
         (unsigned long long)l.shape.lo, d.proj_axes, l.proj_axes);
  UT_CHECK(d.shape == l.shape);
  test_crossing<double>(plan);
  test_crossing<geom::Lattice>(plan);
  return ut_test_end();
}
