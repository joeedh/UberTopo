#pragma once

/* The editor's face-box grid (plan P3b W5, C6, C13): a FaceGrid kept in step
 * with the complex by dirty rows, like EditIndex.
 *
 *  - touch(), alloc() and restore() mark the rows they write, put back or
 *    drop. A flush re-reads, from the current state, the faces and wires
 *    each marked row belongs to: a vertex's faces and wires, an edge's faces
 *    (and the edge as a wire), a coedge's face and edge, a loop's face, a
 *    face or face side. Rollback and undo therefore refresh exactly the
 *    faces whose boxes they restore (C6), and a face appended and then
 *    dropped loses its entries.
 *  - The grid is built on first use (the first Local or OnCommit
 *    validation) and kept from then on; compact barriers and stale-stamp
 *    checks rebuild it eagerly when it exists (C13). rebuild_regions leaves
 *    it alone: it holds geometry only, which build_regions doesn't write
 *    (plan 2026-11-22 §2.3), so its cell size is re-tuned only by those. */

#include <algorithm>
#include <cstdint>
#include <vector>

#include "litestl/util/vector.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/edit/edit_types.h"
#include "ubertopo/topo/face_grid.h"

namespace ubertopo::topo {

template <geom::CoordType C> class EditGrid {
public:
  bool valid() const
  {
    return grid_.valid();
  }
  void invalidate()
  {
    grid_.invalidate();
    dirty_.clear();
  }
  void mark(Grp g, uint32_t row)
  {
    if (grid_.valid()) {
      dirty_.push_back({uint8_t(g), row});
    }
  }
  /** Rebuilds now if the grid is in use (barriers, stale stamps). */
  void rebuild_if_used(const Complex<C> &k)
  {
    if (grid_.valid()) {
      dirty_.clear();
      grid_.build(k);
    }
  }
  /** Brings the grid up to date (builds it on first use). */
  void flush(const Complex<C> &k)
  {
    if (!grid_.valid()) {
      dirty_.clear();
      grid_.build(k);
      return;
    }
    if (dirty_.empty()) {
      return;
    }
    std::sort(dirty_.begin(), dirty_.end());
    dirty_.erase(std::unique(dirty_.begin(), dirty_.end()), dirty_.end());
    litestl::util::Vector<uint32_t, 32> faces, wires;
    auto radial_faces = [&](EdgeRef e) {
      k.for_each_radial(e, [&](CoedgeRef c) {
        const LoopRef l = k.loop_of(c);
        if (l.i < k.loops.capacity() && k.loops.live(l.i)) {
          faces.append(k.face_of(l).i);
        }
      });
    };
    for (const auto &[g, row] : dirty_) {
      switch (Grp(g)) {
        case Grp::Verts:
          if (row < k.verts.capacity() && k.verts.live(row)) {
            k.for_each_edge_at(VertRef(row), [&](EdgeRef e) {
              wires.append(e.i);
              radial_faces(e);
            });
          }
          break;
        case Grp::Edges:
          wires.append(row);
          if (row < k.edges.capacity() && k.edges.live(row)) {
            radial_faces(EdgeRef(row));
          }
          break;
        case Grp::Coedges:
          if (row < k.coedges.capacity() && k.coedges.live(row)) {
            const CoedgeRef c(row);
            wires.append(k.edge_of(c).i);
            const LoopRef l = k.loop_of(c);
            if (l.i < k.loops.capacity() && k.loops.live(l.i)) {
              faces.append(k.face_of(l).i);
            }
          }
          break;
        case Grp::Loops:
          if (row < k.loops.capacity() && k.loops.live(row)) {
            faces.append(k.face_of(LoopRef(row)).i);
          }
          break;
        case Grp::Faces:
          faces.append(row);
          break;
        case Grp::Sides:
          faces.append(row / 2);
          break;
        default:
          break;
      }
    }
    dirty_.clear();
    auto uniq = [](litestl::util::Vector<uint32_t, 32> &v) {
      std::sort(v.begin(), v.end());
      v.resize(int(std::unique(v.begin(), v.end()) - v.begin()));
    };
    uniq(faces);
    uniq(wires);
    for (uint32_t f : faces) {
      grid_.update_face(k, f);
    }
    for (uint32_t e : wires) {
      grid_.update_wire(k, e);
    }
  }
  const FaceGrid &grid() const
  {
    return grid_;
  }
  /** Test hook: the kept grid equals one rebuilt with the same cell size.
   * Flush first. */
  bool same_as_fresh(const Complex<C> &k) const
  {
    if (!grid_.valid()) {
      return true;
    }
    FaceGrid fresh;
    fresh.build_with_cell(k, grid_.cell());
    return grid_.same_as(fresh);
  }

private:
  FaceGrid grid_;
  std::vector<std::pair<uint8_t, uint32_t>> dirty_;
};

} // namespace ubertopo::topo
