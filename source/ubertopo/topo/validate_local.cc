/* Validator: the combinatorial layer restricted to a filter and its
 * one-ring (plan P3b W5, §3.5, C9). The full layer (validate.cc) stays the
 * oracle; this one checks, for the listed rows:
 *
 *  - edges in scope (listed, plus the edges of in-scope coedges): endpoints,
 *    container, disk inverses, radial cycle (closed, every member on the
 *    edge, every in-scope coedge of the edge a member, .e.c set iff it has
 *    coedges);
 *  - loops of listed faces: the face's loop list, closure, size, chaining,
 *    each edge once per face;
 *  - coedges in scope (of those loops, and on the radial cycles of in-scope
 *    edges): links, next/prev inverse, start vertex, chaining to next;
 *  - vertices in scope (listed, plus the ends of in-scope edges): .v.e, disk
 *    closure and membership (every in-scope edge at the vertex is on its
 *    disk), container, ContainerOnFaceVertex, the vertex link (Euler count
 *    from the vertex's own disk and radial cycles);
 *  - IDs of in-scope vertices, edges and faces through the editor's index;
 *    loop IDs among the in-scope loops;
 *  - invariant W on in-scope coedges when regions are clean.
 *
 * Shell and region structure is changed only by barriers, which validate
 * in full. */

#include <algorithm>
#include <unordered_map>

#include "ubertopo/topo/validate.h"

namespace ubertopo::topo {

namespace util = litestl::util;
using ident::ElemKind;
using VC = ValidateCode;

namespace {

void uniq(util::Vector<uint32_t> &v)
{
  std::sort(v.begin(), v.end());
  v.resize(int(std::unique(v.begin(), v.end()) - v.begin()));
}
bool has(const util::Vector<uint32_t> &v, uint32_t x)
{
  return std::binary_search(v.begin(), v.end(), x);
}

struct LocalChecker {
  const ComplexBase &k;
  const VertexTopoView &v;
  const ValidateFilter &flt;
  ValidationReport &rep;
  uint32_t NV = 0, NE = 0, NC = 0, NL = 0, NF = 0;
  util::Vector<uint32_t> F, L, C, E, V;
  std::unordered_map<uint32_t, bool> coedge_ok;

  bool vlive(uint32_t i) const
  {
    return i < NV && attr::get_bool(v.live, i);
  }
  bool elive(uint32_t i) const
  {
    return i < NE && k.edges.live(i);
  }
  bool clive(uint32_t i) const
  {
    return i < NC && k.coedges.live(i);
  }
  bool llive(uint32_t i) const
  {
    return i < NL && k.loops.live(i);
  }
  bool flive(uint32_t i) const
  {
    return i < NF && k.faces.live(i);
  }

  void check_container(ident::ContainerRef cr, ElemKind kind, uint32_t i)
  {
    switch (cr.kind()) {
      case ident::ContainerKind::None:
        return;
      case ident::ContainerKind::Face:
        if (!flive(cr.index())) {
          rep.add(VC::ContainerInvalid, kind, i);
        }
        return;
      case ident::ContainerKind::Region:
        if (!k.regions.live(cr.index())) {
          rep.add(VC::ContainerInvalid, kind, i);
        }
        return;
      default:
        rep.add(VC::ContainerInvalid, kind, i);
    }
  }

  /* the full layer's per-coedge checks, memoised, plus chaining to next */
  bool links_ok(uint32_t c)
  {
    auto it = coedge_ok.find(c);
    if (it != coedge_ok.end()) {
      return it->second;
    }
    bool ok = true;
    const CoedgeRef cr(c);
    const uint32_t e = k.edge_of(cr).i, l = k.loop_of(cr).i, n = k.next(cr).i, p = k.prev(cr).i;
    const uint32_t sv = k.start_vert(cr).i;
    if (!elive(e) || !llive(l) || !clive(n) || !clive(p) || !vlive(sv) || !clive(k.radial_next(cr).i) ||
        !clive(k.radial_prev(cr).i))
    {
      rep.add(VC::DeadReference, ElemKind::Coedge, c);
      ok = false;
    }
    else {
      if (k.prev(CoedgeRef(n)).i != c) {
        rep.add(VC::CoedgeNextPrev, ElemKind::Coedge, c);
        ok = false;
      }
      const auto vs = k.edge_verts(EdgeRef(e));
      if (vs[0].i >= NV || vs[1].i >= NV) {
        ok = false; /* reported per edge */
      }
      else if (sv != (k.sense(cr) ? vs[0].i : vs[1].i)) {
        rep.add(VC::CoedgeStartVertex, ElemKind::Coedge, c);
        ok = false;
      }
      else if (k.loop_of(CoedgeRef(n)).i != l) {
        rep.add(VC::LoopNotClosed, ElemKind::Loop, l);
        ok = false;
      }
      else if (k.start_vert(CoedgeRef(n)) != k.end_vert(cr)) {
        rep.add(VC::CoedgeChain, ElemKind::Coedge, c);
        ok = false;
      }
    }
    coedge_ok[c] = ok;
    return ok;
  }

  void gather()
  {
    for (uint32_t f : flt.faces) {
      if (flive(f)) {
        F.append(f);
      }
    }
    uniq(F);
    /* loops of the listed faces, walking each list (LoopFace, DeadReference) */
    for (uint32_t f : F) {
      const LoopRef first = k.first_loop(FaceRef(f));
      if (!first) {
        rep.add(VC::LooplessFace, ElemKind::Face, f);
        continue;
      }
      uint32_t guard = NL + 1;
      util::Vector<uint32_t, 8> mine;
      for (LoopRef l = first; l; l = k.next_loop(l)) {
        if (!llive(l.i) || guard-- == 0) {
          rep.add(llive(l.i) ? VC::LoopFace : VC::DeadReference, ElemKind::Face, f);
          break;
        }
        if (std::find(mine.begin(), mine.end(), l.i) != mine.end()) {
          rep.add(VC::LoopFace, ElemKind::Loop, l.i); /* a cycle in the list */
          break;
        }
        mine.append(l.i);
        if (k.face_of(l).i != f) {
          rep.add(VC::LoopFace, ElemKind::Loop, l.i);
        }
        L.append(l.i);
      }
    }
    uniq(L);
    for (uint32_t e : flt.edges) {
      if (elive(e)) {
        E.append(e);
      }
    }
    uniq(E);
    /* coedges: of the loops (walked in check_loops), and on the radial
     * cycles of the listed edges */
    for (uint32_t e : E) {
      if (!k.for_each_radial(EdgeRef(e), [&](CoedgeRef c) { C.append(c.i); })) {
        rep.add(VC::RadialNotClosed, ElemKind::Edge, e);
      }
    }
  }

  void check_loops()
  {
    for (uint32_t l : L) {
      const CoedgeRef start = k.first_coedge(LoopRef(l));
      if (!clive(start.i)) {
        rep.add(VC::DeadReference, ElemKind::Loop, l);
        continue;
      }
      const uint32_t size = k.loop_size(LoopRef(l));
      uint32_t n = 0;
      CoedgeRef c = start;
      bool closed = false;
      while (n <= NC) {
        if (!clive(c.i) || k.loop_of(c).i != l) {
          break;
        }
        C.append(c.i);
        n++;
        const CoedgeRef nx = k.next(c);
        if (!links_ok(c.i)) {
          break;
        }
        c = nx;
        if (c == start) {
          closed = true;
          break;
        }
      }
      if (!closed) {
        rep.add(VC::LoopNotClosed, ElemKind::Loop, l);
      }
      else if (n != size) {
        rep.add(VC::LoopSize, ElemKind::Loop, l);
      }
    }
    /* a face uses each edge once */
    util::Vector<uint32_t> es;
    for (uint32_t f : F) {
      es.clear();
      k.for_each_loop(FaceRef(f), [&](LoopRef l) {
        if (llive(l.i)) {
          k.for_each_coedge(l, [&](CoedgeRef c) {
            if (clive(c.i) && es.size() <= NC) {
              es.append(k.edge_of(c).i);
            }
          });
        }
      });
      std::sort(es.begin(), es.end());
      if (std::adjacent_find(es.begin(), es.end()) != es.end()) {
        rep.add(VC::FaceEdgeTwice, ElemKind::Face, f);
      }
    }
  }

  void close_scope()
  {
    uniq(C);
    for (uint32_t c : C) {
      if (clive(c) && links_ok(c) && elive(k.edge_of(CoedgeRef(c)).i)) {
        E.append(k.edge_of(CoedgeRef(c)).i);
      }
    }
    uniq(E);
    for (uint32_t x : flt.verts) {
      if (vlive(x)) {
        V.append(x);
      }
    }
    for (uint32_t e : E) {
      const auto vs = k.edge_verts(EdgeRef(e));
      for (const VertRef w : vs) {
        if (vlive(w.i)) {
          V.append(w.i);
        }
      }
    }
    uniq(V);
  }

  void check_edges()
  {
    for (uint32_t e : E) {
      const auto vs = k.edge_verts(EdgeRef(e));
      if (!vlive(vs[0].i) || !vlive(vs[1].i)) {
        rep.add(vs[0].i == NONE || vs[1].i == NONE ? VC::EdgeEndpoints : VC::DeadReference, ElemKind::Edge, e);
      }
      else if (vs[0] == vs[1]) {
        rep.add(VC::EdgeEndpoints, ElemKind::Edge, e);
      }
      check_container(k.edges.get<".e.container">()[e], ElemKind::Edge, e);
      /* disk inverses at both ends */
      const U32x4 d = k.edges.get<".e.disk">()[e];
      for (uint32_t s = 0; s < 2; s++) {
        const uint32_t nx = d[2 * s + 1], pv = d[2 * s];
        if (nx == NONE || pv == NONE || !elive(disk_edge(nx)) || !elive(disk_edge(pv))) {
          rep.add(VC::DiskInverse, ElemKind::Edge, e);
          break;
        }
        const U32x4 dn = k.edges.get<".e.disk">()[disk_edge(nx)];
        const U32x4 dp = k.edges.get<".e.disk">()[disk_edge(pv)];
        if (dn[2 * disk_slot(nx)] != disk_pack(e, s) || dp[2 * disk_slot(pv) + 1] != disk_pack(e, s)) {
          rep.add(VC::DiskInverse, ElemKind::Edge, e);
          break;
        }
      }
      /* radial cycle */
      const CoedgeRef start = k.edge_coedge(EdgeRef(e));
      if (!start) {
        continue; /* uses are checked from the coedges below */
      }
      if (!clive(start.i) || !links_ok(start.i)) {
        rep.add(VC::DeadReference, ElemKind::Edge, e);
        continue;
      }
      uint32_t n = 0;
      bool closed = false, member = true;
      CoedgeRef c = start;
      while (n <= NC) {
        if (!clive(c.i) || !links_ok(c.i)) {
          break;
        }
        member = member && k.edge_of(c).i == e;
        const CoedgeRef nx = k.radial_next(c);
        if (!clive(nx.i) || k.radial_prev(nx) != c) {
          break;
        }
        n++;
        c = nx;
        if (c == start) {
          closed = true;
          break;
        }
      }
      if (!closed) {
        rep.add(VC::RadialNotClosed, ElemKind::Edge, e);
      }
      else if (!member) {
        rep.add(VC::RadialMembership, ElemKind::Edge, e);
      }
    }
    /* every in-scope coedge is a member of its edge's radial cycle */
    for (uint32_t c : C) {
      if (!clive(c) || !links_ok(c)) {
        continue;
      }
      const EdgeRef e = k.edge_of(CoedgeRef(c));
      if (!k.edge_coedge(e)) {
        rep.add(VC::EdgeCoedge, ElemKind::Edge, e.i);
        continue;
      }
      bool found = false;
      k.for_each_radial(e, [&](CoedgeRef x) { found = found || x.i == c; });
      if (!found) {
        rep.add(VC::RadialMembership, ElemKind::Edge, e.i);
      }
    }
  }

  void check_verts()
  {
    for (uint32_t i : V) {
      const EdgeRef first(v.e[i]);
      bool on_face = false;
      util::Vector<uint32_t, 16> disk;
      if (!first) {
        /* an in-scope edge at the vertex must be on its disk */
      }
      else if (!elive(first.i)) {
        rep.add(VC::DeadReference, ElemKind::Vertex, i);
        continue;
      }
      else {
        const bool closed = k.for_each_disk_edge(VertRef(i), first, [&](EdgeRef e) {
          disk.append(e.i);
          on_face = on_face || bool(k.edge_coedge(e));
        });
        if (!closed) {
          rep.add(VC::DiskNotClosed, ElemKind::Vertex, i);
          continue;
        }
      }
      std::sort(disk.begin(), disk.end());
      if (std::adjacent_find(disk.begin(), disk.end()) != disk.end()) {
        rep.add(VC::DiskMembership, ElemKind::Vertex, i);
      }
      for (uint32_t e : E) {
        const auto vs = k.edge_verts(EdgeRef(e));
        if ((vs[0].i == i || vs[1].i == i) && !std::binary_search(disk.begin(), disk.end(), e)) {
          rep.add(first ? VC::DiskMembership : VC::VertexEdge, ElemKind::Vertex, i);
          break;
        }
      }
      const ident::ContainerRef cr = v.container[i];
      check_container(cr, ElemKind::Vertex, i);
      if (!cr.is_none() && on_face) {
        rep.add(VC::ContainerOnFaceVertex, ElemKind::Vertex, i);
      }
    }
  }

  void check_ids()
  {
    if (!flt.ids) {
      return;
    }
    for (uint32_t i : V) {
      if (flt.ids->count(ElemKind::Vertex, v.id[i]) != 1) {
        rep.add(VC::DuplicateId, ElemKind::Vertex, i);
      }
    }
    for (uint32_t e : E) {
      if (flt.ids->count(ElemKind::Edge, k.edges.get<".id">()[e]) != 1) {
        rep.add(VC::DuplicateId, ElemKind::Edge, e);
      }
    }
    for (uint32_t f : F) {
      if (flt.ids->count(ElemKind::Face, k.faces.get<".id">()[f]) != 1) {
        rep.add(VC::DuplicateId, ElemKind::Face, f);
      }
    }
    util::Vector<std::pair<ident::PersistentId, uint32_t>> ls;
    for (uint32_t l : L) {
      ls.append({k.loops.get<".id">()[l], l});
    }
    std::sort(ls.begin(), ls.end());
    for (uint32_t i = 1; i < ls.size(); i++) {
      if (ls[i].first == ls[i - 1].first) {
        rep.add(VC::DuplicateId, ElemKind::Loop, ls[i].second);
      }
    }
  }

  /* Euler-Poincare at vertex x (see validate.cc check_vertex_links): sectors
   * must number E - V + 2C for the link graph at x */
  void check_vertex_link(uint32_t x)
  {
    const VertRef vx(x);
    util::Vector<uint32_t, 16> es;            /* face edges at x */
    util::Vector<uint32_t, 32> corners;       /* coedges starting at x */
    k.for_each_disk_edge(vx, EdgeRef(v.e[x]), [&](EdgeRef e) {
      if (k.edge_coedge(e)) {
        es.append(e.i);
        k.for_each_radial(e, [&](CoedgeRef c) {
          if (k.start_vert(c) == vx) {
            corners.append(c.i);
          }
        });
      }
    });
    if (corners.size() == 0) {
      return;
    }
    std::sort(corners.begin(), corners.end());
    std::sort(es.begin(), es.end());
    auto ci = [&](uint32_t c) { return uint32_t(std::lower_bound(corners.begin(), corners.end(), c) - corners.begin()); };
    auto ei = [&](uint32_t e) { return uint32_t(std::lower_bound(es.begin(), es.end(), e) - es.begin()); };
    struct Dsu {
      util::Vector<uint32_t, 64> p;
      void init(uint32_t n)
      {
        p.resize(n);
        for (uint32_t i = 0; i < n; i++) {
          p[i] = i;
        }
      }
      uint32_t find(uint32_t a)
      {
        while (p[a] != a) {
          p[a] = p[p[a]];
          a = p[a];
        }
        return a;
      }
      void unite(uint32_t a, uint32_t b)
      {
        a = find(a);
        b = find(b);
        if (a != b) {
          p[a < b ? b : a] = a < b ? a : b;
        }
      }
    } sectors, link;
    sectors.init(2 * corners.size());
    link.init(es.size());
    auto corner_at = [&](CoedgeRef c) { return k.start_vert(c) == vx ? c : k.next(c); };
    for (uint32_t e : es) {
      k.for_each_radial(EdgeRef(e), [&](CoedgeRef cr) {
        const CoedgeRef cn = k.radial_next(cr);
        const uint32_t sa = k.side_toward_next(cr), sb = 1 - k.side_toward_next(cn);
        const CoedgeRef a = corner_at(cr), b = corner_at(cn);
        if (k.start_vert(a) == vx && k.start_vert(b) == vx) {
          sectors.unite(2 * ci(a.i) + sa, 2 * ci(b.i) + sb);
        }
      });
    }
    for (uint32_t c : corners) {
      const EdgeRef e0 = k.edge_of(CoedgeRef(c)), e1 = k.edge_of(k.prev(CoedgeRef(c)));
      if (std::binary_search(es.begin(), es.end(), e1.i)) {
        link.unite(ei(e0.i), ei(e1.i));
      }
    }
    util::Vector<uint32_t, 64> roots;
    for (uint32_t i = 0; i < 2 * corners.size(); i++) {
      roots.append(sectors.find(i));
    }
    std::sort(roots.begin(), roots.end());
    const uint32_t nsec = uint32_t(std::unique(roots.begin(), roots.end()) - roots.begin());
    roots.clear();
    for (uint32_t i = 0; i < es.size(); i++) {
      roots.append(link.find(i));
    }
    std::sort(roots.begin(), roots.end());
    const uint32_t ncomp = uint32_t(std::unique(roots.begin(), roots.end()) - roots.begin());
    if (int64_t(nsec) != int64_t(corners.size()) - int64_t(es.size()) + 2 * int64_t(ncomp)) {
      rep.add(VC::VertexLink, ElemKind::Vertex, x);
    }
  }

  void check_wedges()
  {
    for (uint32_t c : C) {
      if (!clive(c) || !links_ok(c)) {
        continue;
      }
      const CoedgeRef cr(c), cn = k.radial_next(cr);
      if (!clive(cn.i) || !links_ok(cn.i)) {
        continue;
      }
      const FaceRef f = k.face_of(cr), fn = k.face_of(cn);
      if (!flive(f.i) || !flive(fn.i)) {
        continue;
      }
      if (k.region(f, k.side_toward_next(cr)) != k.region(fn, 1 - k.side_toward_next(cn))) {
        rep.add(VC::WedgeInvariant, ElemKind::Coedge, c);
      }
    }
  }

  void run()
  {
    NV = v.capacity;
    NE = k.edges.capacity();
    NC = k.coedges.capacity();
    NL = k.loops.capacity();
    NF = k.faces.capacity();
    gather();
    check_loops();
    close_scope();
    check_edges();
    check_verts();
    check_ids();
    if (rep.ok()) {
      for (uint32_t x : V) {
        check_vertex_link(x);
      }
    }
    if (!k.regions_dirty) {
      check_wedges();
    }
  }
};

} // namespace

void validate_combinatorial_local(const ComplexBase &k,
                                  const VertexTopoView &v,
                                  const ValidateFilter &flt,
                                  ValidationReport &rep)
{
  LocalChecker c{k, v, flt, rep};
  c.run();
}

} // namespace ubertopo::topo
