#pragma once

/* Index remapping after a permutation (plan P2 W10, review 4).
 *
 * AttrGroup::permute moves rows but doesn't rewrite the indices stored in
 * them. Every column of every topology schema has a RemapRule saying what
 * its values index; schema_covered<S> is checked by static_assert, so a new
 * column without a rule is a compile error. */

#include <cstdint>
#include <cstring>
#include <span>

#include "litestl/util/vector.h"
#include "ubertopo/topo/schema.h"

namespace ubertopo::topo {

enum class Remap : uint8_t {
  None,       /* not an index (IDs, flags, generations, hints, geometry refs in P2) */
  Vertex,
  Edge,
  Coedge,
  Loop,
  Face,
  FaceSide,
  Shell,
  Region,
  DiskPacked, /* (edge << 1) | slot */
  Container,  /* ContainerRef: face or region index by kind */
};

template <typename Def> struct RemapRule; /* primary undefined: every column needs a rule */

#define UT_REMAP(Def, kind)                                                                      \
  template <> struct RemapRule<Def> {                                                            \
    static constexpr Remap value = Remap::kind;                                                  \
  };

UT_REMAP(Id, None)
UT_REMAP(Gen, None)
UT_REMAP(Live, None)
template <typename Col> struct RemapRule<AttrDef<"pos", Col>> {
  static constexpr Remap value = Remap::None;
};
/* vertex */
UT_REMAP(Link<".v.e">, Edge)
UT_REMAP(Container<".v.container">, Container)
UT_REMAP(ContainerHintId<".v.container_hint">, None)
/* edge */
UT_REMAP(Link2<".e.vs">, Vertex)
UT_REMAP(Link4<".e.disk">, DiskPacked)
UT_REMAP(Link<".e.c">, Coedge)
UT_REMAP(Container<".e.container">, Container)
UT_REMAP(ContainerHintId<".e.container_hint">, None)
UT_REMAP(Geom<".e.curve">, None)
/* coedge */
UT_REMAP(Link<".c.e">, Edge)
UT_REMAP(Link<".c.v">, Vertex)
UT_REMAP(Link<".c.l">, Loop)
UT_REMAP(Link<".c.next">, Coedge)
UT_REMAP(Link<".c.prev">, Coedge)
UT_REMAP(Link<".c.radial_next">, Coedge)
UT_REMAP(Link<".c.radial_prev">, Coedge)
UT_REMAP(Flags8<".c.flags">, None)
UT_REMAP(Geom<".c.pcurve">, None)
/* loop */
UT_REMAP(Link<".l.c">, Coedge)
UT_REMAP(Link<".l.f">, Face)
UT_REMAP(Link<".l.next">, Loop)
UT_REMAP(LoopSize, None)
/* face */
UT_REMAP(Link<".f.l">, Loop)
UT_REMAP(Link2<".f.region">, Region)
UT_REMAP(Link2<".f.shell">, Shell)
UT_REMAP(Geom<".f.surface">, None)
UT_REMAP(Flags8<".f.flags">, None)
/* face side */
UT_REMAP(Link<".fs.shell_next">, FaceSide)
UT_REMAP(SideHint, None)
/* shell */
UT_REMAP(Link<".s.region">, Region)
UT_REMAP(Link<".s.first">, FaceSide)
UT_REMAP(Link<".s.next">, Shell)
/* region */
UT_REMAP(Link<".r.shell">, Shell)
UT_REMAP(Flags8<".r.flags">, None)
/* global */
UT_REMAP(AllowsProjectable, None)
#undef UT_REMAP

template <typename S> struct SchemaCovered;
template <typename... D> struct SchemaCovered<Schema<D...>> {
  static constexpr bool value = (requires { RemapRule<D>::value; } && ...);
};
template <typename S> constexpr bool schema_covered = SchemaCovered<S>::value;

static_assert(schema_covered<VertexSchema<geom::Lattice>> && schema_covered<VertexSchema<double>> &&
                  schema_covered<VertexSchema<float>>,
              "a vertex column has no RemapRule");
static_assert(schema_covered<EdgeSchema>, "an edge column has no RemapRule");
static_assert(schema_covered<CoedgeSchema>, "a coedge column has no RemapRule");
static_assert(schema_covered<LoopSchema>, "a loop column has no RemapRule");
static_assert(schema_covered<FaceSchema>, "a face column has no RemapRule");
static_assert(schema_covered<FaceSideSchema>, "a face-side column has no RemapRule");
static_assert(schema_covered<ShellSchema>, "a shell column has no RemapRule");
static_assert(schema_covered<RegionSchema>, "a region column has no RemapRule");
static_assert(schema_covered<GlobalSchema>, "a global column has no RemapRule");

/** Old-to-new index maps (attr::ColumnBase::kDropped for removed slots). */
struct RemapMaps {
  litestl::util::Vector<uint32_t> vertex, edge, coedge, loop, face, face_side, shell, region;

  std::span<const uint32_t> of(Remap r) const
  {
    const litestl::util::Vector<uint32_t> *v = nullptr;
    switch (r) {
      case Remap::Vertex:
        v = &vertex;
        break;
      case Remap::Edge:
      case Remap::DiskPacked:
        v = &edge;
        break;
      case Remap::Coedge:
        v = &coedge;
        break;
      case Remap::Loop:
        v = &loop;
        break;
      case Remap::Face:
        v = &face;
        break;
      case Remap::FaceSide:
        v = &face_side;
        break;
      case Remap::Shell:
        v = &shell;
        break;
      case Remap::Region:
        v = &region;
        break;
      default:
        return {};
    }
    return std::span<const uint32_t>(v->data(), v->size());
  }
};

namespace detail {
inline uint32_t map1(std::span<const uint32_t> m, uint32_t x)
{
  return (x == NONE || x >= m.size()) ? x : m[x];
}

template <typename Def, typename G> void remap_column(G &g, const RemapMaps &maps)
{
  constexpr Remap rule = RemapRule<Def>::value;
  if constexpr (rule != Remap::None) {
    auto &col = g.template get<Def>();
    using T = typename Def::value_type;
    const uint32_t n = col.size();
    for (uint32_t i = 0; i < n; i++) {
      T v = col[i];
      const T old = v;
      if constexpr (rule == Remap::Container) {
        if (v.kind() == ident::ContainerKind::Face) {
          v.with_index(map1(maps.of(Remap::Face), v.index()), v);
        }
        else if (v.kind() == ident::ContainerKind::Region) {
          v.with_index(map1(maps.of(Remap::Region), v.index()), v);
        }
      }
      else if constexpr (rule == Remap::DiskPacked) {
        const auto m = maps.of(rule);
        for (auto &x : v) {
          if (x != NONE) {
            x = disk_pack(map1(m, disk_edge(x)), disk_slot(x));
          }
        }
      }
      else if constexpr (std::is_same_v<T, uint32_t>) {
        v = map1(maps.of(rule), v);
      }
      else {
        const auto m = maps.of(rule);
        for (auto &x : v) {
          x = map1(m, x);
        }
      }
      if (std::memcmp(&v, &old, sizeof(T)) != 0) {
        col.set(i, v);
      }
    }
  }
}

template <typename G, typename... D> void remap_all(G &g, const RemapMaps &maps, Schema<D...> *)
{
  (remap_column<D>(g, maps), ...);
}
} // namespace detail

/** Rewrites every index-valued column of a typed group through @p maps. */
template <typename S, typename G> void remap_group(G &g, const RemapMaps &maps)
{
  detail::remap_all(g, maps, static_cast<S *>(nullptr));
}

} // namespace ubertopo::topo
