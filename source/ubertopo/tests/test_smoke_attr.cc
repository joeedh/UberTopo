/* Smoke test: UberTopo builds against litestl::attr (P1) and can use groups
 * with builtin slots, snapshots, mixing, hashing and serialization. */

#include "litestl/attr/group.h"
#include "litestl/attr/schema.h"

#include "ut_test.h"

#include <vector>

using namespace litestl;
using namespace litestl::attr;

/* Compile-time schema (DEC-012): names resolved at compile time. */
using SmokePos = AttrDef<"pos", Column<math::double3>>;
struct SmokeVertEdge : AttrDef<".v.e", Column<uint32_t>, ColumnFlags::Topo> {
  static uint32_t default_value()
  {
    return 0xffffffffu;
  }
};
using SmokeVertexSchema = Schema<SmokePos, SmokeVertEdge>;
static_assert(TypedGroup<SmokeVertexSchema>::slot<".v.e">() == 1);

static void run_schema()
{
  TypedGroup<SmokeVertexSchema> v;
  v.resize(3);
  v.get<"pos">().set(1, math::double3(1, 2, 3));
  UT_CHECK(v.get<SmokeVertEdge>()[2] == 0xffffffffu && v.get<"pos">()[1][2] == 3.0);
}

static void run()
{
  run_schema();
  AttrGroup verts;
  verts.add_builtin<Column<math::double3>>("pos");
  verts.add_builtin<Column<uint32_t>>(".v.e", 0xffffffffu, ColumnFlags::Topo);
  verts.resize(4);
  verts.builtin<Column<math::double3>>(0).set(1, math::double3(1, 0, 0));
  verts.builtin<Column<math::double3>>(0).set(2, math::double3(3, 0, 0));

  AttrGroup snapshot = verts;

  /* new vertex 3 at the midpoint of 1 and 2 */
  std::vector<uint32_t> targets = {3}, offsets = {0, 2}, sources = {1, 2};
  std::vector<double> weights = {0.5, 0.5};
  const char *err = nullptr;
  UT_CHECK(verts.mix(MixBatch{targets, offsets, sources, weights}, nullptr, nullptr, &err));
  UT_CHECK(verts.builtin<Column<math::double3>>(0)[3][0] == 2.0);
  UT_CHECK(verts.builtin<Column<uint32_t>>(1)[3] == 0xffffffffu); /* Topo column not mixed */
  UT_CHECK(snapshot.builtin<Column<math::double3>>(0)[3][0] == 0.0); /* snapshot unchanged */
  UT_CHECK(snapshot.content_hash() != verts.content_hash());

  io::ByteWriter w;
  verts.write(w);
  AttrGroup back;
  back.add_builtin<Column<math::double3>>("pos");
  back.add_builtin<Column<uint32_t>>(".v.e", 0xffffffffu, ColumnFlags::Topo);
  io::ByteReader r(w.data(), w.size());
  UT_CHECK(back.read(r, {}, &err));
  UT_CHECK(back.content_hash() == verts.content_hash());
}

int main()
{
  const size_t baseline = alloc::getMemorySize();
  run();
  UT_CHECK(alloc::getMemorySize() == baseline);
  return ut_test_end();
}
