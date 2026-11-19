#include "ubertopo/topo/validate.h"

#include <algorithm>
#include <cstdio>

namespace ubertopo::topo {

namespace util = litestl::util;
using ident::ElemKind;
using VC = ValidateCode;

const char *validate_code_name(ValidateCode c)
{
  switch (c) {
#define UT_VC(x) \
  case VC::x: \
    return #x;
    UT_VC(DeadReference)
    UT_VC(EdgeEndpoints)
    UT_VC(CoedgeNextPrev)
    UT_VC(CoedgeChain)
    UT_VC(CoedgeStartVertex)
    UT_VC(LoopNotClosed)
    UT_VC(LoopSize)
    UT_VC(LoopFace)
    UT_VC(LooplessFace)
    UT_VC(FaceEdgeTwice)
    UT_VC(RadialNotClosed)
    UT_VC(RadialMembership)
    UT_VC(EdgeCoedge)
    UT_VC(DiskNotClosed)
    UT_VC(DiskInverse)
    UT_VC(DiskMembership)
    UT_VC(VertexEdge)
    UT_VC(ContainerInvalid)
    UT_VC(ContainerOnFaceVertex)
    UT_VC(DuplicateId)
    UT_VC(WedgeInvariant)
    UT_VC(ShellPartition)
    UT_VC(ShellRegion)
    UT_VC(UnboundedRegion)
    UT_VC(VertexLink)
    UT_VC(NonPlanarFace)
    UT_VC(SpuriousProjection)
    UT_VC(ProjectionNotAllowed)
    UT_VC(NotProjectable)
    UT_VC(HoleWinding)
    UT_VC(RadialOrder)
    UT_VC(ContainerGeometry)
    UT_VC(CurveKind)
    UT_VC(CurveRange)
    UT_VC(CurveEndpoint)
    UT_VC(GeometryUnsupported)
    UT_VC(LoopIntersection)
    UT_VC(HoleOutside)
    UT_VC(FaceIntersection)
    UT_VC(WireCrossesFace)
    UT_VC(RegionsStale)
    UT_VC(SkeletonDegenerate)
    UT_VC(SurfaceKind)
    UT_VC(PcurveKind)
    UT_VC(PcurveRange)
    UT_VC(PcurveVertex)
    UT_VC(PcurveDeviation)
    UT_VC(SurfaceLink)
    UT_VC(SurfaceOrientation)
#undef UT_VC
  }
  return "?";
}

void ValidationReport::print(FILE *out) const
{
  fprintf(out, "validation: %u issue(s)\n", total);
  for (const ValidationIssue &i : issues) {
    fprintf(out, "  %s kind=%d index=%u\n", validate_code_name(i.code), int(i.kind), i.index);
  }
}

namespace {

struct Checker {
  const ComplexBase &k;
  const VertexTopoView &v;
  ValidationReport &rep;

  uint32_t NV, NE, NC, NL, NF;
  util::Vector<uint8_t> coedge_ok; /* live, with followable links */

  bool vlive(uint32_t i) const
  {
    return i < NV && attr::get_bool(v.live, i);
  }
  bool elive(uint32_t i) const
  {
    return k.edges.live(i);
  }
  bool clive(uint32_t i) const
  {
    return k.coedges.live(i);
  }
  bool llive(uint32_t i) const
  {
    return k.loops.live(i);
  }
  bool flive(uint32_t i) const
  {
    return k.faces.live(i);
  }

  /* per-coedge link checks; returns false if c's links can't be followed */
  bool coedge_links_ok(uint32_t c)
  {
    const CoedgeRef cr(c);
    const uint32_t e = k.edge_of(cr).i, l = k.loop_of(cr).i, n = k.next(cr).i, p = k.prev(cr).i;
    const uint32_t sv = k.start_vert(cr).i;
    bool ok = true;
    if (!elive(e) || !llive(l) || !clive(n) || !clive(p) || !vlive(sv) || !clive(k.radial_next(cr).i) ||
        !clive(k.radial_prev(cr).i))
    {
      rep.add(VC::DeadReference, ElemKind::Coedge, c);
      return false;
    }
    if (k.prev(CoedgeRef(n)).i != c) {
      rep.add(VC::CoedgeNextPrev, ElemKind::Coedge, c);
      ok = false;
    }
    const auto vs = k.edge_verts(EdgeRef(e));
    if (vs[0].i >= NV || vs[1].i >= NV) {
      return false; /* reported per edge */
    }
    if (sv != (k.sense(cr) ? vs[0].i : vs[1].i)) {
      rep.add(VC::CoedgeStartVertex, ElemKind::Coedge, c);
      ok = false;
    }
    return ok;
  }

  void check_edges()
  {
    for (uint32_t e = 0; e < NE; e++) {
      if (!elive(e)) {
        continue;
      }
      const auto vs = k.edge_verts(EdgeRef(e));
      if (!vlive(vs[0].i) || !vlive(vs[1].i)) {
        rep.add(vs[0].i == NONE || vs[1].i == NONE ? VC::EdgeEndpoints : VC::DeadReference, ElemKind::Edge, e);
      }
      else if (vs[0] == vs[1]) {
        rep.add(VC::EdgeEndpoints, ElemKind::Edge, e); /* P2: straight edges only */
      }
      const ident::ContainerRef cr = k.edges.get<".e.container">()[e];
      check_container(cr, ElemKind::Edge, e);
    }
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

  void check_loops_and_faces()
  {
    /* coedges */
    coedge_ok.resize(NC);
    for (uint32_t c = 0; c < NC; c++) {
      coedge_ok[c] = clive(c) ? uint8_t(coedge_links_ok(c)) : 0;
    }
    /* loops: closed next-walks of .l.size coedges that all point back at the loop */
    util::Vector<uint32_t> loop_owner; /* loop -> face whose list contains it */
    loop_owner.resize(NL);
    for (uint32_t l = 0; l < NL; l++) {
      loop_owner[l] = NONE;
    }
    for (uint32_t f = 0; f < NF; f++) {
      if (!flive(f)) {
        continue;
      }
      const LoopRef first = k.first_loop(FaceRef(f));
      if (!first) {
        rep.add(VC::LooplessFace, ElemKind::Face, f); /* P2: planar faces only */
        continue;
      }
      uint32_t guard = NL + 1;
      for (LoopRef l = first; l; l = k.next_loop(l)) {
        if (!llive(l.i) || guard-- == 0) {
          rep.add(llive(l.i) ? VC::LoopFace : VC::DeadReference, ElemKind::Face, f);
          break;
        }
        if (loop_owner[l.i] != NONE) {
          rep.add(VC::LoopFace, ElemKind::Loop, l.i); /* in two lists, or a cycle */
          break;
        }
        loop_owner[l.i] = f;
        if (k.face_of(l).i != f) {
          rep.add(VC::LoopFace, ElemKind::Loop, l.i);
        }
      }
    }
    for (uint32_t l = 0; l < NL; l++) {
      if (!llive(l)) {
        continue;
      }
      if (loop_owner[l] == NONE) {
        rep.add(VC::LoopFace, ElemKind::Loop, l);
      }
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
        if (!clive(c.i) || !coedge_ok[c.i] || k.loop_of(c).i != l) {
          break;
        }
        n++;
        const CoedgeRef nx = k.next(c);
        if (k.start_vert(nx) != k.end_vert(c)) {
          rep.add(VC::CoedgeChain, ElemKind::Coedge, c.i);
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
    for (uint32_t f = 0; f < NF; f++) {
      if (!flive(f)) {
        continue;
      }
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

  void check_radial()
  {
    util::Vector<uint32_t> uses;
    uses.resize(NE);
    for (uint32_t e = 0; e < NE; e++) {
      uses[e] = 0;
    }
    for (uint32_t c = 0; c < NC; c++) {
      if (clive(c) && coedge_ok[c]) {
        uses[k.edge_of(CoedgeRef(c)).i]++;
      }
    }
    for (uint32_t e = 0; e < NE; e++) {
      if (!elive(e)) {
        continue;
      }
      const CoedgeRef start = k.edge_coedge(EdgeRef(e));
      if (!start) {
        if (uses[e] != 0) {
          rep.add(VC::EdgeCoedge, ElemKind::Edge, e);
        }
        continue;
      }
      if (!clive(start.i) || !coedge_ok[start.i]) {
        rep.add(VC::DeadReference, ElemKind::Edge, e);
        continue;
      }
      uint32_t n = 0;
      bool closed = false, member = true;
      CoedgeRef c = start;
      while (n <= NC) {
        if (!clive(c.i) || !coedge_ok[c.i]) {
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
      else if (!member || n != uses[e]) {
        rep.add(VC::RadialMembership, ElemKind::Edge, e);
      }
    }
  }

  void check_disks()
  {
    util::Vector<uint32_t> deg;
    deg.resize(NV);
    for (uint32_t i = 0; i < NV; i++) {
      deg[i] = 0;
    }
    util::Vector<uint8_t> on_face;
    on_face.resize(NV);
    for (uint32_t i = 0; i < NV; i++) {
      on_face[i] = 0;
    }
    for (uint32_t e = 0; e < NE; e++) {
      if (!elive(e)) {
        continue;
      }
      const auto vs = k.edge_verts(EdgeRef(e));
      if (!vlive(vs[0].i) || !vlive(vs[1].i) || vs[0] == vs[1]) {
        continue;
      }
      deg[vs[0].i]++;
      deg[vs[1].i]++;
      if (k.edge_coedge(EdgeRef(e))) {
        on_face[vs[0].i] = on_face[vs[1].i] = 1;
      }
      /* inverse links at both ends */
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
    }
    for (uint32_t i = 0; i < NV; i++) {
      if (!vlive(i)) {
        continue;
      }
      const EdgeRef first(v.e[i]);
      if (!first) {
        if (deg[i] != 0) {
          rep.add(VC::VertexEdge, ElemKind::Vertex, i);
        }
      }
      else if (!elive(first.i)) {
        rep.add(VC::DeadReference, ElemKind::Vertex, i);
      }
      else {
        uint32_t n = 0;
        const bool closed = k.for_each_disk_edge(VertRef(i), first, [&](EdgeRef) { n++; });
        if (!closed) {
          rep.add(VC::DiskNotClosed, ElemKind::Vertex, i);
        }
        else if (n != deg[i]) {
          rep.add(VC::DiskMembership, ElemKind::Vertex, i);
        }
      }
      const ident::ContainerRef cr = v.container[i];
      check_container(cr, ElemKind::Vertex, i);
      if (!cr.is_none() && on_face[i]) {
        rep.add(VC::ContainerOnFaceVertex, ElemKind::Vertex, i);
      }
    }
  }

  template <typename IdCol, typename LiveFn>
  void check_unique_ids(const IdCol &ids, uint32_t n, LiveFn &&live, ElemKind kind)
  {
    util::Vector<std::pair<ident::PersistentId, uint32_t>> v_;
    for (uint32_t i = 0; i < n; i++) {
      if (live(i)) {
        v_.append({ids[i], i});
      }
    }
    std::sort(v_.begin(), v_.end());
    for (uint32_t i = 1; i < v_.size(); i++) {
      if (v_[i].first == v_[i - 1].first) {
        rep.add(VC::DuplicateId, kind, v_[i].second);
      }
    }
  }

  void check_regions()
  {
    const uint32_t NS = k.shells.capacity(), NR = k.regions.capacity();
    /* exactly one unbounded region, at index 0 */
    uint32_t unbounded = 0;
    for (uint32_t r = 0; r < NR; r++) {
      if (k.regions.live(r) && (k.regions.get<".r.flags">()[r] & region_flags::Unbounded)) {
        unbounded++;
        if (r != 0) {
          rep.add(VC::UnboundedRegion, ElemKind::Region, r);
        }
      }
    }
    if (unbounded != 1) {
      rep.add(VC::UnboundedRegion, ElemKind::Region, 0);
    }
    /* shells partition the live face sides */
    util::Vector<uint32_t> side_shell;
    side_shell.resize(2 * NF);
    for (uint32_t i = 0; i < 2 * NF; i++) {
      side_shell[i] = NONE;
    }
    const auto &shell_next = k.faces.sides().get<".fs.shell_next">();
    for (uint32_t s = 0; s < NS; s++) {
      if (!k.shells.live(s)) {
        continue;
      }
      const uint32_t reg = k.shells.get<".s.region">()[s];
      if (!k.regions.live(reg)) {
        rep.add(VC::ShellRegion, ElemKind::Shell, s);
      }
      uint32_t fs = k.shells.get<".s.first">()[s], guard = 2 * NF + 1;
      while (fs != NONE) {
        if (fs >= 2 * NF || !flive(fs / 2) || guard-- == 0) {
          rep.add(VC::ShellPartition, ElemKind::Shell, s);
          break;
        }
        if (side_shell[fs] != NONE) {
          rep.add(VC::ShellPartition, ElemKind::FaceSide, fs);
          break;
        }
        side_shell[fs] = s;
        if (k.shell(FaceRef(fs / 2), fs % 2).i != s) {
          rep.add(VC::ShellPartition, ElemKind::FaceSide, fs);
        }
        if (k.region(FaceRef(fs / 2), fs % 2).i != reg) {
          rep.add(VC::ShellRegion, ElemKind::FaceSide, fs);
        }
        fs = shell_next[fs];
      }
    }
    for (uint32_t f = 0; f < NF; f++) {
      if (flive(f) && (side_shell[2 * f] == NONE || side_shell[2 * f + 1] == NONE)) {
        rep.add(VC::ShellPartition, ElemKind::Face, f);
      }
    }
    /* each region's shell chain lists shells of that region */
    for (uint32_t r = 0; r < NR; r++) {
      if (!k.regions.live(r)) {
        continue;
      }
      uint32_t s = k.regions.get<".r.shell">()[r], guard = NS + 1;
      while (s != NONE) {
        if (!k.shells.live(s) || guard-- == 0 || k.shells.get<".s.region">()[s] != r) {
          rep.add(VC::ShellRegion, ElemKind::Region, r);
          break;
        }
        s = k.shells.get<".s.next">()[s];
      }
    }
    /* invariant W on every wedge */
    for (uint32_t c = 0; c < NC; c++) {
      if (!coedge_ok[c]) {
        continue;
      }
      const CoedgeRef cr(c), cn = k.radial_next(cr);
      if (!coedge_ok[cn.i]) {
        continue; /* reported by the link checks */
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

  /* Euler-Poincare at each vertex. The radial orders define, at every
   * vertex v, a rotation system on its link graph (nodes: face edges at v;
   * arcs: face corners at v). Its sectors (orbits of the wedge relation on
   * corner sides) must number E - V + 2C, the count for a planar embedding of
   * each link component. An inconsistent radial order raises the genus and
   * lowers the count. */
  void check_vertex_links()
  {
    struct Dsu {
      util::Vector<uint32_t> p;
      void init(uint32_t n)
      {
        p.resize(n);
        for (uint32_t i = 0; i < n; i++) {
          p[i] = i;
        }
      }
      uint32_t find(uint32_t x)
      {
        while (p[x] != x) {
          p[x] = p[p[x]];
          x = p[x];
        }
        return x;
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
    sectors.init(2 * NC); /* key: 2 * corner coedge + side */
    link.init(2 * NE);    /* key: 2 * edge + endpoint slot */
    auto corner_at = [&](CoedgeRef c, VertRef vx) { return k.start_vert(c) == vx ? c : k.next(c); };
    auto eslot = [&](EdgeRef e, VertRef vx) { return k.edge_verts(e)[0] == vx ? 0u : 1u; };
    for (uint32_t c = 0; c < NC; c++) {
      if (!coedge_ok[c]) {
        continue;
      }
      const CoedgeRef cr(c), cn = k.radial_next(cr);
      if (!coedge_ok[cn.i]) {
        continue;
      }
      const uint32_t sa = k.side_toward_next(cr), sb = 1 - k.side_toward_next(cn);
      const auto vs = k.edge_verts(k.edge_of(cr));
      for (const VertRef vx : vs) {
        sectors.unite(2 * corner_at(cr, vx).i + sa, 2 * corner_at(cn, vx).i + sb);
      }
      /* the corner at c's start joins c's edge and prev(c)'s edge in the link */
      const VertRef s0 = k.start_vert(cr);
      const EdgeRef e0 = k.edge_of(cr), e1 = k.edge_of(k.prev(cr));
      link.unite(2 * e0.i + eslot(e0, s0), 2 * e1.i + eslot(e1, s0));
    }
    /* per vertex: corners, link nodes, link components, sectors */
    util::Vector<uint32_t> corners, nodes;
    corners.resize(NV);
    nodes.resize(NV);
    for (uint32_t i = 0; i < NV; i++) {
      corners[i] = nodes[i] = 0;
    }
    struct Key {
      uint32_t v, root;
      bool operator<(const Key &o) const
      {
        return v != o.v ? v < o.v : root < o.root;
      }
      bool operator==(const Key &o) const = default;
    };
    util::Vector<Key> sec_keys, link_keys;
    for (uint32_t c = 0; c < NC; c++) {
      if (!coedge_ok[c]) {
        continue;
      }
      const uint32_t vx = k.start_vert(CoedgeRef(c)).i;
      corners[vx]++;
      sec_keys.append({vx, sectors.find(2 * c)});
      sec_keys.append({vx, sectors.find(2 * c + 1)});
    }
    for (uint32_t e = 0; e < NE; e++) {
      if (!elive(e) || !k.edge_coedge(EdgeRef(e))) {
        continue;
      }
      const auto vs = k.edge_verts(EdgeRef(e));
      for (uint32_t s = 0; s < 2; s++) {
        if (vs[s].i < NV) {
          nodes[vs[s].i]++;
          link_keys.append({vs[s].i, link.find(2 * e + s)});
        }
      }
    }
    auto count_distinct = [&](util::Vector<Key> &ks, util::Vector<uint32_t> &per_v) {
      std::sort(ks.begin(), ks.end());
      per_v.resize(NV);
      for (uint32_t i = 0; i < NV; i++) {
        per_v[i] = 0;
      }
      for (uint32_t i = 0; i < ks.size(); i++) {
        if (i == 0 || !(ks[i] == ks[i - 1])) {
          per_v[ks[i].v]++;
        }
      }
    };
    util::Vector<uint32_t> nsec, ncomp;
    count_distinct(sec_keys, nsec);
    count_distinct(link_keys, ncomp);
    for (uint32_t i = 0; i < NV; i++) {
      if (corners[i] == 0) {
        continue;
      }
      if (int64_t(nsec[i]) != int64_t(corners[i]) - int64_t(nodes[i]) + 2 * int64_t(ncomp[i])) {
        rep.add(VC::VertexLink, ElemKind::Vertex, i);
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
    check_edges();
    check_loops_and_faces();
    check_radial();
    check_disks();
    check_unique_ids(v.id, NV, [&](uint32_t i) { return vlive(i); }, ElemKind::Vertex);
    check_unique_ids(k.edges.get<".id">(), NE, [&](uint32_t i) { return elive(i); }, ElemKind::Edge);
    check_unique_ids(k.loops.get<".id">(), NL, [&](uint32_t i) { return llive(i); }, ElemKind::Loop);
    check_unique_ids(k.faces.get<".id">(), NF, [&](uint32_t i) { return flive(i); }, ElemKind::Face);
    check_unique_ids(
        k.regions.get<".id">(), k.regions.capacity(), [&](uint32_t i) { return k.regions.live(i); }, ElemKind::Region);
    if (rep.ok()) {
      check_vertex_links();
    }
    if (!k.regions_dirty) {
      check_regions();
    }
  }
};

} // namespace

void validate_combinatorial(const ComplexBase &k, const VertexTopoView &v, ValidationReport &rep)
{
  Checker c{k, v, rep, 0, 0, 0, 0, 0, {}};
  c.run();
}

} // namespace ubertopo::topo
