#pragma once

/* Editor primitives, P3b W6: the split core shared by split_face and
 * imprint, island imprint and add_hole, unimprint and remove_hole (plan P3b
 * §3.6, C14). Included only by editor.cc. */

#include <algorithm>
#include <cmath>

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/face_geom.h"

namespace ubertopo::topo {

namespace edit_detail {
inline uint32_t vertex_degree(const auto &k, VertRef v)
{
  uint32_t n = 0;
  k.for_each_edge_at(v, [&](EdgeRef) { n++; });
  return n;
}
} // namespace edit_detail

/* ---- the chain of an imprint: edges in path order -> vertices V0..Vn ---- */

template <geom::CoordType C>
EditCode Editor<C>::read_chain(FaceRef f,
                               std::span<const EdgeRef> chain,
                               litestl::util::Vector<uint32_t, 32> &vs,
                               bool &closed)
{
  using ident::ElemKind;
  vs.clear();
  closed = false;
  if (chain.empty()) {
    return fail_report(EditCode::BadArgument, ElemKind::Face, f.i, "empty chain");
  }
  ident::ContainerRef cf;
  ident::ContainerRef::face(f, cf);
  for (size_t i = 0; i < chain.size(); i++) {
    const EdgeRef e = chain[i];
    if (!k_.edges.live(e.i)) {
      return fail_report(EditCode::DeadElement, ElemKind::Edge, e.i);
    }
    if (k_.edges.template get<".e.c">()[e.i] != NONE) {
      return fail_report(EditCode::NotWire, ElemKind::Edge, e.i);
    }
    if (!(k_.edges.template get<".e.container">()[e.i] == cf)) {
      return fail_report(EditCode::NotEmbedded, ElemKind::Edge, e.i, "a chain edge is not embedded in the face");
    }
    for (size_t j = 0; j < i; j++) {
      if (chain[j] == e) {
        return fail_report(EditCode::ChainNotSimple, ElemKind::Edge, e.i, "an edge repeats");
      }
    }
  }
  /* V0: for one edge its vs[0]; otherwise the end of edge 0 not on edge 1 */
  const auto e0 = k_.edge_verts(chain[0]);
  VertRef v = e0[0];
  if (chain.size() > 1) {
    const auto e1 = k_.edge_verts(chain[1]);
    v = (e0[0] == e1[0] || e0[0] == e1[1]) ? e0[1] : e0[0];
  }
  vs.append(v.i);
  for (const EdgeRef e : chain) {
    const auto ev = k_.edge_verts(e);
    if (ev[0] != v && ev[1] != v) {
      return fail_report(EditCode::ChainNotSimple, ElemKind::Edge, e.i, "the chain is not connected");
    }
    v = ev[0] == v ? ev[1] : ev[0];
    vs.append(v.i);
  }
  closed = chain.size() > 2 && vs[0] == vs.last();
  const int n_unique = closed ? vs.size() - 1 : vs.size();
  for (int i = 0; i < n_unique; i++) {
    for (int j = 0; j < i; j++) {
      if (vs[i] == vs[j]) {
        return fail_report(EditCode::ChainNotSimple, ElemKind::Vertex, vs[i], "a vertex repeats");
      }
    }
  }
  /* interior vertices (all of them for a cycle): embedded in f, degree 2 */
  const int lo = closed ? 0 : 1, hi = closed ? n_unique : n_unique - 1;
  for (int i = lo; i < hi; i++) {
    const VertRef w(vs[i]);
    if (!(k_.verts.template get<".v.container">()[w.i] == cf)) {
      return fail_report(EditCode::NotEmbedded, ElemKind::Vertex, w.i, "a chain vertex is not embedded in the face");
    }
    if (edit_detail::vertex_degree(k_, w) != 2) {
      return fail_report(EditCode::ChainNotSimple, ElemKind::Vertex, w.i, "a chain vertex has other edges");
    }
  }
  return EditCode::Ok;
}

template <geom::CoordType C>
EditCode Editor<C>::fail_report(EditCode c, ident::ElemKind kind, uint32_t index, const char *detail)
{
  rep_.code = c;
  rep_.kind = kind;
  rep_.index = index;
  rep_.detail = detail;
  return c;
}

/* ---- split core: f is split by a path a = V0 .. Vn = b ---- */

template <geom::CoordType C>
EditCode Editor<C>::split_core(FaceRef f,
                               std::span<const uint32_t> chain_e,
                               std::span<const uint32_t> chain_v,
                               bool make_chord,
                               FaceRef *other,
                               EdgeRef *chord)
{
  using ident::ElemKind;
  const VertRef a(chain_v[0]), b(chain_v[chain_v.size() - 1]);
  const uint32_t n = uint32_t(chain_v.size()) - 1;
  if (a == b) {
    return fail_report(EditCode::BadArgument, ElemKind::Vertex, a.i);
  }
  /* a and b on one loop; that loop must be the outer one */
  CoedgeRef ca, cb;
  uint32_t ordinal = 0, l_ordinal = NONE;
  litestl::util::Vector<uint32_t, 8> loops;
  bool on_two_loops = false;
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
    else if (xa || xb) {
      on_two_loops = true;
    }
    ordinal++;
  });
  if (!ca) {
    return fail_report(make_chord || !on_two_loops ? EditCode::NotInLoop : EditCode::NotOuterLoop, ElemKind::Face, f.i);
  }
  const int axis = edit_detail::face_axis<C>(k_, f);
  FacePoints<C> fp;
  if (axis < 0 || !fp.gather(k_, f)) {
    return fail_report(EditCode::DegenerateLoop, ElemKind::Face, f.i);
  }
  if (loops.size() > 1 && geom::outer_loop_proj<C>(fp.view(), axis) != l_ordinal) {
    return fail_report(EditCode::NotOuterLoop, ElemKind::Face, f.i);
  }
  if (make_chord) {
    if (k_.next(ca) == cb || k_.next(cb) == ca) {
      return fail_report(EditCode::Adjacent, ElemKind::Face, f.i);
    }
    if (find_edge(a, b)) {
      return fail_report(EditCode::EdgeExists, ElemKind::Edge, find_edge(a, b).i);
    }
  }
  const ident::PersistentId fid = k_.face_id(f);
  const ident::PersistentId id_l = ident::derive(ident::tag::OpDerived, fid, std::string_view("split_face"), 0u);
  const ident::PersistentId id_r = ident::derive(ident::tag::OpDerived, fid, std::string_view("split_face"), 1u);
  const ident::PersistentId id_e = make_chord ? ident::edge_id(k_.vert_id(a), k_.vert_id(b)) : ident::kNoId;
  if (id_used(Grp::Faces, id_l) || id_used(Grp::Faces, id_r) || (make_chord && id_used(Grp::Edges, id_e))) {
    return fail_report(EditCode::DuplicateId, ElemKind::Face, f.i);
  }
  /* the right child: path P1 = ca .. prev(cb) (a -> b) closed by the chain b -> a */
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
  for (uint32_t i = n; i >= 1; i--) {
    right_pts.append(k_.pos(VertRef(chain_v[i])));
  }
  const uint32_t right_off[2] = {0, uint32_t(right_pts.size())};
  const geom::PolyView<C> right_view{std::span<const Point>(right_pts.data(), right_pts.size()),
                                     std::span<const uint32_t>(right_off, 2)};
  /* neither child may be degenerate (all its corners on one line) */
  {
    litestl::util::Vector<Point, 32> left_pts;
    for (uint32_t c : p2) {
      left_pts.append(k_.pos(k_.start_vert(CoedgeRef(c))));
    }
    for (uint32_t i = 0; i < n; i++) {
      left_pts.append(k_.pos(VertRef(chain_v[i])));
    }
    const uint32_t left_off[2] = {0, uint32_t(left_pts.size())};
    const geom::PolyView<C> left_view{std::span<const Point>(left_pts.data(), left_pts.size()),
                                      std::span<const uint32_t>(left_off, 2)};
    bool deg_l = false, deg_r = false;
    face_points_planar<C>(left_view, &deg_l);
    face_points_planar<C>(right_view, &deg_r);
    if (deg_l || deg_r) {
      return fail_report(EditCode::DegenerateLoop, ElemKind::Face, f.i, "a child would be degenerate");
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
      return fail_report(EditCode::HoleOnChord, ElemKind::Loop, loops[i]);
    }
    (pc == geom::PointClass::Inside ? holes_r : holes_l).append(loops[i]);
  }
  /* the chain's own elements are not inclusions of either child */
  litestl::util::Vector<uint32_t, 32> chain_packed;
  for (uint32_t e : chain_e) {
    chain_packed.append((e << 1) | 1);
  }
  for (uint32_t i = 1; i < n; i++) {
    chain_packed.append(chain_v[i] << 1);
  }
  std::sort(chain_packed.begin(), chain_packed.end());
  litestl::util::Vector<uint32_t, 16> incl, incl_r;
  inclusions(f, incl);
  for (uint32_t x : incl) {
    if (std::binary_search(chain_packed.begin(), chain_packed.end(), x)) {
      continue;
    }
    geom::PointClass pc;
    if (x & 1) {
      const auto evs = k_.edge_verts(EdgeRef(x >> 1));
      pc = side_of(k_.pos(evs[0]));
      if (side_of(k_.pos(evs[1])) != pc) {
        pc = geom::PointClass::Boundary;
      }
    }
    else {
      pc = side_of(k_.pos(VertRef(x >> 1)));
    }
    if (pc == geom::PointClass::Boundary) {
      return fail_report(EditCode::InclusionOnChord, (x & 1) ? ElemKind::Edge : ElemKind::Vertex, x >> 1);
    }
    if (pc == geom::PointClass::Inside) {
      incl_r.append(x);
    }
  }
  const bool was_projected = k_.face_proj_axis(f) >= 0;
  const double area0 = edit_detail::face_area<C>(k_, f);

  /* the chord edge (split_face), then two coedges per chain edge */
  litestl::util::Vector<uint32_t, 32> es;
  if (make_chord) {
    const bool a_lo = k_.vert_id(a) < k_.vert_id(b);
    const EdgeRef e(alloc(Grp::Edges));
    setE<".e.vs">(e, a_lo ? U32x2{a.i, b.i} : U32x2{b.i, a.i});
    setE<".id">(e, id_e);
    disk_insert(e, 0);
    disk_insert(e, 1);
    es.append(e.i);
  }
  else {
    for (uint32_t e : chain_e) {
      es.append(e);
    }
  }
  const CoedgeRef pa = k_.prev(ca), pb = k_.prev(cb);
  /* x1[i]: V(i+1) -> V(i) on edge i (right child); x2[i]: V(i) -> V(i+1) (left) */
  litestl::util::Vector<uint32_t, 32> x1, x2;
  for (uint32_t i = 0; i < n; i++) {
    x1.append(alloc(Grp::Coedges));
    x2.append(alloc(Grp::Coedges));
  }
  const LoopRef L = k_.loop_of(ca);
  const FaceRef fr(alloc(Grp::Faces));
  const LoopRef lr(alloc(Grp::Loops));
  for (uint32_t i = 0; i < n; i++) {
    const EdgeRef e(es[i]);
    const VertRef vi(chain_v[i]), vj(chain_v[i + 1]);
    const bool fwd = k_.edge_verts(e)[0] == vi; /* the edge runs V(i) -> V(i+1) */
    const CoedgeRef c1(x1[i]), c2(x2[i]);
    setC<".c.e">(c1, e.i);
    setC<".c.v">(c1, vj.i);
    setC<".c.flags">(c1, uint8_t(fwd ? 0 : coedge_flags::Sense));
    setC<".c.l">(c1, lr.i);
    setC<".c.e">(c2, e.i);
    setC<".c.v">(c2, vi.i);
    setC<".c.flags">(c2, uint8_t(fwd ? coedge_flags::Sense : 0));
    setC<".c.l">(c2, L.i);
  }
  /* right loop: ... pb -> x1[n-1] -> ... -> x1[0] -> ca ...;
   * left loop: ... pa -> x2[0] -> ... -> x2[n-1] -> cb ... */
  for (uint32_t i = 0; i < n; i++) {
    const CoedgeRef c1(x1[i]), c2(x2[i]);
    setC<".c.next">(c1, i == 0 ? ca.i : x1[i - 1]);
    setC<".c.prev">(c1, i + 1 == n ? pb.i : x1[i + 1]);
    setC<".c.next">(c2, i + 1 == n ? cb.i : x2[i + 1]);
    setC<".c.prev">(c2, i == 0 ? pa.i : x2[i - 1]);
  }
  setC<".c.next">(pb, x1[n - 1]);
  setC<".c.prev">(ca, x1[0]);
  setC<".c.next">(pa, x2[0]);
  setC<".c.prev">(cb, x2[n - 1]);
  setL<".l.c">(L, x2[0]);
  setL<".l.size">(L, uint32_t(p2.size()) + n);
  for (uint32_t c : p1) {
    setC<".c.l">(CoedgeRef(c), lr.i);
  }
  setL<".l.c">(lr, x1[n - 1]);
  setL<".l.size">(lr, uint32_t(p1.size()) + n);
  /* chain elements stop being inclusions */
  if (!make_chord) {
    const ident::ContainerRef none{};
    for (uint32_t e : chain_e) {
      setE<".e.container">(EdgeRef(e), none);
      setE<".e.container_hint">(EdgeRef(e), ident::kNoId);
    }
    for (uint32_t i = 1; i < n; i++) {
      setV<".v.container">(VertRef(chain_v[i]), none);
      setV<".v.container_hint">(VertRef(chain_v[i]), ident::kNoId);
    }
  }
  /* faces: Topo columns of the new child copy the parent (C1) */
  setF<".id">(f, id_l);
  setF<".id">(fr, id_r);
  setF<".f.flags">(fr, k_.face_flags(f));
  setF<".f.surface">(fr, k_.faces.template get<".f.surface">()[f.i]);
  setS<".fs.hint">(2 * fr.i, k_.side_hint(f, 0));
  setS<".fs.hint">(2 * fr.i + 1, k_.side_hint(f, 1));
  /* loop chains: the split loop's part first, then holes in their order */
  auto chain_loops = [&](FaceRef face, ident::PersistentId face_id, LoopRef first, const auto &holes) {
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
  chain_loops(f, id_l, L, holes_l);
  chain_loops(fr, id_r, lr, holes_r);
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
  /* chain radial cycles, then the edges whose face IDs changed (C4, C5) */
  for (uint32_t i = 0; i < n; i++) {
    const bool x1_first = id_r < id_l;
    const uint32_t order[2] = {x1_first ? x1[i] : x2[i], x1_first ? x2[i] : x1[i]};
    link_radial(EdgeRef(es[i]), std::span<const uint32_t>(order, 2));
  }
  fix_projection(f);
  fix_projection(fr);
  for (const FaceRef face : {f, fr}) {
    if (was_projected) {
      const EditCode rc = resort_face_edges(face);
      if (rc != EditCode::Ok) {
        return rc;
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
  cm.add(x1[n - 1], {{cb.i, 1.0}});
  cm.add(x2[0], {{ca.i, 1.0}});
  if (!propagate(Grp::Faces, PropEvent::Split, fm) || !propagate(Grp::Sides, PropEvent::Split, sm) ||
      !propagate(Grp::Loops, PropEvent::Split, lm) || !propagate(Grp::Coedges, PropEvent::Split, cm))
  {
    return fail_report(EditCode::PropagationFailed, ElemKind::Face, f.i);
  }
  lineage_.push_back({fid, id_l, ElemKind::Face, LineageKind::Split});
  lineage_.push_back({fid, id_r, ElemKind::Face, LineageKind::Split});
  if (other) {
    *other = fr;
  }
  if (chord) {
    *chord = EdgeRef(es[0]);
  }
  return EditCode::Ok;
}

/* ---- island core: a closed chain becomes a hole of f, and (make_face) the
 * outer loop of a new face filling it ---- */

template <geom::CoordType C>
EditCode Editor<C>::island_core(FaceRef f,
                                std::span<const uint32_t> chain_e,
                                std::span<const uint32_t> chain_v, /* V0..Vn, Vn == V0 */
                                bool make_face,
                                FaceRef *island)
{
  using ident::ElemKind;
  const uint32_t n = uint32_t(chain_e.size());
  const int axis = edit_detail::face_axis<C>(k_, f);
  FacePoints<C> fp;
  if (axis < 0 || !fp.gather(k_, f)) {
    return fail_report(EditCode::DegenerateLoop, ElemKind::Face, f.i);
  }
  litestl::util::Vector<uint32_t, 8> loops;
  k_.for_each_loop(f, [&](LoopRef l) { loops.append(l.i); });
  const uint32_t outer = loops.size() > 1 ? geom::outer_loop_proj<C>(fp.view(), axis) : 0;
  /* the cycle's polygon and orientations */
  litestl::util::Vector<Point, 32> cyc;
  for (uint32_t i = 0; i < n; i++) {
    cyc.append(k_.pos(VertRef(chain_v[i])));
  }
  const uint32_t cyc_off[2] = {0, n};
  const geom::PolyView<C> cyc_view{std::span<const Point>(cyc.data(), cyc.size()), std::span<const uint32_t>(cyc_off, 2)};
  {
    bool deg = false;
    face_points_planar<C>(cyc_view, &deg);
    if (deg) {
      return fail_report(EditCode::DegenerateLoop, ElemKind::Face, f.i, "the cycle is degenerate");
    }
  }
  litestl::util::Vector<Point, 32> outer_pts;
  k_.for_each_coedge(LoopRef(loops[int(outer)]), [&](CoedgeRef c) { outer_pts.append(k_.pos(k_.start_vert(c))); });
  const int o_outer = geom::loop_orientation_proj<C>(std::span<const Point>(outer_pts.data(), outer_pts.size()), axis);
  const int o_cyc = geom::loop_orientation_proj<C>(std::span<const Point>(cyc.data(), cyc.size()), axis);
  /* the new face's loop runs like f's outer loop; the hole the other way */
  const bool island_fwd = o_cyc == o_outer;
  auto inside = [&](const Point &p) { return geom::point_in_poly_proj<C>(p, cyc_view, axis); };
  for (int i = 0; i < loops.size(); i++) {
    if (uint32_t(i) == outer) {
      continue;
    }
    if (inside(k_.pos(k_.start_vert(k_.first_coedge(LoopRef(loops[i]))))) != geom::PointClass::Outside) {
      return fail_report(EditCode::HoleInside, ElemKind::Loop, loops[i], "a hole of the face lies inside the cycle");
    }
  }
  litestl::util::Vector<uint32_t, 32> chain_packed;
  for (uint32_t e : chain_e) {
    chain_packed.append((e << 1) | 1);
  }
  for (uint32_t i = 0; i < n; i++) {
    chain_packed.append(chain_v[i] << 1);
  }
  std::sort(chain_packed.begin(), chain_packed.end());
  litestl::util::Vector<uint32_t, 16> incl, incl_in;
  inclusions(f, incl);
  for (uint32_t x : incl) {
    if (std::binary_search(chain_packed.begin(), chain_packed.end(), x)) {
      continue;
    }
    geom::PointClass pc;
    if (x & 1) {
      const auto evs = k_.edge_verts(EdgeRef(x >> 1));
      pc = inside(k_.pos(evs[0]));
      if (inside(k_.pos(evs[1])) != pc) {
        pc = geom::PointClass::Boundary;
      }
    }
    else {
      pc = inside(k_.pos(VertRef(x >> 1)));
    }
    if (pc == geom::PointClass::Boundary) {
      return fail_report(EditCode::InclusionOnChord, (x & 1) ? ElemKind::Edge : ElemKind::Vertex, x >> 1);
    }
    if (pc == geom::PointClass::Inside) {
      if (!make_face) {
        return fail_report(EditCode::ContainedElement, (x & 1) ? ElemKind::Edge : ElemKind::Vertex, x >> 1,
                           "an inclusion would float in the opening");
      }
      incl_in.append(x);
    }
  }
  const ident::PersistentId fid = k_.face_id(f);
  ident::PersistentId gid{};
  if (make_face) {
    ident::PersistentId min_e = k_.edge_id(EdgeRef(chain_e[0]));
    for (uint32_t e : chain_e) {
      min_e = std::min(min_e, k_.edge_id(EdgeRef(e)));
    }
    gid = ident::derive(ident::tag::OpDerived, fid, std::string_view("imprint.island"), min_e);
    if (id_used(Grp::Faces, gid)) {
      return fail_report(EditCode::DuplicateId, ElemKind::Face, f.i);
    }
  }
  const bool was_projected = k_.face_proj_axis(f) >= 0;
  const double area0 = edit_detail::face_area<C>(k_, f);

  /* hole loop of f: the cycle against f's outer orientation */
  litestl::util::Vector<uint32_t, 32> hc, gc;
  for (uint32_t i = 0; i < n; i++) {
    hc.append(alloc(Grp::Coedges));
    if (make_face) {
      gc.append(alloc(Grp::Coedges));
    }
  }
  const LoopRef lh(alloc(Grp::Loops));
  FaceRef g;
  LoopRef lg;
  if (make_face) {
    g = FaceRef(alloc(Grp::Faces));
    lg = LoopRef(alloc(Grp::Loops));
  }
  /* coedge i of a loop running "forward" goes V(i) -> V(i+1); backward
   * coedge i goes V(i+1) -> V(i), and the loop order is reversed */
  auto build = [&](const litestl::util::Vector<uint32_t, 32> &cs, LoopRef l, bool fwd_loop) {
    for (uint32_t i = 0; i < n; i++) {
      const CoedgeRef c(cs[i]);
      const EdgeRef e(chain_e[i]);
      const VertRef from(fwd_loop ? chain_v[i] : chain_v[i + 1]);
      setC<".c.e">(c, e.i);
      setC<".c.v">(c, from.i);
      setC<".c.flags">(c, uint8_t(k_.edge_verts(e)[0] == from ? coedge_flags::Sense : 0));
      setC<".c.l">(c, l.i);
      const uint32_t nx = fwd_loop ? cs[(i + 1) % n] : cs[(i + n - 1) % n];
      const uint32_t pv = fwd_loop ? cs[(i + n - 1) % n] : cs[(i + 1) % n];
      setC<".c.next">(c, nx);
      setC<".c.prev">(c, pv);
    }
    setL<".l.c">(l, cs[0]);
    setL<".l.size">(l, n);
  };
  build(hc, lh, !island_fwd);
  /* append the hole to f's loop chain */
  {
    const LoopRef last(loops.last());
    setL<".l.next">(last, lh.i);
    setL<".l.next">(lh, NONE);
    setL<".l.f">(lh, f.i);
    setL<".id">(lh, ident::loop_id(fid, uint32_t(loops.size())));
  }
  const ident::ContainerRef none{};
  for (uint32_t e : chain_e) {
    setE<".e.container">(EdgeRef(e), none);
    setE<".e.container_hint">(EdgeRef(e), ident::kNoId);
  }
  for (uint32_t i = 0; i < n; i++) {
    setV<".v.container">(VertRef(chain_v[i]), none);
    setV<".v.container_hint">(VertRef(chain_v[i]), ident::kNoId);
  }
  if (make_face) {
    build(gc, lg, island_fwd);
    setL<".l.next">(lg, NONE);
    setL<".l.f">(lg, g.i);
    setL<".id">(lg, ident::loop_id(gid, 0));
    setF<".f.l">(g, lg.i);
    setF<".id">(g, gid);
    setF<".f.flags">(g, k_.face_flags(f));
    setF<".f.surface">(g, k_.faces.template get<".f.surface">()[f.i]);
    setS<".fs.hint">(2 * g.i, k_.side_hint(f, 0));
    setS<".fs.hint">(2 * g.i + 1, k_.side_hint(f, 1));
    ident::ContainerRef cg;
    ident::ContainerRef::face(g, cg);
    for (uint32_t x : incl_in) {
      if (x & 1) {
        setE<".e.container">(EdgeRef(x >> 1), cg);
      }
      else {
        setV<".v.container">(VertRef(x >> 1), cg);
      }
    }
  }
  /* radial cycles: the smaller face ID first */
  for (uint32_t i = 0; i < n; i++) {
    if (make_face) {
      const bool g_first = gid < fid;
      const uint32_t order[2] = {g_first ? gc[i] : hc[i], g_first ? hc[i] : gc[i]};
      link_radial(EdgeRef(chain_e[i]), std::span<const uint32_t>(order, 2));
    }
    else {
      const uint32_t order[1] = {hc[i]};
      link_radial(EdgeRef(chain_e[i]), std::span<const uint32_t>(order, 1));
    }
  }
  fix_projection(f);
  if (make_face) {
    fix_projection(g);
  }
  if (was_projected) {
    for (const FaceRef face : {f, g}) {
      if (!face) {
        continue;
      }
      const EditCode rc = resort_face_edges(face);
      if (rc != EditCode::Ok) {
        return rc;
      }
    }
  }
  set_dirty();
  /* propagation */
  MixBuild lm;
  const LoopRef L_outer(loops[int(outer)]);
  lm.add(lh.i, {{L_outer.i, 1.0}});
  if (make_face) {
    lm.add(lg.i, {{L_outer.i, 1.0}});
    const double ag = edit_detail::face_area<C>(k_, g);
    const double w_g = area0 > 0 ? std::min(1.0, ag / area0) : 0.5, w_f = 1.0 - w_g;
    MixBuild fm, sm;
    fm.add(f.i, {{f.i, w_f}});
    fm.add(g.i, {{f.i, w_g}});
    for (uint32_t s = 0; s < 2; s++) {
      sm.add(2 * f.i + s, {{2 * f.i + s, w_f}});
      sm.add(2 * g.i + s, {{2 * f.i + s, w_g}});
    }
    if (!propagate(Grp::Faces, PropEvent::Split, fm) || !propagate(Grp::Sides, PropEvent::Split, sm)) {
      return fail_report(EditCode::PropagationFailed, ElemKind::Face, f.i);
    }
    lineage_.push_back({fid, gid, ElemKind::Face, LineageKind::Split});
  }
  if (!propagate(Grp::Loops, PropEvent::Split, lm)) {
    return fail_report(EditCode::PropagationFailed, ElemKind::Face, f.i);
  }
  if (island) {
    *island = g;
  }
  return EditCode::Ok;
}

/* ---- drop a hole: its coedges and loop go, its edges and vertices become
 * inclusions of f; later holes are renumbered ---- */

template <geom::CoordType C> void Editor<C>::drop_hole(FaceRef f, LoopRef lh)
{
  const ident::PersistentId fid = k_.face_id(f);
  litestl::util::Vector<uint32_t, 32> cs;
  k_.for_each_coedge(lh, [&](CoedgeRef c) { cs.append(c.i); });
  ident::ContainerRef cf;
  ident::ContainerRef::face(f, cf);
  for (uint32_t c : cs) {
    radial_remove(CoedgeRef(c));
  }
  /* then the edges and vertices left without face edges become inclusions */
  for (uint32_t c : cs) {
    const EdgeRef e = k_.edge_of(CoedgeRef(c));
    if (k_.edges.template get<".e.c">()[e.i] == NONE) {
      setE<".e.container">(e, cf);
    }
    const VertRef v = k_.start_vert(CoedgeRef(c));
    bool face_edges = false;
    k_.for_each_edge_at(v, [&](EdgeRef x) { face_edges = face_edges || k_.edges.template get<".e.c">()[x.i] != NONE; });
    if (!face_edges) {
      setV<".v.container">(v, cf);
    }
  }
  for (uint32_t c : cs) {
    free(Grp::Coedges, c);
  }
  /* unlink from the loop chain and renumber */
  litestl::util::Vector<uint32_t, 8> keep;
  k_.for_each_loop(f, [&](LoopRef l) {
    if (l != lh) {
      keep.append(l.i);
    }
  });
  for (int i = 0; i < keep.size(); i++) {
    const LoopRef l(keep[i]);
    setL<".l.next">(l, i + 1 < keep.size() ? keep[i + 1] : NONE);
    const ident::PersistentId want = ident::loop_id(fid, uint32_t(i));
    if (!(k_.loops.template get<".id">()[l.i] == want)) {
      setL<".id">(l, want);
    }
  }
  free(Grp::Loops, lh.i);
}

/* ---- public primitives ---- */

template <geom::CoordType C>
EditCode Editor<C>::imprint(FaceRef f, std::span<const EdgeRef> chain, FaceRef *other)
{
  Prim pr(*this, OpKind::Imprint);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.faces.live(f.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Face, f.i);
  }
  litestl::util::Vector<uint32_t, 32> vs;
  bool closed = false;
  EditCode c = read_chain(f, chain, vs, closed);
  litestl::util::Vector<uint32_t, 32> es;
  for (const EdgeRef e : chain) {
    es.append(e.i);
  }
  if (c == EditCode::Ok) {
    if (closed) {
      c = island_core(f, std::span<const uint32_t>(es.data(), es.size()), std::span<const uint32_t>(vs.data(), vs.size()),
                      true, other);
    }
    else {
      c = split_core(f, std::span<const uint32_t>(es.data(), es.size()), std::span<const uint32_t>(vs.data(), vs.size()),
                     false, other, nullptr);
    }
  }
  if (c != EditCode::Ok) {
    return pr.fail(c, rep_.kind, rep_.index, rep_.detail);
  }
  pr.op.u.append(f.i);
  pr.op.u.append(uint32_t(es.size()));
  for (uint32_t e : es) {
    pr.op.u.append(e);
  }
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::add_hole(FaceRef f, std::span<const EdgeRef> chain)
{
  Prim pr(*this, OpKind::AddHole);
  if (pr.done) {
    return pr.code;
  }
  if (!k_.faces.live(f.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Face, f.i);
  }
  litestl::util::Vector<uint32_t, 32> vs;
  bool closed = false;
  EditCode c = read_chain(f, chain, vs, closed);
  if (c == EditCode::Ok && !closed) {
    c = fail_report(EditCode::ChainNotSimple, ident::ElemKind::Face, f.i, "a hole needs a closed chain");
  }
  litestl::util::Vector<uint32_t, 32> es;
  for (const EdgeRef e : chain) {
    es.append(e.i);
  }
  if (c == EditCode::Ok) {
    c = island_core(f, std::span<const uint32_t>(es.data(), es.size()), std::span<const uint32_t>(vs.data(), vs.size()),
                    false, nullptr);
  }
  if (c != EditCode::Ok) {
    return pr.fail(c, rep_.kind, rep_.index, rep_.detail);
  }
  pr.op.u.append(f.i);
  pr.op.u.append(uint32_t(es.size()));
  for (uint32_t e : es) {
    pr.op.u.append(e);
  }
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::remove_hole(FaceRef f, LoopRef l)
{
  Prim pr(*this, OpKind::RemoveHole);
  if (pr.done) {
    return pr.code;
  }
  using ident::ElemKind;
  if (!k_.faces.live(f.i) || !k_.loops.live(l.i) || k_.face_of(l) != f) {
    return pr.fail(EditCode::BadArgument, ElemKind::Loop, l.i);
  }
  const int axis = edit_detail::face_axis<C>(k_, f);
  FacePoints<C> fp;
  if (axis < 0 || !fp.gather(k_, f)) {
    return pr.fail(EditCode::DegenerateLoop, ElemKind::Face, f.i);
  }
  uint32_t ordinal = 0, mine = NONE, count = 0;
  k_.for_each_loop(f, [&](LoopRef x) {
    if (x == l) {
      mine = ordinal;
    }
    ordinal++;
    count++;
  });
  if (count < 2 || geom::outer_loop_proj<C>(fp.view(), axis) == mine) {
    return pr.fail(EditCode::NotOuterLoop, ElemKind::Loop, l.i, "the loop is the face's outer loop");
  }
  /* the hole's edges carry no other coedges; its vertices no other face edges */
  bool shared = false;
  k_.for_each_coedge(l, [&](CoedgeRef c) {
    shared = shared || radial_members(k_.edge_of(c)).size() != 1;
    k_.for_each_edge_at(k_.start_vert(c), [&](EdgeRef x) {
      bool on_loop = false;
      k_.for_each_radial(x, [&](CoedgeRef y) { on_loop = on_loop || k_.loop_of(y) == l; });
      shared = shared || (!on_loop && k_.edges.template get<".e.c">()[x.i] != NONE);
    });
  });
  if (shared) {
    return pr.fail(EditCode::EdgeShared, ElemKind::Loop, l.i);
  }
  drop_hole(f, l);
  const bool was_projected = k_.face_proj_axis(f) >= 0;
  fix_projection(f);
  if (was_projected) {
    const EditCode rc = resort_face_edges(f);
    if (rc != EditCode::Ok) {
      return pr.fail(rc, rep_.kind, rep_.index);
    }
  }
  set_dirty();
  pr.op.u.append(f.i);
  pr.op.u.append(l.i);
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::unimprint(EdgeRef e, FaceRef *survivor)
{
  Prim pr(*this, OpKind::Unimprint);
  if (pr.done) {
    return pr.code;
  }
  using ident::ElemKind;
  if (!k_.edges.live(e.i)) {
    return pr.fail(EditCode::DeadElement, ElemKind::Edge, e.i);
  }
  const auto m0 = radial_members(e);
  if (m0.size() != 2 || k_.face_of(m0[0]) == k_.face_of(m0[1])) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "the edge doesn't separate two faces");
  }
  FaceRef f1 = k_.face_of(m0[0]), f2 = k_.face_of(m0[1]);
  /* the maximal chain through e: edges between f1 and f2, interior vertices
   * of degree 2 */
  auto joins = [&](EdgeRef x) {
    const auto m = radial_members(x);
    return m.size() == 2 && ((k_.face_of(m[0]) == f1 && k_.face_of(m[1]) == f2) ||
                             (k_.face_of(m[0]) == f2 && k_.face_of(m[1]) == f1));
  };
  auto other_edge = [&](VertRef v, EdgeRef x) {
    EdgeRef o;
    uint32_t deg = 0;
    k_.for_each_edge_at(v, [&](EdgeRef y) {
      deg++;
      if (y != x) {
        o = y;
      }
    });
    return deg == 2 && joins(o) ? o : EdgeRef();
  };
  litestl::util::Vector<uint32_t, 32> es, vs; /* path order */
  bool closed = false;
  {
    /* walk backwards from vs[0] to the start, then forwards */
    EdgeRef x = e;
    VertRef v = k_.edge_verts(e)[0];
    for (;;) {
      const EdgeRef y = other_edge(v, x);
      if (!y || y == e) {
        closed = y == e;
        break;
      }
      x = y;
      const auto yv = k_.edge_verts(y);
      v = yv[0] == v ? yv[1] : yv[0];
    }
    /* x is the first edge, v its free end (or, closed, any vertex of it) */
    VertRef start = v;
    if (closed) {
      x = e;
      start = k_.edge_verts(e)[0];
    }
    EdgeRef cur = x;
    VertRef from = start;
    for (;;) {
      es.append(cur.i);
      vs.append(from.i);
      const auto cv = k_.edge_verts(cur);
      const VertRef to = cv[0] == from ? cv[1] : cv[0];
      const EdgeRef nx = other_edge(to, cur);
      if (!nx || nx.i == es[0]) {
        vs.append(to.i);
        break;
      }
      cur = nx;
      from = to;
    }
  }
  const uint32_t n = uint32_t(es.size());
  /* compatibility, as join_faces */
  for (uint32_t i = 0; i < n; i++) {
    const auto m = radial_members(EdgeRef(es[i]));
    if (k_.sense(m[0]) == k_.sense(m[1])) {
      return pr.fail(EditCode::NotJoinable, ElemKind::Edge, es[i], "inconsistent front sides");
    }
  }
  if (k_.face_flags(f1) != k_.face_flags(f2) ||
      !(k_.faces.template get<".f.surface">()[f1.i] == k_.faces.template get<".f.surface">()[f2.i]) ||
      !(k_.side_hint(f1, 0) == k_.side_hint(f2, 0)) || !(k_.side_hint(f1, 1) == k_.side_hint(f2, 1)))
  {
    return pr.fail(EditCode::Incompatible, ElemKind::Face, f2.i);
  }
  if (k_.face_proj_axis(f1) < 0 || k_.face_proj_axis(f2) < 0) {
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
  /* the chain's coedges on each face */
  auto chain_coedge = [&](uint32_t edge, FaceRef face) {
    CoedgeRef out;
    k_.for_each_radial(EdgeRef(edge), [&](CoedgeRef c) {
      if (k_.face_of(c) == face) {
        out = c;
      }
    });
    return out;
  };
  const LoopRef l1 = k_.loop_of(chain_coedge(es[0], f1)), l2 = k_.loop_of(chain_coedge(es[0], f2));
  ident::ContainerRef none{};
  if (closed) {
    /* island: one face's whole outer loop is the other's hole */
    FaceRef g = f1, f = f2;
    LoopRef lg = l1, lh = l2;
    auto loops_of = [&](FaceRef x) {
      uint32_t c = 0;
      k_.for_each_loop(x, [&](LoopRef) { c++; });
      return c;
    };
    if (!(k_.loop_size(l1) == n && loops_of(f1) == 1 && k_.first_loop(f1) == l1)) {
      std::swap(g, f);
      std::swap(lg, lh);
    }
    if (!(k_.loop_size(lg) == n && k_.first_loop(g) == lg) || k_.loop_size(lh) != n) {
      return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "the cycle is not an island");
    }
    if (loops_of(g) != 1) {
      return pr.fail(EditCode::HoleInside, ElemKind::Face, g.i, "the island has holes");
    }
    if (k_.first_loop(f) == lh) {
      return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "the cycle is the outer loop of both faces");
    }
    const double af = edit_detail::face_area<C>(k_, f), ag = edit_detail::face_area<C>(k_, g);
    MixBuild fm, sm;
    fm.add(f.i, {{f.i, af}, {g.i, ag}});
    for (uint32_t s = 0; s < 2; s++) {
      sm.add(2 * f.i + s, {{2 * f.i + s, af}, {2 * g.i + s, ag}});
    }
    if (!propagate(Grp::Faces, PropEvent::Merge, fm) || !propagate(Grp::Sides, PropEvent::Merge, sm)) {
      return pr.fail(EditCode::PropagationFailed, ElemKind::Face, f.i);
    }
    /* g's inclusions move to f; g and its loop go; the hole goes */
    litestl::util::Vector<uint32_t, 16> incl;
    inclusions(g, incl);
    ident::ContainerRef cf;
    ident::ContainerRef::face(f, cf);
    for (uint32_t x : incl) {
      if (x & 1) {
        setE<".e.container">(EdgeRef(x >> 1), cf);
      }
      else {
        setV<".v.container">(VertRef(x >> 1), cf);
      }
    }
    litestl::util::Vector<uint32_t, 32> gcs;
    k_.for_each_coedge(lg, [&](CoedgeRef c) { gcs.append(c.i); });
    for (uint32_t c : gcs) {
      radial_remove(CoedgeRef(c));
    }
    for (uint32_t c : gcs) {
      free(Grp::Coedges, c);
    }
    free(Grp::Loops, lg.i);
    const ident::PersistentId gid = k_.face_id(g);
    free(Grp::Faces, g.i);
    drop_hole(f, lh);
    const bool was_projected = k_.face_proj_axis(f) >= 0;
    fix_projection(f);
    if (was_projected) {
      const EditCode rc = resort_face_edges(f);
      if (rc != EditCode::Ok) {
        return pr.fail(rc, rep_.kind, rep_.index);
      }
    }
    set_dirty();
    lineage_.push_back({gid, k_.face_id(f), ElemKind::Face, LineageKind::Merge});
    if (survivor) {
      *survivor = f;
    }
    pr.op.u.append(e.i);
    return pr.finish();
  }
  /* split case: the faces share exactly the chain */
  {
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
    if (shared_e != n || shared_v != n + 1) {
      return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "the faces share more than the chain");
    }
  }
  if (k_.face_id(f2) < k_.face_id(f1)) {
    std::swap(f1, f2);
  }
  const FaceRef fs = f1, fa = f2;
  const LoopRef ls = k_.loop_of(chain_coedge(es[0], fs)), la = k_.loop_of(chain_coedge(es[0], fa));
  const double as = edit_detail::face_area<C>(k_, fs), aa = edit_detail::face_area<C>(k_, fa);
  const ident::PersistentId ids = k_.face_id(fs), ida = k_.face_id(fa);
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
  /* the chain's runs of coedges on each loop: first and last of each */
  litestl::util::Vector<uint32_t, 32> run_s, run_a;
  for (uint32_t x : es) {
    run_s.append(chain_coedge(x, fs).i);
    run_a.append(chain_coedge(x, fa).i);
  }
  auto run_ends = [&](const litestl::util::Vector<uint32_t, 32> &run, CoedgeRef &first, CoedgeRef &last) {
    auto in_run = [&](CoedgeRef c) { return std::find(run.begin(), run.end(), c.i) != run.end(); };
    for (uint32_t c : run) {
      if (!in_run(k_.prev(CoedgeRef(c)))) {
        first = CoedgeRef(c);
      }
      if (!in_run(k_.next(CoedgeRef(c)))) {
        last = CoedgeRef(c);
      }
    }
  };
  CoedgeRef s_first, s_last, a_first, a_last;
  run_ends(run_s, s_first, s_last);
  run_ends(run_a, a_first, a_last);
  /* splice: p1 -> n2 ... p2 -> n1 */
  const CoedgeRef p1 = k_.prev(s_first), n1 = k_.next(s_last), p2 = k_.prev(a_first), n2 = k_.next(a_last);
  const uint32_t size = k_.loop_size(ls) + k_.loop_size(la) - 2 * n;
  litestl::util::Vector<uint32_t, 32> moved;
  k_.for_each_coedge(la, [&](CoedgeRef c) {
    if (std::find(run_a.begin(), run_a.end(), c.i) == run_a.end()) {
      moved.append(c.i);
    }
  });
  bool first_in_run = std::find(run_s.begin(), run_s.end(), k_.first_coedge(ls).i) != run_s.end();
  /* chain coedges leave the radial cycles first (while links are intact) */
  for (uint32_t i = 0; i < n; i++) {
    radial_remove(CoedgeRef(run_s[i]));
    radial_remove(CoedgeRef(run_a[i]));
  }
  setC<".c.next">(p1, n2.i);
  setC<".c.prev">(n2, p1.i);
  setC<".c.next">(p2, n1.i);
  setC<".c.prev">(n1, p2.i);
  for (uint32_t c : moved) {
    setC<".c.l">(CoedgeRef(c), ls.i);
  }
  if (first_in_run) {
    setL<".l.c">(ls, n1.i);
  }
  setL<".l.size">(ls, size);
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
  /* the chain becomes inclusions of the survivor */
  for (uint32_t x : es) {
    setE<".e.container">(EdgeRef(x), cr_s);
  }
  for (uint32_t i = 1; i < n; i++) {
    setV<".v.container">(VertRef(vs[i]), cr_s);
  }
  (void)none;
  for (uint32_t i = 0; i < n; i++) {
    free(Grp::Coedges, run_s[i]);
    free(Grp::Coedges, run_a[i]);
  }
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

} // namespace ubertopo::topo
