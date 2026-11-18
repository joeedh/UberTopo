/* ident/: persistent-ID derivation goldens, typed refs, ContainerRef (plan P2 W3). */

#include "ubertopo/ident/id.h"
#include "ubertopo/ident/ref.h"

#include "ut_test.h"

#include <cinttypes>
#include <cstdlib>

using namespace ubertopo::ident;

static bool print_goldens()
{
  return getenv("LITESTL_PRINT_GOLDENS") != nullptr;
}

static void golden(const char *name, PersistentId got, PersistentId expect)
{
  if (print_goldens()) {
    printf("  {\"%s\", {0x%016" PRIx64 "ULL, 0x%016" PRIx64 "ULL}},\n", name, got.lo, got.hi);
    return;
  }
  if (got != expect) {
    fprintf(stderr, "golden %s changed: {0x%016" PRIx64 ", 0x%016" PRIx64 "}\n", name, got.lo, got.hi);
  }
  UT_CHECK(got == expect);
}

static void test_derivations()
{
  const PersistentId root = derive(tag::TestOnly, std::string_view("root"));
  const PersistentId va = derive_generator(root, "v", int64_t(1), int64_t(2), int64_t(3));
  const PersistentId vb = derive_generator(root, "v", int64_t(1), int64_t(2), int64_t(4));
  const PersistentId face = derive_generator(root, "face", uint32_t(7));

  /* determinism contract: pinned values (change only deliberately) */
  golden("root", root, {0x202b7892fc23428cULL, 0x810f7fd5520102efULL});
  golden("vertex_a", va, {0xb858defd3c251eeeULL, 0x80cc1a49b821e0fcULL});
  golden("edge_ab", edge_id(va, vb), {0x33a75d76626f21c1ULL, 0xb81f9a59d85c034eULL});
  golden("loop_1", loop_id(face, 1), {0x5198b505d9adfde9ULL, 0x2e53528467d29a43ULL});
  golden("face_side_1", face_side_id(face, 1), {0x2eea6d2d0b887bd1ULL, 0xf24d83ccb47cb229ULL});
  golden("unbounded", unbounded_region_id(), {0x68b0cf3cb661518cULL, 0x5b4ee136977fbb5aULL});
  golden("region_hint", region_id(va, vb), {0xbde84aa2ba5c31d7ULL, 0x7d9e4e017e4dcba3ULL});
  golden("region_sides", region_id_from_sides(vb), {0xa073fb2bbf97775aULL, 0xb276b66a3d6fd217ULL});

  /* structural properties */
  UT_CHECK(edge_id(va, vb) == edge_id(vb, va));
  UT_CHECK(va != vb && va != kNoId);
  UT_CHECK(loop_id(face, 0) != loop_id(face, 1));
  UT_CHECK(face_side_id(face, 0) != face_side_id(face, 1));
  UT_CHECK(region_id(va, vb) != region_id(va, va));
  UT_CHECK(region_id_from_sides(vb) != region_id(vb, vb));
  /* type tags: u32 1 and u64 1 are different fields */
  UT_CHECK(derive_generator(root, "x", uint32_t(1)) != derive_generator(root, "x", uint64_t(1)));
  /* domain separation */
  UT_CHECK(derive(tag::GeneratorEmit, root) != derive(tag::OpDerived, root));
  /* canonical doubles: -0 == +0 */
  UT_CHECK(derive_generator(root, "d", -0.0) == derive_generator(root, "d", 0.0));
}

static void test_refs()
{
  static_assert(!std::is_convertible_v<uint32_t, VertRef>);
  static_assert(!std::is_convertible_v<EdgeRef, FaceRef>);
  static_assert(!std::is_constructible_v<FaceRef, EdgeRef>);
  static_assert(ElementRef<EdgeRef> && !ElementRef<uint32_t>);
  constexpr VertRef none;
  static_assert(!none && none.is_none() && VertRef(3) && VertRef(3) < VertRef(4));
  Handle<ElemKind::Face> h{5, 9};
  UT_CHECK(h.ref() == FaceRef(5));
}

static void test_container_ref()
{
  ContainerRef c;
  UT_CHECK(c.is_none() && c.bits == 0 && c.as_face().is_none());
  UT_CHECK(ContainerRef::face(FaceRef(12345), c) && c.kind() == ContainerKind::Face && c.as_face() == FaceRef(12345) &&
           c.as_region().is_none());
  UT_CHECK(ContainerRef::region(RegionRef(ContainerRef::kMaxIndex), c) && c.as_region().i == ContainerRef::kMaxIndex);
  ContainerRef d = c;
  UT_CHECK(!ContainerRef::region(RegionRef(ContainerRef::kMaxIndex + 1), d) && d == c); /* overflow refused */
  UT_CHECK(!ContainerRef::make(ContainerKind::None, 3, d));
  ContainerRef e;
  UT_CHECK(c.with_index(7, e) && e.kind() == ContainerKind::Region && e.index() == 7);
  for (uint32_t i : {0u, 1u, 77u, 1u << 29, ContainerRef::kMaxIndex}) {
    UT_CHECK(ContainerRef::face(FaceRef(i), d) && d.index() == i && d.kind() == ContainerKind::Face);
  }
}

int main()
{
  test_derivations();
  test_refs();
  test_container_ref();
  return ut_test_end();
}
