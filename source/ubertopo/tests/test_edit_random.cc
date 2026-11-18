/* Randomised edits (plan P3a W8): a fixed random sequence of primitives on a
 * generated model, in double and Lattice.
 *
 *  - every failed primitive leaves a bit-identical content hash;
 *  - every committed primitive leaves a fully valid complex (the editor
 *    validates at the Global level; spot-checked independently here);
 *  - undo-all returns to the initial hash and redo-all to the final one;
 *  - the run is identical at 1 and 4 bands, double and Lattice give the same
 *    topology hash, and the final content hash is pinned.
 *
 * Points are chosen on the integer grid of the generators (2^-8 in double),
 * and edges are split only at integral midpoints, so both coordinate types
 * see the same exact geometry and the same op sequence. */

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

#include "gen/buildings.h"
#include "litestl/util/alloc.h"
#include "litestl/util/task.h"
#include "support/splitmix64.h"
#include "ut_test.h"

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::gen::I3;
using test::gen::Model;

namespace {

PersistentId root(const char *what)
{
  return ident::derive(ident::tag::TestOnly, std::string_view(what));
}

template <geom::CoordType C> I3 grid(const geom::Point<C> &p)
{
  if constexpr (std::is_same_v<C, geom::Lattice>) {
    return {p[0].raw, p[1].raw, p[2].raw};
  }
  else {
    return {int64_t(std::ldexp(double(p[0]), 8)), int64_t(std::ldexp(double(p[1]), 8)),
            int64_t(std::ldexp(double(p[2]), 8))};
  }
}

template <geom::CoordType C> bool build_model(Complex<C> &k)
{
  Model<C> m(root("edit-random"));
  test::gen::GridOpts g;
  g.nx = 2;
  g.ny = 1;
  g.windows = 1;
  test::gen::room_grid(m, {0, 0, 0}, g, 1);
  test::gen::courtyard(m, {3000, 0, 0}, 2);
  test::gen::fence_in_air(m, {0, 3000, 0}, 2);
  test::gen::sensor_and_wire(m, {3000, 3000, 0}, 3);
  test::gen::sloped_roof(m, {6000, 0, 0}, 3, 4);
  BuildReport br;
  RegionReport rr;
  return m.b.build(k, br, m.policy) && build_regions(k, rr);
}

struct Stats {
  std::map<EditCode, uint32_t> codes;
  std::map<std::string, uint32_t> detail; /* validation / region codes of Invalid and RegionsFailed */
  uint32_t ok = 0, steps = 0;
};

template <typename D> uint32_t pick(const D &d, litestl::test::SplitMix64 &r)
{
  if (d.live_count() == 0) {
    return NONE;
  }
  for (int tries = 0; tries < 64; tries++) {
    const uint32_t i = uint32_t(r.below(d.capacity()));
    if (d.live(i)) {
      return i;
    }
  }
  uint32_t n = uint32_t(r.below(d.live_count())), out = NONE;
  d.for_each_live([&](uint32_t i) {
    if (n-- == 0) {
      out = i;
    }
  });
  return out;
}

template <geom::CoordType C> struct Run {
  Complex<C> k;
  Journal<C> j;
  Stats st;

  /** One random step; returns the code of its (last) primitive. */
  const char *last = "";

  EditCode step(Editor<C> &ed, litestl::test::SplitMix64 &r, uint32_t n)
  {
    using test::gen::gpt;
    const uint32_t op = uint32_t(r.below(100));
    static const char *names[] = {"split_edge", "join_edge", "split_face", "join_faces", "flip", "wire", "kill_edge",
                                  "kill_vertex", "kill_face", "remake", "set_position", "embed", "undo", "redo",
                                  "regions"};
    static const uint32_t bounds[] = {14, 24, 38, 48, 53, 59, 64, 68, 72, 78, 84, 88, 93, 97, 100};
    for (int i = 0; i < 15; i++) {
      if (op < bounds[i]) {
        last = names[i];
        break;
      }
    }
    if (op < 14) { /* split an edge at its midpoint (integral midpoints only) */
      const uint32_t e = pick(k.edges, r);
      if (e == NONE) {
        return EditCode::BadArgument;
      }
      const auto vs = k.edge_verts(EdgeRef(e));
      const I3 a = grid<C>(k.pos(vs[0])), b = grid<C>(k.pos(vs[1]));
      for (int i = 0; i < 3; i++) {
        if ((a[i] + b[i]) & 1) {
          return EditCode::BadArgument;
        }
      }
      return ed.split_edge(EdgeRef(e), gpt<C>({(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2}));
    }
    if (op < 24) {
      const uint32_t v = pick(k.verts, r);
      return v == NONE ? EditCode::BadArgument : ed.join_edge(VertRef(v));
    }
    if (op < 38) { /* split a face between two vertices of its first loop */
      const uint32_t f = pick(k.faces, r);
      if (f == NONE) {
        return EditCode::BadArgument;
      }
      std::vector<VertRef> lv;
      k.for_each_coedge(k.first_loop(FaceRef(f)), [&](CoedgeRef c) { lv.push_back(k.start_vert(c)); });
      const VertRef a = lv[r.below(lv.size())], b = lv[r.below(lv.size())];
      return ed.split_face(FaceRef(f), a, b);
    }
    if (op < 48) {
      const uint32_t e = pick(k.edges, r);
      return e == NONE ? EditCode::BadArgument : ed.join_faces(EdgeRef(e));
    }
    if (op < 53) {
      const uint32_t f = pick(k.faces, r);
      return f == NONE ? EditCode::BadArgument : ed.flip_face(FaceRef(f));
    }
    if (op < 59) { /* a wire in the air, as one transaction of three primitives */
      const I3 p = {int64_t(r.range(-20, 20)) * 100, int64_t(r.range(-20, 20)) * 100, 4000 + int64_t(r.below(10)) * 100};
      VertRef a, b;
      ed.begin("wire");
      EditCode c = ed.make_vertex(gpt<C>(p), ident::derive(ident::tag::TestOnly, std::string_view("rv"), n, 0), &a);
      if (c == EditCode::Ok) {
        c = ed.make_vertex(gpt<C>({p[0] + 200, p[1], p[2]}), ident::derive(ident::tag::TestOnly, std::string_view("rv"), n, 1),
                           &b);
      }
      if (c == EditCode::Ok) {
        c = ed.make_edge(a, b);
      }
      if (c == EditCode::Ok) {
        return ed.commit();
      }
      ed.rollback();
      return c;
    }
    if (op < 64) {
      const uint32_t e = pick(k.edges, r);
      return e == NONE ? EditCode::BadArgument : ed.kill_edge(EdgeRef(e));
    }
    if (op < 68) {
      const uint32_t v = pick(k.verts, r);
      return v == NONE ? EditCode::BadArgument : ed.kill_vertex(VertRef(v));
    }
    if (op < 72) {
      const uint32_t f = pick(k.faces, r);
      return f == NONE ? EditCode::BadArgument : ed.kill_face(FaceRef(f));
    }
    if (op < 78) { /* remake a face: kill it and make it again from its loops */
      const uint32_t f = pick(k.faces, r);
      if (f == NONE) {
        return EditCode::BadArgument;
      }
      std::vector<VertRef> vs;
      std::vector<uint32_t> sizes;
      k.for_each_loop(FaceRef(f), [&](LoopRef l) {
        sizes.push_back(k.loop_size(l));
        k.for_each_coedge(l, [&](CoedgeRef c) { vs.push_back(k.start_vert(c)); });
      });
      const uint8_t flags = uint8_t(k.face_flags(FaceRef(f)) & ~face_flags::ProjAxisMask);
      const PersistentId h0 = k.side_hint(FaceRef(f), 0), h1 = k.side_hint(FaceRef(f), 1);
      ed.begin("remake");
      EditCode c = ed.kill_face(FaceRef(f));
      if (c == EditCode::Ok) {
        c = ed.make_face(std::span<const VertRef>(vs.data(), vs.size()),
                         std::span<const uint32_t>(sizes.data(), sizes.size()),
                         ident::derive(ident::tag::TestOnly, std::string_view("remake"), n),
                         flags,
                         h0,
                         h1);
      }
      if (c == EditCode::Ok) {
        return ed.commit();
      }
      ed.rollback();
      return c;
    }
    if (op < 84) { /* nudge a vertex by a few grid steps (mostly bends faces) */
      const uint32_t v = pick(k.verts, r);
      if (v == NONE) {
        return EditCode::BadArgument;
      }
      const I3 g = grid<C>(k.pos(VertRef(v)));
      const I3 p = {g[0] + int64_t(r.range(-2, 2)), g[1] + int64_t(r.range(-2, 2)), g[2] + int64_t(r.range(-2, 2))};
      return ed.set_position(VertRef(v), gpt<C>(p));
    }
    if (op < 88) { /* embed or unembed an isolated vertex */
      const uint32_t v = pick(k.verts, r);
      if (v == NONE) {
        return EditCode::BadArgument;
      }
      if (k.vert_container(VertRef(v)).is_none() && !k.regions_dirty) {
        return ed.embed_vertex(VertRef(v), EmbedTarget::region(RegionRef(0)));
      }
      return ed.embed_vertex(VertRef(v), EmbedTarget{});
    }
    if (op < 93) {
      return ed.undo();
    }
    if (op < 97) {
      return ed.redo();
    }
    return ed.rebuild_regions();
  }

  /** The whole run; returns the final content hash. */
  PersistentId run(uint64_t seed, uint32_t steps, PersistentId *topo)
  {
    UT_CHECK(build_model(k));
    const PersistentId h0 = content_hash(k);
    Editor<C> ed(k, &j);
    litestl::test::SplitMix64 r(seed);
    for (uint32_t n = 0; n < steps; n++) {
      const PersistentId pre = content_hash(k);
      const uint32_t undos = j.undo_count(), redos = j.redo_count();
      const EditCode c = step(ed, r, n);
      st.codes[c]++;
      st.steps++;
      if (c == EditCode::Invalid && ed.report().validation.issues.size()) {
        st.detail[std::string(last) + ":" + validate_code_name(ed.report().validation.issues[0].code)]++;
      }
      if (c == EditCode::RegionsFailed && ed.report().region_code >= 0) {
        st.detail[std::string(last) + ":" + region_code_name(RegionCode(ed.report().region_code))]++;
      }
      if (c == EditCode::Ok) {
        st.ok++;
      }
      else {
        const bool same = content_hash(k) == pre && j.undo_count() == undos && j.redo_count() == redos;
        UT_CHECK(same);
        if (!same) {
          fprintf(stderr, "  step %u: failed with %s but changed the complex\n", n, edit_code_name(c));
          return {};
        }
      }
      if (n % 40 == 39) {
        const ValidationReport rep = validate(k, ValidateLevel::Global);
        UT_CHECK(rep.ok());
        if (!rep.ok()) {
          fprintf(stderr, "  step %u: invalid after %s\n", n, edit_code_name(c));
          rep.print(stderr);
          return {};
        }
      }
    }
    const PersistentId h_end = content_hash(k);
    const uint32_t applied = j.undo_count();
    for (uint32_t i = 0; i < applied; i++) {
      UT_CHECK(ed.undo() == EditCode::Ok);
    }
    UT_CHECK(content_hash(k) == h0);
    const uint32_t redos = j.redo_count();
    for (uint32_t i = 0; i < redos; i++) {
      const EditCode c = ed.redo();
      UT_CHECK(c == EditCode::Ok);
      if (c != EditCode::Ok) {
        fprintf(stderr, "  redo %u failed: %s\n", i, edit_code_name(c));
        break;
      }
    }
    /* the redo stack held only what the run itself had undone at the end
     * plus everything undone here; replaying all of it reaches h_end only if
     * nothing was left undone by the run */
    if (redos == applied) {
      UT_CHECK(content_hash(k) == h_end);
    }
    UT_CHECK(validate(k, ValidateLevel::Global).ok());
    if (topo) {
      *topo = topology_hash(k);
    }
    return content_hash(k);
  }
};

uint32_t scaled(uint32_t n)
{
  const char *d = getenv("LITESTL_TEST_SCALE_DIV");
  const int div = d ? atoi(d) : 1;
  return div > 1 ? n / uint32_t(div) : n;
}

template <geom::CoordType C> PersistentId run_one(uint64_t seed, uint32_t steps, PersistentId *topo, bool print)
{
  Run<C> run;
  const PersistentId h = run.run(seed, steps, topo);
  if (print) {
    printf("edit_random (%s, seed %llu): %u steps, %u committed;", geom::CoordTraits<C>::key,
           (unsigned long long)seed, run.st.steps, run.st.ok);
    for (const auto &[code, cnt] : run.st.codes) {
      printf(" %s=%u", edit_code_name(code), cnt);
    }
    printf("\n  causes:");
    for (const auto &[what, cnt] : run.st.detail) {
      printf(" %s=%u", what.c_str(), cnt);
    }
    printf("\n");
  }
  return h;
}

} // namespace

int main()
{
  topo::register_column_types();
  const size_t baseline = litestl::alloc::getMemorySize();
  const uint32_t steps = scaled(600);
  for (uint64_t seed : {1ull, 2ull, 3ull}) {
    PersistentId td, tl;
    const PersistentId hd = run_one<double>(seed, steps, &td, true);
    run_one<geom::Lattice>(seed, steps, &tl, true);
    UT_CHECK(td == tl); /* same topology, IDs and attributes in both coordinate types */
    {
      litestl::task::ScopedBandCount scope(4);
      PersistentId t4;
      UT_CHECK(run_one<double>(seed, steps, &t4, false) == hd);
    }
    if (seed == 1 && steps == 600) {
      if (getenv("LITESTL_PRINT_GOLDENS")) {
        printf("golden: {0x%016llxull, 0x%016llxull}\n", (unsigned long long)hd.lo, (unsigned long long)hd.hi);
      }
      const PersistentId golden{0x09421988ba58a022ull, 0x96f79940201fa554ull};
      UT_CHECK(hd == golden);
    }
  }
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
