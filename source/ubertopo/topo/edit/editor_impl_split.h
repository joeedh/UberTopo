#pragma once

/* Editor primitives: the split core of split_face, imprint and connect_loops
 * (plans P3b §3.6, 2026-11-24-connect-loops §3). Included only by editor.cc.
 *
 * f is split by a cut from a to b, vertices of its outer loop. The cut is one
 * piece (a chord to be made, or a chain of wires embedded in f), or several
 * with a hole between neighbours: the cut enters the hole at one vertex and
 * leaves it at another. */

#include <algorithm>
#include <string_view>

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/face_geom.h"

namespace ubertopo::topo {

template <geom::CoordType C>
EditCode Editor<C>::split_core(FaceRef f,
                               std::span<const CutPiece> pieces,
                               std::span<const CutHole> cut_holes,
                               bool make_chord,
                               std::string_view tag,
                               FaceRef *other,
                               EdgeRef *chord)
{
  using ident::ElemKind;
  const VertRef a(pieces.front().v.front()), b(pieces.back().v.back());
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
  Skeleton<C> sk(k_);
  FacePoints<C> fp;
  if (axis < 0 || !fp.gather(k_, f, sk)) {
    return fail_report(EditCode::DegenerateLoop, ElemKind::Face, f.i);
  }
  if (loops.size() > 1 && geom::outer_loop_proj<C>(fp.view(), axis) != l_ordinal) {
    return fail_report(EditCode::NotOuterLoop, ElemKind::Face, f.i);
  }
  if (make_chord) {
    if (pieces.size() == 1 && (k_.next(ca) == cb || k_.next(cb) == ca)) {
      return fail_report(EditCode::Adjacent, ElemKind::Face, f.i);
    }
    for (const CutPiece &p : pieces) {
      const EdgeRef e = find_edge(VertRef(p.v.front()), VertRef(p.v.back()));
      if (e) {
        return fail_report(EditCode::EdgeExists, ElemKind::Edge, e.i);
      }
    }
  }
  const ident::PersistentId fid = k_.face_id(f);
  const ident::PersistentId id_l = ident::derive(ident::tag::OpDerived, fid, tag, 0u);
  const ident::PersistentId id_r = ident::derive(ident::tag::OpDerived, fid, tag, 1u);
  const auto chord_id = [&](const CutPiece &p) {
    return ident::edge_id(k_.vert_id(VertRef(p.v.front())), k_.vert_id(VertRef(p.v.back())));
  };
  bool used = id_used(Grp::Faces, id_l) || id_used(Grp::Faces, id_r);
  for (const CutPiece &p : pieces) {
    used = used || (make_chord && id_used(Grp::Edges, chord_id(p)));
  }
  if (used) {
    return fail_report(EditCode::DuplicateId, ElemKind::Face, f.i);
  }
  /* The right child: path P1 = ca .. prev(cb) (a -> b), closed by the cut
   * b -> a. The left child: path P2 = cb .. prev(ca), closed by the cut
   * a -> b. Both follow next along a hole: the left from where the cut enters
   * it to where it leaves, the right the other way. */
  litestl::util::Vector<uint32_t, 32> p1, p2;
  for (CoedgeRef c = ca; c != cb; c = k_.next(c)) {
    p1.append(c.i);
  }
  for (CoedgeRef c = cb; c != ca; c = k_.next(c)) {
    p2.append(c.i);
  }
  litestl::util::Vector<litestl::util::Vector<uint32_t, 8>, 4> arc_r, arc_l; /* by cut hole */
  for (const CutHole &h : cut_holes) {
    litestl::util::Vector<uint32_t, 8> l, r;
    for (CoedgeRef c = h.in; c != h.out; c = k_.next(c)) {
      l.append(c.i);
    }
    for (CoedgeRef c = h.out; c != h.in; c = k_.next(c)) {
      r.append(c.i);
    }
    arc_l.append(l);
    arc_r.append(r);
  }
  const uint32_t np = uint32_t(pieces.size());
  /* the children's outer loops, as stored vertices and as refined skeletons
   * (plan P4b §4.3): vertex i of a piece, then the flattening points of the
   * piece's edge toward the next vertex (none for a chord to be made) */
  litestl::util::Vector<Point, 32> right_pts, left_pts, right_v, left_v;
  auto chain_run = [&](litestl::util::Vector<Point, 32> &pts, litestl::util::Vector<Point, 32> &vs, const CutPiece &pc,
                       uint32_t i, uint32_t to) {
    const Point &p = k_.pos(VertRef(pc.v[i]));
    pts.append(p);
    vs.append(p);
    const uint32_t ei = std::min(i, to);
    if (ei < pc.e.size()) {
      const EdgeRef e(pc.e[ei]);
      const auto in = sk.interior(e);
      const bool fwd = k_.edge_verts(e)[0].i == pc.v[i];
      for (size_t j = 0; j < in.size(); j++) {
        pts.append(sk.pos(in[fwd ? j : in.size() - 1 - j]));
      }
    }
  };
  auto loop_runs = [&](litestl::util::Vector<Point, 32> &pts, litestl::util::Vector<Point, 32> &vs,
                       const auto &path) {
    FacePoints<C> run;
    for (uint32_t c : path) {
      run.append_run(k_, CoedgeRef(c), &sk);
      vs.append(k_.pos(k_.start_vert(CoedgeRef(c))));
    }
    for (const Point &p : run.pts) {
      pts.append(p);
    }
  };
  loop_runs(right_pts, right_v, p1);
  for (uint32_t j = np; j-- > 0;) {
    for (uint32_t i = uint32_t(pieces[j].v.size()) - 1; i >= 1; i--) {
      chain_run(right_pts, right_v, pieces[j], i, i - 1);
    }
    if (j > 0) {
      loop_runs(right_pts, right_v, arc_r[int(j) - 1]);
    }
  }
  loop_runs(left_pts, left_v, p2);
  for (uint32_t j = 0; j < np; j++) {
    for (uint32_t i = 0; i + 1 < pieces[j].v.size(); i++) {
      chain_run(left_pts, left_v, pieces[j], i, i + 1);
    }
    if (j + 1 < np) {
      loop_runs(left_pts, left_v, arc_l[int(j)]);
    }
  }
  auto view = [](const litestl::util::Vector<Point, 32> &pts, const uint32_t *off) {
    return geom::PolyView<C>{std::span<const Point>(pts.data(), pts.size()), std::span<const uint32_t>(off, 2)};
  };
  const uint32_t right_off[2] = {0, uint32_t(right_pts.size())}, left_off[2] = {0, uint32_t(left_pts.size())};
  const uint32_t right_voff[2] = {0, uint32_t(right_v.size())}, left_voff[2] = {0, uint32_t(left_v.size())};
  const geom::PolyView<C> right_view = view(right_pts, right_off);
  /* neither child may be degenerate (face_planar_rule's view all on one line) */
  {
    bool deg_l = false, deg_r = false;
    face_planar_rule<C>(view(left_v, left_voff), view(left_pts, left_off), &deg_l);
    face_planar_rule<C>(view(right_v, right_voff), right_view, &deg_r);
    if (deg_l || deg_r) {
      return fail_report(EditCode::DegenerateLoop, ElemKind::Face, f.i, "a child would be degenerate");
    }
  }
  auto side_of = [&](const Point &p) {
    return geom::point_in_poly_proj<C>(p, right_view, axis); /* Inside: right child */
  };
  /* holes and inclusions go to the child containing them */
  litestl::util::Vector<uint32_t, 8> holes_l, holes_r;
  /* the children's loop vertices, as (vertex, bit 0: right, bit 1: left);
   * the cut's ends are on both */
  litestl::util::Vector<std::pair<uint32_t, uint32_t>, 32> loop_verts;
  for (uint32_t c : p1) {
    const uint32_t v = k_.start_vert(CoedgeRef(c)).i;
    loop_verts.append({v, v == a.i ? 3u : 1u});
  }
  for (uint32_t c : p2) {
    const uint32_t v = k_.start_vert(CoedgeRef(c)).i;
    loop_verts.append({v, v == b.i ? 3u : 2u});
  }
  for (uint32_t h = 0; h < cut_holes.size(); h++) {
    for (uint32_t c : arc_r[int(h)]) {
      loop_verts.append({k_.start_vert(CoedgeRef(c)).i, c == cut_holes[h].out.i ? 3u : 1u});
    }
    for (uint32_t c : arc_l[int(h)]) {
      loop_verts.append({k_.start_vert(CoedgeRef(c)).i, c == cut_holes[h].in.i ? 3u : 2u});
    }
  }
  for (int i = 0; i < loops.size(); i++) {
    bool cut = uint32_t(i) == l_ordinal;
    for (const CutHole &h : cut_holes) {
      cut = cut || k_.loop_of(h.in).i == loops[i];
    }
    if (cut) {
      continue;
    }
    const geom::PointClass pc = side_of(k_.pos(k_.start_vert(k_.first_coedge(LoopRef(loops[i])))));
    if (pc == geom::PointClass::Boundary) {
      return fail_report(EditCode::HoleOnChord, ElemKind::Loop, loops[i]);
    }
    (pc == geom::PointClass::Inside ? holes_r : holes_l).append(loops[i]);
    k_.for_each_coedge(LoopRef(loops[i]), [&](CoedgeRef c) {
      loop_verts.append({k_.start_vert(c).i, pc == geom::PointClass::Inside ? 1u : 2u});
    });
  }
  std::sort(loop_verts.begin(), loop_verts.end());
  /* the children a wire can be in by one of its ends: a vertex of f's loops
   * by its loop, any other by its position (none: on the cut) */
  auto end_sides = [&](VertRef v) -> uint32_t {
    const auto it = std::lower_bound(loop_verts.begin(), loop_verts.end(), std::pair<uint32_t, uint32_t>(v.i, 0u));
    if (it != loop_verts.end() && (*it).first == v.i) {
      return (*it).second;
    }
    const geom::PointClass pc = side_of(k_.pos(v));
    return pc == geom::PointClass::Inside ? 1u : pc == geom::PointClass::Outside ? 2u : 0u;
  };
  /* a point of the wire between its ends: a skeleton point, or the midpoint,
   * exactly */
  auto middle_side = [&](EdgeRef e) {
    const auto in = sk.interior(e);
    if (in.size() > 0) {
      return side_of(sk.pos(in[in.size() / 2]));
    }
    litestl::util::Vector<Point, 32> twice;
    for (const Point &p : right_pts) {
      twice.append(Point{p[0] + p[0], p[1] + p[1], p[2] + p[2]});
    }
    const auto evs = k_.edge_verts(e);
    const Point &p = k_.pos(evs[0]), &q = k_.pos(evs[1]);
    return geom::point_in_poly_proj<C>(Point{p[0] + q[0], p[1] + q[1], p[2] + q[2]}, view(twice, right_off), axis);
  };
  /* the chain's own elements are not inclusions of either child */
  litestl::util::Vector<uint32_t, 32> chain_packed;
  for (const CutPiece &p : pieces) {
    for (uint32_t e : p.e) {
      chain_packed.append((e << 1) | 1);
    }
    for (size_t i = 1; i + 1 < p.v.size(); i++) {
      chain_packed.append(p.v[i] << 1);
    }
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
      const EdgeRef w(x >> 1);
      const auto evs = k_.edge_verts(w);
      const uint32_t sides = end_sides(evs[0]) & end_sides(evs[1]);
      pc = sides == 3   ? middle_side(w)
           : sides == 1 ? geom::PointClass::Inside
           : sides == 2 ? geom::PointClass::Outside
                        : geom::PointClass::Boundary;
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

  /* the cut's edges from a to b, each V -> W: chords are made, then two
   * coedges per edge */
  litestl::util::Vector<uint32_t, 32> es, e_from, e_to;
  litestl::util::Vector<uint32_t, 8> first, last; /* of each piece */
  for (const CutPiece &p : pieces) {
    first.append(uint32_t(es.size()));
    if (make_chord) {
      const VertRef va(p.v.front()), vb(p.v.back());
      const bool a_lo = k_.vert_id(va) < k_.vert_id(vb);
      const EdgeRef e(alloc(Grp::Edges));
      setE<".e.vs">(e, a_lo ? U32x2{va.i, vb.i} : U32x2{vb.i, va.i});
      setE<".id">(e, chord_id(p));
      disk_insert(e, 0);
      disk_insert(e, 1);
      es.append(e.i);
    }
    else {
      for (uint32_t e : p.e) {
        es.append(e);
      }
    }
    for (size_t i = 0; i + 1 < p.v.size(); i++) {
      e_from.append(p.v[i]);
      e_to.append(p.v[i + 1]);
    }
    last.append(uint32_t(es.size()) - 1);
  }
  const uint32_t n = uint32_t(es.size());
  /* x1[i]: W -> V on edge i (right child); x2[i]: V -> W (left) */
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
    const VertRef vi(e_from[i]), vj(e_to[i]);
    const bool fwd = k_.edge_verts(e)[0] == vi; /* the edge runs V -> W */
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
  /* the loops, linked where paths, arcs and cut edges meet */
  const auto link = [&](uint32_t c, uint32_t to) {
    setC<".c.next">(CoedgeRef(c), to);
    setC<".c.prev">(CoedgeRef(to), c);
  };
  uint32_t size_r = uint32_t(p1.size()) + n, size_l = uint32_t(p2.size()) + n;
  uint32_t at = p1.last();
  for (uint32_t j = np; j-- > 0;) {
    for (uint32_t i = last[int(j)] + 1; i-- > first[int(j)];) {
      link(at, x1[i]);
      at = x1[i];
    }
    if (j > 0) {
      const auto &arc = arc_r[int(j) - 1];
      link(at, arc[0]);
      at = arc[arc.size() - 1];
      size_r += uint32_t(arc.size());
    }
  }
  link(at, ca.i);
  at = p2.last();
  for (uint32_t j = 0; j < np; j++) {
    for (uint32_t i = first[int(j)]; i <= last[int(j)]; i++) {
      link(at, x2[i]);
      at = x2[i];
    }
    if (j + 1 < np) {
      const auto &arc = arc_l[int(j)];
      link(at, arc[0]);
      at = arc[arc.size() - 1];
      size_l += uint32_t(arc.size());
    }
  }
  link(at, cb.i);
  setL<".l.c">(L, x2[0]);
  setL<".l.size">(L, size_l);
  for (uint32_t c : p1) {
    setC<".c.l">(CoedgeRef(c), lr.i);
  }
  litestl::util::Vector<uint32_t, 8> gone; /* the loops of cut holes */
  for (uint32_t h = 0; h < cut_holes.size(); h++) {
    gone.append(k_.loop_of(cut_holes[h].in).i);
    for (uint32_t c : arc_r[int(h)]) {
      setC<".c.l">(CoedgeRef(c), lr.i);
    }
    for (uint32_t c : arc_l[int(h)]) {
      setC<".c.l">(CoedgeRef(c), L.i);
    }
  }
  setL<".l.c">(lr, x1[n - 1]);
  setL<".l.size">(lr, size_r);
  /* chain elements stop being inclusions */
  if (!make_chord) {
    const ident::ContainerRef none{};
    for (const CutPiece &p : pieces) {
      for (uint32_t e : p.e) {
        setE<".e.container">(EdgeRef(e), none);
        setE<".e.container_hint">(EdgeRef(e), ident::kNoId);
      }
      for (size_t i = 1; i + 1 < p.v.size(); i++) {
        setV<".v.container">(VertRef(p.v[i]), none);
        setV<".v.container_hint">(VertRef(p.v[i]), ident::kNoId);
      }
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
  for (uint32_t l : gone) {
    free(Grp::Loops, l);
  }
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
  /* a piece's end coedge copies the parent's coedge at the same vertex */
  for (uint32_t j = 0; j < np; j++) {
    cm.add(x1[last[int(j)]], {{j + 1 < np ? cut_holes[j].in.i : cb.i, 1.0}});
    cm.add(x2[first[int(j)]], {{j > 0 ? cut_holes[j - 1].out.i : ca.i, 1.0}});
  }
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

} // namespace ubertopo::topo
