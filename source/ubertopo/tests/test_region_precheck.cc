/* validate_regions' flat pre-check against the walk it stands in for (plan
 * 2026-11-24-incremental-regions §5): it accepts what build_regions writes,
 * and it never accepts a complex the walk reports something on. */

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate_detail_regions.h"

#include "gen/buildings.h"

#include "litestl/util/alloc.h"
#include "litestl/util/task.h"
#include "support/splitmix64.h"
#include "support/topo_fixtures.h"
#include "ut_test.h"

#include <functional>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::ContainerKind;
using ident::ContainerRef;
using test::I3;
using test::pid;
using test::pt;

namespace {

template <geom::CoordType C> VertexTopoView view(const Complex<C> &k)
{
  return {k.verts.template get<".v.e">(), k.verts.template get<".v.container">(), k.verts.template get<".id">(),
          k.verts.template get<".live">(), k.verts.capacity()};
}

template <geom::CoordType C> bool precheck(const Complex<C> &k)
{
  return vdetail::regions_precheck(k, view<C>(k));
}

template <geom::CoordType C> ValidationReport walk(const Complex<C> &k)
{
  ValidationReport r;
  vdetail::validate_regions_walk(k, view<C>(k), r);
  return r;
}

/* validate_regions reports what the walk reports, and the pre-check passes
 * only if that is nothing */
template <geom::CoordType C> bool agrees(const Complex<C> &k)
{
  const ValidationReport w = walk<C>(k);
  ValidationReport r;
  validate_regions(k, view<C>(k), r);
  bool same = r.total == w.total && r.issues.size() == w.issues.size();
  for (int i = 0; same && i < int(w.issues.size()); i++) {
    same = r.issues[i].code == w.issues[i].code && r.issues[i].kind == w.issues[i].kind &&
           r.issues[i].index == w.issues[i].index;
  }
  return same && (w.ok() || !precheck<C>(k));
}

ContainerRef raw(ContainerKind kind, uint32_t i)
{
  ContainerRef c;
  c.bits = (uint32_t(kind) << 30) | i;
  return c;
}

/* Two rooms side by side, a room apart with a pillar in it, a fence, an
 * embedded vertex and wire; dead rows in every domain, full of garbage. */
template <geom::CoordType C> void base_model(Complex<C> &k)
{
  test::GridBuilder<C> g;
  g.box({0, 0, 0}, {10, 10, 10});
  g.box({10, 0, 0}, {20, 10, 10});
  g.box({100, 0, 0}, {140, 40, 40});
  g.box({110, 10, 10}, {120, 20, 20});
  g.face({{{50, 0, 0}, {60, 0, 0}, {60, 0, 10}, {50, 0, 10}}});
  const uint32_t gone = g.face({{{70, 0, 0}, {80, 0, 0}, {80, 0, 10}, {70, 0, 10}}});
  const uint32_t a = g.b.vertex(pid(100), pt<C>({-20, 0, 0})), b = g.b.vertex(pid(101), pt<C>({-20, 0, 10}));
  g.b.wire(pid(200), a, b);
  g.b.vertex(pid(102), pt<C>({55, 0, 5}), ContainerHint::in_face(gone - 1));
  BuildReport br;
  RegionReport rr;
  UT_CHECK(g.b.build(k, br) && build_regions(k, rr));
  {
    /* a dead face among the live ones */
    Journal<C> j;
    EditorOptions opt;
    opt.validate = ValidateMode::None;
    Editor<C> ed(k, &j, nullptr, opt);
    for (uint32_t f = 0; f < k.faces.capacity(); f++) {
      const auto p = geom::to_double3<C>(k.pos(k.start_vert(k.first_coedge(k.first_loop(FaceRef(f))))));
      const double s = std::is_same_v<C, geom::Lattice> ? 1 : 256;
      if (p[0] * s >= 70 && p[0] * s <= 80) {
        UT_CHECK(ed.kill_face(FaceRef(f)) == EditCode::Ok);
        break;
      }
    }
    UT_CHECK(ed.rebuild_regions() == EditCode::Ok);
  }
  k.verts.free(k.verts.alloc());
  k.edges.free(k.edges.alloc());
  k.coedges.free(k.coedges.alloc());
  k.loops.free(k.loops.alloc());
  k.shells.free(k.shells.alloc());
  k.regions.free(k.regions.alloc());
  const uint32_t bad = 0x7fffffffu;
  const auto dead = [](auto &dom, auto &&f) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < dom.capacity(); i++) {
      if (!dom.live(i)) {
        f(i);
        n++;
      }
    }
    UT_CHECK(n > 0);
  };
  dead(k.verts, [&](uint32_t i) { k.verts.template get<".v.container">().set(i, raw(ContainerKind(3), 1)); });
  dead(k.edges, [&](uint32_t i) {
    k.edges.template get<".e.container">().set(i, raw(ContainerKind(3), 1));
    k.edges.template get<".e.c">().set(i, 0u);
    k.edges.template get<".e.vs">().set(i, U32x2{bad, bad});
  });
  dead(k.coedges, [&](uint32_t i) {
    k.coedges.template get<".c.l">().set(i, bad);
    k.coedges.template get<".c.radial_next">().set(i, bad);
  });
  dead(k.loops, [&](uint32_t i) { k.loops.template get<".l.f">().set(i, bad); });
  dead(k.faces, [&](uint32_t i) {
    k.faces.template get<".f.shell">().set(i, U32x2{bad, bad});
    k.faces.template get<".f.region">().set(i, U32x2{bad, bad});
    k.faces.sides().template get<".fs.shell_next">().set(2 * i, 0u);
  });
  dead(k.shells, [&](uint32_t i) {
    k.shells.template get<".s.region">().set(i, bad);
    k.shells.template get<".s.first">().set(i, 0u);
    k.shells.template get<".s.next">().set(i, 0u);
  });
  dead(k.regions, [&](uint32_t i) {
    k.regions.template get<".r.flags">().set(i, region_flags::Unbounded);
    k.regions.template get<".r.shell">().set(i, bad);
  });
  k.regions_dirty = false;
}

template <geom::CoordType C> void test_accepts()
{
  for (const int bands : {1, 4}) {
    litestl::task::ScopedBandCount scope(bands);
    Complex<C> k;
    base_model<C>(k);
    UT_CHECK(walk<C>(k).ok() && precheck<C>(k) && agrees<C>(k));
    const auto model = [&](auto &&fill) {
      test::gen::Model<C> m(ident::derive(ident::tag::TestOnly, std::string_view("precheck")));
      fill(m);
      Complex<C> kk;
      BuildReport br;
      RegionReport rr;
      UT_CHECK(m.b.build(kk, br, m.policy) && build_regions(kk, rr) && precheck<C>(kk) && walk<C>(kk).ok());
    };
    model([](auto &m) { test::gen::random_city(m, 11, 25); });
    model([](auto &m) { test::gen::nested_solids(m, {0, 0, 0}); });
    model([](auto &m) { test::gen::courtyard(m, {0, 0, 0}); });
    model([](auto &m) { test::gen::fence_in_air(m, {0, 0, 0}, 3); });
    model([](auto &m) { test::gen::sensor_and_wire(m, {0, 0, 0}); });
    model([](auto &m) { test::gen::sloped_roof(m, {0, 0, 0}, 1); });
  }
}

/* one corruption per rule; the walk reports each */
template <geom::CoordType C> void test_rules()
{
  Complex<C> base;
  base_model<C>(base);
  const auto bad = [&](const char *what, const std::function<void(Complex<C> &)> &f) {
    Complex<C> k = base;
    f(k);
    const bool reported = !walk<C>(k).ok(), pre = precheck<C>(k);
    if (!reported || pre) {
      fprintf(stderr, "%s: walk reports %d, pre-check passes %d\n", what, int(reported), int(pre));
    }
    UT_CHECK(reported && !pre && agrees<C>(k));
  };
  const uint32_t NR = base.regions.live_count(), NS = base.shells.live_count();
  UT_CHECK(NR >= 5 && NS >= 5);

  /* region IDs and flags */
  bad("region 0 dead", [](Complex<C> &k) { k.regions.free(0); });
  bad("no unbounded region", [](Complex<C> &k) { k.regions.template get<".r.flags">().set(0, uint8_t(0)); });
  bad("two unbounded regions",
      [](Complex<C> &k) { k.regions.template get<".r.flags">().set(2, region_flags::Unbounded); });
  bad("a region with region 0's ID", [NR](Complex<C> &k) {
    auto &id = k.regions.template get<".id">();
    id.set(NR - 1, id[0]);
  });
  bad("a region with region 0's ID, the first", [](Complex<C> &k) {
    auto &id = k.regions.template get<".id">();
    id.set(1, id[0]);
  });
  bad("neighbours share an ID", [](Complex<C> &k) {
    auto &id = k.regions.template get<".id">();
    id.set(2, id[1]);
  });
  bad("two regions share an ID", [](Complex<C> &k) {
    auto &id = k.regions.template get<".id">();
    id.set(3, id[1]);
  });

  {
    /* nothing refers to region 0, which is dead and keeps its flag */
    Complex<C> k;
    RegionReport rr;
    UT_CHECK(build_regions(k, rr) && precheck<C>(k) && walk<C>(k).ok());
    attr::set_bool(k.regions.template get<".live">(), 0, false);
    UT_CHECK(!walk<C>(k).ok() && !precheck<C>(k) && agrees<C>(k));
  }

  /* region chains */
  bad("chain to a dead shell", [NS](Complex<C> &k) { k.regions.template get<".r.shell">().set(1, NS); });
  bad("chain past the shells", [](Complex<C> &k) { k.regions.template get<".r.shell">().set(1, 0x7fffffffu); });
  bad("chain with another region's shell", [](Complex<C> &k) {
    k.regions.template get<".r.shell">().set(1, k.regions.template get<".r.shell">()[2]);
  });
  bad("chain ends in another region's", [](Complex<C> &k) {
    k.shells.template get<".s.next">().set(k.regions.template get<".r.shell">()[1],
                                           k.regions.template get<".r.shell">()[2]);
  });
  bad("chain is a cycle", [](Complex<C> &k) {
    const uint32_t s = k.regions.template get<".r.shell">()[1];
    k.shells.template get<".s.next">().set(s, s);
  });

  /* shells */
  bad("a shell of a dead region", [](Complex<C> &k) {
    const uint32_t s = k.shells.alloc(); /* with no sides */
    k.shells.template get<".s.region">().set(s, 0x7fffffffu);
    k.shells.template get<".s.first">().set(s, NONE);
  });
  for (uint32_t side = 0; side < 2; side++) {
    for (uint32_t f = 0; f < base.faces.capacity(); f++) {
      if (!base.faces.live(f)) {
        continue;
      }
      const auto set = [f, side](auto &col, uint32_t x) {
        U32x2 y = col[f];
        y[side] = x;
        col.set(f, y);
      };
      bad("a side in a dead shell", [&](Complex<C> &k) { set(k.faces.template get<".f.shell">(), NS); });
      bad("a side in no shell", [&](Complex<C> &k) { set(k.faces.template get<".f.shell">(), NONE); });
      bad("a side in another shell", [&](Complex<C> &k) {
        auto &c = k.faces.template get<".f.shell">();
        set(c, (c[f][side] + 1) % NS);
      });
      bad("a side in another region than its shell", [&](Complex<C> &k) {
        auto &c = k.faces.template get<".f.region">();
        set(c, (c[f][side] + 1) % NR);
      });
      bad("a chain that stops early, or goes on", [&](Complex<C> &k) {
        auto &c = k.faces.sides().template get<".fs.shell_next">();
        c.set(2 * f + side, c[2 * f + side] == NONE ? 2 * f + side : NONE);
      });
    }
  }
  bad("a first side that isn't the first", [](Complex<C> &k) {
    auto &c = k.shells.template get<".s.first">();
    c.set(0, k.faces.sides().template get<".fs.shell_next">()[c[0]]);
  });
  bad("a shell without its sides", [](Complex<C> &k) { k.shells.template get<".s.first">().set(0, NONE); });
  bad("a shell without sides, but with a first one", [](Complex<C> &k) {
    const uint32_t s = k.shells.alloc();
    k.shells.template get<".s.region">().set(s, 0u);
    k.shells.template get<".s.first">().set(s, k.shells.template get<".s.first">()[0]);
  });

  /* a whole shell's sides moved to another region, which keeps invariant W,
   * and the shell not */
  for (uint32_t s = 0; s < base.shells.capacity(); s++) {
    if (!base.shells.live(s)) {
      continue;
    }
    bad("a shell in another region than its sides", [&](Complex<C> &k) {
      auto &fr = k.faces.template get<".f.region">();
      const auto &sn = k.faces.sides().template get<".fs.shell_next">();
      for (uint32_t fs = k.shells.template get<".s.first">()[s]; fs != NONE; fs = sn[fs]) {
        U32x2 r = fr[fs / 2];
        r[fs & 1] = (r[fs & 1] + 1) % NR;
        fr.set(fs / 2, r);
      }
      UT_CHECK(walk<C>(k).count(ValidateCode::WedgeInvariant) == 0);
    });
  }

  /* containers: on the loose vertex, the wire, and on faces' vertices and edges */
  const uint32_t dead_face = [&] {
    uint32_t f = 0;
    while (base.faces.live(f)) {
      f++;
    }
    return f;
  }();
  uint32_t loose = NONE, wire = NONE, face_edge = NONE;
  for (uint32_t i = 0; i < base.verts.capacity(); i++) {
    if (base.verts.live(i) && base.verts.template get<".id">()[i] == pid(102)) {
      loose = i;
    }
  }
  for (uint32_t e = 0; e < base.edges.capacity(); e++) {
    if (base.edges.live(e)) {
      (base.edge_coedge(EdgeRef(e)) ? face_edge : wire) = e;
    }
  }
  UT_CHECK(loose != NONE && wire != NONE && face_edge != NONE && dead_face < base.faces.capacity());
  for (const ContainerRef c : {raw(ContainerKind::Face, dead_face), raw(ContainerKind::Face, 0x3ffffffu),
                               raw(ContainerKind::Region, NR), raw(ContainerKind::Region, 0x3ffffffu),
                               raw(ContainerKind(3), 0)})
  {
    bad("a vertex's container", [&](Complex<C> &k) { k.verts.template get<".v.container">().set(loose, c); });
    bad("a wire's container", [&](Complex<C> &k) { k.edges.template get<".e.container">().set(wire, c); });
    bad("a face edge's container", [&](Complex<C> &k) { k.edges.template get<".e.container">().set(face_edge, c); });
  }
  for (uint32_t i = 0; i < base.verts.capacity(); i++) {
    bool on_face = false;
    base.edges.for_each_live([&](uint32_t e) {
      const auto vs = base.edge_verts(EdgeRef(e));
      on_face = on_face || (base.edge_coedge(EdgeRef(e)) && (vs[0].i == i || vs[1].i == i));
    });
    if (on_face) {
      bad("a container on a face vertex", [&](Complex<C> &k) {
        k.verts.template get<".v.container">().set(i, raw(ContainerKind::Region, 1));
      });
    }
  }

  /* invariant W alone: one side cut off into a shell of its own, in another
   * region, with every other rule kept */
  for (uint32_t fs = 0; fs < 2 * base.faces.capacity(); fs++) {
    if (!base.faces.live(fs / 2)) {
      continue;
    }
    bad("invariant W", [&](Complex<C> &k) {
      auto &sfirst = k.shells.template get<".s.first">();
      auto &sn = k.faces.sides().template get<".fs.shell_next">();
      U32x2 sh = k.faces.template get<".f.shell">()[fs / 2], reg = k.faces.template get<".f.region">()[fs / 2];
      const uint32_t old = sh[fs & 1];
      if (sfirst[old] == fs) {
        sfirst.set(old, sn[fs]);
      }
      else {
        uint32_t p = sfirst[old];
        while (sn[p] != fs) {
          p = sn[p];
        }
        sn.set(p, sn[fs]);
      }
      sn.set(fs, NONE);
      const uint32_t s = k.shells.alloc();
      sh[fs & 1] = s;
      reg[fs & 1] = (reg[fs & 1] + 1) % NR;
      k.shells.template get<".s.region">().set(s, reg[fs & 1]);
      sfirst.set(s, fs);
      k.shells.template get<".s.next">().set(s, NONE);
      k.faces.template get<".f.shell">().set(fs / 2, sh);
      k.faces.template get<".f.region">().set(fs / 2, reg);
      const ValidationReport r = walk<C>(k);
      UT_CHECK(r.total > 0 && r.count(ValidateCode::WedgeInvariant) == r.issues.size());
    });
  }
}

/* valid, but not what build_regions writes: the walk decides */
template <geom::CoordType C> void test_left_to_the_walk()
{
  Complex<C> base;
  base_model<C>(base);
  {
    Complex<C> k = base; /* shell 0's chain with its first two sides swapped */
    auto &first = k.shells.template get<".s.first">();
    auto &sn = k.faces.sides().template get<".fs.shell_next">();
    const uint32_t a = first[0], b = sn[a];
    sn.set(a, sn[b]);
    sn.set(b, a);
    first.set(0, b);
    UT_CHECK(walk<C>(k).ok() && !precheck<C>(k) && agrees<C>(k));
  }
  {
    Complex<C> k = base; /* dirty: region structure isn't checked, IDs are */
    k.shells.template get<".s.region">().set(0, 0x7fffffffu);
    k.regions_dirty = true;
    UT_CHECK(walk<C>(k).ok() && !precheck<C>(k) && agrees<C>(k));
  }
}

/* random damage, in and out of range (ASan builds check what is read) */
template <geom::CoordType C> void test_random_damage(int rounds)
{
  Complex<C> base;
  base_model<C>(base);
  litestl::test::SplitMix64 r(29);
  uint32_t reported = 0, passed = 0, left = 0;
  for (int round = 0; round < rounds; round++) {
    Complex<C> k = base;
    const auto value = [&](uint32_t n) -> uint32_t {
      switch (r.below(8)) {
        case 0:
          return NONE;
        case 1:
          return 0x7fffffffu;
        case 2:
          return n + uint32_t(r.below(3));
        default:
          return uint32_t(r.below(n));
      }
    };
    const auto set = [&](auto &col, uint32_t n, uint32_t limit) { col.set(uint32_t(r.below(n)), value(limit)); };
    const auto set2 = [&](auto &col, uint32_t n, uint32_t limit) {
      const uint32_t i = uint32_t(r.below(n));
      auto x = col[i];
      x[r.below(2)] = value(limit);
      col.set(i, x);
    };
    const auto kill = [&](auto &dom) {
      const uint32_t i = uint32_t(r.below(dom.capacity()));
      attr::set_bool(dom.template get<".live">(), i, !dom.live(i));
    };
    const uint32_t NV = k.verts.capacity(), NE = k.edges.capacity(), NC = k.coedges.capacity(),
                   NL = k.loops.capacity(), NF = k.faces.capacity(), NS = k.shells.capacity(),
                   NR = k.regions.capacity();
    for (uint64_t n = 1 + r.below(3); n-- > 0;) {
      switch (r.below(19)) {
        case 0:
          set2(k.faces.template get<".f.shell">(), NF, NS);
          break;
        case 1:
          set2(k.faces.template get<".f.region">(), NF, NR);
          break;
        case 2:
          set(k.faces.sides().template get<".fs.shell_next">(), 2 * NF, 2 * NF);
          break;
        case 3:
          set(k.shells.template get<".s.first">(), NS, 2 * NF);
          break;
        case 4:
          set(k.shells.template get<".s.region">(), NS, NR);
          break;
        case 5:
          set(k.shells.template get<".s.next">(), NS, NS);
          break;
        case 6:
          set(k.regions.template get<".r.shell">(), NR, NS);
          break;
        case 7:
          k.regions.template get<".r.flags">().set(uint32_t(r.below(NR)), uint8_t(r.below(4)));
          break;
        case 8: {
          auto &id = k.regions.template get<".id">();
          id.set(uint32_t(r.below(NR)), id[uint32_t(r.below(NR))]);
          break;
        }
        case 9:
          k.verts.template get<".v.container">().set(uint32_t(r.below(NV)),
                                                     raw(ContainerKind(r.below(4)), value(NF) & 0x3fffffffu));
          break;
        case 10:
          k.edges.template get<".e.container">().set(uint32_t(r.below(NE)),
                                                     raw(ContainerKind(r.below(4)), value(NR) & 0x3fffffffu));
          break;
        case 11:
          set(k.coedges.template get<".c.radial_next">(), NC, NC);
          break;
        case 12:
          set(k.coedges.template get<".c.l">(), NC, NL);
          break;
        case 13: {
          auto &c = k.coedges.template get<".c.flags">();
          const uint32_t i = uint32_t(r.below(NC));
          c.set(i, uint8_t(c[i] ^ coedge_flags::Sense));
          break;
        }
        case 14:
          set(k.loops.template get<".l.f">(), NL, NF);
          break;
        case 15:
          set(k.edges.template get<".e.c">(), NE, NC);
          break;
        case 16:
          set2(k.edges.template get<".e.vs">(), NE, NV);
          break;
        default:
          switch (r.below(7)) {
            case 0:
              kill(k.verts);
              break;
            case 1:
              kill(k.edges);
              break;
            case 2:
              kill(k.coedges);
              break;
            case 3:
              kill(k.loops);
              break;
            case 4:
              kill(k.faces);
              break;
            case 5:
              kill(k.shells);
              break;
            default:
              kill(k.regions);
          }
      }
    }
    const bool w = walk<C>(k).ok(), p = precheck<C>(k);
    UT_CHECK(agrees<C>(k));
    reported += !w;
    passed += p;
    left += w && !p;
  }
  /* all three outcomes are common: reported, accepted, left to the walk */
  UT_CHECK(reported > uint32_t(rounds) / 4 && passed > uint32_t(rounds) / 50 && left > uint32_t(rounds) / 50);
}

template <geom::CoordType C> void run_all(int rounds)
{
  test_accepts<C>();
  test_rules<C>();
  test_left_to_the_walk<C>();
  test_random_damage<C>(rounds);
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  run_all<geom::Lattice>(4000);
  run_all<double>(1000);
  run_all<float>(1000);
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
