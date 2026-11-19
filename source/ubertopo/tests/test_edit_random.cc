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
#include "support/curve_fixture.h"
#include "support/legacy_fixture.h"
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
  std::map<EditCode, uint32_t> group_codes; /* commits of group transactions */
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
  PersistentId structure;

  /** One random step; returns the code of its (last) primitive. */
  const char *last = "";

  EditCode step(Editor<C> &ed, litestl::test::SplitMix64 &r, uint32_t n)
  {
    using test::gen::gpt;
    const uint32_t op = uint32_t(r.below(133));
    static const char *names[] = {"split_edge", "join_edge", "split_face", "join_faces", "flip",      "wire",
                                  "kill_edge",  "kill_vertex", "kill_face", "remake",   "set_position", "embed",
                                  "undo",       "redo",      "regions",    "imprint",  "unimprint",  "hole",
                                  "remove_hole", "weld",     "compact",    "arc",      "curved_wire", "clear_curve",
                                  "split_at",   "curve_join"};
    static const uint32_t bounds[] = {14, 24, 38, 48, 53, 59, 64, 68, 72, 78, 84, 88, 93, 97, 100,
                                      104, 107, 111, 113, 117, 120, 124, 127, 128, 131, 133};
    for (int i = 0; i < 26; i++) {
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
    if (op < 100) {
      return ed.rebuild_regions();
    }
    return step_p3b(ed, r, n, op);
  }

  /* P3b primitives (plan P3b §4): each builds its input in the same
   * transaction, so a refusal anywhere rolls the whole step back */
  EditCode step_p3b(Editor<C> &ed, litestl::test::SplitMix64 &r, uint32_t n, uint32_t op)
  {
    using test::gen::gpt;
    auto vid = [&](uint32_t i) { return ident::derive(ident::tag::TestOnly, std::string_view("p3b"), n, i); };
    auto finish = [&](EditCode c) {
      if (c == EditCode::Ok) {
        return ed.commit();
      }
      ed.rollback();
      return c;
    };
    /* three consecutive corners of a face's first loop */
    auto corners = [&](FaceRef f, I3 out[3]) {
      std::vector<I3> lv;
      k.for_each_coedge(k.first_loop(f), [&](CoedgeRef c) { lv.push_back(grid<C>(k.pos(k.start_vert(c)))); });
      const size_t i = r.below(lv.size());
      for (int j = 0; j < 3; j++) {
        out[j] = lv[(i + j) % lv.size()];
      }
    };
    if (op < 104) { /* imprint a two-edge chord a -> m -> b through a face */
      const uint32_t f = pick(k.faces, r);
      if (f == NONE) {
        return EditCode::BadArgument;
      }
      std::vector<VertRef> lv;
      k.for_each_coedge(k.first_loop(FaceRef(f)), [&](CoedgeRef c) { lv.push_back(k.start_vert(c)); });
      const VertRef a = lv[r.below(lv.size())], b = lv[r.below(lv.size())];
      const I3 ga = grid<C>(k.pos(a)), gb = grid<C>(k.pos(b));
      for (int i = 0; i < 3; i++) {
        if ((ga[i] + gb[i]) & 1) {
          return EditCode::BadArgument;
        }
      }
      const EmbedTarget in = EmbedTarget::face(FaceRef(f));
      ed.begin("imprint");
      VertRef m;
      EdgeRef e1, e2;
      EditCode c = ed.make_vertex(gpt<C>({(ga[0] + gb[0]) / 2, (ga[1] + gb[1]) / 2, (ga[2] + gb[2]) / 2}), vid(0), &m);
      if (c == EditCode::Ok) {
        c = ed.embed_vertex(m, in);
      }
      if (c == EditCode::Ok) {
        c = ed.make_edge(a, m, ident::kNoId, &e1, in);
      }
      if (c == EditCode::Ok) {
        c = ed.make_edge(m, b, ident::kNoId, &e2, in);
      }
      if (c == EditCode::Ok) {
        const EdgeRef chain[2] = {e1, e2};
        c = ed.imprint(FaceRef(f), std::span<const EdgeRef>(chain, 2));
      }
      return finish(c);
    }
    if (op < 107) {
      const uint32_t e = pick(k.edges, r);
      return e == NONE ? EditCode::BadArgument : ed.unimprint(EdgeRef(e));
    }
    if (op < 111) { /* an inner triangle of three corners: a hole, or an island */
      const uint32_t f = pick(k.faces, r);
      if (f == NONE) {
        return EditCode::BadArgument;
      }
      I3 t[3];
      corners(FaceRef(f), t);
      I3 q[3];
      for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
          const int64_t s4 = t[0][i] + t[1][i] + t[2][i] + t[j][i];
          if (s4 % 4) {
            return EditCode::BadArgument;
          }
          q[j][i] = s4 / 4;
        }
      }
      const EmbedTarget in = EmbedTarget::face(FaceRef(f));
      const bool island = r.below(2) == 0;
      ed.begin(island ? "island" : "hole");
      VertRef v[3];
      EdgeRef es[3];
      EditCode c = EditCode::Ok;
      for (int j = 0; j < 3 && c == EditCode::Ok; j++) {
        c = ed.make_vertex(gpt<C>(q[j]), vid(uint32_t(j)), &v[j]);
        if (c == EditCode::Ok) {
          c = ed.embed_vertex(v[j], in);
        }
      }
      for (int j = 0; j < 3 && c == EditCode::Ok; j++) {
        c = ed.make_edge(v[j], v[(j + 1) % 3], ident::kNoId, &es[j], in);
      }
      if (c == EditCode::Ok) {
        c = island ? ed.imprint(FaceRef(f), std::span<const EdgeRef>(es, 3))
                   : ed.add_hole(FaceRef(f), std::span<const EdgeRef>(es, 3));
      }
      return finish(c);
    }
    if (op < 113) {
      const uint32_t f = pick(k.faces, r);
      if (f == NONE) {
        return EditCode::BadArgument;
      }
      const LoopRef first = k.first_loop(FaceRef(f));
      const LoopRef hole = first ? k.next_loop(first) : LoopRef();
      return hole ? ed.remove_hole(FaceRef(f), hole) : EditCode::BadArgument;
    }
    if (op < 117) { /* snap a wire end onto another vertex and weld them */
      const uint32_t u = pick(k.verts, r), w = pick(k.verts, r);
      if (u == NONE || w == NONE || u == w) {
        return EditCode::BadArgument;
      }
      ed.begin("weld");
      EditCode c = ed.set_position(VertRef(u), k.pos(VertRef(w)));
      if (c == EditCode::Ok) {
        const std::pair<VertRef, VertRef> pr[1] = {{VertRef(w), VertRef(u)}};
        c = ed.weld(std::span<const std::pair<VertRef, VertRef>>(pr, 1));
      }
      return finish(c);
    }
    if (op < 120) {
      return ed.compact();
    }
    return step_curves(ed, r, n, op);
  }

  /* P4a W6 (plan §6.3): carriers on edges, curved wires, splits and joins */
  EditCode step_curves(Editor<C> &ed, litestl::test::SplitMix64 &r, uint32_t n, uint32_t op)
  {
    using test::gen::gpt;
    const auto &curve = k.edges.template get<".e.curve">();
    auto finish = [&](EditCode c) {
      if (c == EditCode::Ok) {
        return ed.commit();
      }
      ed.rollback();
      return c;
    };
    /* a curved edge, or NONE */
    auto pick_curved = [&]() {
      const uint32_t e = pick(k.edges, r);
      return e != NONE && !curve[e].none() ? e : NONE;
    };
    if (op < 124) { /* an arc carrier between an edge's endpoints */
      const uint32_t e = pick(k.edges, r);
      if (e == NONE) {
        return EditCode::BadArgument;
      }
      const double alpha = (r.below(2) ? 1 : -1) * 0.1 * double(1 + r.below(5));
      const auto vs = k.edge_verts(EdgeRef(e));
      litestl::attr::PoolRef ref;
      double s = 0;
      const EditCode c = test::arc_carrier(ed, vs[0], vs[1], alpha, &ref, &s);
      return c != EditCode::Ok ? c : ed.set_edge_curve(EdgeRef(e), ref, 0, s);
    }
    if (op < 127) { /* a clothoid wire in the air: start vertex, end at round_C(c(L)) */
      const double unit = geom::to_double3<C>(gpt<C>({1, 0, 0}))[0]; /* frame units per grid unit */
      const I3 p = {int64_t(r.range(-20, 20)) * 100, int64_t(r.range(-20, 20)) * 100, 4000 + int64_t(r.below(10)) * 100};
      const double theta = 0.25 * double(r.range(-12, 12));
      const double k0 = 0.001 * double(r.range(-4, 4)) / unit, k1 = 0.001 * double(r.range(-4, 4)) / unit;
      const double len = double(150 + 50 * r.below(6)) * unit;
      const auto pd = geom::to_double3<C>(gpt<C>(p));
      curves::Cesaro2 hz;
      if (curves::Cesaro2::clothoid(pd[0], pd[1], theta, k0, k1, len, hz)) {
        return EditCode::BadArgument;
      }
      litestl::attr::PoolRef hr, ar;
      EditCode c = ed.intern_curve(curves::cesaro2_kind(), hz, &hr);
      curves::Alignment3 al;
      if (c == EditCode::Ok && curves::Alignment3::make(k.geom, hr, curves::Fn1D::constant(pd[2], 0, len), al)) {
        return EditCode::BadArgument;
      }
      if (c == EditCode::Ok) {
        c = ed.intern_curve(curves::alignment3_kind(), al, &ar);
      }
      if (c != EditCode::Ok) {
        return c;
      }
      curves::Vec<3> x;
      curves::curve3(k.geom, ar)->eval(len, 0, &x);
      VertRef a, b;
      EdgeRef e;
      ed.begin("curved_wire");
      c = ed.make_vertex(gpt<C>(p), ident::derive(ident::tag::TestOnly, std::string_view("cw"), n, 0), &a);
      if (c == EditCode::Ok) {
        c = ed.make_vertex(test::fpt<C>(x[0], x[1], x[2]), ident::derive(ident::tag::TestOnly, std::string_view("cw"), n, 1),
                           &b);
      }
      if (c == EditCode::Ok) {
        c = ed.make_edge(a, b, ident::kNoId, &e);
      }
      if (c == EditCode::Ok) {
        /* make_edge may store the ends in either order: vs[0] is at t0 */
        const bool fwd = k.edge_verts(e)[0] == a;
        c = ed.set_edge_curve(e, ar, fwd ? 0 : len, fwd ? len : 0);
      }
      return finish(c);
    }
    if (op < 128) { /* clear a carrier */
      const uint32_t e = pick_curved();
      return e == NONE ? EditCode::BadArgument : ed.set_edge_curve(EdgeRef(e), litestl::attr::PoolRef{}, 0, 0);
    }
    if (op < 131) { /* split a curved edge at a quarter of its range */
      const uint32_t e = pick_curved();
      if (e == NONE) {
        return EditCode::BadArgument;
      }
      const auto rg = k.edges.template get<".e.range">()[e];
      const double q = 0.25 * double(1 + r.below(3));
      return ed.split_edge_at(EdgeRef(e), rg[0] + (rg[1] - rg[0]) * q);
    }
    /* join a curved edge at one of its ends */
    const uint32_t e = pick_curved();
    if (e == NONE) {
      return EditCode::BadArgument;
    }
    return ed.join_edge(k.edge_verts(EdgeRef(e))[r.below(2)]);
  }

  /** The whole run; returns the final content hash. */
  PersistentId run(uint64_t seed, uint32_t steps, PersistentId *topo, ValidateMode mode, uint32_t group = 0)
  {
    UT_CHECK(build_model(k));
    const PersistentId h0 = content_hash(k);
    Editor<C> ed(k, &j, nullptr, EditorOptions{mode});
    ed.options().verify_redo = true; /* redo checks its result against the stored hash (P3b C11) */
    litestl::test::SplitMix64 r(seed);
    /* group > 0: every `group` steps share one transaction (OnCommit runs,
     * plan P3b C12); states inside it may be invalid */
    bool in_txn = false;
    PersistentId txn_pre{};
    for (uint32_t n = 0; n < steps; n++) {
      if (group && n % group == 0) {
        UT_CHECK(ed.begin("group") == EditCode::Ok);
        in_txn = true;
        txn_pre = content_hash(k);
      }
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
      /* the editor's dirty-row index equals a rebuilt one (P3b W4) */
      if (!ed.index_consistent()) {
        UT_CHECK(false);
        fprintf(stderr, "  step %u: editor index diverged after %s\n", n, edit_code_name(c));
        return {};
      }
      /* the editor's dirty-row face-box grid equals a rebuilt one (P3b W5, C6) */
      if (!ed.grid_consistent()) {
        UT_CHECK(false);
        fprintf(stderr, "  step %u: editor grid diverged after %s\n", n, edit_code_name(c));
        return {};
      }
      /* Local and OnCommit modes: the full validator must agree after every
       * commit (inside a group transaction the state may be invalid) */
      const bool local = mode == ValidateMode::Local || mode == ValidateMode::OnCommit;
      if (!in_txn && (n % 40 == 39 || (local && c == EditCode::Ok))) {
        const ValidationReport rep = validate(k, ValidateLevel::Global);
        UT_CHECK(rep.ok());
        if (!rep.ok()) {
          fprintf(stderr, "  step %u: invalid after %s\n", n, edit_code_name(c));
          rep.print(stderr);
          return {};
        }
      }
      /* collect between transactions (plan P4a §6.3): never changes the
       * hash, and later undo / redo still find every entry they need */
      if (!in_txn && n % 50 == 49) {
        const PersistentId before = content_hash(k);
        UT_CHECK(ed.collect_geometry() == EditCode::Ok && content_hash(k) == before);
      }
      if (in_txn && (n % group == group - 1 || n + 1 == steps)) {
        in_txn = false;
        const EditCode cc = ed.commit();
        if (cc == EditCode::Ok) {
          const ValidationReport rep = validate(k, ValidateLevel::Global);
          UT_CHECK(rep.ok());
          if (!rep.ok()) {
            fprintf(stderr, "  step %u: invalid after a group commit\n", n);
            rep.print(stderr);
            return {};
          }
        }
        else {
          UT_CHECK(cc == EditCode::Invalid && content_hash(k) == txn_pre);
        }
        st.group_codes[cc]++;
      }
    }
    const PersistentId h_end = content_hash(k);
    const uint32_t applied = j.undo_count();
    for (uint32_t i = 0; i < applied; i++) {
      UT_CHECK(ed.undo() == EditCode::Ok);
    }
    UT_CHECK(content_hash(k) == h0);
    UT_CHECK(ed.index_consistent() && ed.grid_consistent());
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
      /* carriers are in frame units, which differ between coordinate types:
       * compare the structure without positions and geometry columns */
      *topo = test::structure_hash(k, true);
    }
    structure = test::structure_hash(k);
    return content_hash(k);
  }
};

uint32_t scaled(uint32_t n)
{
  const char *d = getenv("LITESTL_TEST_SCALE_DIV");
  const int div = d ? atoi(d) : 1;
  return div > 1 ? n / uint32_t(div) : n;
}

template <geom::CoordType C>
PersistentId run_one(uint64_t seed,
                     uint32_t steps,
                     PersistentId *topo,
                     bool print,
                     ValidateMode mode = ValidateMode::Full,
                     uint32_t group = 0,
                     PersistentId *structure = nullptr)
{
  Run<C> run;
  const PersistentId h = run.run(seed, steps, topo, mode, group);
  if (structure) {
    *structure = run.structure;
  }
  if (group) {
    printf("edit_random grouped (%s, seed %llu, %u per transaction): commits", geom::CoordTraits<C>::key,
           (unsigned long long)seed, group);
    for (const auto &[code, cnt] : run.st.group_codes) {
      printf(" %s=%u", edit_code_name(code), cnt);
    }
    printf("\n");
  }
  if (print) {
    printf("edit_random (%s, seed %llu): %u steps, %u committed;", geom::CoordTraits<C>::key,
           (unsigned long long)seed, run.st.steps, run.st.ok);
    for (const auto &[code, cnt] : run.st.codes) {
      printf(" %s=%u", edit_code_name(code), cnt);
    }
    printf("\n  final: %u faces, %u regions", run.k.faces.live_count(), run.k.regions.live_count());
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
    PersistentId td, tl, sd;
    const PersistentId hd = run_one<double>(seed, steps, &td, true, ValidateMode::Full, 0, &sd);
    run_one<geom::Lattice>(seed, steps, &tl, true);
    UT_CHECK(td == tl); /* same topology, IDs and attributes in both coordinate types (geometry aside) */
    {
      litestl::task::ScopedBandCount scope(4);
      PersistentId t4;
      UT_CHECK(run_one<double>(seed, steps, &t4, false) == hd);
    }
    {
      /* local validation accepts exactly what full validation accepts on this
       * sequence: the same final state, and full validity after each commit */
      PersistentId tloc;
      UT_CHECK(run_one<double>(seed, steps, &tloc, false, ValidateMode::Local) == hd);
      /* OnCommit: the step mix has multi-primitive transactions (wire,
       * remake, imprint, hole, weld) whose intermediate states Full refuses,
       * so hashes are not compared (plan P3b C12); the run checks full
       * validity after every commit instead */
      PersistentId toc;
      run_one<double>(seed, steps, &toc, false, ValidateMode::OnCommit);
      /* OnCommit with several primitives per transaction: every commit is
       * fully valid, and a refused one restores the state before it */
      PersistentId tgrp;
      run_one<double>(seed, steps, &tgrp, false, ValidateMode::OnCommit, 8);
    }
    if (steps == 600) {
      if (getenv("LITESTL_PRINT_GOLDENS")) {
        printf("golden %llu: {0x%016llxull, 0x%016llxull}\n", (unsigned long long)seed, (unsigned long long)hd.lo,
               (unsigned long long)hd.hi);
        printf("structure golden %llu: {0x%016llxull, 0x%016llxull}\n", (unsigned long long)seed,
               (unsigned long long)sd.lo, (unsigned long long)sd.hi);
      }
      /* re-pinned for P4a W6: the step mix gained curve steps (arcs,
       * clothoid wires, clearing, split_edge_at, curved joins), which changes
       * the random sequence. W5 values, for the record (the geometry store's
       * columns, structure unchanged): {0x530458424ac186a9, 0x7ca59d2d57e5d1d1},
       * {0x7cb72ae1e192ca43, 0x5d1a3663bcccaedd}, {0xfacd1364227e0117,
       * 0xb8b5e75892a3a346}. Earlier, P3b W9: imprint, unimprint, holes, weld
       * and compact joined the mix (and §3.1: region rebuilds advance the
       * generations of shells and regions that changed). Seeds 2 and 3 end
       * with hinted rooms, so their goldens cover region IDs (DEC-017). */
      const PersistentId goldens[3] = {PersistentId{0x7ce5ffd466d9054bull, 0x670cb9e60e3d5bb2ull},
                                       PersistentId{0x6c72c409e108ce3bull, 0x65dcb1c54131ecf4ull},
                                       PersistentId{0xaa60a8a7e5aa9661ull, 0xe6a3903c669fef9aull}};
      UT_CHECK(hd == goldens[seed - 1]);
      /* the structure hash leaves out the geometry columns. Pinned with the
       * format-2 code and unchanged by the P4a W5 store, it showed that W5's
       * content re-pin changed geometry columns only (plan P4a §5.4; those
       * values: {0x312bf4c14cd7cc33, 0xde5a81cf3970cca5}, {0x95af9a0f06132fff,
       * 0xae2d7fbb428b6b96}, {0x6d9b2bccf6f70c48, 0xb63b38dcdf26f9e6}).
       * Re-pinned for W6: the curve steps change the random sequence. */
      const PersistentId structure[3] = {PersistentId{0x87019c85d025cd98ull, 0x00cdbd6afe4b6663ull},
                                         PersistentId{0x918bc20da10b0c36ull, 0xadab290c82009bbcull},
                                         PersistentId{0x42aeec0fb1eab127ull, 0x3117e834d4be2d8eull}};
      UT_CHECK(sd == structure[seed - 1]);
    }
  }
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
