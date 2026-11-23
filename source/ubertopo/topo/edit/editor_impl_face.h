#pragma once

/* Editor<C>: face primitives, side hints, attributes and the regions barrier
 * (plan P3 §3.4, C1, C3–C8, C10–C12, C17). Template definitions, included
 * only by editor.cc. */

#include <algorithm>
#include <cmath>
#include <memory>

#include "ubertopo/topo/compact.h"
#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/face_geom.h"
#include "ubertopo/topo/radial.h"
#include "ubertopo/topo/regions.h"

namespace ubertopo::topo {

namespace edit_detail {

/** |area| of a polygon projected along @p axis (double; for weights only). */
template <geom::CoordType C> double proj_area(std::span<const geom::Point<C>> pts, int axis)
{
  const int u = (axis + 1) % 3, v = (axis + 2) % 3;
  double a = 0;
  for (size_t i = 0; i < pts.size(); i++) {
    const auto p = geom::to_double3<C>(pts[i]), q = geom::to_double3<C>(pts[(i + 1) % pts.size()]);
    a += p[u] * q[v] - q[u] * p[v];
  }
  return std::fabs(a) * 0.5;
}

/** Measure of a face for propagation weights: outer loop minus holes. */
template <geom::CoordType C> double face_area(const Complex<C> &k, FaceRef f)
{
  /* on the refined skeleton: a curved face's measure to within skeleton_tol */
  Skeleton<C> sk(k);
  FacePoints<C> fp;
  if (!fp.gather(k, f, sk)) {
    return 0;
  }
  const auto view = fp.view();
  int axis = k.face_proj_axis(f);
  if (axis < 0) {
    axis = face_frame<C>(view).axis;
  }
  if (axis < 0) {
    return 0;
  }
  const uint32_t outer = geom::outer_loop_proj<C>(view, axis);
  double a = 0;
  for (uint32_t l = 0; l < view.loop_count(); l++) {
    const double al = proj_area<C>(view.loop(l), axis);
    a += l == outer ? al : -al;
  }
  return std::max(a, 0.0);
}

/** Projection axis of a face for exact point tests (stored or dominant). */
template <geom::CoordType C> int face_axis(const Complex<C> &k, FaceRef f)
{
  const int a = k.face_proj_axis(f);
  return a >= 0 ? a : face_frame<C>(k, f).axis;
}

} // namespace edit_detail

/* ---- make_face / kill_face ---- */

template <geom::CoordType C>
EditCode Editor<C>::make_face(std::span<const VertRef> verts,
                              std::span<const uint32_t> loop_sizes,
                              ident::PersistentId id,
                              uint8_t flags,
                              ident::PersistentId front_hint,
                              ident::PersistentId back_hint,
                              FaceRef *out)
{
  Prim pr(*this, OpKind::MakeFace);
  if (pr.done) {
    return pr.code;
  }
  using ident::ElemKind;
  if (id == ident::kNoId || loop_sizes.empty()) {
    return pr.fail(EditCode::BadArgument, ElemKind::Face);
  }
  if (flags & face_flags::ProjAxisMask) {
    return pr.fail(EditCode::ReservedFaceFlags, ElemKind::Face);
  }
  if (id_used(Grp::Faces, id)) {
    return pr.fail(EditCode::DuplicateId, ElemKind::Face);
  }
  size_t total = 0;
  for (uint32_t m : loop_sizes) {
    if (m < 3) {
      return pr.fail(EditCode::BadLoop, ElemKind::Face, NONE, "loop shorter than 3");
    }
    total += m;
  }
  if (total != verts.size()) {
    return pr.fail(EditCode::BadArgument, ElemKind::Face, NONE, "loop sizes don't match the vertex count");
  }
  litestl::util::Vector<uint32_t, 32> sorted;
  for (const VertRef v : verts) {
    if (!k_.verts.live(v.i)) {
      return pr.fail(EditCode::DeadElement, ElemKind::Vertex, v.i);
    }
    sorted.append(v.i);
  }
  std::sort(sorted.begin(), sorted.end());
  for (int i = 1; i < sorted.size(); i++) {
    if (sorted[i] == sorted[i - 1]) {
      return pr.fail(EditCode::BadLoop, ElemKind::Vertex, sorted[i], "repeated vertex");
    }
  }
  /* edges, containers */
  const auto &vc = k_.verts.template get<".v.container">();
  const auto &vh = k_.verts.template get<".v.container_hint">();
  const auto &ec = k_.edges.template get<".e.container">();
  const auto &eh = k_.edges.template get<".e.container_hint">();
  litestl::util::Vector<uint32_t, 32> edges;
  {
    size_t j0 = 0;
    for (uint32_t m : loop_sizes) {
      for (uint32_t j = 0; j < m; j++) {
        const VertRef u = verts[j0 + j], w = verts[j0 + (j + 1) % m];
        const EdgeRef e = find_edge(u, w);
        if (!e) {
          return pr.fail(EditCode::BadLoop, ElemKind::Vertex, u.i, "no edge between consecutive vertices");
        }
        if (!ec[e.i].is_none() || !(eh[e.i] == ident::kNoId)) {
          return pr.fail(EditCode::ContainedElement, ElemKind::Edge, e.i);
        }
        edges.append(e.i);
      }
      j0 += m;
    }
    for (const VertRef v : verts) {
      if (!vc[v.i].is_none() || !(vh[v.i] == ident::kNoId)) {
        return pr.fail(EditCode::ContainedElement, ElemKind::Vertex, v.i);
      }
    }
    litestl::util::Vector<uint32_t, 32> se = edges;
    std::sort(se.begin(), se.end());
    for (int i = 1; i < se.size(); i++) {
      if (se[i] == se[i - 1]) {
        return pr.fail(EditCode::EdgeUsedTwice, ElemKind::Edge, se[i]);
      }
    }
  }
  /* geometry: the builder's rule (face_geom.h) */
  int8_t axis = -1;
  {
    litestl::util::Vector<Point, 32> pts;
    litestl::util::Vector<uint32_t, 8> off;
    off.append(0);
    size_t j0 = 0;
    for (uint32_t m : loop_sizes) {
      for (uint32_t j = 0; j < m; j++) {
        pts.append(k_.pos(verts[j0 + j]));
      }
      j0 += m;
      off.append(uint32_t(pts.size()));
    }
    const geom::PolyView<C> view{std::span<const Point>(pts.data(), pts.size()),
                                 std::span<const uint32_t>(off.data(), off.size())};
    /* the refined skeleton of the loops (plan P4b §4.3): each edge's
     * flattening points in the direction the loop runs it */
    Skeleton<C> sk(k_);
    litestl::util::Vector<Point, 32> spts;
    litestl::util::Vector<uint32_t, 8> soff;
    soff.append(0);
    j0 = 0;
    for (uint32_t m : loop_sizes) {
      for (uint32_t j = 0; j < m; j++) {
        const VertRef u = verts[j0 + j];
        const EdgeRef e(edges[j0 + j]);
        spts.append(k_.pos(u));
        const auto in = sk.interior(e);
        const bool fwd = k_.edge_verts(e)[0] == u;
        for (size_t i = 0; i < in.size(); i++) {
          spts.append(sk.pos(in[fwd ? i : in.size() - 1 - i]));
        }
      }
      j0 += m;
      soff.append(uint32_t(spts.size()));
    }
    const geom::PolyView<C> sview{std::span<const Point>(spts.data(), spts.size()),
                                  std::span<const uint32_t>(soff.data(), soff.size())};
    litestl::util::Vector<geom::Triangle> tris;
    switch (classify_face<C>(view, sview, k_.allows_projectable(), axis, tris)) {
      case FaceGeom::Ok:
        break;
      case FaceGeom::DegenerateLoop:
        return pr.fail(EditCode::DegenerateLoop, ElemKind::Face);
      case FaceGeom::NonPlanarFace:
        return pr.fail(EditCode::NonPlanarFace, ElemKind::Face);
      case FaceGeom::NotProjectable:
        return pr.fail(EditCode::NotProjectable, ElemKind::Face);
      case FaceGeom::HoleOrientation:
        return pr.fail(EditCode::HoleOrientation, ElemKind::Face);
    }
  }
  /* build */
  const FaceRef f(alloc(Grp::Faces));
  setF<".id">(f, id);
  setF<".f.flags">(f, axis >= 0 ? with_proj_axis(flags, axis) : flags);
  setS<".fs.hint">(2 * f.i, front_hint);
  setS<".fs.hint">(2 * f.i + 1, back_hint);
  litestl::util::Vector<uint32_t, 32> new_c;
  {
    size_t j0 = 0;
    LoopRef prev_l;
    for (uint32_t li = 0; li < uint32_t(loop_sizes.size()); li++) {
      const uint32_t m = loop_sizes[li];
      const LoopRef l(alloc(Grp::Loops));
      setL<".l.f">(l, f.i);
      setL<".id">(l, ident::loop_id(id, li));
      setL<".l.size">(l, m);
      if (li == 0) {
        setF<".f.l">(f, l.i);
      }
      else {
        setL<".l.next">(prev_l, l.i);
      }
      prev_l = l;
      litestl::util::Vector<uint32_t, 16> cs;
      for (uint32_t j = 0; j < m; j++) {
        cs.append(alloc(Grp::Coedges));
      }
      for (uint32_t j = 0; j < m; j++) {
        const CoedgeRef c(cs[j]);
        const EdgeRef e(edges[int(j0 + j)]);
        const VertRef u = verts[j0 + j];
        setC<".c.e">(c, e.i);
        setC<".c.v">(c, u.i);
        setC<".c.l">(c, l.i);
        setC<".c.flags">(c, uint8_t(k_.edge_verts(e)[0] == u ? coedge_flags::Sense : 0));
        setC<".c.next">(c, cs[(j + 1) % m]);
        setC<".c.prev">(c, cs[(j + m - 1) % m]);
        new_c.append(c.i);
      }
      setL<".l.c">(l, cs[0]);
      j0 += m;
    }
  }
  /* one half-plane cache for every edge of the face: only radial links
   * change in this loop, and a projected face's entry costs its T(f) */
  edit_detail::HalfPlanes<C> hp(k_);
  for (int i = 0; i < new_c.size(); i++) {
    const uint32_t one[1] = {new_c[i]};
    const EditCode rc = resort_radial(EdgeRef(edges[i]), std::span<const uint32_t>(one, 1), &hp);
    if (rc != EditCode::Ok) {
      return pr.fail(rc, rep_.kind, rep_.index);
    }
  }
  set_dirty();
  pr.op.u.append(flags);
  pr.op.u.append(uint32_t(loop_sizes.size()));
  for (uint32_t m : loop_sizes) {
    pr.op.u.append(m);
  }
  for (const VertRef v : verts) {
    pr.op.u.append(v.i);
  }
  pr.op.ids.append(id);
  pr.op.ids.append(front_hint);
  pr.op.ids.append(back_hint);
  if (out) {
    *out = f;
  }
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::kill_face(FaceRef f)
{
  Prim pr(*this, OpKind::KillFace);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.faces.live(f.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Face, f.i);
  }
  litestl::util::Vector<uint32_t, 16> incl;
  inclusions(f, incl);
  if (incl.size()) {
    return pr.fail(EditCode::FaceHasInclusions, ident::ElemKind::Face, f.i);
  }
  for (uint32_t s = 0; s < 2; s++) {
    const ident::PersistentId h = k_.side_hint(f, s);
    if (hint_in_use(h) && !hint_carried(h, 2 * f.i, 2 * f.i + 1)) {
      return pr.fail(EditCode::HintInUse, ident::ElemKind::Face, f.i);
    }
  }
  litestl::util::Vector<uint32_t, 32> cs, ls;
  k_.for_each_loop(f, [&](LoopRef l) {
    ls.append(l.i);
    k_.for_each_coedge(l, [&](CoedgeRef c) { cs.append(c.i); });
  });
  for (uint32_t c : cs) {
    radial_remove(CoedgeRef(c));
  }
  for (uint32_t c : cs) {
    free(Grp::Coedges, c);
  }
  for (uint32_t l : ls) {
    free(Grp::Loops, l);
  }
  free(Grp::Faces, f.i);
  set_dirty();
  pr.op.u.append(f.i);
  return pr.finish();
}

/* ---- split_face / join_faces ---- */

template <geom::CoordType C>
EditCode Editor<C>::split_face(FaceRef f, VertRef a, VertRef b, FaceRef *other, EdgeRef *chord)
{
  Prim pr(*this, OpKind::SplitFace);
  if (pr.done) {
    return pr.code;
  }
  using ident::ElemKind;
  if (!k_.faces.live(f.i)) {
    return pr.fail(EditCode::DeadElement, ElemKind::Face, f.i);
  }
  if (!k_.verts.live(a.i) || !k_.verts.live(b.i) || a == b) {
    return pr.fail(EditCode::BadArgument, ElemKind::Vertex, a.i);
  }
  if (surfaced(f)) {
    return pr.fail(EditCode::SurfacedFace, ElemKind::Face, f.i);
  }
  /* the shared split core (editor_impl_imprint.h) with a new chord a -> b */
  const uint32_t ab[2] = {a.i, b.i};
  const EditCode c = split_core(f, {}, std::span<const uint32_t>(ab, 2), true, other, chord);
  if (c != EditCode::Ok) {
    return pr.fail(c, rep_.kind, rep_.index, rep_.detail);
  }
  pr.op.u.append(f.i);
  pr.op.u.append(a.i);
  pr.op.u.append(b.i);
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::join_faces(EdgeRef e, FaceRef *survivor)
{
  Prim pr(*this, OpKind::JoinFaces);
  if (pr.done) {
    return pr.code;
  }
  using ident::ElemKind;
  if (!k_.edges.live(e.i)) {
    return pr.fail(EditCode::DeadElement, ElemKind::Edge, e.i);
  }
  const auto m = radial_members(e);
  if (m.size() != 2) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "the edge doesn't have exactly two coedges");
  }
  CoedgeRef c1 = m[0], c2 = m[1];
  FaceRef f1 = k_.face_of(c1), f2 = k_.face_of(c2);
  if (f1 == f2) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "both coedges on one face");
  }
  if (surfaced(f1) || surfaced(f2)) {
    return pr.fail(EditCode::SurfacedFace, ElemKind::Face, surfaced(f1) ? f1.i : f2.i);
  }
  if (k_.sense(c1) == k_.sense(c2)) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "inconsistent front sides");
  }
  if (k_.face_flags(f1) != k_.face_flags(f2) ||
      !(k_.faces.template get<".f.surface">()[f1.i] == k_.faces.template get<".f.surface">()[f2.i]) ||
      !(k_.side_hint(f1, 0) == k_.side_hint(f2, 0)) || !(k_.side_hint(f1, 1) == k_.side_hint(f2, 1)))
  {
    return pr.fail(EditCode::Incompatible, ElemKind::Face, f2.i);
  }
  /* the faces share exactly e and its two endpoints */
  litestl::util::Vector<uint32_t, 32> e2s, v2s;
  k_.for_each_loop(f2, [&](LoopRef l) {
    k_.for_each_coedge(l, [&](CoedgeRef c) {
      e2s.append(k_.edge_of(c).i);
      v2s.append(k_.start_vert(c).i);
    });
  });
  std::sort(e2s.begin(), e2s.end());
  std::sort(v2s.begin(), v2s.end());
  uint32_t shared_e = 0, shared_v = 0;
  k_.for_each_loop(f1, [&](LoopRef l) {
    k_.for_each_coedge(l, [&](CoedgeRef c) {
      shared_e += std::binary_search(e2s.begin(), e2s.end(), k_.edge_of(c).i);
      shared_v += std::binary_search(v2s.begin(), v2s.end(), k_.start_vert(c).i);
    });
  });
  if (shared_e != 1 || shared_v != 2) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "the faces share more than the edge");
  }
  if (k_.face_proj_axis(f1) < 0) {
    FacePoints<C> p1, p2;
    p1.gather(k_, f1);
    p2.gather(k_, f2);
    for (const Point &p : p2.pts) {
      p1.pts.append(p);
    }
    const uint32_t off[2] = {0, uint32_t(p1.pts.size())};
    const geom::PolyView<C> all{std::span<const Point>(p1.pts.data(), p1.pts.size()), std::span<const uint32_t>(off, 2)};
    if (!face_points_planar<C>(all)) {
      return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "not coplanar");
    }
  }
  /* survivor: the smaller face ID */
  if (k_.face_id(f2) < k_.face_id(f1)) {
    std::swap(f1, f2);
    std::swap(c1, c2);
  }
  const FaceRef fs = f1, fa = f2;
  const CoedgeRef cs = c1, cx = c2;
  const LoopRef ls = k_.loop_of(cs), la = k_.loop_of(cx);
  const double as = edit_detail::face_area<C>(k_, fs), aa = edit_detail::face_area<C>(k_, fa);
  const ident::PersistentId ids = k_.face_id(fs), ida = k_.face_id(fa), ide = k_.edge_id(e);

  /* propagate before anything is freed */
  MixBuild fm, sm, lm;
  fm.add(fs.i, {{fs.i, as}, {fa.i, aa}});
  for (uint32_t s = 0; s < 2; s++) {
    sm.add(2 * fs.i + s, {{2 * fs.i + s, as}, {2 * fa.i + s, aa}});
  }
  lm.add(ls.i, {{ls.i, 1.0}, {la.i, 1.0}});
  if (!propagate(Grp::Faces, PropEvent::Merge, fm) || !propagate(Grp::Sides, PropEvent::Merge, sm) ||
      !propagate(Grp::Loops, PropEvent::Merge, lm))
  {
    return pr.fail(EditCode::PropagationFailed, ElemKind::Face, fs.i);
  }
  /* splice: p1 -> n2 ... p2 -> n1 ... */
  const CoedgeRef p1 = k_.prev(cs), n1 = k_.next(cs), p2 = k_.prev(cx), n2 = k_.next(cx);
  const uint32_t size = k_.loop_size(ls) + k_.loop_size(la) - 2;
  litestl::util::Vector<uint32_t, 32> moved;
  k_.for_each_coedge(la, [&](CoedgeRef c) {
    if (c != cx) {
      moved.append(c.i);
    }
  });
  setC<".c.next">(p1, n2.i);
  setC<".c.prev">(n2, p1.i);
  setC<".c.next">(p2, n1.i);
  setC<".c.prev">(n1, p2.i);
  for (uint32_t c : moved) {
    setC<".c.l">(CoedgeRef(c), ls.i);
  }
  if (k_.first_coedge(ls) == cs) {
    setL<".l.c">(ls, n1.i);
  }
  setL<".l.size">(ls, size);
  /* the absorbed face's other loops follow the survivor's */
  litestl::util::Vector<uint32_t, 8> chain_s, extra;
  k_.for_each_loop(fs, [&](LoopRef l) { chain_s.append(l.i); });
  k_.for_each_loop(fa, [&](LoopRef l) {
    if (l != la) {
      extra.append(l.i);
    }
  });
  for (uint32_t l : extra) {
    chain_s.append(l);
  }
  for (int i = 0; i < chain_s.size(); i++) {
    const LoopRef l(chain_s[i]);
    setL<".l.f">(l, fs.i);
    setL<".id">(l, ident::loop_id(ids, uint32_t(i)));
    setL<".l.next">(l, i + 1 < chain_s.size() ? chain_s[i + 1] : NONE);
  }
  /* inclusions of the absorbed face */
  litestl::util::Vector<uint32_t, 16> incl;
  inclusions(fa, incl);
  ident::ContainerRef cr_s;
  ident::ContainerRef::face(fs, cr_s);
  for (uint32_t x : incl) {
    if (x & 1) {
      setE<".e.container">(EdgeRef(x >> 1), cr_s);
    }
    else {
      setV<".v.container">(VertRef(x >> 1), cr_s);
    }
  }
  /* remove e and its coedges, the absorbed loop and face */
  setE<".e.c">(e, NONE);
  disk_remove(e, 0);
  disk_remove(e, 1);
  free(Grp::Coedges, cs.i);
  free(Grp::Coedges, cx.i);
  free(Grp::Edges, e.i);
  free(Grp::Loops, la.i);
  free(Grp::Faces, fa.i);
  const bool was_projected = k_.face_proj_axis(fs) >= 0;
  fix_projection(fs);
  if (was_projected) {
    const EditCode rc = resort_face_edges(fs);
    if (rc != EditCode::Ok) {
      return pr.fail(rc, rep_.kind, rep_.index);
    }
  }
  else {
    k_.for_each_loop(fs, [&](LoopRef l) { k_.for_each_coedge(l, [&](CoedgeRef c) { refresh_ec(k_.edge_of(c)); }); });
  }
  set_dirty();
  lineage_.push_back({ida, ids, ElemKind::Face, LineageKind::Merge});
  lineage_.push_back({ids, ids, ElemKind::Face, LineageKind::Merge});
  pr.op.u.append(e.i);
  if (survivor) {
    *survivor = fs;
  }
  return pr.finish();
}

/* ---- flip_face ---- */

template <geom::CoordType C> EditCode Editor<C>::flip_face(FaceRef f)
{
  Prim pr(*this, OpKind::FlipFace);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.faces.live(f.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Face, f.i);
  }
  struct Old {
    uint32_t c, next, prev, end;
  };
  litestl::util::Vector<Old, 32> olds;
  k_.for_each_loop(f, [&](LoopRef l) {
    k_.for_each_coedge(l, [&](CoedgeRef c) { olds.append({c.i, k_.next(c).i, k_.prev(c).i, k_.end_vert(c).i}); });
  });
  MixBuild corners, sides;
  for (const Old &o : olds) {
    const CoedgeRef c(o.c);
    setC<".c.next">(c, o.prev);
    setC<".c.prev">(c, o.next);
    setC<".c.v">(c, o.end);
    setC<".c.flags">(c, uint8_t(k_.coedges.template get<".c.flags">()[c.i] ^ coedge_flags::Sense));
    corners.add(o.c, {{o.next, 1.0}}); /* the corner at the new start vertex */
  }
  sides.add(2 * f.i, {{2 * f.i + 1, 1.0}});
  sides.add(2 * f.i + 1, {{2 * f.i, 1.0}});
  const ident::PersistentId h0 = k_.side_hint(f, 0), h1 = k_.side_hint(f, 1);
  setS<".fs.hint">(2 * f.i, h1);
  setS<".fs.hint">(2 * f.i + 1, h0);
  setF<".f.flags">(f, uint8_t(k_.face_flags(f) ^ face_flags::Reversed));
  if (!propagate(Grp::Coedges, PropEvent::Reverse, corners) || !propagate(Grp::Sides, PropEvent::Reverse, sides)) {
    return pr.fail(EditCode::PropagationFailed, ident::ElemKind::Face, f.i);
  }
  if (k_.face_proj_axis(f) >= 0) {
    const EditCode rc = resort_face_edges(f);
    if (rc != EditCode::Ok) {
      return pr.fail(rc, rep_.kind, rep_.index);
    }
  }
  set_dirty();
  pr.op.u.append(f.i);
  return pr.finish();
}

/* ---- hints, attributes ---- */

template <geom::CoordType C> EditCode Editor<C>::set_side_hint(FaceRef f, uint32_t side, ident::PersistentId hint)
{
  Prim pr(*this, OpKind::SetSideHint);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.faces.live(f.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Face, f.i);
  }
  if (side > 1) {
    return pr.fail(EditCode::BadArgument, ident::ElemKind::Face, f.i);
  }
  const ident::PersistentId old = k_.side_hint(f, side);
  if (!(old == hint) && hint_in_use(old) && !hint_carried(old, 2 * f.i + side, NONE)) {
    return pr.fail(EditCode::HintInUse, ident::ElemKind::Face, f.i);
  }
  setS<".fs.hint">(2 * f.i + side, hint);
  set_dirty();
  pr.op.u.append(f.i);
  pr.op.u.append(side);
  pr.op.ids.append(hint);
  return pr.finish();
}

template <geom::CoordType C>
EditCode Editor<C>::set_attr(Grp g, uint32_t row, uint32_t column, std::span<const uint8_t> bytes)
{
  Prim pr(*this, OpKind::SetAttr);
  if (pr.done) {
    return pr.code;
  }
  if (uint32_t(g) >= kGroupCount || g == Grp::Global) {
    return pr.fail(EditCode::BadArgument);
  }
  const bool ok_row = g == Grp::Sides ? k_.faces.live(row / 2) : live(g, row);
  if (!ok_row) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Vertex, row);
  }
  litestl::attr::AttrGroup &gr = group_of(k_, g);
  if (column >= gr.column_count()) {
    return pr.fail(EditCode::BadArgument, ident::ElemKind::Vertex, row, "no such column");
  }
  litestl::attr::ColumnBase &col = gr.column(column);
  if (has_flag(col.flags(), litestl::attr::ColumnFlags::Topo) || (g == Grp::Verts && gr.name(column) == "pos") ||
      bytes.size() != col.elem_size())
  {
    return pr.fail(EditCode::BadArgument, ident::ElemKind::Vertex, row, "Topo column, pos, or wrong size");
  }
  touch(g, row);
  col.set_bytes(row, bytes.data());
  pr.op.u.append(uint32_t(g));
  pr.op.u.append(row);
  pr.op.u.append(column);
  pr.op.bytes.assign(bytes.begin(), bytes.end());
  return pr.finish();
}

/* ---- barriers: regions (C7, C13) and compaction (P3b W2) ---- */

template <geom::CoordType C> EditCode Editor<C>::rebuild_regions()
{
  return run_barrier(OpKind::RebuildRegions);
}

template <geom::CoordType C> EditCode Editor<C>::compact()
{
  return run_barrier(OpKind::Compact);
}

template <geom::CoordType C> EditCode Editor<C>::run_barrier(OpKind kind)
{
  rep_ = EditReport{};
  if (!sp_.empty()) {
    rep_.code = EditCode::OpenTransaction;
    return rep_.code;
  }
  if (kind == OpKind::Compact && !k_.frozen() && !has_tombstones(k_)) {
    return EditCode::Ok; /* nothing to drop: no transaction */
  }
  EditCode c = begin_internal(kind == OpKind::Compact ? "compact" : "rebuild_regions");
  if (c != EditCode::Ok) {
    rep_.code = c;
    return c;
  }
  barrier_snapshot_ = std::make_unique<Complex<C>>(k_);
  barrier_kind_ = kind;
  if (kind == OpKind::Compact) {
    topo::compact(k_);
  }
  else {
    RegionReport rr;
    if (!build_regions(k_, rr)) {
      rep_.code = EditCode::RegionsFailed;
      for (const RegionIssue &i : rr.issues) {
        if (region_code_is_error(i.code)) {
          rep_.region_code = int(i.code);
          rep_.index = i.index;
          break;
        }
      }
      rollback();
      return EditCode::RegionsFailed;
    }
    if (opt_.validate != ValidateMode::None) {
      /* the index and grid are untouched until this passes, so a failure
       * rolls back by the snapshot alone */
      ValidationReport r = opt_.validate == ValidateMode::Full ? validate(k_, ValidateLevel::Embedding) :
                                                                  validate_region_barrier();
      if (!r.ok()) {
        rep_.validation = r;
        rep_.code = EditCode::Invalid;
        rollback();
        return EditCode::Invalid;
      }
    }
  }
  OpRecord<C> op;
  op.kind = kind;
  ops_.push_back(std::move(op));
  c = commit_internal(redoing_);
  /* eagerly, inside the barrier (C13). compact renumbers rows: both are
   * rebuilt. build_regions writes only regions and resolved containers: the
   * index re-keys those, and the grid, which holds geometry only, stays */
  if (kind == OpKind::Compact) {
    idx_.invalidate();
    idx_.flush(k_);
    grid_.rebuild_if_used(k_);
  }
  else {
    idx_.refresh_regions(k_);
  }
  sync_index_stamp();
  if (c == EditCode::Ok) {
    /* barriers collect after they commit (plan P4a §5.2): the snapshot is
     * in the journal by now, so its refs are roots */
    (void)collect_internal({}, nullptr);
  }
  return c;
}

template <geom::CoordType C> ValidationReport Editor<C>::validate_region_barrier()
{
  ValidationReport r;
  const VertexTopoView v{k_.verts.template get<".v.e">(),
                         k_.verts.template get<".v.container">(),
                         k_.verts.template get<".id">(),
                         k_.verts.template get<".live">(),
                         k_.verts.capacity()};
  validate_combinatorial(k_, v, r); /* shells and regions are checked here */
  if (!r.ok()) {
    return r;
  }
  /* the one embedding check that reads regions (ContainerGeometry), skipped
   * while they were dirty: every element with a region container, found by
   * a column scan (the index isn't refreshed yet) */
  ValidateFilter flt;
  const auto &vc = k_.verts.template get<".v.container">();
  const auto &ec = k_.edges.template get<".e.container">();
  k_.verts.for_each_live([&](uint32_t i) {
    if (vc[i].kind() == ident::ContainerKind::Region) {
      flt.verts.append(i);
    }
  });
  k_.edges.for_each_live([&](uint32_t i) {
    if (ec[i].kind() == ident::ContainerKind::Region) {
      flt.edges.append(i);
    }
  });
  if (flt.verts.size() == 0 && flt.edges.size() == 0) {
    return r;
  }
  /* region 0's ray through the grid only if it is already built: a barrier
   * never builds it (about as costly as the rebuild it saves) */
  if (grid_.valid()) {
    grid_.flush(k_);
    flt.grid = &grid_.grid();
  }
  validate_embedding(k_, r, &flt);
  return r;
}

} // namespace ubertopo::topo
