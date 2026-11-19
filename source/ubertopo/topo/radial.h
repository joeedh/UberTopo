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
 * The cycle is sorted counter-clockwise about vs[0] -> vs[1] (for a curved
 * edge with a skeleton, about its first skeleton segment; plan P4b §4.3),
 * starting at the
 * coedge whose face has the smallest persistent ID; exactly equal angles
 * (overlapping coplanar faces: invalid geometry) tie-break by face ID and are
 * reported. */

#include <cstdint>

#include "litestl/util/vector.h"
#include "ubertopo/geom/cdt/cdt.h"
#include "ubertopo/geom/predicates.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/face_skeleton.h"

namespace ubertopo::topo {

/** Projection of a planar face: dominant axis and front sign (0 = degenerate). */
struct FaceFrame {
  int8_t axis = -1;
  int8_t sigma = 0;
};

/** Gathers a face's loops as a geom::PolyView source.
 *
 * gather(k, f): the stored vertices, one per coedge (planarity reads these).
 * gather(k, f, sk): the refined skeleton (plan P4b §4.3): each coedge adds its
 * start vertex, then its edge's flattening points in the coedge's direction.
 * ids[i] is point i's skeleton id (a vertex index below sk's vertex count);
 * owner[i] is the coedge whose run point i belongs to (its start vertex
 * first). Without curved edges both give the same lists. */
template <geom::CoordType C> struct FacePoints {
  litestl::util::Vector<geom::Point<C>, 32> pts;
  litestl::util::Vector<uint32_t, 8> off;
  litestl::util::Vector<uint32_t, 32> ids;
  litestl::util::Vector<uint32_t, 32> owner;

  bool gather(const Complex<C> &k, FaceRef f)
  {
    return gather_impl(k, f, nullptr);
  }
  bool gather(const Complex<C> &k, FaceRef f, Skeleton<C> &sk)
  {
    return gather_impl(k, f, &sk);
  }
  /** Appends coedge c's run: its start vertex, then (with a skeleton) the
   * flattening points of its edge in c's direction. */
  void append_run(const Complex<C> &k, CoedgeRef c, Skeleton<C> *sk)
  {
    const VertRef v = k.start_vert(c);
    pts.append(k.pos(v));
    ids.append(v.i);
    owner.append(c.i);
    if (!sk) {
      return;
    }
    const auto in = sk->interior(k.edge_of(c));
    const bool fwd = k.sense(c);
    for (size_t j = 0; j < in.size(); j++) {
      const uint32_t id = in[fwd ? j : in.size() - 1 - j];
      pts.append(sk->pos(id));
      ids.append(id);
      owner.append(c.i);
    }
  }

private:
  bool gather_impl(const Complex<C> &k, FaceRef f, Skeleton<C> *sk)
  {
    pts.clear();
    off.clear();
    ids.clear();
    owner.clear();
    off.append(0);
    bool ok = k.for_each_loop(f, [&](LoopRef l) {
      k.for_each_coedge(l, [&](CoedgeRef c) { append_run(k, c, sk); });
      off.append(uint32_t(pts.size()));
    });
    return ok;
  }

public:
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

/** The frame of face @p f on its refined skeleton (plan P4b §4.3). */
template <geom::CoordType C> FaceFrame face_frame(const Complex<C> &k, FaceRef f)
{
  Skeleton<C> sk(k);
  FacePoints<C> fp;
  if (!fp.gather(k, f, sk)) {
    return {};
  }
  return face_frame<C>(fp.view());
}

/** The axis radial order sorts about (plan P4b §4.3): vs[0] -> vs[1] for a
 * straight edge; for a curved edge with a skeleton, vs[0] -> its first
 * skeleton point. */
template <geom::CoordType C>
std::pair<geom::Point<C>, geom::Point<C>> edge_axis(const Complex<C> &k, EdgeRef e, Skeleton<C> *sk = nullptr)
{
  const auto vs = k.edge_verts(e);
  if (sk) {
    const auto in = sk->interior(e);
    if (in.size() > 0) {
      return {k.pos(vs[0]), sk->pos(in[0])};
    }
  }
  return {k.pos(vs[0]), k.pos(vs[1])};
}

/** Coedge @p c's segment at its edge's vs[0], in c's direction: the axis of
 * edge_axis, reversed when c runs vs[1] -> vs[0]. */
template <geom::CoordType C>
std::pair<geom::Point<C>, geom::Point<C>> coedge_axis(const Complex<C> &k, CoedgeRef c, Skeleton<C> *sk = nullptr)
{
  const auto ax = edge_axis<C>(k, k.edge_of(c), sk);
  return k.sense(c) ? ax : std::pair<geom::Point<C>, geom::Point<C>>{ax.second, ax.first};
}

/** Half-plane of coedge @p c (see the header comment); false if c's loop has
 * no vertex off the edge line (a degenerate loop). With a skeleton, a curved
 * edge's line is its first skeleton segment (coedge_axis), and the off-line
 * point is the first skeleton point after it in loop order (so a loop of two
 * vertices joined by arcs still has one). */
template <geom::CoordType C>
bool coedge_half_plane(const Complex<C> &k, CoedgeRef c, FaceFrame fr, geom::HalfPlane<C> &out, Skeleton<C> *sk = nullptr)
{
  if (fr.axis < 0 || fr.sigma == 0) {
    return false;
  }
  const auto ab = coedge_axis<C>(k, c, sk);
  const auto &a = ab.first;
  const auto &b = ab.second;
  if (sk) {
    /* the loop's skeleton points after the axis, in loop order: the axis
     * ends one point after c's start (c runs vs[0] -> vs[1]) or at the next
     * coedge's start (c runs back); without curved edges, the same walk as
     * the stored vertices below */
    FacePoints<C> fp;
    for (CoedgeRef x = c;;) {
      fp.append_run(k, x, sk);
      x = k.next(x);
      if (!x || x == c || fp.pts.size() > (1u << 30)) {
        break;
      }
    }
    const uint32_t m = uint32_t(fp.pts.size());
    uint32_t run = 1;
    while (run < m && fp.owner[run] == c.i) {
      run++;
    }
    const uint32_t end = k.sense(c) ? 1 : run;
    for (uint32_t j = 1; j + 1 < m; j++) {
      const geom::Point<C> &q = fp.pts[(end + j) % m];
      const int o = geom::orient2d_proj<C>(a, b, q, fr.axis);
      if (o != 0) {
        out.q = q;
        out.s = o * fr.sigma;
        return true;
      }
    }
    return false;
  }
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

/** The canonical triangulation T(f) of a projected face (amendment A1), in
 * corner indices of FacePoints order (loops from .f.l, coedges from .l.c).
 * False for planar faces or if the face isn't projectable along its axis. */
template <geom::CoordType C>
bool face_triangles(const Complex<C> &k, FaceRef f, litestl::util::Vector<geom::Triangle> &out, Skeleton<C> *sk = nullptr)
{
  const int axis = k.face_proj_axis(f);
  if (axis < 0) {
    return false;
  }
  FacePoints<C> fp;
  if (!(sk ? fp.gather(k, f, *sk) : fp.gather(k, f))) {
    return false;
  }
  return geom::cdt_proj<C>(fp.view(), axis, out) == geom::CdtStatus::Ok;
}

/** For each coedge of a projected face, the point completing its T(f)
 * triangle: the triangle with the coedge's segment at its edge's vs[0]
 * (coedge_axis: the whole edge when straight) as a front-winding edge.
 * Appends (coedge, point) pairs; false if T(f) can't be formed. */
template <geom::CoordType C>
bool projected_third_vertices(const Complex<C> &k,
                              FaceRef f,
                              litestl::util::Vector<std::pair<CoedgeRef, geom::Point<C>>> &out,
                              Skeleton<C> *sk = nullptr)
{
  const int axis = k.face_proj_axis(f);
  FacePoints<C> fp;
  litestl::util::Vector<geom::Triangle> tris;
  if (axis < 0 || !(sk ? fp.gather(k, f, *sk) : fp.gather(k, f)) ||
      geom::cdt_proj<C>(fp.view(), axis, tris) != geom::CdtStatus::Ok)
  {
    return false;
  }
  /* the successor of each point within its loop */
  const uint32_t n = uint32_t(fp.pts.size());
  litestl::util::Vector<uint32_t, 32> next;
  next.resize(n);
  for (uint32_t l = 0; l + 1 < fp.off.size(); l++) {
    for (uint32_t i = fp.off[l]; i < fp.off[l + 1]; i++) {
      next[i] = i + 1 < fp.off[l + 1] ? i + 1 : fp.off[l];
    }
  }
  /* each coedge's segment (a, b): the first of its run when it runs from
   * vs[0], else the last (which ends at vs[0]) */
  size_t found = 0, coedges = 0;
  const size_t before = out.size();
  for (uint32_t i = 0; i < n; i++) {
    if (i > 0 && fp.owner[i] == fp.owner[i - 1]) {
      continue; /* not the start of a run */
    }
    coedges++;
    const CoedgeRef c(fp.owner[i]);
    uint32_t a = i;
    if (!k.sense(c)) {
      while (next[a] != i && fp.owner[next[a]] == c.i) {
        a = next[a];
      }
    }
    const uint32_t b = next[a];
    for (const geom::Triangle &t : tris) {
      for (int j = 0; j < 3; j++) {
        if (t[j] == a && t[(j + 1) % 3] == b) {
          out.append({c, fp.pts[t[(j + 2) % 3]]});
          found++;
        }
      }
    }
  }
  return found == coedges && out.size() - before == coedges;
}

/** Sorts the coedges of edge @p e (given in any order) counter-clockwise about
 * vs[0] -> vs[1]. @p half_plane_of(CoedgeRef, HalfPlane &) -> bool gives each
 * coedge's half-plane (planar rule or T(f) triangle). Returns false if a
 * half-plane can't be formed; *tie is set if two angles are exactly equal. */
template <geom::CoordType C, typename HalfPlaneOf>
bool radial_order(const Complex<C> &k,
                  EdgeRef e,
                  std::span<const uint32_t> in,
                  HalfPlaneOf &&half_plane_of,
                  litestl::util::Vector<uint32_t, 8> &out,
                  bool *tie,
                  Skeleton<C> *sk = nullptr)
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
    if (!half_plane_of(CoedgeRef(c), it.h)) {
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
  const auto axis = edge_axis<C>(k, e, sk);
  const auto &v0 = axis.first, &v1 = axis.second;
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
