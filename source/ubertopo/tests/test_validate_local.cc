/* The local combinatorial layer (plan P3b W5, C9): corruptions reachable only
 * through the one-ring of a filter like the editor's (one face and its
 * edges, no vertices) are still found. */

#include <cstdio>
#include <vector>

#include "support/edit_fixture.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using VC = ValidateCode;

namespace {

/* IDs counted by a scan (tests only; the editor uses its index) */
template <geom::CoordType C> struct ScanIds final : IdOracle {
  const Complex<C> *k;
  uint32_t count(ident::ElemKind kind, ident::PersistentId id) const override
  {
    uint32_t n = 0;
    auto scan = [&](const auto &d) {
      d.for_each_live([&](uint32_t i) { n += d.template get<".id">()[i] == id; });
    };
    if (kind == ident::ElemKind::Vertex) {
      scan(k->verts);
    }
    else if (kind == ident::ElemKind::Edge) {
      scan(k->edges);
    }
    else {
      scan(k->faces);
    }
    return n;
  }
};

/* the editor's filter for a primitive that touched only face f */
template <geom::CoordType C> ValidateFilter face_filter(const Complex<C> &k, uint32_t f)
{
  ValidateFilter flt;
  flt.faces.append(f);
  k.for_each_loop(FaceRef(f), [&](LoopRef l) { k.for_each_coedge(l, [&](CoedgeRef c) { flt.edges.append(k.edge_of(c).i); }); });
  std::sort(flt.edges.begin(), flt.edges.end());
  flt.edges.resize(int(std::unique(flt.edges.begin(), flt.edges.end()) - flt.edges.begin()));
  return flt;
}

template <geom::CoordType C> ValidationReport local_comb(const Complex<C> &k, const ValidateFilter &flt)
{
  ScanIds<C> ids;
  ids.k = &k;
  ValidateFilter f = flt;
  f.ids = &ids;
  ValidationReport rep;
  const VertexTopoView v{k.verts.template get<".v.e">(), k.verts.template get<".v.container">(),
                         k.verts.template get<".id">(), k.verts.template get<".live">(), k.verts.capacity()};
  validate_combinatorial_local(k, v, f, rep);
  return rep;
}

template <geom::CoordType C>
void expect_local(const Complex<C> &k, uint32_t f, VC code, const char *what)
{
  const ValidationReport rep = local_comb(k, face_filter(k, f));
  if (rep.count(code) == 0) {
    fprintf(stderr, "%s: %s not found locally; got: ", what, validate_code_name(code));
    rep.print(stderr);
  }
  UT_CHECK(rep.count(code) > 0);
}

template <geom::CoordType C> void test_one_ring()
{
  /* three boxes in an L: some edges carry three faces */
  test::GridBuilder<C> g;
  g.box({0, 0, 0}, {4, 4, 4});
  g.box({4, 0, 0}, {8, 4, 4});
  g.box({0, 4, 0}, {4, 8, 4});
  Complex<C> base;
  BuildReport br;
  UT_CHECK(g.b.build(base, br) && validate(base).ok());
  /* no false positives on the valid complex, for every face */
  base.faces.for_each_live([&](uint32_t f) { UT_CHECK(local_comb(base, face_filter(base, f)).ok()); });

  /* a reversed three-face radial cycle: consistent links, non-planar vertex
   * links; listed: one face at that edge */
  {
    Complex<C> x = base;
    uint32_t f = NONE;
    for (uint32_t e = 0; e < x.edges.capacity() && f == NONE; e++) {
      std::vector<uint32_t> cyc;
      x.for_each_radial(EdgeRef(e), [&](CoedgeRef c) { cyc.push_back(c.i); });
      if (cyc.size() == 3) {
        for (size_t i = 0; i < 3; i++) {
          x.coedges.template get<".c.radial_next">().set(cyc[i], cyc[(i + 2) % 3]);
          x.coedges.template get<".c.radial_prev">().set(cyc[i], cyc[(i + 1) % 3]);
        }
        f = x.face_of(CoedgeRef(cyc[0])).i;
      }
    }
    UT_CHECK(f != NONE);
    expect_local(x, f, VC::VertexLink, "reversed radial cycle");
  }
  /* a corner of the listed face loses its disk entry (.v.e): only the
   * one-ring (the ends of the face's edges) reaches that vertex */
  {
    Complex<C> x = base;
    const uint32_t f = 0;
    const VertRef v = x.start_vert(x.first_coedge(x.first_loop(FaceRef(f))));
    x.verts.template get<".v.e">().set(v.i, NONE);
    expect_local(x, f, VC::VertexEdge, "corner without .v.e");
  }
  /* a neighbour face's coedge on one of the listed face's edges flips its
   * sense: its start vertex no longer matches */
  {
    Complex<C> x = base;
    const uint32_t f = 0;
    const CoedgeRef c0 = x.first_coedge(x.first_loop(FaceRef(f)));
    CoedgeRef other;
    x.for_each_radial(x.edge_of(c0), [&](CoedgeRef c) {
      if (x.face_of(c).i != f) {
        other = c;
      }
    });
    UT_CHECK(bool(other));
    x.coedges.template get<".c.flags">().set(other.i, uint8_t(x.coedges.template get<".c.flags">()[other.i] ^
                                                              coedge_flags::Sense));
    expect_local(x, f, VC::CoedgeStartVertex, "neighbour coedge sense");
  }
  /* a corner's ID duplicates another vertex's: found through the oracle */
  {
    Complex<C> x = base;
    const uint32_t f = 0;
    const VertRef v = x.start_vert(x.first_coedge(x.first_loop(FaceRef(f))));
    uint32_t other = NONE;
    x.verts.for_each_live([&](uint32_t i) {
      if (i != v.i && other == NONE) {
        other = i;
      }
    });
    x.verts.template get<".id">().set(v.i, x.verts.template get<".id">()[other]);
    expect_local(x, f, VC::DuplicateId, "duplicate vertex ID");
  }
}

} // namespace

int main()
{
  topo::register_column_types();
  test_one_ring<double>();
  test_one_ring<geom::Lattice>();
  return ut_test_end();
}
