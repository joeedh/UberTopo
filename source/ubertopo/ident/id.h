#pragma once

/* Persistent IDs (design 04 §3, DEC-009).
 *
 * A PersistentId is a 128-bit SipHash-2-4 tuple hash (litestl Hasher128).
 * Every derivation starts with an UberTopo DomainTag, so generator-emitted,
 * builder-derived, region and operator-derived IDs never share a space, and
 * every field is type-tagged. The outputs are part of the determinism
 * contract: the goldens in test_ident.cc pin them. */

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "litestl/util/hash128.h"

namespace ubertopo::ident {

using PersistentId = litestl::hash::Hash128;
using litestl::hash::DomainTag;
using litestl::hash::Hasher128;

/** The null ID (never produced by a derivation, except with negligible
 * probability). */
inline constexpr PersistentId kNoId{};

/** UberTopo domain tags (>= 256; litestl reserves the values below). */
namespace tag {
inline constexpr DomainTag GeneratorEmit = DomainTag(256);
inline constexpr DomainTag BuilderEdge = DomainTag(257);
inline constexpr DomainTag BuilderLoop = DomainTag(258);
inline constexpr DomainTag Region = DomainTag(259);
inline constexpr DomainTag OpDerived = DomainTag(260);
inline constexpr DomainTag TestOnly = DomainTag(261);
inline constexpr DomainTag FaceSide = DomainTag(262);
/* CSG results (csg/) */
inline constexpr DomainTag BoolTree = DomainTag(263);
inline constexpr DomainTag BoolLabel = DomainTag(264);
inline constexpr DomainTag BoolVertex = DomainTag(265);
inline constexpr DomainTag BoolFace = DomainTag(266);
inline constexpr DomainTag BoolRegion = DomainTag(267);
} // namespace tag

namespace detail {
inline void put(Hasher128 &h, PersistentId v)
{
  h.h128(v);
}
inline void put(Hasher128 &h, std::string_view v)
{
  h.str(v);
}
inline void put(Hasher128 &h, const char *v)
{
  h.str(std::string_view(v));
}
inline void put(Hasher128 &h, double v)
{
  h.f64_canonical(v);
}
inline void put(Hasher128 &h, bool v)
{
  h.boolean(v);
}
template <typename T>
  requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
void put(Hasher128 &h, T v)
{
  if constexpr (std::is_signed_v<T>) {
    if constexpr (sizeof(T) <= 4) {
      h.i32(int32_t(v));
    }
    else {
      h.i64(int64_t(v));
    }
  }
  else {
    if constexpr (sizeof(T) <= 4) {
      h.u32(uint32_t(v));
    }
    else {
      h.u64(uint64_t(v));
    }
  }
}
} // namespace detail

/** Typed tuple hash under @p domain: H128(fields...). */
template <typename... Fields> PersistentId derive(DomainTag domain, const Fields &...fields)
{
  Hasher128 h(domain);
  (detail::put(h, fields), ...);
  return h.finish();
}

/** Generator-emitted ID: H128(parent, op_site, discriminators...). */
template <typename... Disc>
PersistentId derive_generator(PersistentId parent, std::string_view op_site, const Disc &...disc)
{
  return derive(tag::GeneratorEmit, parent, op_site, disc...);
}

/** Edge between two vertices: independent of direction and of storage index. */
inline PersistentId edge_id(PersistentId va, PersistentId vb)
{
  return va < vb ? derive(tag::BuilderEdge, va, vb) : derive(tag::BuilderEdge, vb, va);
}

/** Loop @p ordinal of a face, in the order the face's loops were given. */
inline PersistentId loop_id(PersistentId face, uint32_t ordinal)
{
  return derive(tag::BuilderLoop, face, ordinal);
}

/** Identity of a face side, (face id, side) with side 0 = front, 1 = back. */
inline PersistentId face_side_id(PersistentId face, uint32_t side)
{
  return derive(tag::FaceSide, face, side);
}

/** The unbounded region (region 0) of every complex. */
inline PersistentId unbounded_region_id()
{
  return derive(tag::Region, std::string_view("unbounded"));
}

/** A region named by a generator hint. The minimal face-side ID keeps it
 * unique when two disconnected regions carry the same hint. */
inline PersistentId region_id(PersistentId hint, PersistentId min_face_side)
{
  return derive(tag::Region, std::string_view("hint"), hint, min_face_side);
}

/** A region named by a hint that no other region wins (DEC-017): stable
 * under edits that keep the region's winning hint. */
inline PersistentId region_id_from_hint(PersistentId hint)
{
  return derive(tag::Region, std::string_view("hint_only"), hint);
}

/** A region without a hint, named by its minimal face-side ID. */
inline PersistentId region_id_from_sides(PersistentId min_face_side)
{
  return derive(tag::Region, std::string_view("sides"), min_face_side);
}

} // namespace ubertopo::ident
