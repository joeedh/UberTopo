#pragma once

/* Typed element references (design 04 §1-2).
 *
 *   Ref<K>       storage index into domain K; distinct type per domain, so a
 *                Ref<Edge> never converts to a Ref<Face>. Columns store raw u32.
 *   Handle<K>    {index, generation}: detects use-after-free/compaction for
 *                references held outside the complex.
 *   ContainerRef what a vertex or wire edge is included in (design 01 §5):
 *                2-bit kind (none, face, region) + 30-bit index. */

#include <compare>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace ubertopo::ident {

/** Topological domains (named ElemKind so it doesn't clash with topo::Domain). */
enum class ElemKind : uint8_t { Vertex, Edge, Coedge, Loop, Face, FaceSide, Shell, Region };

inline constexpr uint32_t NONE = 0xffffffffu;

template <ElemKind K> struct Ref {
  static constexpr ElemKind kind = K;
  uint32_t i = NONE;

  constexpr Ref() = default;
  constexpr explicit Ref(uint32_t index) : i(index) {}

  constexpr explicit operator bool() const
  {
    return i != NONE;
  }
  constexpr bool is_none() const
  {
    return i == NONE;
  }
  friend constexpr bool operator==(Ref a, Ref b) = default;
  friend constexpr std::strong_ordering operator<=>(Ref a, Ref b) = default;
};

template <typename T>
concept ElementRef = requires { T::kind; } && std::same_as<T, Ref<T::kind>>;

using VertRef = Ref<ElemKind::Vertex>;
using EdgeRef = Ref<ElemKind::Edge>;
using CoedgeRef = Ref<ElemKind::Coedge>;
using LoopRef = Ref<ElemKind::Loop>;
using FaceRef = Ref<ElemKind::Face>;
using FaceSideRef = Ref<ElemKind::FaceSide>;
using ShellRef = Ref<ElemKind::Shell>;
using RegionRef = Ref<ElemKind::Region>;

template <ElemKind K> struct Handle {
  static constexpr ElemKind kind = K;
  uint32_t index = NONE;
  uint32_t gen = 0;

  constexpr Ref<K> ref() const
  {
    return Ref<K>(index);
  }
  friend constexpr bool operator==(Handle a, Handle b) = default;
};

enum class ContainerKind : uint8_t { None = 0, Face = 1, Region = 2 };

/** Packed container reference; the all-zero value is "none" (the column
 * default). */
struct ContainerRef {
  uint32_t bits = 0;

  static constexpr uint32_t kIndexBits = 30;
  static constexpr uint32_t kMaxIndex = (1u << kIndexBits) - 1;

  constexpr ContainerRef() = default;

  /** False if @p index does not fit in 30 bits. */
  static constexpr bool make(ContainerKind k, uint32_t index, ContainerRef &out)
  {
    if (index > kMaxIndex || k == ContainerKind::None) {
      return false;
    }
    out.bits = (uint32_t(k) << kIndexBits) | index;
    return true;
  }
  static constexpr bool face(FaceRef f, ContainerRef &out)
  {
    return make(ContainerKind::Face, f.i, out);
  }
  static constexpr bool region(RegionRef r, ContainerRef &out)
  {
    return make(ContainerKind::Region, r.i, out);
  }

  constexpr ContainerKind kind() const
  {
    return ContainerKind(bits >> kIndexBits);
  }
  constexpr uint32_t index() const
  {
    return bits & kMaxIndex;
  }
  constexpr bool is_none() const
  {
    return kind() == ContainerKind::None;
  }
  /** NONE unless this refers to a face. */
  constexpr FaceRef as_face() const
  {
    return kind() == ContainerKind::Face ? FaceRef(index()) : FaceRef();
  }
  constexpr RegionRef as_region() const
  {
    return kind() == ContainerKind::Region ? RegionRef(index()) : RegionRef();
  }
  /** Same kind, new index (used when a renumbering remaps the target). */
  constexpr bool with_index(uint32_t index, ContainerRef &out) const
  {
    return make(kind(), index, out);
  }
  friend constexpr bool operator==(ContainerRef a, ContainerRef b) = default;
};
static_assert(sizeof(ContainerRef) == 4 && std::has_unique_object_representations_v<ContainerRef>);

} // namespace ubertopo::ident
