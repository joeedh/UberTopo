#pragma once

/* Builtin columns of every topological domain, as compile-time schemas
 * (design 01 §2, DEC-012; plan P2 W4).
 *
 * Flags: every link column is Topo — hashed, serialised, never mixed. The
 * design's "D" (derived) marker means *recomputable from the T columns*; it
 * is not litestl's Derived flag, which is reserved for freeze caches (those
 * are excluded from hashes and bytes). Shared bookkeeping columns use the
 * same name in every domain: ".id" (persistent ID), ".gen" (generation),
 * ".live" (slot in use). */

#include "litestl/attr/bool_column.h"
#include "litestl/attr/schema.h"
#include "ubertopo/geom/coord.h"
#include "ubertopo/ident/id.h"
#include "ubertopo/topo/types.h"

/* Value keys for UberTopo column value types (part of the serialised format). */
namespace litestl::attr {
template <> struct ValueKey<ubertopo::geom::Lattice> {
  static std::string key()
  {
    return "lat";
  }
};
template <> struct ValueKey<ubertopo::ident::ContainerRef> {
  static std::string key()
  {
    return "cref";
  }
};
template <> struct ValueKey<ubertopo::topo::LegacyGeomRef> {
  static std::string key()
  {
    return "gref";
  }
};
} // namespace litestl::attr

namespace ubertopo::topo {

namespace attr = litestl::attr;
using attr::AttrDef;
using attr::ColumnFlags;
using attr::Schema;

template <litestl::util::StrLiteral Name>
struct Link : AttrDef<Name, attr::Column<uint32_t>, ColumnFlags::Topo> {
  static uint32_t default_value()
  {
    return NONE;
  }
};
template <litestl::util::StrLiteral Name>
struct Link2 : AttrDef<Name, attr::Column<U32x2>, ColumnFlags::Topo> {
  static U32x2 default_value()
  {
    return kNone2;
  }
};
template <litestl::util::StrLiteral Name>
struct Link4 : AttrDef<Name, attr::Column<U32x4>, ColumnFlags::Topo> {
  static U32x4 default_value()
  {
    return kNone4;
  }
};
template <litestl::util::StrLiteral Name>
struct Flags8 : AttrDef<Name, attr::Column<uint8_t>, ColumnFlags::Topo> {};
template <litestl::util::StrLiteral Name>
struct Container : AttrDef<Name, attr::Column<ident::ContainerRef>, ColumnFlags::Topo> {};
/** A geometry store ref (plan P4a §4.1); kNoRef = straight / planar. */
template <litestl::util::StrLiteral Name>
struct Geom : AttrDef<Name, attr::Column<GeomRef>, ColumnFlags::Topo> {};
/** Formats 1 and 2 (io_legacy.cc only). */
template <litestl::util::StrLiteral Name>
struct LegacyGeom : AttrDef<Name, attr::Column<LegacyGeomRef>, ColumnFlags::Topo> {};

/** The parameter range [t0, t1] of .e.curve the edge uses (P4a), oriented
 * from vs[0] to vs[1]; (0, 0) while the edge is straight. */
struct EdgeRange : AttrDef<".e.range", attr::Column<litestl::math::double2>, ColumnFlags::Topo> {
  static litestl::math::double2 default_value()
  {
    return litestl::math::double2(0.0, 0.0);
  }
};
namespace edge_geom_flags {
inline constexpr uint8_t PcurvePrimary = 1; /* design 05 §1; always 0 in P4a */
}
/** The tolerance of curve endpoint checks (plan P4a §5.3). */
struct CurveTol : AttrDef<".g.curve_tol", attr::Column<double>, ColumnFlags::Topo> {
  static double default_value()
  {
    return 0x1p-30;
  }
};

using Id = AttrDef<".id", attr::Column<ident::PersistentId>, ColumnFlags::Topo>;
using Gen = AttrDef<".gen", attr::Column<uint32_t>, ColumnFlags::Topo>;
using Live = AttrDef<".live", attr::BoolColumn, ColumnFlags::Topo>;

template <geom::CoordType C> using Pos = AttrDef<"pos", attr::Column<geom::Point<C>>>;

/** A region container named by a generator hint, resolved into ".v.container"
 * / ".e.container" by build_regions() (region indices don't exist at build
 * time; plan P2 W6). kNoId when there is none. */
template <litestl::util::StrLiteral Name>
struct ContainerHintId : AttrDef<Name, attr::Column<ident::PersistentId>, ColumnFlags::Topo> {};

template <geom::CoordType C>
using VertexSchema =
    Schema<Pos<C>, Link<".v.e">, Container<".v.container">, ContainerHintId<".v.container_hint">, Id, Gen, Live>;

using EdgeSchema = Schema<Link2<".e.vs">,
                          Link4<".e.disk">,
                          Link<".e.c">,
                          Container<".e.container">,
                          ContainerHintId<".e.container_hint">,
                          Geom<".e.curve">,
                          EdgeRange,
                          Flags8<".e.geom_flags">,
                          Id,
                          Gen,
                          Live>;

using CoedgeSchema = Schema<Link<".c.e">,
                            Link<".c.v">,
                            Link<".c.l">,
                            Link<".c.next">,
                            Link<".c.prev">,
                            Link<".c.radial_next">,
                            Link<".c.radial_prev">,
                            Flags8<".c.flags">,
                            Geom<".c.pcurve">,
                            Gen,
                            Live>;

struct LoopSize : AttrDef<".l.size", attr::Column<uint32_t>, ColumnFlags::Topo> {};
using LoopSchema = Schema<Link<".l.c">, Link<".l.f">, Link<".l.next">, LoopSize, Id, Gen, Live>;

using FaceSchema = Schema<Link<".f.l">,
                          Link2<".f.region">,
                          Link2<".f.shell">,
                          Geom<".f.surface">,
                          Flags8<".f.flags">,
                          Id,
                          Gen,
                          Live>;

/** Face sides: 2 rows per face, row 2f + side. Sized and permuted only
 * together with the face domain (FaceDomain). */
struct SideHint : AttrDef<".fs.hint", attr::Column<ident::PersistentId>, ColumnFlags::Topo> {};
using FaceSideSchema = Schema<Link<".fs.shell_next">, SideHint>;

using ShellSchema = Schema<Link<".s.region">, Link<".s.first">, Link<".s.next">, Gen, Live>;

using RegionSchema = Schema<Link<".r.shell">, Flags8<".r.flags">, Id, Gen, Live>;

/** Complex-wide values (a group of size 1). ".g.allows_projectable": the
 * complex was built with the projectable-faces opt-in (amendment A1); the
 * validator rejects projected faces without it. */
using AllowsProjectable = AttrDef<".g.allows_projectable", attr::BoolColumn, ColumnFlags::Topo>;
using GlobalSchema = Schema<AllowsProjectable, CurveTol>;

/** Registers every UberTopo column type with litestl's registry (needed
 * before reading a stream in a process that hasn't created such columns). */
void register_column_types();

} // namespace ubertopo::topo
