#pragma once

/* Editor<C>: vertex, edge and wire primitives (plan P3 §3.4, C1, C5, C6,
 * C8, C10, C11). Template definitions, included only by editor.cc. */

#include <cmath>

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/face_geom.h"
#include "ubertopo/topo/radial.h"

namespace ubertopo::topo {

namespace edit_detail {

template <geom::CoordType C> double dist(const geom::Point<C> &a, const geom::Point<C> &b)
{
  const auto da = geom::to_double3<C>(a), db = geom::to_double3<C>(b);
  const double x = da[0] - db[0], y = da[1] - db[1], z = da[2] - db[2];
  return std::sqrt(x * x + y * y + z * z);
}

/** p strictly inside segment [a, b] (exact). */
template <geom::CoordType C>
bool strictly_inside(const geom::Point<C> &p, const geom::Point<C> &a, const geom::Point<C> &b)
{
  for (int axis = 0; axis < 3; axis++) {
    if (geom::orient2d_proj<C>(a, b, p, axis) != 0) {
      return false;
    }
  }
  const int ca = geom::lex_compare<C>(p, a), cb = geom::lex_compare<C>(p, b);
  return ca != 0 && cb != 0 && ca != cb;
}

/** Fraction of the way from a to b (in double; 0.5 for a degenerate segment). */
template <geom::CoordType C>
double fraction(const geom::Point<C> &p, const geom::Point<C> &a, const geom::Point<C> &b)
{
  const double l = dist<C>(a, b);
  return l > 0 ? dist<C>(a, p) / l : 0.5;
}

} // namespace edit_detail

/* ---- vertices ---- */

template <geom::CoordType C> EditCode Editor<C>::make_vertex(const Point &p, ident::PersistentId id, VertRef *out)
{
  Prim pr(*this, OpKind::MakeVertex);
  if (pr.done) {
    return pr.code;
  }
  if (id == ident::kNoId) {
    return pr.fail(EditCode::BadArgument, ident::ElemKind::Vertex, NONE, "null id");
  }
  if (id_used(Grp::Verts, id)) {
    return pr.fail(EditCode::DuplicateId, ident::ElemKind::Vertex);
  }
  const VertRef v(alloc(Grp::Verts));
  setV<"pos">(v, p);
  setV<".id">(v, id);
  pr.op.pts.append(p);
  pr.op.ids.append(id);
  if (out) {
    *out = v;
  }
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::kill_vertex(VertRef v)
{
  Prim pr(*this, OpKind::KillVertex);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.verts.live(v.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Vertex, v.i);
  }
  if (k_.vert_edge(v)) {
    return pr.fail(EditCode::NotIsolated, ident::ElemKind::Vertex, v.i);
  }
  free(Grp::Verts, v.i);
  pr.op.u.append(v.i);
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::set_position(VertRef v, const Point &p)
{
  Prim pr(*this, OpKind::SetPosition);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.verts.live(v.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Vertex, v.i);
  }
  setV<"pos">(v, p);
  /* faces at v: their geometry changed (C5: a projected face that became
   * planar drops its axis; radial cycles of all their edges are re-derived) */
  litestl::util::Vector<uint32_t, 16> faces;
  k_.for_each_edge_at(v, [&](EdgeRef e) {
    k_.for_each_radial(e, [&](CoedgeRef c) {
      const uint32_t f = k_.face_of(c).i;
      if (std::find(faces.begin(), faces.end(), f) == faces.end()) {
        faces.append(f);
      }
    });
  });
  if (faces.size()) {
    set_dirty();
  }
  for (uint32_t f : faces) {
    if (k_.face_proj_axis(FaceRef(f)) < 0) {
      FacePoints<C> fp;
      if (!fp.gather(k_, FaceRef(f)) || !face_points_planar<C>(fp.view())) {
        return pr.fail(EditCode::NonPlanarFace, ident::ElemKind::Face, f);
      }
    }
    fix_projection(FaceRef(f));
  }
  for (uint32_t f : faces) {
    const EditCode c = resort_face_edges(FaceRef(f));
    if (c != EditCode::Ok) {
      return pr.fail(c, rep_.kind, rep_.index);
    }
  }
  pr.op.u.append(v.i);
  pr.op.pts.append(p);
  return pr.finish();
}

/* ---- wires ---- */

template <geom::CoordType C>
EditCode Editor<C>::make_edge(VertRef a, VertRef b, ident::PersistentId id, EdgeRef *out, EmbedTarget in)
{
  Prim pr(*this, OpKind::MakeEdge);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.verts.live(a.i) || !k_.verts.live(b.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Vertex, k_.verts.live(a.i) ? b.i : a.i);
  }
  if (a == b) {
    return pr.fail(EditCode::SameVertex, ident::ElemKind::Vertex, a.i);
  }
  if (find_edge(a, b)) {
    return pr.fail(EditCode::EdgeExists, ident::ElemKind::Edge, find_edge(a, b).i);
  }
  if (id == ident::kNoId) {
    id = ident::edge_id(k_.vert_id(a), k_.vert_id(b));
  }
  if (id_used(Grp::Edges, id)) {
    return pr.fail(EditCode::DuplicateId, ident::ElemKind::Edge);
  }
  /* edges run from the smaller vertex ID to the larger (the builder's rule) */
  const bool swap = k_.vert_id(b) < k_.vert_id(a);
  const VertRef v0 = swap ? b : a, v1 = swap ? a : b;
  const EdgeRef e(alloc(Grp::Edges));
  setE<".e.vs">(e, U32x2{v0.i, v1.i});
  setE<".id">(e, id);
  disk_insert(e, 0);
  disk_insert(e, 1);
  if (in.kind != EmbedTarget::Kind::None) {
    /* created embedded (a wire lying in a face must never be free, P3b C5) */
    const EditCode c = embed_impl(true, e.i, in);
    if (c != EditCode::Ok) {
      return pr.fail(c, rep_.kind, rep_.index, rep_.detail);
    }
  }
  pr.op.u.append(a.i);
  pr.op.u.append(b.i);
  pr.op.u.append(uint32_t(in.kind));
  pr.op.u.append(in.index);
  pr.op.ids.append(id);
  if (out) {
    *out = e;
  }
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::kill_edge(EdgeRef e)
{
  Prim pr(*this, OpKind::KillEdge);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.edges.live(e.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Edge, e.i);
  }
  if (k_.edge_coedge(e)) {
    return pr.fail(EditCode::NotWire, ident::ElemKind::Edge, e.i);
  }
  disk_remove(e, 0);
  disk_remove(e, 1);
  free(Grp::Edges, e.i);
  pr.op.u.append(e.i);
  return pr.finish();
}

/* ---- embedding (C6) ---- */

template <geom::CoordType C> EditCode Editor<C>::embed_vertex(VertRef v, EmbedTarget t)
{
  return embed_impl(false, v.i, t);
}
template <geom::CoordType C> EditCode Editor<C>::embed_edge(EdgeRef e, EmbedTarget t)
{
  return embed_impl(true, e.i, t);
}

template <geom::CoordType C> EditCode Editor<C>::embed_impl(bool is_edge, uint32_t index, EmbedTarget t)
{
  Prim pr(*this, is_edge ? OpKind::EmbedEdge : OpKind::EmbedVertex);
  if (pr.done) {
    return pr.code;
  }
  const ident::ElemKind kind = is_edge ? ident::ElemKind::Edge : ident::ElemKind::Vertex;
  if (is_edge ? !k_.edges.live(index) : !k_.verts.live(index)) {
    return pr.fail(EditCode::DeadElement, kind, index);
  }
  if (is_edge && k_.edge_coedge(EdgeRef(index))) {
    return pr.fail(EditCode::NotWire, kind, index);
  }
  if (!is_edge && k_.vert_edge(VertRef(index))) {
    return pr.fail(EditCode::NotIsolated, kind, index);
  }
  ident::ContainerRef cr;
  ident::PersistentId hint = ident::kNoId;
  if (t.kind == EmbedTarget::Kind::Face) {
    if (!k_.faces.live(t.index) || !ident::ContainerRef::face(FaceRef(t.index), cr)) {
      return pr.fail(EditCode::DeadElement, ident::ElemKind::Face, t.index);
    }
  }
  else if (t.kind == EmbedTarget::Kind::Region) {
    if (k_.regions_dirty) {
      return pr.fail(EditCode::RegionsDirty, ident::ElemKind::Region, t.index);
    }
    if (!k_.regions.live(t.index) || !ident::ContainerRef::region(RegionRef(t.index), cr)) {
      return pr.fail(EditCode::DeadElement, ident::ElemKind::Region, t.index);
    }
    if (t.index == 0) {
      hint = ident::unbounded_region_id();
    }
    else {
      /* the region's winning hint (build_regions' vote: most sides, ties to
       * the smaller hint), if no other region wins it (P3b C4) */
      hint = region_winning_hint(t.index);
      const bool ambiguous = !(hint == ident::kNoId) && idx_.other_winner(t.index, hint);
      if (hint == ident::kNoId || ambiguous) {
        return pr.fail(EditCode::UnhintedRegion, ident::ElemKind::Region, t.index);
      }
    }
  }
  if (is_edge) {
    setE<".e.container">(EdgeRef(index), cr);
    setE<".e.container_hint">(EdgeRef(index), hint);
  }
  else {
    setV<".v.container">(VertRef(index), cr);
    setV<".v.container_hint">(VertRef(index), hint);
  }
  pr.op.u.append(index);
  pr.op.u.append(uint32_t(t.kind));
  pr.op.u.append(t.index);
  return pr.finish();
}

/* ---- split_edge / join_edge ---- */

template <geom::CoordType C>
EditCode Editor<C>::split_edge(EdgeRef e, const Point &p, VertRef *v_out, EdgeRef *e_out)
{
  Prim pr(*this, OpKind::SplitEdge);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.edges.live(e.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Edge, e.i);
  }
  const auto vs = k_.edge_verts(e);
  const VertRef a = vs[0], b = vs[1];
  const Point pa = k_.pos(a), pb = k_.pos(b);
  if (!edit_detail::strictly_inside<C>(p, pa, pb)) {
    return pr.fail(EditCode::NotOnEdge, ident::ElemKind::Edge, e.i);
  }
  /* IDs: half r = 0 is the one at the endpoint with the smaller vertex ID */
  const ident::PersistentId eid = k_.edge_id(e);
  const uint32_t r_e = k_.vert_id(a) < k_.vert_id(b) ? 0 : 1;
  const ident::PersistentId id_e = ident::derive(ident::tag::OpDerived, eid, std::string_view("split_edge"), r_e);
  const ident::PersistentId id_e2 =
      ident::derive(ident::tag::OpDerived, eid, std::string_view("split_edge"), 1 - r_e);
  const ident::PersistentId id_v = ident::derive(ident::tag::OpDerived, eid, std::string_view("split_edge.v"));
  if (id_used(Grp::Verts, id_v) || id_used(Grp::Edges, id_e) || id_used(Grp::Edges, id_e2)) {
    return pr.fail(EditCode::DuplicateId, ident::ElemKind::Edge, e.i);
  }
  const double t = edit_detail::fraction<C>(p, pa, pb);
  const bool wire = !k_.edge_coedge(e);

  /* the new vertex (C1: a wire's container and hint carry over) */
  const VertRef v(alloc(Grp::Verts));
  setV<"pos">(v, p);
  setV<".id">(v, id_v);
  if (wire) {
    setV<".v.container">(v, k_.edges.template get<".e.container">()[e.i]);
    setV<".v.container_hint">(v, k_.edges.template get<".e.container_hint">()[e.i]);
  }
  /* the new half (v, b) takes e's place in b's disk cycle */
  const EdgeRef e2(alloc(Grp::Edges));
  setE<".e.vs">(e2, U32x2{v.i, b.i});
  setE<".id">(e2, id_e2);
  setE<".e.container">(e2, k_.edges.template get<".e.container">()[e.i]);
  setE<".e.container_hint">(e2, k_.edges.template get<".e.container_hint">()[e.i]);
  setE<".e.curve">(e2, k_.edges.template get<".e.curve">()[e.i]);
  disk_replace(e, 1, e2, 1);
  setE<".e.vs">(e, U32x2{a.i, v.i});
  setE<".id">(e, id_e);
  disk_insert(e, 1);
  disk_insert(e2, 0);

  /* coedges: one new coedge per coedge of e, in the same loop (C11) */
  const auto members = radial_members(e);
  litestl::util::Vector<uint32_t, 8> twins;
  MixBuild corners;
  bool projected = false;
  for (const CoedgeRef c : members) {
    const CoedgeRef c2(alloc(Grp::Coedges));
    const LoopRef l = k_.loop_of(c);
    setC<".c.e">(c2, e2.i);
    setC<".c.l">(c2, l.i);
    setC<".c.flags">(c2, k_.coedges.template get<".c.flags">()[c.i]);
    if (k_.sense(c)) {
      /* c: a -> v; c2: v -> b, after c */
      const CoedgeRef n = k_.next(c);
      setC<".c.v">(c2, v.i);
      setC<".c.next">(c2, n.i);
      setC<".c.prev">(c2, c.i);
      setC<".c.prev">(n, c2.i);
      setC<".c.next">(c, c2.i);
      corners.add(c2.i, {{c.i, 1.0 - t}, {n.i, t}});
    }
    else {
      /* was b -> a; now c2: b -> v before c, and c: v -> a */
      const CoedgeRef pv = k_.prev(c), n = k_.next(c);
      setC<".c.v">(c2, b.i);
      setC<".c.v">(c, v.i);
      setC<".c.prev">(c2, pv.i);
      setC<".c.next">(c2, c.i);
      setC<".c.next">(pv, c2.i);
      setC<".c.prev">(c, c2.i);
      corners.add(c2.i, {{c.i, 1.0}});
      corners.add(c.i, {{c.i, t}, {n.i, 1.0 - t}});
    }
    setL<".l.size">(l, k_.loop_size(l) + 1);
    twins.append(c2.i);
    projected = projected || k_.face_proj_axis(k_.face_of(c)) >= 0;
  }
  link_radial(e2, std::span<const uint32_t>(twins.data(), twins.size()));

  /* projected faces: T(f) changed (C5) */
  if (projected) {
    set_dirty();
    for (const CoedgeRef c : members) {
      const FaceRef f = k_.face_of(c);
      if (k_.face_proj_axis(f) >= 0) {
        const EditCode rc = resort_face_edges(f);
        if (rc != EditCode::Ok) {
          return pr.fail(rc, rep_.kind, rep_.index);
        }
      }
    }
  }

  /* propagation */
  MixBuild vm, em;
  vm.add(v.i, {{a.i, 1.0 - t}, {b.i, t}});
  em.add(e.i, {{e.i, t}});
  em.add(e2.i, {{e.i, 1.0 - t}});
  if (!propagate(Grp::Verts, PropEvent::Interp, vm) || !propagate(Grp::Edges, PropEvent::Split, em) ||
      !propagate(Grp::Coedges, PropEvent::Interp, corners))
  {
    return pr.fail(EditCode::PropagationFailed, ident::ElemKind::Edge, e.i);
  }

  lineage_.push_back({eid, id_e, ident::ElemKind::Edge, LineageKind::Split});
  lineage_.push_back({eid, id_e2, ident::ElemKind::Edge, LineageKind::Split});
  lineage_.push_back({eid, id_v, ident::ElemKind::Vertex, LineageKind::Split});
  pr.op.u.append(e.i);
  pr.op.pts.append(p);
  if (v_out) {
    *v_out = v;
  }
  if (e_out) {
    *e_out = e2;
  }
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::join_edge(VertRef v, EdgeRef *survivor)
{
  Prim pr(*this, OpKind::JoinEdge);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.verts.live(v.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Vertex, v.i);
  }
  litestl::util::Vector<EdgeRef, 4> es;
  k_.for_each_edge_at(v, [&](EdgeRef e) { es.append(e); });
  if (es.size() != 2) {
    return pr.fail(EditCode::NotJoinable, ident::ElemKind::Vertex, v.i, "degree != 2");
  }
  EdgeRef e1 = es[0], e2 = es[1];
  if (k_.edge_id(e2) < k_.edge_id(e1)) {
    std::swap(e1, e2);
  }
  const uint32_t s1 = edge_slot(e1, v), s2 = edge_slot(e2, v);
  const VertRef a = k_.edge_verts(e1)[1 - s1], b = k_.edge_verts(e2)[1 - s2];
  if (a == b || find_edge(a, b)) {
    return pr.fail(EditCode::EdgeExists, ident::ElemKind::Vertex, v.i);
  }
  if (!edit_detail::strictly_inside<C>(k_.pos(v), k_.pos(a), k_.pos(b))) {
    return pr.fail(EditCode::NotJoinable, ident::ElemKind::Vertex, v.i, "not collinear");
  }
  const auto &ec = k_.edges.template get<".e.container">();
  const auto &eh = k_.edges.template get<".e.container_hint">();
  const auto &ev = k_.edges.template get<".e.curve">();
  if (!(ec[e1.i] == ec[e2.i]) || !(eh[e1.i] == eh[e2.i]) || !(ev[e1.i] == ev[e2.i])) {
    return pr.fail(EditCode::Incompatible, ident::ElemKind::Edge, e2.i);
  }
  /* v may only carry the edges' own container (a split wire's vertex, C1) */
  if (!(k_.vert_container(v) == ec[e1.i]) || !(k_.verts.template get<".v.container_hint">()[v.i] == eh[e1.i])) {
    return pr.fail(EditCode::Incompatible, ident::ElemKind::Vertex, v.i, "the vertex has its own container");
  }
  const auto m1 = radial_members(e1), m2 = radial_members(e2);
  if (m1.size() != m2.size()) {
    return pr.fail(EditCode::NotJoinable, ident::ElemKind::Edge, e2.i, "different radial cycles");
  }
  litestl::util::Vector<std::pair<CoedgeRef, CoedgeRef>, 8> pairs;
  for (const CoedgeRef c1 : m1) {
    const CoedgeRef n = k_.next(c1), p = k_.prev(c1);
    if (k_.edge_of(n) == e2) {
      pairs.append({c1, n});
    }
    else if (k_.edge_of(p) == e2) {
      pairs.append({c1, p});
    }
    else {
      return pr.fail(EditCode::NotJoinable, ident::ElemKind::Coedge, c1.i);
    }
  }
  const double l1 = edit_detail::dist<C>(k_.pos(v), k_.pos(a)), l2 = edit_detail::dist<C>(k_.pos(v), k_.pos(b));
  const ident::PersistentId id1 = k_.edge_id(e1), id2 = k_.edge_id(e2), idv = k_.vert_id(v);

  /* coedges: drop each c2, splicing its loop (C8) */
  MixBuild corners;
  bool projected = false;
  for (const auto &[c1, c2] : pairs) {
    const LoopRef l = k_.loop_of(c1);
    if (k_.next(c1) == c2) {
      const CoedgeRef n = k_.next(c2);
      setC<".c.next">(c1, n.i);
      setC<".c.prev">(n, c1.i);
    }
    else {
      const CoedgeRef p = k_.prev(c2);
      setC<".c.prev">(c1, p.i);
      setC<".c.next">(p, c1.i);
      setC<".c.v">(c1, k_.start_vert(c2).i);
      corners.add(c1.i, {{c2.i, 1.0}});
    }
    if (k_.first_coedge(l) == c2) {
      setL<".l.c">(l, c1.i);
    }
    setL<".l.size">(l, k_.loop_size(l) - 1);
    projected = projected || k_.face_proj_axis(k_.face_of(c1)) >= 0;
  }
  /* corner data moves before the dropped coedges are freed */
  if (!propagate(Grp::Coedges, PropEvent::Split, corners)) {
    return pr.fail(EditCode::PropagationFailed, ident::ElemKind::Vertex, v.i);
  }
  for (const auto &pc : pairs) {
    free(Grp::Coedges, pc.second.i);
  }
  /* disk cycles: e1 takes e2's place at b */
  disk_remove(e1, s1);
  disk_remove(e2, s2);
  disk_replace(e2, 1 - s2, e1, s1);
  U32x2 vs1 = k_.edges.template get<".e.vs">()[e1.i];
  vs1[s1] = b.i;
  setE<".e.vs">(e1, vs1);

  MixBuild em;
  em.add(e1.i, {{e1.i, l1}, {e2.i, l2}});
  if (!propagate(Grp::Edges, PropEvent::Merge, em)) {
    return pr.fail(EditCode::PropagationFailed, ident::ElemKind::Edge, e1.i);
  }
  free(Grp::Edges, e2.i);
  free(Grp::Verts, v.i);
  if (projected) {
    set_dirty();
    for (const auto &pc : pairs) {
      const FaceRef f = k_.face_of(pc.first);
      if (k_.face_proj_axis(f) >= 0) {
        fix_projection(f);
        const EditCode rc = resort_face_edges(f);
        if (rc != EditCode::Ok) {
          return pr.fail(rc, rep_.kind, rep_.index);
        }
      }
    }
  }
  lineage_.push_back({id2, id1, ident::ElemKind::Edge, LineageKind::Merge});
  lineage_.push_back({id1, id1, ident::ElemKind::Edge, LineageKind::Merge});
  pr.op.u.append(v.i);
  if (survivor) {
    *survivor = e1;
  }
  return pr.finish();
}

} // namespace ubertopo::topo
