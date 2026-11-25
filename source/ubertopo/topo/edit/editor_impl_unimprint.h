#pragma once

/* Editor primitive unimprint (plan P3b W6, §3.6): the inverse of imprint's
 * split and island cases. Uses drop_hole from editor_impl_imprint.h.
 * Included only by editor.cc. */

#include <algorithm>

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/face_geom.h"

namespace ubertopo::topo {

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
  if (surfaced(f1) || surfaced(f2)) {
    return pr.fail(EditCode::SurfacedFace, ElemKind::Face, surfaced(f1) ? f1.i : f2.i);
  }
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
    uint32_t lh_ordinal = 0, n_loops = 0;
    k_.for_each_loop(f, [&](LoopRef l) {
      lh_ordinal = l == lh ? n_loops : lh_ordinal;
      n_loops++;
    });
    Skeleton<C> sk(k_);
    FacePoints<C> fp;
    const int axis = edit_detail::face_axis<C>(k_, f);
    if (axis < 0 || !fp.gather(k_, f, sk)) {
      return pr.fail(EditCode::DegenerateLoop, ElemKind::Face, f.i);
    }
    if (geom::outer_loop_proj<C>(fp.view(), axis) == lh_ordinal) {
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
