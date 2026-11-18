#pragma once

/* Geometric classification of a face given as loops of points (plan P2 W6,
 * amendment A1; shared with the editor's make_face, plan P3 §3.4).
 *
 *  - planar: all points exactly coplanar; every loop has a nonzero projected
 *    orientation and every hole winds opposite to the outer loop;
 *  - projected (only when allowed): non-planar, and the canonical CDT of the
 *    projection along the dominant canonical Newell axis (else the other axes
 *    in index order) exists. The axis that works is returned with T(f).
 *
 * The first failing rule decides the code, in the order above. */

#include <cstdint>

#include "litestl/util/vector.h"
#include "ubertopo/geom/cdt/cdt.h"
#include "ubertopo/geom/predicates.h"

namespace ubertopo::topo {

enum class FaceGeom : uint8_t { Ok, DegenerateLoop, NonPlanarFace, NotProjectable, HoleOrientation };

/** True if every point of @p view is exactly coplanar with a base triple
 * (pts[0], the first point distinct from it, the first point off their
 * line); false for degenerate inputs (*degenerate set). */
template <geom::CoordType C> bool face_points_planar(const geom::PolyView<C> &view, bool *degenerate = nullptr)
{
  const auto pts = view.pts;
  int ib = -1, ic = -1;
  for (int i = 1; i < int(pts.size()) && ib < 0; i++) {
    if (geom::lex_compare<C>(pts[i], pts[0]) != 0) {
      ib = i;
    }
  }
  for (int i = 1; ib >= 0 && i < int(pts.size()) && ic < 0; i++) {
    for (int k = 0; k < 3; k++) {
      if (geom::orient2d_proj<C>(pts[0], pts[ib], pts[i], k) != 0) {
        ic = i;
        break;
      }
    }
  }
  if (degenerate) {
    *degenerate = ic < 0;
  }
  if (ic < 0) {
    return false;
  }
  for (size_t i = 0; i < pts.size(); i++) {
    if (geom::vol3<C>(pts[0], pts[ib], pts[ic], pts[i]) != 0) {
      return false;
    }
  }
  return true;
}

/** Classifies a face (see the header comment). @p axis_out: the projection
 * axis of a projected face, -1 for a planar one. @p tris receives T(f) of a
 * projected face (scratch otherwise). */
template <geom::CoordType C>
FaceGeom classify_face(const geom::PolyView<C> &view,
                       bool allow_projectable,
                       int8_t &axis_out,
                       litestl::util::Vector<geom::Triangle> &tris)
{
  axis_out = -1;
  bool degenerate = false;
  const bool planar = face_points_planar<C>(view, &degenerate);
  if (degenerate) {
    return FaceGeom::DegenerateLoop;
  }
  if (!planar) {
    if (!allow_projectable) {
      return FaceGeom::NonPlanarFace;
    }
    const int dom = geom::project_axis_canonical<C>(view);
    int axes[3] = {dom, -1, -1}, na = dom >= 0 ? 1 : 0;
    for (int k = 0; k < 3; k++) {
      if (k != dom) {
        axes[na++] = k;
      }
    }
    bool winding = false;
    for (int a = 0; a < na; a++) {
      const geom::CdtStatus st = geom::cdt_proj<C>(view, axes[a], tris);
      if (st == geom::CdtStatus::Ok) {
        axis_out = int8_t(axes[a]);
        return FaceGeom::Ok;
      }
      winding = winding || st == geom::CdtStatus::HoleOrientation;
    }
    return winding ? FaceGeom::HoleOrientation : FaceGeom::NotProjectable;
  }
  const int axis = geom::project_axis<C>(view);
  if (axis < 0) {
    return FaceGeom::DegenerateLoop;
  }
  const uint32_t outer = geom::outer_loop_proj<C>(view, axis);
  const int so = geom::loop_orientation_proj<C>(view.loop(outer), axis);
  for (uint32_t l = 0; l < view.loop_count(); l++) {
    const int s = geom::loop_orientation_proj<C>(view.loop(l), axis);
    if (s == 0) {
      return FaceGeom::DegenerateLoop;
    }
    if (l != outer && s == so) {
      return FaceGeom::HoleOrientation;
    }
  }
  return FaceGeom::Ok;
}

} // namespace ubertopo::topo
