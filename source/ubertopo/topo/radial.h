#pragma once

/* Exact radial order of planar faces about an edge (design 01 §4, plan P2 W5).
 *
 * Half-plane of coedge c of face f at edge e: the side of e's line, within
 * f's plane, that f's interior occupies near e. f's interior lies to the left
 * of c seen from f's front. In the projection along f's axis, "left" is
 * orient2d_proj > 0 when the projection preserves front orientation
 * (sigma = +1) and < 0 otherwise. q is the first vertex of c's loop after c
 * that is not collinear with e; s = +1 if q is on the interior side. No point
 * is constructed, so everything is exact.
 *
 * The cycle is sorted counter-clockwise about vs[0] -> vs[1], starting at the
 * coedge whose face has the smallest persistent ID; exactly equal angles
 * (overlapping coplanar faces: invalid geometry) tie-break by face ID and are
 * reported. */

#include <cstdint>

#include "litestl/util/vector.h"
#include "ubertopo/geom/predicates.h"
#include "ubertopo/topo/complex.h"

namespace ubertopo::topo {

/** Projection of a planar face: dominant axis and front sign (0 = degenerate). */
struct FaceFrame {
  int8_t axis = -1;
  int8_t sigma = 0;
};

/** Gathers a face's loops as a geom::PolyView source. */
template <geom::CoordType C> struct FacePoints {
  litestl::util::Vector<geom::Point<C>, 32> pts;
  litestl::util::Vector<uint32_t, 8> off;

  bool gather(const Complex<C> &k, FaceRef f)
  {
    pts.clear();
    off.clear();
    off.append(0);
    bool ok = k.for_each_loop(f, [&](LoopRef l) {
      k.for_each_coedge(l, [&](CoedgeRef c) { pts.append(k.pos(k.start_vert(c))); });
      off.append(uint32_t(pts.size()));
    });
    return ok;
  }
  geom::PolyView<C> view() const
  {
    return {std::span<const geom::Point<C>>(pts.data(), pts.size()), std::span<const uint32_t>(off.data(), off.size())};
  }
};

template <geom::CoordType C> FaceFrame face_frame(const geom::PolyView<C> &v)
{
  FaceFrame fr;
  const int axis = geom::project_axis<C>(v);
  if (axis < 0) {
    return fr;
  }
  fr.axis = int8_t(axis);
  fr.sigma = int8_t(geom::face_front_sign<C>(v, axis));
  return fr;
}

template <geom::CoordType C> FaceFrame face_frame(const Complex<C> &k, FaceRef f)
{
  FacePoints<C> fp;
  if (!fp.gather(k, f)) {
    return {};
  }
  return face_frame<C>(fp.view());
}

/** Half-plane of coedge @p c (see the header comment); false if c's loop has
 * no vertex off the edge line (a degenerate loop). */
template <geom::CoordType C>
bool coedge_half_plane(const Complex<C> &k, CoedgeRef c, FaceFrame fr, geom::HalfPlane<C> &out)
{
  if (fr.axis < 0 || fr.sigma == 0) {
    return false;
  }
  const auto &a = k.pos(k.start_vert(c));
  const auto &b = k.pos(k.end_vert(c));
  const uint32_t guard = k.loop_size(k.loop_of(c));
  CoedgeRef cc = k.next(c);
  for (uint32_t n = 0; n < guard && cc && cc != c; n++, cc = k.next(cc)) {
    const auto &q = k.pos(k.end_vert(cc));
    const int o = geom::orient2d_proj<C>(a, b, q, fr.axis);
    if (o != 0) {
      out.q = q;
      out.s = o * fr.sigma;
      return true;
    }
  }
  return false;
}

/** Sorts the coedges of edge @p e (given in any order) counter-clockwise about
 * vs[0] -> vs[1]. @p frame_of(FaceRef) -> FaceFrame. Returns false if a
 * half-plane can't be formed; *tie is set if two angles are exactly equal. */
template <geom::CoordType C, typename FrameOf>
bool radial_order(const Complex<C> &k,
                  EdgeRef e,
                  std::span<const uint32_t> in,
                  FrameOf &&frame_of,
                  litestl::util::Vector<uint32_t, 8> &out,
                  bool *tie)
{
  struct Item {
    uint32_t c;
    ident::PersistentId fid;
    geom::HalfPlane<C> h;
  };
  litestl::util::Vector<Item, 8> items;
  for (uint32_t c : in) {
    Item it;
    it.c = c;
    const FaceRef f = k.face_of(CoedgeRef(c));
    it.fid = k.face_id(f);
    if (!coedge_half_plane<C>(k, CoedgeRef(c), frame_of(f), it.h)) {
      return false;
    }
    items.append(it);
  }
  out.clear();
  if (items.size() == 0) {
    return true;
  }
  /* reference: the smallest face ID (face IDs are unique within an edge) */
  uint32_t r = 0;
  for (uint32_t i = 1; i < items.size(); i++) {
    if (items[i].fid < items[r].fid) {
      r = i;
    }
  }
  std::swap(items[0], items[r]);
  const auto vs = k.edge_verts(e);
  const auto &v0 = k.pos(vs[0]), &v1 = k.pos(vs[1]);
  const geom::HalfPlane<C> ref = items[0].h;
  /* insertion sort of items[1..]: cycles are small; keys are unique */
  for (uint32_t i = 2; i < items.size(); i++) {
    Item x = items[i];
    uint32_t j = i;
    while (j > 1) {
      const int cmp = geom::radial_compare<C>(v0, v1, ref, x.h, items[j - 1].h);
      if (cmp < 0 || (cmp == 0 && x.fid < items[j - 1].fid)) {
        items[j] = items[j - 1];
        j--;
      }
      else {
        break;
      }
    }
    items[j] = x;
  }
  if (tie) {
    *tie = false;
    for (uint32_t i = 1; i < items.size(); i++) {
      const uint32_t n = (i + 1) % items.size();
      if (geom::radial_class<C>(v0, v1, ref, items[i].h) == 0 ||
          (n != 0 && geom::radial_compare<C>(v0, v1, ref, items[i].h, items[n].h) == 0))
      {
        *tie = true;
      }
    }
  }
  for (const Item &it : items) {
    out.append(it.c);
  }
  return true;
}

} // namespace ubertopo::topo
