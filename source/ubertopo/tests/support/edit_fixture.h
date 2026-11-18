#pragma once

/* Shared fixture of the editor unit tests (test_edit_primitives*.cc). */

#include <cstdio>

#include "ubertopo/topo/builder.h"
#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

#include "topo_fixtures.h"
#include "../ut_test.h"

namespace ubertopo::test {

using namespace ubertopo::topo;

/* checks an editor call's code; on mismatch prints the editor's report */
#define EXPECT_EDIT(ed, call, want)                                                       \
  do {                                                                                    \
    const EditCode got_ = (call);                                                         \
    UT_CHECK(got_ == (want));                                                             \
    if (got_ != (want)) {                                                                 \
      fprintf(stderr, "  -> got %s (%s), index %u\n", edit_code_name(got_),               \
              (ed).report().detail ? (ed).report().detail : "", (ed).report().index);     \
      if (got_ == EditCode::Invalid) {                                                    \
        (ed).report().validation.print(stderr);                                           \
      }                                                                                   \
    }                                                                                     \
  } while (0)

template <geom::CoordType C> struct Fixture {
  Complex<C> k;
  Journal<C> j;

  /** A box [0, 4]^3 (grid units), regions built. */
  bool box(I3 lo = {0, 0, 0}, I3 hi = {4, 4, 4})
  {
    test::GridBuilder<C> g;
    g.box(lo, hi);
    BuildReport br;
    RegionReport rr;
    return g.b.build(k, br) && build_regions(k, rr);
  }
  VertRef vert_at(const I3 &p) const
  {
    const auto q = pt<C>(p);
    VertRef out;
    k.verts.for_each_live([&](uint32_t v) {
      if (geom::lex_compare<C>(k.pos(VertRef(v)), q) == 0) {
        out = VertRef(v);
      }
    });
    return out;
  }
  EdgeRef edge_between(const I3 &a, const I3 &b) const
  {
    const VertRef va = vert_at(a), vb = vert_at(b);
    EdgeRef out;
    k.edges.for_each_live([&](uint32_t e) {
      const auto vs = k.edge_verts(EdgeRef(e));
      if ((vs[0] == va && vs[1] == vb) || (vs[0] == vb && vs[1] == va)) {
        out = EdgeRef(e);
      }
    });
    return out;
  }
  /** The face whose vertices include all of @p ps. */
  FaceRef face_with(std::initializer_list<I3> ps) const
  {
    FaceRef out;
    k.faces.for_each_live([&](uint32_t f) {
      uint32_t hit = 0;
      k.for_each_loop(FaceRef(f), [&](LoopRef l) {
        k.for_each_coedge(l, [&](CoedgeRef c) {
          for (const I3 &p : ps) {
            hit += k.start_vert(c) == vert_at(p);
          }
        });
      });
      if (hit == ps.size()) {
        out = FaceRef(f);
      }
    });
    return out;
  }
  bool valid() const
  {
    const ValidationReport r = validate(k, ValidateLevel::Global);
    if (!r.ok()) {
      r.print(stderr);
    }
    return r.ok();
  }
};

template <geom::CoordType C> ident::PersistentId h(const Complex<C> &k)
{
  return content_hash(k);
}

} // namespace ubertopo::test
