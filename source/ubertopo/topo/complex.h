#pragma once

/* The cell complex (design 01; plan P2 W4).
 *
 * ComplexBase holds every domain whose columns don't depend on the coordinate
 * type, plus navigation over them (non-template, R7). Complex<C> adds the
 * vertex domain, the only one with a Coord-typed column ("pos").
 *
 * Value semantics: copying a complex is a snapshot (copy-on-write columns).
 * All link access goes through get<"name">() or the accessors below, never
 * through raw slot indices (DEC-012). */

#include <array>
#include <cstdint>

#include "litestl/attr/group.h"
#include "ubertopo/geom/coord.h"
#include "ubertopo/ident/ref.h"
#include "ubertopo/topo/domain.h"
#include "ubertopo/topo/schema.h"

namespace ubertopo::topo {

using ident::CoedgeRef;
using ident::EdgeRef;
using ident::FaceRef;
using ident::FaceSideRef;
using ident::LoopRef;
using ident::RegionRef;
using ident::ShellRef;
using ident::VertRef;

/** Face side index: 2f + side. */
constexpr uint32_t face_side_index(FaceRef f, uint32_t side)
{
  return 2 * f.i + side;
}

/** The wedge rule (design 01 §4): the side of face(c) that faces the wedge
 * between c and radial_next(c). */
constexpr uint32_t side_toward_next(bool sense)
{
  return sense ? FRONT : BACK;
}

class ComplexBase {
public:
  Domain<EdgeSchema> edges;
  Domain<CoedgeSchema> coedges;
  Domain<LoopSchema> loops;
  FaceDomain faces;
  Domain<ShellSchema> shells;
  Domain<RegionSchema> regions;
  attr::TypedGroup<GlobalSchema> global;
  geom::TileFrame frame;

  ComplexBase()
  {
    global.resize(1);
  }

  bool allows_projectable() const
  {
    return attr::get_bool(global.get<".g.allows_projectable">(), 0);
  }
  void set_allows_projectable(bool v)
  {
    attr::set_bool(global.get<".g.allows_projectable">(), 0, v);
  }
  /** Regions and shells don't reflect the current faces (design 01 §6). */
  bool regions_dirty = true;

  /* ---- coedges ---- */
  EdgeRef edge_of(CoedgeRef c) const
  {
    return EdgeRef(coedges.get<".c.e">()[c.i]);
  }
  VertRef start_vert(CoedgeRef c) const
  {
    return VertRef(coedges.get<".c.v">()[c.i]);
  }
  LoopRef loop_of(CoedgeRef c) const
  {
    return LoopRef(coedges.get<".c.l">()[c.i]);
  }
  FaceRef face_of(CoedgeRef c) const
  {
    return face_of(loop_of(c));
  }
  CoedgeRef next(CoedgeRef c) const
  {
    return CoedgeRef(coedges.get<".c.next">()[c.i]);
  }
  CoedgeRef prev(CoedgeRef c) const
  {
    return CoedgeRef(coedges.get<".c.prev">()[c.i]);
  }
  CoedgeRef radial_next(CoedgeRef c) const
  {
    return CoedgeRef(coedges.get<".c.radial_next">()[c.i]);
  }
  CoedgeRef radial_prev(CoedgeRef c) const
  {
    return CoedgeRef(coedges.get<".c.radial_prev">()[c.i]);
  }
  bool sense(CoedgeRef c) const
  {
    return (coedges.get<".c.flags">()[c.i] & coedge_flags::Sense) != 0;
  }
  /** End vertex of the coedge in loop direction. */
  VertRef end_vert(CoedgeRef c) const
  {
    const auto vs = edge_verts(edge_of(c));
    return sense(c) ? vs[1] : vs[0];
  }
  uint32_t side_toward_next(CoedgeRef c) const
  {
    return topo::side_toward_next(sense(c));
  }

  /* ---- edges ---- */
  std::array<VertRef, 2> edge_verts(EdgeRef e) const
  {
    const U32x2 v = edges.get<".e.vs">()[e.i];
    return {VertRef(v[0]), VertRef(v[1])};
  }
  /** Any coedge of the radial cycle; NONE for a wire edge. */
  CoedgeRef edge_coedge(EdgeRef e) const
  {
    return CoedgeRef(edges.get<".e.c">()[e.i]);
  }
  ident::PersistentId edge_id(EdgeRef e) const
  {
    return edges.get<".id">()[e.i];
  }

  /* ---- loops ---- */
  CoedgeRef first_coedge(LoopRef l) const
  {
    return CoedgeRef(loops.get<".l.c">()[l.i]);
  }
  FaceRef face_of(LoopRef l) const
  {
    return FaceRef(loops.get<".l.f">()[l.i]);
  }
  LoopRef next_loop(LoopRef l) const
  {
    return LoopRef(loops.get<".l.next">()[l.i]);
  }
  uint32_t loop_size(LoopRef l) const
  {
    return loops.get<".l.size">()[l.i];
  }

  /* ---- faces ---- */
  LoopRef first_loop(FaceRef f) const
  {
    return LoopRef(faces.get<".f.l">()[f.i]);
  }
  RegionRef region(FaceRef f, uint32_t side) const
  {
    return RegionRef(faces.get<".f.region">()[f.i][side]);
  }
  ShellRef shell(FaceRef f, uint32_t side) const
  {
    return ShellRef(faces.get<".f.shell">()[f.i][side]);
  }
  uint8_t face_flags(FaceRef f) const
  {
    return faces.get<".f.flags">()[f.i];
  }
  /** Projection axis of a projected (non-planar) face, or -1 if planar. */
  int face_proj_axis(FaceRef f) const
  {
    return proj_axis(face_flags(f));
  }
  ident::PersistentId face_id(FaceRef f) const
  {
    return faces.get<".id">()[f.i];
  }
  ident::PersistentId side_hint(FaceRef f, uint32_t side) const
  {
    return faces.sides().get<".fs.hint">()[face_side_index(f, side)];
  }

  /* ---- cycle walks (each returns false if the cycle doesn't close within
   * the domain's capacity: a corrupt complex) ---- */

  /** f(LoopRef) for each loop of face @p fa. */
  template <typename F> bool for_each_loop(FaceRef fa, F &&f) const
  {
    uint32_t guard = loops.capacity() + 1;
    for (LoopRef l = first_loop(fa); l; l = next_loop(l)) {
      if (guard-- == 0 || l.i >= loops.capacity()) {
        return false;
      }
      f(l);
    }
    return true;
  }

  /** f(CoedgeRef) for each coedge of loop @p l, in loop order. */
  template <typename F> bool for_each_coedge(LoopRef l, F &&f) const
  {
    const CoedgeRef start = first_coedge(l);
    if (!start) {
      return true;
    }
    uint32_t guard = coedges.capacity() + 1;
    CoedgeRef c = start;
    do {
      if (guard-- == 0 || !c || c.i >= coedges.capacity()) {
        return false;
      }
      f(c);
      c = next(c);
    } while (c != start);
    return true;
  }

  /** f(CoedgeRef) for each coedge of the radial cycle of @p e, in angular order. */
  template <typename F> bool for_each_radial(EdgeRef e, F &&f) const
  {
    const CoedgeRef start = edge_coedge(e);
    if (!start) {
      return true;
    }
    uint32_t guard = coedges.capacity() + 1;
    CoedgeRef c = start;
    do {
      if (guard-- == 0 || !c || c.i >= coedges.capacity()) {
        return false;
      }
      f(c);
      c = radial_next(c);
    } while (c != start);
    return true;
  }

  /** f(EdgeRef) for each edge of the disk cycle at vertex @p v, starting at
   * @p first (the vertex's ".v.e"). */
  template <typename F> bool for_each_disk_edge(VertRef v, EdgeRef first, F &&f) const
  {
    if (!first) {
      return true;
    }
    uint32_t guard = edges.capacity() + 1;
    EdgeRef e = first;
    do {
      if (guard-- == 0 || !e || e.i >= edges.capacity()) {
        return false;
      }
      const auto vs = edge_verts(e);
      const uint32_t slot = vs[0] == v ? 0 : (vs[1] == v ? 1 : 2);
      if (slot == 2) {
        return false; /* the disk cycle left the vertex */
      }
      f(e);
      const uint32_t packed = edges.get<".e.disk">()[e.i][2 * slot + 1];
      if (packed == NONE) {
        return false;
      }
      e = EdgeRef(disk_edge(packed));
    } while (e != first);
    return true;
  }
};

template <geom::CoordType C> class Complex : public ComplexBase {
public:
  using coord_type = C;
  using Point = geom::Point<C>;

  Domain<VertexSchema<C>> verts;

  const Point &pos(VertRef v) const
  {
    return verts.template get<"pos">()[v.i];
  }
  ident::PersistentId vert_id(VertRef v) const
  {
    return verts.template get<".id">()[v.i];
  }
  EdgeRef vert_edge(VertRef v) const
  {
    return EdgeRef(verts.template get<".v.e">()[v.i]);
  }
  ident::ContainerRef vert_container(VertRef v) const
  {
    return verts.template get<".v.container">()[v.i];
  }

  /** f(EdgeRef) for each edge at @p v (disk walk). */
  template <typename F> bool for_each_edge_at(VertRef v, F &&f) const
  {
    return for_each_disk_edge(v, vert_edge(v), f);
  }
};

} // namespace ubertopo::topo
