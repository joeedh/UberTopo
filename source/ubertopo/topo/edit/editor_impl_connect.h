#pragma once

/* Editor primitives: connect_loops and disconnect_loops (plan
 * 2026-11-24-connect-loops). Included only by editor.cc. */

#include <algorithm>

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/face_geom.h"

namespace ubertopo::topo {

template <geom::CoordType C>
EditCode Editor<C>::connect_loops(FaceRef f, std::span<const VertRef> path, FaceRef *other, EdgeRef *first_chord)
{
  Prim pr(*this, OpKind::ConnectLoops);
  if (pr.done) {
    return pr.code;
  }
  using ident::ElemKind;
  if (!k_.faces.live(f.i)) {
    return pr.fail(EditCode::DeadElement, ElemKind::Face, f.i);
  }
  if (path.size() < 4 || path.size() % 2 != 0) {
    return pr.fail(EditCode::BadArgument, ElemKind::Face, f.i, "a cut has an even number of vertices, at least 4");
  }
  for (const VertRef v : path) {
    if (!k_.verts.live(v.i)) {
      return pr.fail(EditCode::BadArgument, ElemKind::Vertex, v.i);
    }
  }
  if (surfaced(f)) {
    return pr.fail(EditCode::SurfacedFace, ElemKind::Face, f.i);
  }
  /* where each vertex is on f */
  litestl::util::Vector<CoedgeRef, 8> at;
  for (const VertRef v : path) {
    CoedgeRef found;
    k_.for_each_loop(f, [&](LoopRef l) {
      k_.for_each_coedge(l, [&](CoedgeRef c) {
        if (k_.start_vert(c) == v) {
          found = c;
        }
      });
    });
    if (!found) {
      return pr.fail(EditCode::NotInLoop, ElemKind::Vertex, v.i);
    }
    at.append(found);
  }
  const int axis = edit_detail::face_axis<C>(k_, f);
  Skeleton<C> sk(k_);
  FacePoints<C> fp;
  if (axis < 0 || !fp.gather(k_, f, sk)) {
    return pr.fail(EditCode::DegenerateLoop, ElemKind::Face, f.i);
  }
  const uint32_t outer_ordinal = geom::outer_loop_proj<C>(fp.view(), axis);
  LoopRef outer;
  uint32_t ordinal = 0;
  k_.for_each_loop(f, [&](LoopRef l) {
    if (ordinal++ == outer_ordinal) {
      outer = l;
    }
  });
  const uint32_t n = uint32_t(path.size());
  for (uint32_t i = 0; i < n; i++) {
    const bool end = i == 0 || i + 1 == n;
    if ((k_.loop_of(at[int(i)]) == outer) != end) {
      return pr.fail(EditCode::NotOuterLoop, ElemKind::Vertex, path[i].i,
                     end ? "an end of the cut is on a hole" : "the cut meets the outer loop between its ends");
    }
  }
  litestl::util::Vector<CutHole, 4> holes;
  for (uint32_t i = 1; i + 2 < n; i += 2) {
    const CoedgeRef in = at[int(i)], out = at[int(i) + 1];
    if (k_.loop_of(in) != k_.loop_of(out) || in == out) {
      return pr.fail(EditCode::ChainNotSimple, ElemKind::Vertex, path[i].i,
                     "a hole is entered and left at two of its vertices");
    }
    for (const CutHole &h : holes) {
      if (k_.loop_of(h.in) == k_.loop_of(in)) {
        return pr.fail(EditCode::ChainNotSimple, ElemKind::Vertex, path[i].i, "a hole is cut twice");
      }
    }
    holes.append({in, out});
  }
  litestl::util::Vector<uint32_t, 8> vs;
  for (const VertRef v : path) {
    vs.append(v.i);
  }
  litestl::util::Vector<CutPiece, 4> pieces;
  for (uint32_t i = 0; i < n; i += 2) {
    pieces.append({{}, std::span<const uint32_t>(vs.data() + i, 2)});
  }
  const EditCode c = split_core(f, std::span<const CutPiece>(pieces.data(), pieces.size()),
                                std::span<const CutHole>(holes.data(), holes.size()), true, "connect_loops", other,
                                first_chord);
  if (c != EditCode::Ok) {
    return pr.fail(c, rep_.kind, rep_.index, rep_.detail);
  }
  pr.op.u.append(f.i);
  for (uint32_t v : vs) {
    pr.op.u.append(v);
  }
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::disconnect_loops(EdgeRef e, FaceRef *survivor)
{
  Prim pr(*this, OpKind::DisconnectLoops);
  if (pr.done) {
    return pr.code;
  }
  using ident::ElemKind;
  if (!k_.edges.live(e.i)) {
    return pr.fail(EditCode::DeadElement, ElemKind::Edge, e.i);
  }
  const auto given = radial_members(e);
  if (given.size() != 2) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "the edge doesn't have exactly two coedges");
  }
  FaceRef fs = k_.face_of(given[0]), fa = k_.face_of(given[1]); /* survivor, absorbed */
  if (fs == fa) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "both coedges on one face");
  }
  if (surfaced(fs) || surfaced(fa)) {
    return pr.fail(EditCode::SurfacedFace, ElemKind::Face, surfaced(fs) ? fs.i : fa.i);
  }
  if (k_.face_flags(fs) != k_.face_flags(fa) ||
      !(k_.side_hint(fs, 0) == k_.side_hint(fa, 0)) || !(k_.side_hint(fs, 1) == k_.side_hint(fa, 1)))
  {
    return pr.fail(EditCode::Incompatible, ElemKind::Face, fa.i);
  }
  if (k_.face_id(fa) < k_.face_id(fs)) {
    std::swap(fs, fa);
  }
  const bool swapped = k_.face_of(given[0]) != fs;
  const LoopRef ls = k_.loop_of(given[swapped ? 1 : 0]), la = k_.loop_of(given[swapped ? 0 : 1]);

  /* the shared edges, in the order of a walk along the survivor's loop from
   * its first coedge, so that nothing depends on which one was given */
  struct Shared {
    CoedgeRef s, a;
  };
  litestl::util::Vector<Shared, 8> shared;
  litestl::util::Vector<uint32_t, 32> vs, va;
  EdgeRef bad;
  k_.for_each_loop(fs, [&](LoopRef l) {
    k_.for_each_coedge(l, [&](CoedgeRef c) {
      vs.append(k_.start_vert(c).i);
      CoedgeRef mate;
      uint32_t count = 0;
      k_.for_each_radial(k_.edge_of(c), [&](CoedgeRef x) {
        count++;
        if (k_.face_of(x) == fa) {
          mate = x;
        }
      });
      if (!mate) {
        return;
      }
      if (count != 2 || k_.sense(c) == k_.sense(mate) || l != ls || k_.loop_of(mate) != la) {
        bad = k_.edge_of(c);
      }
      else {
        shared.append({c, mate});
      }
    });
  });
  if (bad) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, bad.i,
                   "a shared edge has other faces, the wrong direction, or is on other loops");
  }
  k_.for_each_loop(fa, [&](LoopRef l) { k_.for_each_coedge(l, [&](CoedgeRef c) { va.append(k_.start_vert(c).i); }); });
  std::sort(va.begin(), va.end());
  uint32_t shared_v = 0;
  for (uint32_t v : vs) {
    shared_v += std::binary_search(va.begin(), va.end(), v);
  }
  const uint32_t m = uint32_t(shared.size());
  if (m < 2 || shared_v != 2 * m) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i,
                   m < 2 ? "the faces share one edge" : "shared edges meet, or the faces meet at another vertex");
  }
  if (k_.face_proj_axis(fs) < 0) {
    FacePoints<C> p1, p2;
    p1.gather(k_, fs);
    p2.gather(k_, fa);
    for (const Point &p : p2.pts) {
      p1.pts.append(p);
    }
    const uint32_t off[2] = {0, uint32_t(p1.pts.size())};
    const geom::PolyView<C> all{std::span<const Point>(p1.pts.data(), p1.pts.size()), std::span<const uint32_t>(off, 2)};
    if (!face_points_planar<C>(all)) {
      return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "not coplanar");
    }
  }

  /* the loops after the splices: each shared coedge is replaced by what
   * follows its mate */
  litestl::util::Vector<uint32_t, 32> walk;
  k_.for_each_coedge(ls, [&](CoedgeRef c) { walk.append(c.i); });
  auto next_after = [&](uint32_t c) {
    const CoedgeRef x = k_.next(CoedgeRef(c));
    for (const Shared &s : shared) {
      if (s.s == x) {
        return k_.next(s.a).i;
      }
      if (s.a == x) {
        return k_.next(s.s).i;
      }
    }
    return x.i;
  };
  auto is_shared = [&](uint32_t c) {
    return std::any_of(shared.begin(), shared.end(), [&](const Shared &s) { return s.s.i == c || s.a.i == c; });
  };
  litestl::util::Vector<uint32_t, 32> seen; /* sorted coedges of the cycles so far */
  litestl::util::Vector<litestl::util::Vector<uint32_t, 16>, 4> cycles;
  for (uint32_t c0 : walk) {
    if (is_shared(c0) || std::binary_search(seen.begin(), seen.end(), c0)) {
      continue;
    }
    litestl::util::Vector<uint32_t, 16> cyc;
    for (uint32_t c = c0;;) {
      cyc.append(c);
      c = next_after(c);
      if (c == c0) {
        break;
      }
    }
    for (uint32_t c : cyc) {
      seen.append(c);
    }
    std::sort(seen.begin(), seen.end());
    cycles.append(std::move(cyc));
  }
  if (cycles.size() != m) {
    return pr.fail(EditCode::NotJoinable, ElemKind::Edge, e.i, "the union of the faces isn't planar");
  }
  /* the cycle that takes the survivor's loop: the new outer loop, if both
   * loops were outer ones. Every cycle has coedges of both loops, so all were
   * found. */
  Skeleton<C> sk(k_);
  const int axis = edit_detail::face_axis<C>(k_, fs);
  auto outer_of = [&](FaceRef f) -> LoopRef {
    FacePoints<C> fp;
    const int ax = edit_detail::face_axis<C>(k_, f);
    LoopRef out;
    if (ax < 0 || !fp.gather(k_, f, sk)) {
      return out;
    }
    uint32_t ordinal = 0;
    const uint32_t want = geom::outer_loop_proj<C>(fp.view(), ax);
    k_.for_each_loop(f, [&](LoopRef l) {
      if (ordinal++ == want) {
        out = l;
      }
    });
    return out;
  };
  const LoopRef outer_s = outer_of(fs), outer_a = outer_of(fa);
  if (!outer_s || !outer_a) {
    return pr.fail(EditCode::DegenerateLoop, ElemKind::Face, !outer_s ? fs.i : fa.i);
  }
  FacePoints<C> run;
  run.off.append(0);
  for (const auto &cyc : cycles) {
    for (uint32_t c : cyc) {
      run.append_run(k_, CoedgeRef(c), &sk);
    }
    run.off.append(uint32_t(run.pts.size()));
  }
  const uint32_t keeps = geom::outer_loop_proj<C>(run.view(), axis);

  const double as = edit_detail::face_area<C>(k_, fs), aa = edit_detail::face_area<C>(k_, fa);
  const ident::PersistentId ids = k_.face_id(fs), ida = k_.face_id(fa);
  litestl::util::Vector<uint32_t, 4> rows; /* by cycle */
  for (uint32_t i = 0; i < m; i++) {
    rows.append(i == keeps ? ls.i : alloc(Grp::Loops));
  }
  /* propagate before anything is freed */
  MixBuild fm, sm, lm, ln;
  fm.add(fs.i, {{fs.i, as}, {fa.i, aa}});
  for (uint32_t s = 0; s < 2; s++) {
    sm.add(2 * fs.i + s, {{2 * fs.i + s, as}, {2 * fa.i + s, aa}});
  }
  lm.add(ls.i, {{ls.i, 1.0}, {la.i, 1.0}});
  for (uint32_t r : rows) {
    if (r != ls.i) {
      ln.add(r, {{ls.i, 1.0}});
    }
  }
  if (!propagate(Grp::Faces, PropEvent::Merge, fm) || !propagate(Grp::Sides, PropEvent::Merge, sm) ||
      !propagate(Grp::Loops, PropEvent::Merge, lm) || !propagate(Grp::Loops, PropEvent::Split, ln))
  {
    return pr.fail(EditCode::PropagationFailed, ElemKind::Face, fs.i);
  }
  /* splice; no two shared edges meet, so their neighbours aren't shared */
  const auto link = [&](CoedgeRef c, CoedgeRef to) {
    setC<".c.next">(c, to.i);
    setC<".c.prev">(to, c.i);
  };
  for (const Shared &x : shared) {
    const CoedgeRef p1 = k_.prev(x.s), n1 = k_.next(x.s), p2 = k_.prev(x.a), n2 = k_.next(x.a);
    link(p1, n2);
    link(p2, n1);
  }
  for (uint32_t i = 0; i < m; i++) {
    const LoopRef l(rows[int(i)]);
    for (uint32_t c : cycles[int(i)]) {
      if (k_.loop_of(CoedgeRef(c)) != l) {
        setC<".c.l">(CoedgeRef(c), l.i);
      }
    }
    setL<".l.c">(l, cycles[int(i)][0]);
    setL<".l.size">(l, uint32_t(cycles[int(i)].size()));
  }
  /* loops: the survivor's, the new ones, the absorbed face's others; if the
   * absorbed face had the outer loop, that one goes first */
  litestl::util::Vector<uint32_t, 8> chain;
  if (outer_a != la) {
    chain.append(outer_a.i);
  }
  k_.for_each_loop(fs, [&](LoopRef l) { chain.append(l.i); });
  for (uint32_t r : rows) {
    if (r != ls.i) {
      chain.append(r);
    }
  }
  k_.for_each_loop(fa, [&](LoopRef l) {
    if (l != la && l != outer_a) {
      chain.append(l.i);
    }
  });
  setF<".f.l">(fs, chain[0]);
  for (int i = 0; i < int(chain.size()); i++) {
    const LoopRef l(chain[i]);
    setL<".l.f">(l, fs.i);
    setL<".id">(l, ident::loop_id(ids, uint32_t(i)));
    setL<".l.next">(l, i + 1 < int(chain.size()) ? chain[i + 1] : NONE);
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
  for (const Shared &x : shared) {
    const EdgeRef se = k_.edge_of(x.s);
    setE<".e.c">(se, NONE);
    disk_remove(se, 0);
    disk_remove(se, 1);
    free(Grp::Coedges, x.s.i);
    free(Grp::Coedges, x.a.i);
    free(Grp::Edges, se.i);
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
