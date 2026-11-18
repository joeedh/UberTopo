#include "ubertopo/topo/builder.h"

#include <algorithm>
#include <cstring>

#include "litestl/util/sort.h"
#include "litestl/util/task.h"
#include "ubertopo/geom/cdt/cdt.h"
#include "ubertopo/geom/predicates.h"
#include "ubertopo/topo/radial.h"

namespace ubertopo::topo {

namespace util = litestl::util;
namespace task = litestl::task;
using ident::PersistentId;

const char *build_code_name(BuildCode c)
{
  switch (c) {
    case BuildCode::VertexPositionMismatch:
      return "VertexPositionMismatch";
    case BuildCode::VertexContainerMismatch:
      return "VertexContainerMismatch";
    case BuildCode::BadVertexKey:
      return "BadVertexKey";
    case BuildCode::BadFaceKey:
      return "BadFaceKey";
    case BuildCode::DuplicateFaceId:
      return "DuplicateFaceId";
    case BuildCode::DuplicateWireId:
      return "DuplicateWireId";
    case BuildCode::LooplessFace:
      return "LooplessFace";
    case BuildCode::LoopTooShort:
      return "LoopTooShort";
    case BuildCode::RepeatedVertexInLoop:
      return "RepeatedVertexInLoop";
    case BuildCode::VertexSharedBetweenLoops:
      return "VertexSharedBetweenLoops";
    case BuildCode::EdgeUsedTwice:
      return "EdgeUsedTwice";
    case BuildCode::DegenerateLoop:
      return "DegenerateLoop";
    case BuildCode::NonPlanarFace:
      return "NonPlanarFace";
    case BuildCode::HoleOrientation:
      return "HoleOrientation";
    case BuildCode::DegenerateWire:
      return "DegenerateWire";
    case BuildCode::WireOnFaceEdge:
      return "WireOnFaceEdge";
    case BuildCode::DuplicateWire:
      return "DuplicateWire";
    case BuildCode::ContainerOnFaceVertex:
      return "ContainerOnFaceVertex";
    case BuildCode::NotProjectable:
      return "NotProjectable";
    case BuildCode::ReservedFaceFlags:
      return "ReservedFaceFlags";
    case BuildCode::WireOnProjectedFace:
      return "WireOnProjectedFace";
    case BuildCode::InternalError:
      return "InternalError";
    case BuildCode::RadialTie:
      return "RadialTie";
  }
  return "?";
}

bool build_code_is_error(BuildCode c)
{
  return c != BuildCode::RadialTie;
}

template <geom::CoordType C> uint32_t Builder<C>::vertex(PersistentId id, const Point &p, ContainerHint c)
{
  verts_.append({id, p, c});
  return uint32_t(verts_.size() - 1);
}

template <geom::CoordType C>
uint32_t Builder<C>::face(PersistentId id,
                          std::span<const uint32_t> keys,
                          std::span<const uint32_t> loop_sizes,
                          PersistentId front,
                          PersistentId back,
                          uint8_t flags)
{
  FIn f;
  f.id = id;
  f.key_begin = uint32_t(fkeys_.size());
  f.loop_begin = uint32_t(fsizes_.size());
  f.nloops = uint32_t(loop_sizes.size());
  f.hint[0] = front;
  f.hint[1] = back;
  f.flags = flags;
  for (uint32_t k : keys) {
    fkeys_.append(k);
  }
  for (uint32_t s : loop_sizes) {
    fsizes_.append(s);
  }
  faces_.append(f);
  return uint32_t(faces_.size() - 1);
}

template <geom::CoordType C>
uint32_t Builder<C>::face(PersistentId id,
                          std::initializer_list<std::initializer_list<uint32_t>> loops,
                          PersistentId front,
                          PersistentId back,
                          uint8_t flags)
{
  util::Vector<uint32_t> keys, sizes;
  for (const auto &l : loops) {
    sizes.append(uint32_t(l.size()));
    for (uint32_t k : l) {
      keys.append(k);
    }
  }
  return face(id,
              std::span<const uint32_t>(keys.data(), keys.size()),
              std::span<const uint32_t>(sizes.data(), sizes.size()),
              front,
              back,
              flags);
}

template <geom::CoordType C> uint32_t Builder<C>::wire(PersistentId id, uint32_t a, uint32_t b, ContainerHint c)
{
  wires_.append({id, a, b, c});
  return uint32_t(wires_.size() - 1);
}

namespace {

constexpr int kChunk = 1024;

template <typename T> std::span<T> sp(util::Vector<T> &v)
{
  return std::span<T>(v.data(), v.size());
}
template <typename T> std::span<const T> csp(const util::Vector<T> &v)
{
  return std::span<const T>(v.data(), v.size());
}

/** Copies @p v into elements [0, v.size()) of @p col, page-parallel. */
template <typename Col, typename T> void write_column(Col &col, const util::Vector<T> &v)
{
  col.parallel_write(0, uint32_t(v.size()), [&](std::span<T> s, uint32_t base) {
    for (size_t i = 0; i < s.size(); i++) {
      s[i] = v[base + i];
    }
  });
}

template <typename T> bool bit_eq(const T &a, const T &b)
{
  return std::memcmp(&a, &b, sizeof(T)) == 0;
}

struct Pair {
  uint32_t a, b; /* vertex indices, a < b */
  friend bool operator<(const Pair &x, const Pair &y)
  {
    return x.a != y.a ? x.a < y.a : x.b < y.b;
  }
  friend bool operator==(const Pair &x, const Pair &y) = default;
};

} // namespace

template <geom::CoordType C> bool Builder<C>::build(Complex<C> &out, BuildReport &report, const BuildPolicy &policy) const
{
  out = Complex<C>();
  const uint32_t nvin = uint32_t(verts_.size());

  /* ---- 1. vertices: weld by ID ---- */
  util::Vector<uint32_t> vorder;
  for (uint32_t i = 0; i < nvin; i++) {
    vorder.append(i);
  }
  /* (id, key) is unique; output depends only on the ids */
  util::sort_unique_keys(sp(vorder), [&](uint32_t x, uint32_t y) {
    return verts_[x].id != verts_[y].id ? verts_[x].id < verts_[y].id : x < y;
  });
  util::Vector<uint32_t> key_to_v;
  key_to_v.resize(nvin);
  util::Vector<uint32_t> v_rep; /* vertex -> its first input key */
  for (uint32_t r = 0; r < nvin; r++) {
    const uint32_t k = vorder[r];
    if (r > 0 && verts_[k].id == verts_[vorder[r - 1]].id) {
      const uint32_t rep = v_rep[v_rep.size() - 1];
      if (!bit_eq(verts_[k].p, verts_[rep].p)) {
        report.add(BuildCode::VertexPositionMismatch, verts_[k].id, k);
      }
      if (!(verts_[k].c == verts_[rep].c)) {
        report.add(BuildCode::VertexContainerMismatch, verts_[k].id, k);
      }
      key_to_v[k] = uint32_t(v_rep.size() - 1);
    }
    else {
      key_to_v[k] = uint32_t(v_rep.size());
      v_rep.append(k);
    }
  }
  if (policy.weld_equal_positions && v_rep.size() > 1) {
    /* group by position bytes; each group maps to its smallest ID (the first
     * in ID order) */
    const uint32_t n = uint32_t(v_rep.size());
    util::Vector<uint32_t> byp;
    for (uint32_t v = 0; v < n; v++) {
      byp.append(v);
    }
    util::sort_unique_keys(sp(byp), [&](uint32_t x, uint32_t y) {
      const int c = std::memcmp(&verts_[v_rep[x]].p, &verts_[v_rep[y]].p, sizeof(Point));
      return c != 0 ? c < 0 : x < y;
    });
    util::Vector<uint32_t> rep_of;
    rep_of.resize(n);
    for (uint32_t r = 0; r < n; r++) {
      const uint32_t v = byp[r];
      rep_of[v] = (r > 0 && bit_eq(verts_[v_rep[v]].p, verts_[v_rep[byp[r - 1]]].p)) ? rep_of[byp[r - 1]] : v;
    }
    util::Vector<uint32_t> renum, kept;
    renum.resize(n);
    for (uint32_t v = 0; v < n; v++) {
      if (rep_of[v] == v) {
        renum[v] = uint32_t(kept.size());
        kept.append(v_rep[v]);
      }
    }
    for (uint32_t k = 0; k < nvin; k++) {
      key_to_v[k] = renum[rep_of[key_to_v[k]]];
    }
    v_rep = kept;
  }
  const uint32_t V = uint32_t(v_rep.size());

  /* ---- 2. faces: sorted by ID ---- */
  const uint32_t nf = uint32_t(faces_.size());
  util::Vector<uint32_t> forder;
  for (uint32_t i = 0; i < nf; i++) {
    forder.append(i);
  }
  {
    auto less = [&](uint32_t x, uint32_t y) { return faces_[x].id < faces_[y].id; };
    size_t tie = 0;
    while (!util::sort_unique_keys(sp(forder), less, &tie)) {
      report.add(BuildCode::DuplicateFaceId, faces_[forder[tie]].id, forder[tie]);
      /* drop the duplicate and retry, to report every duplicate once */
      forder.remove_at(int(tie));
    }
  }
  util::Vector<uint32_t> key_to_f;
  key_to_f.resize(nf);
  for (uint32_t i = 0; i < nf; i++) {
    key_to_f[i] = NONE;
  }
  for (uint32_t r = 0; r < forder.size(); r++) {
    key_to_f[forder[r]] = r;
  }
  const uint32_t F = uint32_t(forder.size());

  /* per face (in output order): loop offsets into a flat vertex array */
  util::Vector<uint32_t> f_loop0, l_vert0, lv; /* face -> first loop; loop -> first vertex; loop vertices */
  for (uint32_t f = 0; f < F; f++) {
    const FIn &fi = faces_[forder[f]];
    if (fi.flags & face_flags::ProjAxisMask) {
      report.add(BuildCode::ReservedFaceFlags, fi.id, forder[f]);
    }
    f_loop0.append(uint32_t(l_vert0.size()));
    uint32_t kb = fi.key_begin;
    for (uint32_t l = 0; l < fi.nloops; l++) {
      l_vert0.append(uint32_t(lv.size()));
      const uint32_t m = fsizes_[fi.loop_begin + l];
      for (uint32_t j = 0; j < m; j++, kb++) {
        const uint32_t key = fkeys_[kb];
        if (key >= nvin) {
          report.add(BuildCode::BadVertexKey, fi.id, key);
          lv.append(0);
        }
        else {
          lv.append(key_to_v[key]);
        }
      }
    }
  }
  f_loop0.append(uint32_t(l_vert0.size()));
  l_vert0.append(uint32_t(lv.size()));
  const uint32_t L = uint32_t(l_vert0.size() - 1);
  const uint32_t NC = uint32_t(lv.size());
  if (!report.ok()) {
    return false;
  }

  /* per-face combinatorial and geometric checks (parallel, one code per face) */
  auto vpos = [&](uint32_t v) -> const Point & { return verts_[v_rep[v]].p; };
  util::Vector<int32_t> f_err;
  f_err.resize(F);
  util::Vector<int8_t> f_axis; /* projected faces: the projection axis; -1 = planar */
  f_axis.resize(F);
  for (uint32_t f = 0; f < F; f++) {
    f_axis[f] = -1;
  }
  util::Vector<util::Vector<geom::Triangle>> f_tris; /* T(f) of projected faces */
  f_tris.resize(F);
  task::parallel_for_chunks(litestl::util::IndexRange(0, int(F)), 256, [&](int, litestl::util::IndexRange r) {
    util::Vector<uint64_t, 64> vv;
    util::Vector<Point, 32> pts;
    util::Vector<uint32_t, 8> off;
    for (int f : r) {
      int32_t err = -1;
      auto fail = [&](BuildCode c) {
        if (err < 0) {
          err = int32_t(c);
        }
      };
      const uint32_t l0 = f_loop0[f], l1 = f_loop0[f + 1];
      if (l0 == l1) {
        fail(BuildCode::LooplessFace);
      }
      vv.clear();
      for (uint32_t l = l0; l < l1; l++) {
        const uint32_t m = l_vert0[l + 1] - l_vert0[l];
        if (m < 3) {
          fail(BuildCode::LoopTooShort);
        }
        for (uint32_t j = l_vert0[l]; j < l_vert0[l + 1]; j++) {
          vv.append((uint64_t(lv[j]) << 32) | l);
        }
      }
      std::sort(vv.begin(), vv.end());
      for (size_t i = 1; i < vv.size(); i++) {
        if ((vv[i] >> 32) == (vv[i - 1] >> 32)) {
          fail(uint32_t(vv[i]) == uint32_t(vv[i - 1]) ? BuildCode::RepeatedVertexInLoop
                                                       : BuildCode::VertexSharedBetweenLoops);
        }
      }
      if (err >= 0) {
        f_err[f] = err;
        continue;
      }
      /* geometry: planarity, non-degenerate loops, hole orientation */
      pts.clear();
      off.clear();
      off.append(0);
      for (uint32_t l = l0; l < l1; l++) {
        for (uint32_t j = l_vert0[l]; j < l_vert0[l + 1]; j++) {
          pts.append(vpos(lv[j]));
        }
        off.append(uint32_t(pts.size()));
      }
      const geom::PolyView<C> view{std::span<const Point>(pts.data(), pts.size()),
                                   std::span<const uint32_t>(off.data(), off.size())};
      /* base triple: pts[0], the first distinct point, the first off their line */
      int ib = -1, ic = -1;
      for (int i = 1; i < int(pts.size()) && ib < 0; i++) {
        if (geom::lex_compare<C>(pts[i], pts[0]) != 0) {
          ib = i;
        }
      }
      for (int i = 1; ib >= 0 && i < int(pts.size()) && ic < 0; i++) {
        for (int k = 0; k < 3; k++) {
          if (geom::orient2d_proj<C>(pts[0], pts[ib], pts[i], k) != 0) {
            ic = i;
            break;
          }
        }
      }
      bool planar = ic >= 0;
      if (ic < 0) {
        fail(BuildCode::DegenerateLoop);
      }
      else {
        for (size_t i = 0; i < pts.size() && planar; i++) {
          planar = geom::vol3<C>(pts[0], pts[ib], pts[ic], pts[i]) == 0;
        }
        if (!planar && !policy.allow_projectable_faces) {
          fail(BuildCode::NonPlanarFace);
        }
      }
      if (err < 0 && !planar) {
        /* amendment A1: projectable along the dominant canonical Newell axis,
         * else the others in index order; T(f) is the canonical CDT */
        const int dom = geom::project_axis_canonical<C>(view);
        int axes[3] = {dom, -1, -1}, na = dom >= 0 ? 1 : 0;
        for (int k = 0; k < 3; k++) {
          if (k != dom) {
            axes[na++] = k;
          }
        }
        bool winding = false, ok = false;
        for (int a = 0; a < na && !ok; a++) {
          const geom::CdtStatus st = geom::cdt_proj<C>(view, axes[a], f_tris[f]);
          if (st == geom::CdtStatus::Ok) {
            f_axis[f] = int8_t(axes[a]);
            ok = true;
          }
          winding = winding || st == geom::CdtStatus::HoleOrientation;
        }
        if (!ok) {
          fail(winding ? BuildCode::HoleOrientation : BuildCode::NotProjectable);
        }
      }
      if (err < 0 && planar) {
        const int axis = geom::project_axis<C>(view);
        if (axis < 0) {
          fail(BuildCode::DegenerateLoop);
        }
        else {
          const uint32_t outer = geom::outer_loop_proj<C>(view, axis);
          const int so = geom::loop_orientation_proj<C>(view.loop(outer), axis);
          for (uint32_t l = 0; l < view.loop_count(); l++) {
            const int s = geom::loop_orientation_proj<C>(view.loop(l), axis);
            if (s == 0) {
              fail(BuildCode::DegenerateLoop);
            }
            else if (l != outer && s == so) {
              fail(BuildCode::HoleOrientation);
            }
          }
        }
      }
      f_err[f] = err;
    }
  });
  for (uint32_t f = 0; f < F; f++) {
    if (f_err[f] >= 0) {
      report.add(BuildCode(f_err[f]), faces_[forder[f]].id, forder[f]);
    }
    else if (f_axis[f] >= 0) {
      report.projected_faces++;
    }
  }

  /* ---- 3. edges: unique unordered vertex pairs from faces and wires ---- */
  struct PairUse {
    Pair p;
    uint32_t wire; /* NONE for a face use */
  };
  util::Vector<PairUse> uses;
  for (uint32_t l = 0; l < L; l++) {
    const uint32_t b = l_vert0[l], m = l_vert0[l + 1] - b;
    for (uint32_t j = 0; j < m; j++) {
      const uint32_t x = lv[b + j], y = lv[b + (j + 1) % m];
      uses.append({{std::min(x, y), std::max(x, y)}, NONE});
    }
  }
  const uint32_t nw = uint32_t(wires_.size());
  util::Vector<uint32_t> w_a, w_b;
  for (uint32_t w = 0; w < nw; w++) {
    const WIn &wi = wires_[w];
    if (wi.a >= nvin || wi.b >= nvin) {
      report.add(BuildCode::BadVertexKey, wi.id, w);
      w_a.append(0);
      w_b.append(0);
      continue;
    }
    const uint32_t a = key_to_v[wi.a], b = key_to_v[wi.b];
    w_a.append(a);
    w_b.append(b);
    if (a == b) {
      report.add(BuildCode::DegenerateWire, wi.id, w);
      continue;
    }
    uses.append({{std::min(a, b), std::max(a, b)}, w});
  }
  {
    /* duplicate wire IDs */
    util::Vector<uint32_t> wo;
    for (uint32_t w = 0; w < nw; w++) {
      wo.append(w);
    }
    util::sort_unique_keys(sp(wo), [&](uint32_t x, uint32_t y) {
      return wires_[x].id != wires_[y].id ? wires_[x].id < wires_[y].id : x < y;
    });
    for (uint32_t i = 1; i < nw; i++) {
      if (wires_[wo[i]].id == wires_[wo[i - 1]].id) {
        report.add(BuildCode::DuplicateWireId, wires_[wo[i]].id, wo[i]);
      }
    }
  }
  /* face uses (wire = NONE) sort after wire uses of the same pair */
  std::sort(uses.begin(), uses.end(), [](const PairUse &x, const PairUse &y) {
    return x.p == y.p ? x.wire < y.wire : x.p < y.p;
  });
  util::Vector<Pair> epair;
  util::Vector<uint32_t> e_wire; /* edge -> wire index or NONE */
  for (uint32_t i = 0; i < uses.size(); i++) {
    const PairUse &u = uses[i];
    if (i > 0 && uses[i - 1].p == u.p) {
      const uint32_t prev_wire = uses[i - 1].wire;
      if (prev_wire != NONE && u.wire != NONE) {
        report.add(BuildCode::DuplicateWire, wires_[u.wire].id, u.wire);
      }
      else if (prev_wire != NONE && u.wire == NONE && e_wire[e_wire.size() - 1] != NONE) {
        report.add(BuildCode::WireOnFaceEdge, wires_[prev_wire].id, prev_wire);
        e_wire[e_wire.size() - 1] = NONE;
      }
      continue;
    }
    epair.append(u.p);
    e_wire.append(u.wire);
  }
  const uint32_t E = uint32_t(epair.size());
  auto find_edge = [&](uint32_t x, uint32_t y) {
    const Pair p{std::min(x, y), std::max(x, y)};
    const Pair *base = epair.data();
    return uint32_t(std::lower_bound(base, base + epair.size(), p) - base);
  };

  /* vertices used by face edges may not carry a container */
  util::Vector<uint8_t> v_on_face;
  v_on_face.resize(V);
  for (uint32_t v = 0; v < V; v++) {
    v_on_face[v] = 0;
  }
  for (uint32_t j = 0; j < NC; j++) {
    v_on_face[lv[j]] = 1;
  }
  for (uint32_t v = 0; v < V; v++) {
    const VIn &vi = verts_[v_rep[v]];
    if (vi.c.kind != ContainerHint::Kind::None && v_on_face[v]) {
      report.add(BuildCode::ContainerOnFaceVertex, vi.id, v_rep[v]);
    }
    if (vi.c.kind == ContainerHint::Kind::Face && (vi.c.face_key >= nf || key_to_f[vi.c.face_key] == NONE)) {
      report.add(BuildCode::BadFaceKey, vi.id, v_rep[v]);
    }
  }
  for (uint32_t w = 0; w < nw; w++) {
    const WIn &wi = wires_[w];
    if (wi.c.kind == ContainerHint::Kind::Face && (wi.c.face_key >= nf || key_to_f[wi.c.face_key] == NONE)) {
      report.add(BuildCode::BadFaceKey, wi.id, w);
    }
    else if (wi.c.kind == ContainerHint::Kind::Face && f_axis[key_to_f[wi.c.face_key]] >= 0) {
      report.add(BuildCode::WireOnProjectedFace, wi.id, w); /* a straight wire leaves a folded surface */
    }
  }
  if (!report.ok()) {
    return false;
  }

  /* ---- 4. write vertices, edges, loops, coedges, faces ---- */
  out.frame = frame_;
  out.regions_dirty = true;
  out.set_allows_projectable(policy.allow_projectable_faces);
  {
    out.verts.alloc_n(V);
    util::Vector<Point> pos;
    util::Vector<PersistentId> vid, vhint;
    util::Vector<ident::ContainerRef> vcont;
    for (uint32_t v = 0; v < V; v++) {
      const VIn &vi = verts_[v_rep[v]];
      pos.append(vi.p);
      vid.append(vi.id);
      ident::ContainerRef cr;
      PersistentId h{};
      if (vi.c.kind == ContainerHint::Kind::Face) {
        ident::ContainerRef::face(FaceRef(key_to_f[vi.c.face_key]), cr);
      }
      else if (vi.c.kind == ContainerHint::Kind::Region) {
        h = vi.c.region;
      }
      vcont.append(cr);
      vhint.append(h);
    }
    write_column(out.verts.template get<"pos">(), pos);
    write_column(out.verts.template get<".id">(), vid);
    write_column(out.verts.template get<".v.container">(), vcont);
    write_column(out.verts.template get<".v.container_hint">(), vhint);
  }
  {
    out.edges.alloc_n(E);
    util::Vector<U32x2> vs;
    util::Vector<PersistentId> eid, ehint;
    util::Vector<ident::ContainerRef> econt;
    for (uint32_t e = 0; e < E; e++) {
      vs.append({epair[e].a, epair[e].b});
      ident::ContainerRef cr;
      PersistentId h{};
      if (e_wire[e] != NONE) {
        const WIn &wi = wires_[e_wire[e]];
        eid.append(wi.id);
        if (wi.c.kind == ContainerHint::Kind::Face) {
          ident::ContainerRef::face(FaceRef(key_to_f[wi.c.face_key]), cr);
        }
        else if (wi.c.kind == ContainerHint::Kind::Region) {
          h = wi.c.region;
        }
      }
      else {
        eid.append(ident::edge_id(verts_[v_rep[epair[e].a]].id, verts_[v_rep[epair[e].b]].id));
      }
      econt.append(cr);
      ehint.append(h);
    }
    write_column(out.edges.template get<".e.vs">(), vs);
    write_column(out.edges.template get<".id">(), eid);
    write_column(out.edges.template get<".e.container">(), econt);
    write_column(out.edges.template get<".e.container_hint">(), ehint);
  }
  util::Vector<uint32_t> c_e, c_v, c_l, c_next, c_prev;
  util::Vector<uint8_t> c_flags;
  util::Vector<uint32_t> l_c, l_f, l_next, l_size;
  util::Vector<PersistentId> l_id;
  {
    out.coedges.alloc_n(NC);
    out.loops.alloc_n(L);
    c_e.resize(NC);
    c_v.resize(NC);
    c_l.resize(NC);
    c_next.resize(NC);
    c_prev.resize(NC);
    c_flags.resize(NC);
    for (uint32_t f = 0; f < F; f++) {
      const PersistentId fid = faces_[forder[f]].id;
      for (uint32_t l = f_loop0[f]; l < f_loop0[f + 1]; l++) {
        const uint32_t b = l_vert0[l], m = l_vert0[l + 1] - b;
        for (uint32_t j = 0; j < m; j++) {
          const uint32_t c = b + j, x = lv[c], y = lv[b + (j + 1) % m];
          c_e[c] = find_edge(x, y);
          c_v[c] = x;
          c_l[c] = l;
          c_next[c] = b + (j + 1) % m;
          c_prev[c] = b + (j + m - 1) % m;
          c_flags[c] = x < y ? coedge_flags::Sense : 0; /* vs[0] is the smaller index */
        }
        l_c.append(b);
        l_f.append(f);
        l_next.append(l + 1 < f_loop0[f + 1] ? l + 1 : NONE);
        l_size.append(m);
        l_id.append(ident::loop_id(fid, l - f_loop0[f]));
      }
    }
    write_column(out.coedges.template get<".c.e">(), c_e);
    write_column(out.coedges.template get<".c.v">(), c_v);
    write_column(out.coedges.template get<".c.l">(), c_l);
    write_column(out.coedges.template get<".c.next">(), c_next);
    write_column(out.coedges.template get<".c.prev">(), c_prev);
    write_column(out.coedges.template get<".c.flags">(), c_flags);
    write_column(out.loops.template get<".l.c">(), l_c);
    write_column(out.loops.template get<".l.f">(), l_f);
    write_column(out.loops.template get<".l.next">(), l_next);
    write_column(out.loops.template get<".l.size">(), l_size);
    write_column(out.loops.template get<".id">(), l_id);
  }
  {
    out.faces.alloc_n(F);
    util::Vector<uint32_t> f_l;
    util::Vector<uint8_t> f_flags;
    util::Vector<PersistentId> f_id, hints;
    for (uint32_t f = 0; f < F; f++) {
      const FIn &fi = faces_[forder[f]];
      f_l.append(f_loop0[f] < f_loop0[f + 1] ? f_loop0[f] : NONE);
      f_flags.append(f_axis[f] >= 0 ? with_proj_axis(fi.flags, f_axis[f]) : fi.flags);
      f_id.append(fi.id);
      hints.append(fi.hint[0]);
      hints.append(fi.hint[1]);
    }
    write_column(out.faces.template get<".f.l">(), f_l);
    write_column(out.faces.template get<".f.flags">(), f_flags);
    write_column(out.faces.template get<".id">(), f_id);
    write_column(out.faces.sides().template get<".fs.hint">(), hints);
  }

  /* ---- 5. radial cycles (exact, parallel over edges) ---- */
  {
    util::Vector<FaceFrame> frames;
    frames.resize(F);
    task::parallel_for_chunks(litestl::util::IndexRange(0, int(F)), 256, [&](int, litestl::util::IndexRange r) {
      for (int f : r) {
        frames[f] = face_frame<C>(out, FaceRef(uint32_t(f)));
      }
    });
    /* projected faces: the third vertex of each coedge's T(f) triangle */
    util::Vector<uint32_t> c_third;
    c_third.resize(NC);
    for (uint32_t c = 0; c < NC; c++) {
      c_third[c] = NONE;
    }
    for (uint32_t f = 0; f < F; f++) {
      if (f_axis[f] < 0) {
        continue;
      }
      const uint32_t base = l_vert0[f_loop0[f]];
      for (const geom::Triangle &t : f_tris[f]) {
        for (int i = 0; i < 3; i++) {
          const uint32_t c = base + t[i];
          if (c_next[c] == base + t[(i + 1) % 3]) {
            c_third[c] = lv[base + t[(i + 2) % 3]];
          }
        }
      }
    }
    util::Vector<uint32_t> cnt, start;
    cnt.resize(E);
    start.resize(E);
    for (uint32_t e = 0; e < E; e++) {
      cnt[e] = 0;
    }
    for (uint32_t c = 0; c < NC; c++) {
      cnt[c_e[c]]++;
    }
    task::parallel_exclusive_scan<uint32_t>(csp(cnt), sp(start), kChunk);
    util::Vector<uint32_t> by_edge, fill;
    by_edge.resize(NC);
    fill = start;
    for (uint32_t c = 0; c < NC; c++) {
      by_edge[fill[c_e[c]]++] = c; /* ascending coedge index per edge */
    }
    util::Vector<uint32_t> rn, rp, e_c;
    util::Vector<uint8_t> e_flag; /* 1 = tie, 2 = failure */
    rn.resize(NC);
    rp.resize(NC);
    e_c.resize(E);
    e_flag.resize(E);
    task::parallel_for_chunks(litestl::util::IndexRange(0, int(E)), kChunk, [&](int, litestl::util::IndexRange r) {
      util::Vector<uint32_t, 8> order;
      for (int e : r) {
        e_flag[e] = 0;
        const uint32_t n = cnt[e];
        if (n == 0) {
          e_c[e] = NONE;
          continue;
        }
        bool tie = false;
        auto half_plane = [&](CoedgeRef c, geom::HalfPlane<C> &h) {
          const uint32_t f = l_f[c_l[c.i]];
          if (f_axis[f] >= 0) {
            if (c_third[c.i] == NONE) {
              return false;
            }
            h.q = vpos(c_third[c.i]);
            h.s = 1; /* T(f) triangles lie on the interior side */
            return true;
          }
          return coedge_half_plane<C>(out, c, frames[f], h);
        };
        const bool ok = radial_order<C>(out,
                                        EdgeRef(uint32_t(e)),
                                        std::span<const uint32_t>(by_edge.data() + start[e], n),
                                        half_plane,
                                        order,
                                        &tie);
        if (!ok) {
          e_flag[e] = 2;
          e_c[e] = NONE;
          continue;
        }
        e_flag[e] = tie ? 1 : 0;
        e_c[e] = order[0];
        for (uint32_t i = 0; i < n; i++) {
          rn[order[i]] = order[(i + 1) % n];
          rp[order[i]] = order[(i + n - 1) % n];
        }
      }
    });
    for (uint32_t e = 0; e < E; e++) {
      if (e_flag[e] == 2) {
        report.add(BuildCode::InternalError, out.edge_id(EdgeRef(e)), e);
      }
      else if (e_flag[e] == 1) {
        report.add(BuildCode::RadialTie, out.edge_id(EdgeRef(e)), e);
      }
    }
    if (!report.ok()) {
      out = Complex<C>();
      return false;
    }
    write_column(out.coedges.template get<".c.radial_next">(), rn);
    write_column(out.coedges.template get<".c.radial_prev">(), rp);
    write_column(out.edges.template get<".e.c">(), e_c);
  }

  /* ---- 6. disk cycles: edges at each vertex in edge-index order ---- */
  {
    util::Vector<uint32_t> deg, start;
    deg.resize(V);
    start.resize(V);
    for (uint32_t v = 0; v < V; v++) {
      deg[v] = 0;
    }
    for (uint32_t e = 0; e < E; e++) {
      deg[epair[e].a]++;
      deg[epair[e].b]++;
    }
    task::parallel_exclusive_scan<uint32_t>(csp(deg), sp(start), kChunk);
    util::Vector<uint32_t> inc, fill = start; /* packed (edge << 1 | slot) */
    inc.resize(2 * E);
    for (uint32_t e = 0; e < E; e++) {
      inc[fill[epair[e].a]++] = disk_pack(e, 0);
      inc[fill[epair[e].b]++] = disk_pack(e, 1);
    }
    util::Vector<U32x4> disk;
    disk.resize(E);
    util::Vector<uint32_t> v_e;
    v_e.resize(V);
    task::parallel_for_chunks(litestl::util::IndexRange(0, int(V)), kChunk, [&](int, litestl::util::IndexRange r) {
      for (int v : r) {
        const uint32_t n = deg[v], s = start[v];
        v_e[v] = n ? disk_edge(inc[s]) : NONE;
        for (uint32_t i = 0; i < n; i++) {
          const uint32_t cur = inc[s + i];
          const uint32_t pv = inc[s + (i + n - 1) % n], nx = inc[s + (i + 1) % n];
          /* each (edge, slot) belongs to exactly one vertex: disjoint writes */
          disk[disk_edge(cur)][2 * disk_slot(cur)] = pv;
          disk[disk_edge(cur)][2 * disk_slot(cur) + 1] = nx;
        }
      }
    });
    write_column(out.edges.template get<".e.disk">(), disk);
    write_column(out.verts.template get<".v.e">(), v_e);
  }
  return true;
}

template class Builder<double>;
template class Builder<float>;
template class Builder<geom::Lattice>;

} // namespace ubertopo::topo
