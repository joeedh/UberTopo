#pragma once

/* Editor<C>: disk and radial cycle maintenance (plan P3 §3.4, C4, C5).
 * Template definitions, included only by editor.cc.
 *
 * Disk cycles: .e.disk[2·slot] = prev, [2·slot + 1] = next, entries packed
 * (edge << 1) | slot. A new edge is inserted just before the vertex's .v.e
 * entry (at the end of the walk). Radial cycles are always re-sorted from
 * geometry with radial_order() (the same rule as the builder and the
 * validator), so .e.c is the coedge whose face has the smallest ID. */

#include <map>
#include <vector>

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/face_geom.h"
#include "ubertopo/topo/radial.h"

namespace ubertopo::topo {

namespace edit_detail {

/** Half-planes of coedges for radial_order (planar rule or T(f) triangle). */
template <geom::CoordType C> struct HalfPlanes {
  const Complex<C> &k;
  std::map<uint32_t, FaceFrame> frames;
  std::map<uint32_t, std::vector<std::pair<CoedgeRef, VertRef>>> third;
  FaceRef failed; /* the face whose half-plane couldn't be formed */

  bool operator()(CoedgeRef c, geom::HalfPlane<C> &h)
  {
    const FaceRef f = k.face_of(c);
    if (k.face_proj_axis(f) >= 0) {
      auto it = third.find(f.i);
      if (it == third.end()) {
        litestl::util::Vector<std::pair<CoedgeRef, VertRef>> tv;
        std::vector<std::pair<CoedgeRef, VertRef>> v;
        if (projected_third_vertices<C>(k, f, tv)) {
          for (const auto &x : tv) {
            v.push_back(x);
          }
        }
        it = third.emplace(f.i, std::move(v)).first;
      }
      for (const auto &x : it->second) {
        if (x.first == c) {
          h.q = k.pos(x.second);
          h.s = 1; /* T(f) triangles lie on the interior side */
          return true;
        }
      }
      failed = f;
      return false;
    }
    auto it = frames.find(f.i);
    if (it == frames.end()) {
      it = frames.emplace(f.i, face_frame<C>(k, f)).first;
    }
    if (!coedge_half_plane<C>(k, c, it->second, h)) {
      failed = f;
      return false;
    }
    return true;
  }
};

} // namespace edit_detail

template <geom::CoordType C> void Editor<C>::disk_insert(EdgeRef e, uint32_t slot)
{
  const VertRef v = k_.edge_verts(e)[slot];
  const EdgeRef first = k_.vert_edge(v);
  const uint32_t me = disk_pack(e.i, slot);
  if (!first) {
    set_disk(e, 2 * slot, me);
    set_disk(e, 2 * slot + 1, me);
    setV<".v.e">(v, e.i);
    return;
  }
  const uint32_t fs = edge_slot(first, v);
  const uint32_t first_p = disk_pack(first.i, fs);
  const uint32_t last_p = k_.edges.template get<".e.disk">()[first.i][2 * fs];
  set_disk(e, 2 * slot, last_p);
  set_disk(e, 2 * slot + 1, first_p);
  set_disk(EdgeRef(disk_edge(last_p)), 2 * disk_slot(last_p) + 1, me);
  set_disk(first, 2 * fs, me);
}

template <geom::CoordType C> void Editor<C>::disk_remove(EdgeRef e, uint32_t slot)
{
  const VertRef v = k_.edge_verts(e)[slot];
  const uint32_t me = disk_pack(e.i, slot);
  const U32x4 d = k_.edges.template get<".e.disk">()[e.i];
  const uint32_t p = d[2 * slot], n = d[2 * slot + 1];
  if (p == me) {
    setV<".v.e">(v, NONE);
  }
  else {
    set_disk(EdgeRef(disk_edge(p)), 2 * disk_slot(p) + 1, n);
    set_disk(EdgeRef(disk_edge(n)), 2 * disk_slot(n), p);
    if (k_.vert_edge(v) == e) {
      setV<".v.e">(v, disk_edge(n));
    }
  }
  set_disk(e, 2 * slot, NONE);
  set_disk(e, 2 * slot + 1, NONE);
}

template <geom::CoordType C>
void Editor<C>::disk_replace(EdgeRef old_e, uint32_t old_slot, EdgeRef new_e, uint32_t new_slot)
{
  const VertRef v = k_.edge_verts(old_e)[old_slot];
  const uint32_t op = disk_pack(old_e.i, old_slot), np = disk_pack(new_e.i, new_slot);
  const U32x4 d = k_.edges.template get<".e.disk">()[old_e.i];
  const uint32_t p = d[2 * old_slot], n = d[2 * old_slot + 1];
  if (p == op) {
    set_disk(new_e, 2 * new_slot, np);
    set_disk(new_e, 2 * new_slot + 1, np);
  }
  else {
    set_disk(new_e, 2 * new_slot, p);
    set_disk(new_e, 2 * new_slot + 1, n);
    set_disk(EdgeRef(disk_edge(p)), 2 * disk_slot(p) + 1, np);
    set_disk(EdgeRef(disk_edge(n)), 2 * disk_slot(n), np);
  }
  if (k_.vert_edge(v) == old_e) {
    setV<".v.e">(v, new_e.i);
  }
  set_disk(old_e, 2 * old_slot, NONE);
  set_disk(old_e, 2 * old_slot + 1, NONE);
}

template <geom::CoordType C> void Editor<C>::link_radial(EdgeRef e, std::span<const uint32_t> order)
{
  const uint32_t n = uint32_t(order.size());
  if (n == 0) {
    setE<".e.c">(e, NONE);
    return;
  }
  for (uint32_t i = 0; i < n; i++) {
    const CoedgeRef c(order[i]);
    setC<".c.radial_next">(c, order[(i + 1) % n]);
    setC<".c.radial_prev">(c, order[(i + n - 1) % n]);
  }
  setE<".e.c">(e, order[0]);
}

template <geom::CoordType C> EditCode Editor<C>::resort_radial(EdgeRef e, std::span<const uint32_t> extra)
{
  litestl::util::Vector<uint32_t, 8> in, order;
  k_.for_each_radial(e, [&](CoedgeRef c) { in.append(c.i); });
  for (uint32_t c : extra) {
    in.append(c);
  }
  edit_detail::HalfPlanes<C> hp{k_, {}, {}, {}};
  bool tie = false;
  if (!radial_order<C>(k_, e, std::span<const uint32_t>(in.data(), in.size()), hp, order, &tie)) {
    rep_.kind = ident::ElemKind::Face;
    rep_.index = hp.failed.i;
    return hp.failed && k_.face_proj_axis(hp.failed) >= 0 ? EditCode::NotProjectable : EditCode::DegenerateLoop;
  }
  if (tie) {
    rep_.kind = ident::ElemKind::Edge;
    rep_.index = e.i;
    return EditCode::RadialTie;
  }
  link_radial(e, std::span<const uint32_t>(order.data(), order.size()));
  return EditCode::Ok;
}

template <geom::CoordType C> void Editor<C>::radial_remove(CoedgeRef c)
{
  const EdgeRef e = k_.edge_of(c);
  const CoedgeRef p = k_.radial_prev(c), n = k_.radial_next(c);
  if (n == c) {
    setE<".e.c">(e, NONE);
  }
  else {
    setC<".c.radial_next">(p, n.i);
    setC<".c.radial_prev">(n, p.i);
    if (k_.edge_coedge(e) == c) {
      setE<".e.c">(e, n.i);
    }
    refresh_ec(e);
  }
  setC<".c.radial_next">(c, NONE);
  setC<".c.radial_prev">(c, NONE);
}

template <geom::CoordType C> void Editor<C>::refresh_ec(EdgeRef e)
{
  CoedgeRef best = k_.edge_coedge(e);
  if (!best) {
    return;
  }
  ident::PersistentId best_id = k_.face_id(k_.face_of(best));
  k_.for_each_radial(e, [&](CoedgeRef c) {
    const ident::PersistentId id = k_.face_id(k_.face_of(c));
    if (id < best_id) {
      best_id = id;
      best = c;
    }
  });
  if (best != k_.edge_coedge(e)) {
    setE<".e.c">(e, best.i);
  }
}

template <geom::CoordType C> EditCode Editor<C>::resort_face_edges(FaceRef f)
{
  EditCode code = EditCode::Ok;
  k_.for_each_loop(f, [&](LoopRef l) {
    k_.for_each_coedge(l, [&](CoedgeRef c) {
      if (code == EditCode::Ok) {
        code = resort_radial(k_.edge_of(c));
      }
    });
  });
  return code;
}

template <geom::CoordType C> void Editor<C>::fix_projection(FaceRef f)
{
  const uint8_t fl = k_.face_flags(f);
  if (proj_axis(fl) < 0) {
    return;
  }
  FacePoints<C> fp;
  if (!fp.gather(k_, f)) {
    return;
  }
  if (face_points_planar<C>(fp.view())) {
    setF<".f.flags">(f, uint8_t(fl & ~face_flags::ProjAxisMask));
  }
}

} // namespace ubertopo::topo
