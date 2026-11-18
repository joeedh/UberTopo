#pragma once

/* Editor<C>: face primitives, side hints, attributes and the regions barrier
 * (plan P3 §3.4, C1, C3–C8, C10–C12, C17). Template definitions, included
 * only by editor.cc. */

#include <algorithm>
#include <cmath>
#include <memory>

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
  FacePoints<C> fp;
  if (!fp.gather(k, f)) {
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
    litestl::util::Vector<geom::Triangle> tris;
    switch (classify_face<C>(view, k_.allows_projectable(), axis, tris)) {
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
  for (int i = 0; i < new_c.size(); i++) {
    const uint32_t one[1] = {new_c[i]};
    const EditCode rc = resort_radial(EdgeRef(edges[i]), std::span<const uint32_t>(one, 1));
    if (rc != EditCode::Ok) {
      return pr.fail(rc, rep_.kind, rep_.index);
    }
  }
  set_dirty();
  id_add(Grp::Faces, id, f.i);
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
  id_remove(Grp::Faces, k_.face_id(f));
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
  /* a and b on one loop; that loop must be the outer one */
  CoedgeRef ca, cb;
  uint32_t ordinal = 0, l_ordinal = NONE;
  litestl::util::Vector<uint32_t, 8> loops;
  k_.for_each_loop(f, [&](LoopRef l) {
    loops.append(l.i);
    CoedgeRef xa, xb;
    k_.for_each_coedge(l, [&](CoedgeRef c) {
      if (k_.start_vert(c) == a) {
        xa = c;
      }
      if (k_.start_vert(c) == b) {
        xb = c;
      }
    });
    if (xa && xb) {
      ca = xa;
      cb = xb;
      l_ordinal = ordinal;
    }
    ordinal++;
  });
  if (!ca) {
    return pr.fail(EditCode::NotInLoop, ElemKind::Face, f.i);
  }
  const int axis = edit_detail::face_axis<C>(k_, f);
  FacePoints<C> fp;
  if (axis < 0 || !fp.gather(k_, f)) {
    return pr.fail(EditCode::DegenerateLoop, ElemKind::Face, f.i);
  }
  if (loops.size() > 1 && geom::outer_loop_proj<C>(fp.view(), axis) != l_ordinal) {
    return pr.fail(EditCode::NotOuterLoop, ElemKind::Face, f.i);
  }
  if (k_.next(ca) == cb || k_.next(cb) == ca) {
    return pr.fail(EditCode::Adjacent, ElemKind::Face, f.i);
  }
  if (find_edge(a, b)) {
    return pr.fail(EditCode::EdgeExists, ElemKind::Edge, find_edge(a, b).i);
  }
  const ident::PersistentId fid = k_.face_id(f);
  const ident::PersistentId id_l = ident::derive(ident::tag::OpDerived, fid, std::string_view("split_face"), 0u);
  const ident::PersistentId id_r = ident::derive(ident::tag::OpDerived, fid, std::string_view("split_face"), 1u);
  const ident::PersistentId id_e = ident::edge_id(k_.vert_id(a), k_.vert_id(b));
  if (id_used(Grp::Faces, id_l) || id_used(Grp::Faces, id_r) || id_used(Grp::Edges, id_e)) {
    return pr.fail(EditCode::DuplicateId, ElemKind::Face, f.i);
  }
  /* the right child: path P1 = ca .. prev(cb) (a -> b) closed by the chord b -> a */
  litestl::util::Vector<uint32_t, 32> p1, p2;
  for (CoedgeRef c = ca; c != cb; c = k_.next(c)) {
    p1.append(c.i);
  }
  for (CoedgeRef c = cb; c != ca; c = k_.next(c)) {
    p2.append(c.i);
  }
  litestl::util::Vector<Point, 32> right_pts;
  for (uint32_t c : p1) {
    right_pts.append(k_.pos(k_.start_vert(CoedgeRef(c))));
  }
  right_pts.append(k_.pos(b));
  const uint32_t right_off[2] = {0, uint32_t(right_pts.size())};
  const geom::PolyView<C> right_view{std::span<const Point>(right_pts.data(), right_pts.size()),
                                     std::span<const uint32_t>(right_off, 2)};
  /* neither child may be degenerate (all its corners on one line) */
  {
    litestl::util::Vector<Point, 32> left_pts;
    for (uint32_t c : p2) {
      left_pts.append(k_.pos(k_.start_vert(CoedgeRef(c))));
    }
    left_pts.append(k_.pos(a));
    const uint32_t left_off[2] = {0, uint32_t(left_pts.size())};
    const geom::PolyView<C> left_view{std::span<const Point>(left_pts.data(), left_pts.size()),
                                      std::span<const uint32_t>(left_off, 2)};
    bool deg_l = false, deg_r = false;
    face_points_planar<C>(left_view, &deg_l);
    face_points_planar<C>(right_view, &deg_r);
    if (deg_l || deg_r) {
      return pr.fail(EditCode::DegenerateLoop, ElemKind::Face, f.i, "a child would be degenerate");
    }
  }
  auto side_of = [&](const Point &p) {
    return geom::point_in_poly_proj<C>(p, right_view, axis); /* Inside: right child */
  };
  /* holes and inclusions go to the child containing them */
  litestl::util::Vector<uint32_t, 8> holes_l, holes_r;
  for (int i = 0; i < loops.size(); i++) {
    if (uint32_t(i) == l_ordinal) {
      continue;
    }
    const geom::PointClass pc = side_of(k_.pos(k_.start_vert(k_.first_coedge(LoopRef(loops[i])))));
    if (pc == geom::PointClass::Boundary) {
      return pr.fail(EditCode::HoleOnChord, ElemKind::Loop, loops[i]);
    }
    (pc == geom::PointClass::Inside ? holes_r : holes_l).append(loops[i]);
  }
  litestl::util::Vector<uint32_t, 16> incl, incl_r;
  inclusions(f, incl);
  for (uint32_t x : incl) {
    geom::PointClass pc;
    if (x & 1) {
      const auto vs = k_.edge_verts(EdgeRef(x >> 1));
      pc = side_of(k_.pos(vs[0]));
      if (side_of(k_.pos(vs[1])) != pc) {
        pc = geom::PointClass::Boundary;
      }
    }
    else {
      pc = side_of(k_.pos(VertRef(x >> 1)));
    }
    if (pc == geom::PointClass::Boundary) {
      return pr.fail(EditCode::InclusionOnChord, (x & 1) ? ElemKind::Edge : ElemKind::Vertex, x >> 1);
    }
    if (pc == geom::PointClass::Inside) {
      incl_r.append(x);
    }
  }
  const bool was_projected = k_.face_proj_axis(f) >= 0;
  const double area0 = edit_detail::face_area<C>(k_, f);

  /* chord edge and its two coedges */
  const bool a_lo = k_.vert_id(a) < k_.vert_id(b);
  const EdgeRef e(alloc(Grp::Edges));
  setE<".e.vs">(e, a_lo ? U32x2{a.i, b.i} : U32x2{b.i, a.i});
  setE<".id">(e, id_e);
  disk_insert(e, 0);
  disk_insert(e, 1);
  const CoedgeRef pa = k_.prev(ca), pb = k_.prev(cb);
  const CoedgeRef x1(alloc(Grp::Coedges)), x2(alloc(Grp::Coedges));
  const LoopRef L = k_.loop_of(ca);
  const FaceRef fr(alloc(Grp::Faces));
  const LoopRef lr(alloc(Grp::Loops));
  /* x1: b -> a closes the right child; x2: a -> b closes the left one */
  setC<".c.e">(x1, e.i);
  setC<".c.v">(x1, b.i);
  setC<".c.flags">(x1, uint8_t(a_lo ? 0 : coedge_flags::Sense));
  setC<".c.next">(x1, ca.i);
  setC<".c.prev">(x1, pb.i);
  setC<".c.next">(pb, x1.i);
  setC<".c.prev">(ca, x1.i);
  setC<".c.e">(x2, e.i);
  setC<".c.v">(x2, a.i);
  setC<".c.flags">(x2, uint8_t(a_lo ? coedge_flags::Sense : 0));
  setC<".c.next">(x2, cb.i);
  setC<".c.prev">(x2, pa.i);
  setC<".c.next">(pa, x2.i);
  setC<".c.prev">(cb, x2.i);
  setC<".c.l">(x2, L.i);
  setL<".l.c">(L, x2.i);
  setL<".l.size">(L, uint32_t(p2.size()) + 1);
  for (uint32_t c : p1) {
    setC<".c.l">(CoedgeRef(c), lr.i);
  }
  setC<".c.l">(x1, lr.i);
  setL<".l.c">(lr, x1.i);
  setL<".l.size">(lr, uint32_t(p1.size()) + 1);
  /* faces: Topo columns of the new child copy the parent (C1) */
  setF<".id">(f, id_l);
  setF<".id">(fr, id_r);
  setF<".f.flags">(fr, k_.face_flags(f));
  setF<".f.surface">(fr, k_.faces.template get<".f.surface">()[f.i]);
  setS<".fs.hint">(2 * fr.i, k_.side_hint(f, 0));
  setS<".fs.hint">(2 * fr.i + 1, k_.side_hint(f, 1));
  /* loop chains: the split loop's part first, then holes in their order */
  auto chain = [&](FaceRef face, ident::PersistentId face_id, LoopRef first, const auto &holes) {
    setF<".f.l">(face, first.i);
    LoopRef prev = first;
    setL<".id">(first, ident::loop_id(face_id, 0));
    setL<".l.f">(first, face.i);
    for (int i = 0; i < holes.size(); i++) {
      const LoopRef h(holes[i]);
      setL<".l.next">(prev, h.i);
      setL<".l.f">(h, face.i);
      setL<".id">(h, ident::loop_id(face_id, uint32_t(i + 1)));
      prev = h;
    }
    setL<".l.next">(prev, NONE);
  };
  chain(f, id_l, L, holes_l);
  chain(fr, id_r, lr, holes_r);
  ident::ContainerRef cr_r;
  ident::ContainerRef::face(fr, cr_r);
  for (uint32_t x : incl_r) {
    if (x & 1) {
      setE<".e.container">(EdgeRef(x >> 1), cr_r);
    }
    else {
      setV<".v.container">(VertRef(x >> 1), cr_r);
    }
  }
  /* chord radial cycle, then the edges whose face IDs changed (C4, C5) */
  {
    const bool x1_first = id_r < id_l;
    const uint32_t order[2] = {x1_first ? x1.i : x2.i, x1_first ? x2.i : x1.i};
    link_radial(e, std::span<const uint32_t>(order, 2));
  }
  fix_projection(f);
  fix_projection(fr);
  for (const FaceRef face : {f, fr}) {
    if (was_projected) {
      const EditCode rc = resort_face_edges(face);
      if (rc != EditCode::Ok) {
        return pr.fail(rc, rep_.kind, rep_.index);
      }
    }
    else {
      k_.for_each_loop(face, [&](LoopRef l) { k_.for_each_coedge(l, [&](CoedgeRef c) { refresh_ec(k_.edge_of(c)); }); });
    }
  }
  set_dirty();

  /* propagation: measure fractions of the parent's area */
  const double ar = edit_detail::face_area<C>(k_, fr);
  const double fr_r = area0 > 0 ? std::min(1.0, ar / area0) : 0.5, fr_l = 1.0 - fr_r;
  MixBuild fm, sm, lm, cm;
  fm.add(f.i, {{f.i, fr_l}});
  fm.add(fr.i, {{f.i, fr_r}});
  for (uint32_t s = 0; s < 2; s++) {
    sm.add(2 * f.i + s, {{2 * f.i + s, fr_l}});
    sm.add(2 * fr.i + s, {{2 * f.i + s, fr_r}});
  }
  lm.add(lr.i, {{L.i, 1.0}});
  cm.add(x1.i, {{cb.i, 1.0}});
  cm.add(x2.i, {{ca.i, 1.0}});
  if (!propagate(Grp::Faces, PropEvent::Split, fm) || !propagate(Grp::Sides, PropEvent::Split, sm) ||
      !propagate(Grp::Loops, PropEvent::Split, lm) || !propagate(Grp::Coedges, PropEvent::Split, cm))
  {
    return pr.fail(EditCode::PropagationFailed, ElemKind::Face, f.i);
  }
  id_remove(Grp::Faces, fid);
  id_add(Grp::Faces, id_l, f.i);
  id_add(Grp::Faces, id_r, fr.i);
  id_add(Grp::Edges, id_e, e.i);
  lineage_.push_back({fid, id_l, ElemKind::Face, LineageKind::Split});
  lineage_.push_back({fid, id_r, ElemKind::Face, LineageKind::Split});
  pr.op.u.append(f.i);
  pr.op.u.append(a.i);
  pr.op.u.append(b.i);
  if (other) {
    *other = fr;
  }
  if (chord) {
    *chord = e;
  }
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
  id_remove(Grp::Faces, ida);
  id_remove(Grp::Edges, ide);
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

/* ---- regions barrier (C7, C13) ---- */

template <geom::CoordType C> EditCode Editor<C>::rebuild_regions()
{
  rep_ = EditReport{};
  if (!sp_.empty()) {
    rep_.code = EditCode::OpenTransaction;
    return rep_.code;
  }
  EditCode c = begin_internal("rebuild_regions");
  if (c != EditCode::Ok) {
    rep_.code = c;
    return c;
  }
  barrier_snapshot_ = std::make_unique<Complex<C>>(k_);
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
    ValidationReport r = validate(k_, ValidateLevel::Embedding);
    if (!r.ok()) {
      rep_.validation = r;
      rep_.code = EditCode::Invalid;
      rollback();
      return EditCode::Invalid;
    }
  }
  OpRecord<C> op;
  op.kind = OpKind::RebuildRegions;
  ops_.push_back(std::move(op));
  c = commit_internal(redoing_);
  for (IdIndex &x : ids_) {
    x.valid = false;
  }
  return c;
}

} // namespace ubertopo::topo
