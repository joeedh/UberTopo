#pragma once

/* Small value types and flag bits stored in topology columns (design 01 §2). */

#include <array>
#include <cstdint>

#include "ubertopo/ident/ref.h"

namespace ubertopo::topo {

using ident::NONE;

/** Reference into a geometry pool (design 05; P4). NONE means a straight
 * segment / planar face; every GeomRef is NONE in P2. */
struct GeomRef {
  uint32_t i = NONE;
  friend constexpr bool operator==(GeomRef a, GeomRef b) = default;
};
static_assert(sizeof(GeomRef) == 4 && std::has_unique_object_representations_v<GeomRef>);

using U32x2 = std::array<uint32_t, 2>;
using U32x4 = std::array<uint32_t, 4>;
inline constexpr U32x2 kNone2 = {NONE, NONE};
inline constexpr U32x4 kNone4 = {NONE, NONE, NONE, NONE};

enum Side : uint32_t { FRONT = 0, BACK = 1 };

namespace coedge_flags {
inline constexpr uint8_t Sense = 1; /* runs vs[0] -> vs[1] */
}
namespace face_flags {
inline constexpr uint8_t Virtual = 1;  /* tile seam or world box */
inline constexpr uint8_t Aperture = 2; /* fills a hole of another face */
inline constexpr uint8_t Reversed = 4; /* surface normal opposes the front side */
} // namespace face_flags
namespace region_flags {
inline constexpr uint8_t Unbounded = 1; /* index 0 only */
inline constexpr uint8_t Virtual = 2;
} // namespace region_flags

/** Disk-cycle link: (edge << 1) | slot, slot = which endpoint of that edge. */
constexpr uint32_t disk_pack(uint32_t edge, uint32_t slot)
{
  return (edge << 1) | slot;
}
constexpr uint32_t disk_edge(uint32_t packed)
{
  return packed >> 1;
}
constexpr uint32_t disk_slot(uint32_t packed)
{
  return packed & 1u;
}

} // namespace ubertopo::topo
