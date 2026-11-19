/* The face-box grid (face_grid.h; plan P3b W5, C6, C7). */

#include "ubertopo/topo/face_grid.h"

#include <algorithm>
#include <cmath>

#include "ubertopo/topo/validate_detail.h"

namespace ubertopo::topo {

namespace util = litestl::util;

namespace {

template <geom::CoordType C> bool face_box(const Complex<C> &k, uint32_t f, FaceGrid::Box &b)
{
  bool any = false;
  for (int i = 0; i < 3; i++) {
    b.lo[i] = INFINITY;
    b.hi[i] = -INFINITY;
  }
  k.for_each_loop(FaceRef(f), [&](LoopRef l) {
    k.for_each_coedge(l, [&](CoedgeRef c) {
      const auto d = geom::to_double3<C>(k.pos(k.start_vert(c)));
      for (int i = 0; i < 3; i++) {
        b.lo[i] = std::min(b.lo[i], d[i]);
        b.hi[i] = std::max(b.hi[i], d[i]);
      }
      any = true;
    });
  });
  return any;
}

template <geom::CoordType C> FaceGrid::Box points_box(const Complex<C> &k, std::initializer_list<uint32_t> vs)
{
  FaceGrid::Box b;
  for (int i = 0; i < 3; i++) {
    b.lo[i] = INFINITY;
    b.hi[i] = -INFINITY;
  }
  for (uint32_t v : vs) {
    const auto d = geom::to_double3<C>(k.pos(VertRef(v)));
    for (int i = 0; i < 3; i++) {
      b.lo[i] = std::min(b.lo[i], d[i]);
      b.hi[i] = std::max(b.hi[i], d[i]);
    }
  }
  return b;
}

} // namespace

void FaceGrid::grow_bounds(const Box &b)
{
  if (!bounds_set_) {
    bounds_ = b;
    bounds_set_ = true;
    return;
  }
  for (int i = 0; i < 3; i++) {
    bounds_.lo[i] = std::min(bounds_.lo[i], b.lo[i]);
    bounds_.hi[i] = std::max(bounds_.hi[i], b.hi[i]);
  }
}

void FaceGrid::add_entry(const CellKey &key, const Entry &x)
{
  if (auto *v = cells_.lookup_ptr(key)) {
    v->append(x);
    return;
  }
  util::Vector<Entry, 4> v;
  v.append(x);
  cells_.insert(key, std::move(v));
}

void FaceGrid::drop(util::Map<uint32_t, Rec> &recs, uint32_t elem, bool face)
{
  Rec *r = recs.lookup_ptr(elem);
  if (!r) {
    return;
  }
  for (const CellKey &key : r->cells) {
    auto *v = cells_.lookup_ptr(key);
    if (!v) {
      continue;
    }
    for (int i = v->size(); i-- > 0;) {
      const Entry &x = (*v)[i];
      if (x.elem == elem && (x.kind == Kind::Wire) != face) {
        (*v)[i] = v->last();
        v->resize(v->size() - 1);
      }
    }
    if (v->size() == 0) {
      cells_.remove(key);
    }
  }
  recs.remove(elem);
}

template <geom::CoordType C> void FaceGrid::update_face(const Complex<C> &k, uint32_t f)
{
  drop(faces_, f, true);
  if (f >= k.faces.capacity() || !k.faces.live(f)) {
    return;
  }
  Rec r;
  if (!face_box<C>(k, f, r.box)) {
    return;
  }
  auto add_cells = [&](const Box &b, const Entry &x) {
    for_cells(b, [&](const CellKey &key) {
      add_entry(key, x);
      r.cells.append(key);
    });
  };
  bool by_tris = false;
  if (cell_count(r.box) > kLargeCells) {
    ValidationReport dummy;
    util::Vector<std::array<uint32_t, 3>> tris;
    if (vdetail::face_checks_and_tris<C>(k, f, false, dummy, tris)) {
      by_tris = true;
      /* front orientation: as face_fan (T(f) as given for projected faces;
       * the outer loop's orientation for planar ones) */
      if (k.face_proj_axis(FaceRef(f)) < 0) {
        FacePoints<C> fp;
        fp.gather(k, FaceRef(f));
        const int axis = vdetail::face_axis<C>(k, FaceRef(f), fp.view());
        const int front = geom::face_front_sign<C>(fp.view(), axis);
        for (auto &t : tris) {
          const int o = geom::orient2d_proj<C>(k.pos(VertRef(t[0])), k.pos(VertRef(t[1])), k.pos(VertRef(t[2])), axis);
          if (o != 0 && o != front) {
            std::swap(t[1], t[2]);
          }
        }
      }
      for (uint32_t i = 0; i < uint32_t(tris.size()); i++) {
        const Box tb = points_box<C>(k, {tris[i][0], tris[i][1], tris[i][2]});
        r.tris.append(tris[i]);
        r.tri_boxes.append(tb);
        add_cells(tb, Entry{f, i, Kind::FaceTri});
      }
    }
  }
  if (!by_tris) {
    add_cells(r.box, Entry{f, NONE, Kind::Face});
  }
  std::sort(r.cells.begin(), r.cells.end(), [](const CellKey &a, const CellKey &b) {
    return a.x != b.x ? a.x < b.x : (a.y != b.y ? a.y < b.y : a.z < b.z);
  });
  r.cells.resize(int(std::unique(r.cells.begin(), r.cells.end()) - r.cells.begin()));
  grow_bounds(r.box);
  faces_.insert(f, std::move(r));
}

template <geom::CoordType C> void FaceGrid::update_wire(const Complex<C> &k, uint32_t e)
{
  drop(wires_, e, false);
  if (e >= k.edges.capacity() || !k.edges.live(e) || k.edge_coedge(EdgeRef(e))) {
    return;
  }
  const auto vs = k.edge_verts(EdgeRef(e));
  Rec r;
  r.box = points_box<C>(k, {vs[0].i, vs[1].i});
  const auto p = geom::to_double3<C>(k.pos(vs[0])), q = geom::to_double3<C>(k.pos(vs[1]));
  const double pd[3] = {p[0], p[1], p[2]}, qd[3] = {q[0], q[1], q[2]};
  segment_boxes(pd, qd, [&](const Box &b) { for_cells(b, [&](const CellKey &key) { r.cells.append(key); }); });
  std::sort(r.cells.begin(), r.cells.end(), [](const CellKey &a, const CellKey &b) {
    return a.x != b.x ? a.x < b.x : (a.y != b.y ? a.y < b.y : a.z < b.z);
  });
  r.cells.resize(int(std::unique(r.cells.begin(), r.cells.end()) - r.cells.begin()));
  for (const CellKey &key : r.cells) {
    add_entry(key, Entry{e, NONE, Kind::Wire});
  }
  grow_bounds(r.box);
  wires_.insert(e, std::move(r));
}

template <geom::CoordType C> void FaceGrid::build_with_cell(const Complex<C> &k, double cell)
{
  invalidate();
  cell_ = cell;
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    if (k.faces.live(f)) {
      update_face(k, f);
    }
  }
  for (uint32_t e = 0; e < k.edges.capacity(); e++) {
    if (k.edges.live(e) && !k.edge_coedge(EdgeRef(e))) {
      update_wire(k, e);
    }
  }
  valid_ = true;
}

template <geom::CoordType C> void FaceGrid::build(const Complex<C> &k)
{
  /* the cell: the smallest power of two >= the median face-box extent */
  util::Vector<double> ext;
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    Box b;
    if (k.faces.live(f) && face_box<C>(k, f, b)) {
      ext.append(std::max({b.hi[0] - b.lo[0], b.hi[1] - b.lo[1], b.hi[2] - b.lo[2]}));
    }
  }
  if (ext.size() == 0) {
    for (uint32_t e = 0; e < k.edges.capacity(); e++) {
      if (k.edges.live(e) && !k.edge_coedge(EdgeRef(e))) {
        const auto vs = k.edge_verts(EdgeRef(e));
        const Box b = points_box<C>(k, {vs[0].i, vs[1].i});
        ext.append(std::max({b.hi[0] - b.lo[0], b.hi[1] - b.lo[1], b.hi[2] - b.lo[2]}));
      }
    }
  }
  double cell = 1.0;
  if (ext.size()) {
    std::nth_element(ext.begin(), ext.begin() + ext.size() / 2, ext.end());
    const double m = ext[ext.size() / 2];
    if (m > 0 && std::isfinite(m)) {
      cell = std::exp2(std::ceil(std::log2(m)));
    }
  }
  build_with_cell(k, cell);
}

bool FaceGrid::same_as(const FaceGrid &o) const
{
  if (cell_ != o.cell_) {
    return false;
  }
  auto same_recs = [](const util::Map<uint32_t, Rec> &a, const util::Map<uint32_t, Rec> &b) {
    uint32_t na = 0, nb = 0;
    for (auto &pr : a) {
      na++;
      const Rec *r = b.lookup_ptr(pr.key);
      if (!r || r->cells.size() != pr.value.cells.size() || r->tris.size() != pr.value.tris.size()) {
        return false;
      }
      for (int i = 0; i < r->cells.size(); i++) {
        if (!(r->cells[i] == pr.value.cells[i])) {
          return false;
        }
      }
      for (int i = 0; i < r->tris.size(); i++) {
        if (r->tris[i] != pr.value.tris[i]) {
          return false;
        }
      }
      for (int i = 0; i < 3; i++) {
        if (r->box.lo[i] != pr.value.box.lo[i] || r->box.hi[i] != pr.value.box.hi[i]) {
          return false;
        }
      }
    }
    for (auto &pr : b) {
      (void)pr;
      nb++;
    }
    return na == nb;
  };
  if (!same_recs(faces_, o.faces_) || !same_recs(wires_, o.wires_)) {
    return false;
  }
  auto key_of = [](const Entry &x) { return std::array<uint32_t, 3>{uint32_t(x.kind), x.elem, x.tri}; };
  uint32_t na = 0, nb = 0;
  for (auto &pr : cells_) {
    na++;
    const auto *v = o.cells_.lookup_ptr(pr.key);
    if (!v || v->size() != pr.value.size()) {
      return false;
    }
    util::Vector<std::array<uint32_t, 3>> a, b;
    for (const Entry &x : pr.value) {
      a.append(key_of(x));
    }
    for (const Entry &x : *v) {
      b.append(key_of(x));
    }
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    for (int i = 0; i < a.size(); i++) {
      if (a[i] != b[i]) {
        return false;
      }
    }
  }
  for (auto &pr : o.cells_) {
    (void)pr;
    nb++;
  }
  return na == nb;
}

#define UT_GRID_INST(C)                                                                                               \
  template void FaceGrid::build<C>(const Complex<C> &);                                                               \
  template void FaceGrid::build_with_cell<C>(const Complex<C> &, double);                                             \
  template void FaceGrid::update_face<C>(const Complex<C> &, uint32_t);                                               \
  template void FaceGrid::update_wire<C>(const Complex<C> &, uint32_t);
UT_GRID_INST(double)
UT_GRID_INST(float)
UT_GRID_INST(geom::Lattice)
#undef UT_GRID_INST

} // namespace ubertopo::topo
