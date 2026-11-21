#pragma once

/* Editor primitive weld (plan P3b W7, §3.6, C3, C14): merges pairs of
 * coincident vertices and fuses the parallel edges that result, in one
 * primitive, so an invalid input (coincident distinct vertices) can become
 * a valid result. Included only by editor.cc. */

#include <algorithm>
#include <bit>
#include <map>

#include "ubertopo/topo/edit/editor.h"

namespace ubertopo::topo {

template <geom::CoordType C> EditCode Editor<C>::weld(std::span<const std::pair<VertRef, VertRef>> pairs)
{
  Prim pr(*this, OpKind::Weld);
  if (pr.done) {
    return pr.code;
  }
  using ident::ElemKind;
  if (pairs.empty()) {
    return pr.fail(EditCode::BadArgument, ElemKind::Vertex, NONE, "no pairs");
  }
  /* checks: live, distinct, coincident, disjoint, not adjacent */
  struct Pair {
    VertRef s, x; /* survivor (smaller ID), absorbed */
  };
  litestl::util::Vector<Pair, 8> ps;
  litestl::util::Vector<uint32_t, 16> seen;
  for (const auto &[a, b] : pairs) {
    if (!k_.verts.live(a.i) || !k_.verts.live(b.i) || a == b) {
      return pr.fail(EditCode::BadArgument, ElemKind::Vertex, a.i);
    }
    if (geom::lex_compare<C>(k_.pos(a), k_.pos(b)) != 0) {
      return pr.fail(EditCode::Coincident, ElemKind::Vertex, b.i, "the vertices of a pair don't coincide");
    }
    for (uint32_t v : seen) {
      if (v == a.i || v == b.i) {
        return pr.fail(EditCode::BadArgument, ElemKind::Vertex, v, "a vertex is in two pairs");
      }
    }
    seen.append(a.i);
    seen.append(b.i);
    if (find_edge(a, b)) {
      return pr.fail(EditCode::Adjacent, ElemKind::Edge, find_edge(a, b).i);
    }
    if (vert_surfaced(a) || vert_surfaced(b)) {
      return pr.fail(EditCode::SurfacedFace, ElemKind::Vertex, vert_surfaced(a) ? a.i : b.i);
    }
    const bool a_lo = k_.vert_id(a) < k_.vert_id(b);
    ps.append({a_lo ? a : b, a_lo ? b : a});
  }
  std::sort(ps.begin(), ps.end(), [&](const Pair &p, const Pair &q) { return k_.vert_id(p.s) < k_.vert_id(q.s); });

  /* the containers before, per pair (resolved after the merge) */
  struct Cont {
    ident::ContainerRef c;
    ident::PersistentId h;
  };
  auto cont = [&](VertRef v) {
    return Cont{k_.verts.template get<".v.container">()[v.i], k_.verts.template get<".v.container_hint">()[v.i]};
  };
  litestl::util::Vector<Cont, 8> cs_s, cs_x;
  for (const Pair &p : ps) {
    cs_s.append(cont(p.s));
    cs_x.append(cont(p.x));
  }
  /* propagation before anything moves */
  {
    MixBuild vm;
    for (const Pair &p : ps) {
      vm.add(p.s.i, {{p.s.i, 1.0}, {p.x.i, 1.0}});
    }
    if (!propagate(Grp::Verts, PropEvent::Merge, vm)) {
      return pr.fail(EditCode::PropagationFailed, ElemKind::Vertex, ps[0].s.i);
    }
  }
  /* 1. re-point the absorbed vertices' edges and coedges at the survivors */
  for (const Pair &p : ps) {
    litestl::util::Vector<uint32_t, 16> es;
    k_.for_each_edge_at(p.x, [&](EdgeRef e) { es.append(e.i); });
    for (uint32_t ei : es) {
      const EdgeRef e(ei);
      const uint32_t slot = edge_slot(e, p.x);
      disk_remove(e, slot);
      U32x2 vs = k_.edges.template get<".e.vs">()[e.i];
      vs[slot] = p.s.i;
      setE<".e.vs">(e, vs);
      disk_insert(e, slot);
      k_.for_each_radial(e, [&](CoedgeRef c) {
        if (k_.coedges.template get<".c.v">()[c.i] == p.x.i) {
          setC<".c.v">(c, p.s.i);
        }
      });
    }
  }
  /* 2. fuse parallel edges at the survivors: the smaller edge ID survives */
  bool moved_coedges = false;
  litestl::util::Vector<uint32_t, 16> fused_edges;
  for (const Pair &p : ps) {
    std::map<uint32_t, litestl::util::Vector<uint32_t, 4>> by_other;
    k_.for_each_edge_at(p.s, [&](EdgeRef e) {
      const auto vs = k_.edge_verts(e);
      by_other[(vs[0] == p.s ? vs[1] : vs[0]).i].append(e.i);
    });
    for (auto &[other, group] : by_other) {
      (void)other;
      if (group.size() < 2) {
        continue;
      }
      std::sort(group.begin(), group.end(),
                [&](uint32_t a, uint32_t b) { return k_.edge_id(EdgeRef(a)) < k_.edge_id(EdgeRef(b)); });
      const EdgeRef e(group[0]);
      const U32x2 evs = k_.edges.template get<".e.vs">()[e.i];
      /* fused edges: both straight, or the same carrier between the same
       * vertices (plan P4a §5.1; no curved digons, OI-P4a-1) */
      {
        const auto &cv = k_.edges.template get<".e.curve">();
        const auto &rg = k_.edges.template get<".e.range">();
        const auto &gf = k_.edges.template get<".e.geom_flags">();
        auto bits = [](double x) { return std::bit_cast<uint64_t>(x); };
        for (int gi = 1; gi < group.size(); gi++) {
          const uint32_t y = group[gi];
          const U32x2 yvs = k_.edges.template get<".e.vs">()[y];
          const bool same_dir = yvs[0] == evs[0];
          const auto re = rg[e.i], ry = rg[y];
          const bool ranges = same_dir ? bits(re[0]) == bits(ry[0]) && bits(re[1]) == bits(ry[1])
                                       : bits(re[0]) == bits(ry[1]) && bits(re[1]) == bits(ry[0]);
          if (!(cv[e.i] == cv[y]) || gf[e.i] != gf[y] || (!cv[e.i].none() && !ranges)) {
            return pr.fail(EditCode::Incompatible, ElemKind::Edge, y, "fused edges with different carriers");
          }
        }
      }
      litestl::util::Vector<uint32_t, 8> extra;
      MixBuild em; /* equal weights over the whole group */
      em.targets.append(e.i);
      for (uint32_t gi : group) {
        em.sources.append(gi);
        em.weights.append(1.0);
      }
      em.offsets.append(uint32_t(em.sources.size()));
      if (!propagate(Grp::Edges, PropEvent::Merge, em)) {
        return pr.fail(EditCode::PropagationFailed, ElemKind::Edge, e.i);
      }
      /* containers of the fused edge */
      Cont ce{k_.edges.template get<".e.container">()[e.i], k_.edges.template get<".e.container_hint">()[e.i]};
      bool has_coedges = k_.edges.template get<".e.c">()[e.i] != NONE;
      for (int gi = 1; gi < group.size(); gi++) {
        const EdgeRef y(group[gi]);
        litestl::util::Vector<uint32_t, 8> ycs;
        k_.for_each_radial(y, [&](CoedgeRef c) { ycs.append(c.i); });
        if (ycs.size()) {
          has_coedges = true;
          moved_coedges = true;
        }
        else {
          const Cont cy{k_.edges.template get<".e.container">()[y.i], k_.edges.template get<".e.container_hint">()[y.i]};
          if (!has_coedges && !(cy.c == ce.c && cy.h == ce.h)) {
            return pr.fail(EditCode::Incompatible, ElemKind::Edge, y.i, "fused wires with different containers");
          }
        }
        for (uint32_t c : ycs) {
          const CoedgeRef cc(c);
          setC<".c.e">(cc, e.i);
          const bool sense = evs[0] == k_.coedges.template get<".c.v">()[c];
          setC<".c.flags">(cc, uint8_t(sense ? (k_.coedges.template get<".c.flags">()[c] | coedge_flags::Sense)
                                             : (k_.coedges.template get<".c.flags">()[c] & ~coedge_flags::Sense)));
          extra.append(c);
        }
        setE<".e.c">(y, NONE);
        disk_remove(y, 0);
        disk_remove(y, 1);
        lineage_.push_back({k_.edge_id(y), k_.edge_id(e), ElemKind::Edge, LineageKind::Merge});
        free(Grp::Edges, y.i);
      }
      if (extra.size()) {
        const EditCode rc = resort_radial(e, std::span<const uint32_t>(extra.data(), extra.size()));
        if (rc != EditCode::Ok) {
          return pr.fail(rc, rep_.kind, rep_.index);
        }
      }
      if (has_coedges) {
        setE<".e.container">(e, ident::ContainerRef{});
        setE<".e.container_hint">(e, ident::kNoId);
      }
      fused_edges.append(e.i);
    }
  }
  /* 3. vertex containers (C14): none with coedges, else face, else region */
  for (int i = 0; i < ps.size(); i++) {
    const Pair &p = ps[i];
    bool face_vertex = false;
    k_.for_each_edge_at(p.s, [&](EdgeRef e) { face_vertex = face_vertex || k_.edges.template get<".e.c">()[e.i] != NONE; });
    Cont r{};
    if (!face_vertex) {
      const Cont a = cs_s[i], b = cs_x[i];
      const bool fa = a.c.kind() == ident::ContainerKind::Face, fb = b.c.kind() == ident::ContainerKind::Face;
      const bool ra = a.c.kind() == ident::ContainerKind::Region, rb = b.c.kind() == ident::ContainerKind::Region;
      if (fa && fb && !(a.c == b.c)) {
        return pr.fail(EditCode::Incompatible, ElemKind::Vertex, p.s.i, "different face containers");
      }
      if (fa || fb) {
        r = fa ? a : b;
      }
      else if (ra && rb && !(a.c == b.c && a.h == b.h)) {
        return pr.fail(EditCode::Incompatible, ElemKind::Vertex, p.s.i, "different region containers");
      }
      else if (ra || rb) {
        r = ra ? a : b;
      }
      else if (!(a.h == b.h) && !(a.h == ident::kNoId) && !(b.h == ident::kNoId)) {
        return pr.fail(EditCode::Incompatible, ElemKind::Vertex, p.s.i, "different container hints");
      }
      else {
        r.h = a.h == ident::kNoId ? b.h : a.h;
      }
    }
    if (!(cont(p.s).c == r.c)) {
      setV<".v.container">(p.s, r.c);
    }
    if (!(cont(p.s).h == r.h)) {
      setV<".v.container_hint">(p.s, r.h);
    }
    lineage_.push_back({k_.vert_id(p.x), k_.vert_id(p.s), ElemKind::Vertex, LineageKind::Merge});
    free(Grp::Verts, p.x.i);
  }
  if (moved_coedges) {
    set_dirty();
  }
  pr.op.u.append(uint32_t(pairs.size()));
  for (const auto &[a, b] : pairs) {
    pr.op.u.append(a.i);
    pr.op.u.append(b.i);
  }
  return pr.finish();
}

} // namespace ubertopo::topo
