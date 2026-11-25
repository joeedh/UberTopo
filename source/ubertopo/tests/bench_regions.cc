/* bench_regions: what region rebuilds cost (OI-P3-1; plan
 * docs/plans/2026-11-24-incremental-regions.md). Reported, not gated. Not in
 * ctest: run by hand. With an argument, only the models whose name starts
 * with it.
 *  - build_regions on each model, at the default band count and at 1 band;
 *  - the editor's rebuild_regions barrier after one edit (Local and None
 *    validation) and its undo, against the edit itself, per kind of edit:
 *    an interior wall split, an exterior wall split, a wall removed (two
 *    rooms merge, and about half of the regions are renumbered), a vertex
 *    moved;
 *  - the first embed into a region after a barrier (city only).
 * Models:
 *  - city: a grid of rooms, one exterior shell;
 *  - sensors: the largest city with 10^4 vertices embedded in its rooms;
 *  - terrain: a sheet of projected quads, one shell;
 *  - hall: a room with tessellated walls and 2000 solid objects inside it
 *    (nesting);
 *  - road on terrain: curved edges (build_regions only). */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "gen/buildings.h"
#include "gen/road_terrain.h"
#include "support/splitmix64.h"
#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/regions.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using test::gen::GridOpts;
using test::gen::Model;

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

double median(std::vector<double> v)
{
  if (v.empty()) {
    return 0;
  }
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

/* build_regions on a fresh copy of k, n times; the copy isn't timed */
template <geom::CoordType C> double time_build(const Complex<C> &k, int n)
{
  std::vector<double> t;
  for (int i = 0; i < n; i++) {
    Complex<C> c = k;
    RegionReport rr;
    const auto t0 = Clock::now();
    const bool ok = build_regions(c, rr);
    t.push_back(ms_since(t0));
    if (!ok) {
      printf("  build_regions failed\n");
      return 0;
    }
  }
  return median(t);
}

bool finish(Model<double> &m, Complex<double> &k)
{
  BuildReport br;
  RegionReport rr;
  return m.b.build(k, br, m.policy) && build_regions(k, rr);
}

bool build_city(Complex<double> &k, int n, int sensors = 0)
{
  Model<double> m(ident::derive(ident::tag::TestOnly, std::string_view("bench-city")));
  GridOpts g;
  g.nx = n;
  g.ny = n;
  g.floors = 2;
  g.cell = 400;
  g.height = 300;
  test::gen::room_grid(m, {0, 0, 0}, g, 1);
  /* room labels repeat every 100 columns: rooms 26..99 have their own */
  int made = 0;
  for (int f = 0; f < g.floors; f++) {
    for (int j = 0; j < n; j++) {
      for (int i = 26; i < 100 && i < n && made < sensors; i++, made++) {
        m.b.vertex(ident::derive_generator(m.root, "bench-sensor", i, j, f),
                   test::gen::gpt<double>({i * g.cell + 123, j * g.cell + 77, f * g.height + 101}),
                   ContainerHint::in_region(m.label("room", 1, i + 100 * j, f)));
      }
    }
  }
  return finish(m, k);
}

bool build_terrain(Complex<double> &k, int n)
{
  Model<double> m(ident::derive(ident::tag::TestOnly, std::string_view("bench-terrain")));
  m.policy.allow_projectable_faces = true;
  litestl::test::SplitMix64 r(3);
  std::vector<int64_t> h(size_t(n + 1) * (n + 1));
  for (int64_t &z : h) {
    z = int64_t(r.below(64));
  }
  const auto p = [&](int i, int j) { return test::gen::I3{i * 256, j * 256, h[size_t(j) * (n + 1) + i]}; };
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++) {
      m.face({{p(i, j), p(i + 1, j), p(i + 1, j + 1), p(i, j + 1)}}, m.outside(), m.outside());
    }
  }
  return finish(m, k);
}

/* a cube of 6 n^2 quads with 2000 boxes in it */
bool build_hall(Complex<double> &k, int n)
{
  Model<double> m(ident::derive(ident::tag::TestOnly, std::string_view("bench-hall")));
  const auto hall = m.label("hall"), out = m.outside();
  const int64_t w = 400, e = n * w;
  for (int axis = 0; axis < 3; axis++) {
    for (const bool high : {false, true}) {
      for (int a = 0; a < n; a++) {
        for (int b = 0; b < n; b++) {
          const auto p = [&](int i, int j) {
            test::gen::I3 q;
            q[axis] = high ? e : 0;
            q[(axis + 1) % 3] = i * w;
            q[(axis + 2) % 3] = j * w;
            return q;
          };
          /* counter-clockwise in (axis + 1, axis + 2): the front faces +axis */
          m.face({{p(a, b), p(a + 1, b), p(a + 1, b + 1), p(a, b + 1)}}, high ? out : hall, high ? hall : out);
        }
      }
    }
  }
  int made = 0;
  const int64_t step = e / 13;
  for (int x = 0; x < 13; x++) {
    for (int y = 0; y < 13; y++) {
      for (int z = 0; z < 13 && made < 2000; z++, made++) {
        const test::gen::I3 lo = {x * step + 31, y * step + 37, z * step + 41};
        m.box(lo, {lo[0] + step / 3, lo[1] + step / 3, lo[2] + step / 3}, m.label("object", x, y, z), hall);
      }
    }
  }
  return finish(m, k);
}

/* a quad face without holes: its outer loop's four vertices */
bool quad(const Complex<double> &k, FaceRef f, VertRef v[4])
{
  const LoopRef l = k.first_loop(f);
  if (!l || k.next_loop(l) || k.loop_size(l) != 4) {
    return false;
  }
  int i = 0;
  k.for_each_coedge(l, [&](CoedgeRef c) { v[i++] = VertRef(k.coedges.get<".c.v">()[c.i]); });
  return true;
}

using Ed = Editor<double>;
/* one edit at a random place: Ok if it was made, else nothing changed */
using Edit = std::function<EditCode(Ed &, litestl::test::SplitMix64 &)>;

FaceRef random_face(const Complex<double> &k, litestl::test::SplitMix64 &r, VertRef v[4])
{
  const uint32_t f = uint32_t(r.below(k.faces.capacity()));
  return k.faces.live(f) && quad(k, FaceRef(f), v) ? FaceRef(f) : FaceRef();
}

/* splits a random quad; which >= 0: one with that many sides in region 0 */
Edit split(int which)
{
  return [which](Ed &ed, litestl::test::SplitMix64 &r) {
    const Complex<double> &k = ed.complex();
    VertRef v[4];
    const FaceRef f = random_face(k, r, v);
    if (!f || (which >= 0 && int(k.region(f, 0).i == 0) + int(k.region(f, 1).i == 0) != which)) {
      return EditCode::BadArgument;
    }
    return ed.split_face(f, v[0], v[2]);
  };
}

EditCode remove_wall(Ed &ed, litestl::test::SplitMix64 &r)
{
  const Complex<double> &k = ed.complex();
  VertRef v[4];
  const FaceRef f = random_face(k, r, v);
  if (!f || k.region(f, 0).i == 0 || k.region(f, 1).i == 0) {
    return EditCode::BadArgument;
  }
  return ed.kill_face(f);
}

/* lifts a terrain vertex */
EditCode lift(Ed &ed, litestl::test::SplitMix64 &r)
{
  const Complex<double> &k = ed.complex();
  const uint32_t v = uint32_t(r.below(k.verts.capacity()));
  if (!k.verts.live(v)) {
    return EditCode::BadArgument;
  }
  auto p = k.pos(VertRef(v));
  p[2] += 1.0 / 256;
  return ed.set_position(VertRef(v), p);
}

/* splits 20 random edges at their midpoints: vertices that can slide along
 * their line and keep every face planar */
std::vector<VertRef> make_sliders(Ed &ed)
{
  litestl::test::SplitMix64 r(11);
  const Complex<double> &k = ed.complex();
  std::vector<VertRef> out;
  for (int t = 0; t < 10000 && out.size() < 20; t++) {
    const uint32_t e = uint32_t(r.below(k.edges.capacity()));
    if (!k.edges.live(e) || !k.edge_coedge(EdgeRef(e))) {
      continue;
    }
    const auto vs = k.edge_verts(EdgeRef(e));
    const auto a = k.pos(vs[0]), b = k.pos(vs[1]);
    VertRef v;
    if (ed.split_edge(EdgeRef(e), {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2}, &v) == EditCode::Ok) {
      out.push_back(v);
    }
  }
  if (ed.rebuild_regions() != EditCode::Ok) {
    out.clear();
  }
  return out;
}

/* slides one of them halfway toward a neighbour */
Edit slide(const std::vector<VertRef> &sliders)
{
  return [sliders](Ed &ed, litestl::test::SplitMix64 &r) {
    const Complex<double> &k = ed.complex();
    const VertRef v = sliders[r.below(sliders.size())];
    const auto vs = k.edge_verts(k.vert_edge(v));
    const auto a = k.pos(vs[0]), b = k.pos(vs[1]);
    return ed.set_position(v, {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2});
  };
}

/* @p samples times: the edit, the barrier (Local and None in turn), their undo */
void run_edits(Ed &ed, const char *name, const Edit &edit, int samples, bool embed = false)
{
  litestl::test::SplitMix64 r(7);
  std::vector<double> te, barrier[2], temb, undo_b, undo_e;
  int done = 0;
  for (int tries = 0; tries < 20000 && done < samples; tries++) {
    auto t0 = Clock::now();
    if (edit(ed, r) != EditCode::Ok) {
      continue;
    }
    te.push_back(ms_since(t0));
    ed.options().validate = done % 2 ? ValidateMode::None : ValidateMode::Local;
    t0 = Clock::now();
    const EditCode c = ed.rebuild_regions();
    barrier[done % 2].push_back(ms_since(t0));
    ed.options().validate = ValidateMode::Local;
    if (c != EditCode::Ok) {
      printf("  %s: rebuild_regions failed: %s\n", name, edit_code_name(c));
      ed.undo();
      return;
    }
    if (embed) {
      /* the first embed into a region after the barrier finds every region's
       * winning hint */
      ed.options().validate = ValidateMode::None;
      VertRef pv;
      ed.begin("embed");
      ed.make_vertex({0, 0, 0}, ident::derive(ident::tag::TestOnly, std::string_view("probe")), &pv);
      t0 = Clock::now();
      ed.embed_vertex(pv, EmbedTarget::region(RegionRef(1)));
      temb.push_back(ms_since(t0));
      ed.rollback();
      ed.options().validate = ValidateMode::Local;
    }
    t0 = Clock::now();
    ed.undo();
    undo_b.push_back(ms_since(t0));
    t0 = Clock::now();
    ed.undo();
    undo_e.push_back(ms_since(t0));
    done++;
  }
  printf("  %-20s edit %7.3f ms; barrier %7.1f ms (Local), %7.1f ms (None); undo of the barrier %.1f ms, of the edit "
         "%.3f ms",
         name, median(te), median(barrier[0]), median(barrier[1]), median(undo_b), median(undo_e));
  if (embed) {
    printf("; first region embed %.2f ms", median(temb));
  }
  printf(" (%d samples)\n", done);
  fflush(stdout);
}

void report_build(const char *name, const Complex<double> &k, int n = 5)
{
  const double t_all = time_build(k, n);
  double t_one = 0;
  {
    litestl::task::ScopedBandCount one(1);
    t_one = time_build(k, (n + 1) / 2);
  }
  printf("%s: %u faces, %u shells, %u regions: build_regions %.1f ms (1 band %.1f ms)\n", name, k.faces.live_count(),
         k.shells.live_count(), k.regions.live_count(), t_all, t_one);
  fflush(stdout);
}

EditorOptions options()
{
  EditorOptions o{ValidateMode::Local};
  o.verify_redo = false;
  return o;
}

void city(int n)
{
  Complex<double> k;
  if (!build_city(k, n)) {
    printf("city %d: build failed\n", n);
    return;
  }
  char name[64];
  snprintf(name, sizeof(name), "city %dx%d", n, n);
  report_build(name, k);
  Journal<double> j;
  Ed ed(k, &j, nullptr, options());
  run_edits(ed, "interior wall split", split(0), 10, true);
  run_edits(ed, "exterior wall split", split(1), 10);
  run_edits(ed, "wall removed", remove_wall, 10);
  const std::vector<VertRef> sliders = make_sliders(ed);
  if (sliders.size()) {
    run_edits(ed, "vertex moved", slide(sliders), 10);
  }
}

void sensors()
{
  Complex<double> k;
  if (!build_city(k, 126, 10000)) {
    printf("sensors: build failed\n");
    return;
  }
  report_build("sensors", k);
  Journal<double> j;
  Ed ed(k, &j, nullptr, options());
  run_edits(ed, "interior wall split", split(0), 6);
  run_edits(ed, "wall removed", remove_wall, 6);
}

void terrain()
{
  Complex<double> k;
  if (!build_terrain(k, 320)) {
    printf("terrain: build failed\n");
    return;
  }
  report_build("terrain", k);
  Journal<double> j;
  Ed ed(k, &j, nullptr, options());
  run_edits(ed, "face split", split(-1), 10);
  run_edits(ed, "vertex moved", lift, 10);
}

void hall()
{
  Complex<double> k;
  if (!build_hall(k, 40)) {
    printf("hall: build failed\n");
    return;
  }
  report_build("hall", k, 3);
  Journal<double> j;
  Ed ed(k, &j, nullptr, options());
  run_edits(ed, "object split", split(0), 6); /* an object's face: the hall on one side */
  run_edits(ed, "hall wall split", split(1), 6);
}

void road()
{
  namespace rt = test::gen::rt;
  rt::Plan plan;
  if (rt::plan_road(plan)) {
    return;
  }
  rt::Model<double> m;
  rt::build_terrain(m);
  EditorOptions opt;
  opt.validate = ValidateMode::None;
  Editor<double> ed(m.k, &m.j, nullptr, opt);
  if (rt::place_road(ed, m, plan) != EditCode::Ok) {
    printf("road: place_road failed\n");
    return;
  }
  printf("road on terrain: %u faces, 140 curved edges: build_regions %.1f ms\n", m.k.faces.live_count(),
         time_build(m.k, 5));
}

} // namespace

int main(int argc, char **argv)
{
  topo::register_column_types();
  const char *only = argc > 1 ? argv[1] : "";
  const auto want = [&](const char *name) { return strncmp(name, only, strlen(only)) == 0; };
  if (want("city")) {
    for (int n : {40, 72, 126}) {
      city(n);
    }
  }
  if (want("sensors")) {
    sensors();
  }
  if (want("terrain")) {
    terrain();
  }
  if (want("hall")) {
    hall();
  }
  if (want("road")) {
    road();
  }
  return 0;
}
