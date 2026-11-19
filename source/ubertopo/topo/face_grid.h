#pragma once

/* A persistent uniform grid over face boxes and wires (plan P3b W5, §3.5,
 * C6, C7, C13), so a filtered validation finds the faces near an edit
 * without visiting every face.
 *
 *  - Cells are cubes of side cell() (a power of two, the smallest one at
 *    least the median face-box extent when the grid was built); cell (i, j,
 *    k) covers [i, i + 1) * cell() on each axis. The size stays fixed until
 *    the next build().
 *  - A face spanning more than kLargeCells cells is entered by the boxes of
 *    its T(f) triangles (kept in its record, front-oriented), so a contact
 *    test or a ray touches only the nearby triangles; other faces by their
 *    box.
 *  - A wire is entered along its segment, piece by piece.
 *  - Boxes are padded by a tiny fraction of a cell before they are entered or
 *    queried, so exact contacts at cell boundaries are never lost to the
 *    rounding of coordinates to doubles.
 *
 * The grid is coordinate-independent; build() and update_*() read a
 * Complex<C> and are instantiated in face_grid.cc. The editor decides which
 * rows to update (editor_grid.h). */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "litestl/util/map.h"
#include "litestl/util/vector.h"
#include "ubertopo/geom/coord.h"
#include "ubertopo/topo/complex.h"

namespace ubertopo::topo {

class FaceGrid {
public:
  static constexpr uint32_t kLargeCells = 64;

  enum class Kind : uint8_t { Face, FaceTri, Wire };
  struct Entry {
    uint32_t elem = NONE; /* face or edge index */
    uint32_t tri = NONE;  /* FaceTri: index into the face record's tris */
    Kind kind = Kind::Face;
    bool operator==(const Entry &o) const = default;
  };
  struct Box {
    double lo[3], hi[3];
    bool overlaps(const Box &o) const
    {
      for (int i = 0; i < 3; i++) {
        if (lo[i] > o.hi[i] || o.lo[i] > hi[i]) {
          return false;
        }
      }
      return true;
    }
  };
  struct CellKey {
    int64_t x = 0, y = 0, z = 0;
    bool operator==(const CellKey &o) const = default;
    uint64_t computeHash() const
    {
      return uint64_t(x) * 0x9E3779B97F4A7C15ull ^ uint64_t(y) * 0xC2B2AE3D27D4EB4Full ^
             uint64_t(z) * 0x165667B19E3779F9ull;
    }
  };
  struct Rec {
    Box box{};
    litestl::util::Vector<CellKey, 8> cells;                  /* sorted, unique */
    litestl::util::Vector<std::array<uint32_t, 3>, 4> tris;   /* large faces: T(f), front-oriented */
    litestl::util::Vector<Box, 4> tri_boxes;
    /* the face (or wire) had a curved edge when entered: its tris hold
     * skeleton ids of that build's table, valid only by triangle index
     * (the canonical CDT gives the same order); plan P4b §12 */
    bool curved = false;
  };

  bool valid() const
  {
    return valid_;
  }
  void invalidate()
  {
    valid_ = false;
    cells_ = decltype(cells_)();
    faces_ = decltype(faces_)();
    wires_ = decltype(wires_)();
    bounds_set_ = false;
  }
  double cell() const
  {
    return cell_;
  }

  /** Builds from scratch: O(faces + wires). */
  template <geom::CoordType C> void build(const Complex<C> &k);
  /** Re-reads face @p f: drops its entries, then enters it again if live. */
  template <geom::CoordType C> void update_face(const Complex<C> &k, uint32_t f);
  /** Re-reads edge @p e: entered only while it is a live wire. */
  template <geom::CoordType C> void update_wire(const Complex<C> &k, uint32_t e);

  /** fn(const Entry &) for every entry in a cell overlapping @p b (an entry
   * in several such cells is reported once per cell). */
  template <typename F> void query(const Box &b, F &&fn) const
  {
    for_cells(b, [&](const CellKey &key) {
      if (const auto *v = cells_.lookup_ptr(key)) {
        for (const Entry &x : *v) {
          fn(x);
        }
      }
    });
  }
  /** fn(const Entry &) for the entries of the cells along segment (p, q)
   * inside the grid's bounds (conservative: whole cells around each piece). */
  template <typename F> void query_segment(const double p[3], const double q[3], F &&fn) const
  {
    segment_boxes(p, q, [&](const Box &b) {
      if (b.overlaps(bounds_)) {
        query(b, fn);
      }
    });
  }

  const Rec *face_rec(uint32_t f) const
  {
    return faces_.lookup_ptr(f);
  }
  const Rec *wire_rec(uint32_t e) const
  {
    return wires_.lookup_ptr(e);
  }
  /** A box containing everything ever entered since the last build. */
  const Box &bounds() const
  {
    return bounds_;
  }

  /** Same entries as @p o (tests: a grid kept by dirty rows must equal one
   * rebuilt from scratch with the same cell size). */
  bool same_as(const FaceGrid &o) const;
  /** Rebuilds with a fixed cell size (tests). */
  template <geom::CoordType C> void build_with_cell(const Complex<C> &k, double cell);

private:
  double cell_ = 1.0;
  bool valid_ = false;
  Box bounds_{{0, 0, 0}, {0, 0, 0}};
  bool bounds_set_ = false;
  litestl::util::Map<CellKey, litestl::util::Vector<Entry, 4>> cells_;
  litestl::util::Map<uint32_t, Rec> faces_, wires_;

  double pad() const
  {
    return cell_ * 1e-7;
  }
  int64_t cell_of(double x) const
  {
    return int64_t(std::floor(x / cell_));
  }
  template <typename F> void for_cells(const Box &b, F &&fn) const
  {
    const double d = pad();
    const int64_t x0 = cell_of(b.lo[0] - d), x1 = cell_of(b.hi[0] + d);
    const int64_t y0 = cell_of(b.lo[1] - d), y1 = cell_of(b.hi[1] + d);
    const int64_t z0 = cell_of(b.lo[2] - d), z1 = cell_of(b.hi[2] + d);
    for (int64_t x = x0; x <= x1; x++) {
      for (int64_t y = y0; y <= y1; y++) {
        for (int64_t z = z0; z <= z1; z++) {
          fn(CellKey{x, y, z});
        }
      }
    }
  }
  uint64_t cell_count(const Box &b) const
  {
    const double d = pad();
    uint64_t n = 1;
    for (int i = 0; i < 3; i++) {
      n *= uint64_t(cell_of(b.hi[i] + d) - cell_of(b.lo[i] - d) + 1);
    }
    return n;
  }
  /** Pieces of segment (p, q) no longer than a cell, as boxes. */
  template <typename F> void segment_boxes(const double p[3], const double q[3], F &&fn) const
  {
    double len = 0;
    for (int i = 0; i < 3; i++) {
      len = std::max(len, std::abs(q[i] - p[i]));
    }
    const uint64_t n = std::max<uint64_t>(1, uint64_t(std::ceil(len / cell_)));
    for (uint64_t s = 0; s < n; s++) {
      const double t0 = double(s) / double(n), t1 = double(s + 1) / double(n);
      Box b;
      for (int i = 0; i < 3; i++) {
        const double a = p[i] + (q[i] - p[i]) * t0, c = s + 1 == n ? q[i] : p[i] + (q[i] - p[i]) * t1;
        b.lo[i] = std::min(a, c);
        b.hi[i] = std::max(a, c);
      }
      fn(b);
    }
  }
  void grow_bounds(const Box &b);
  void add_entry(const CellKey &key, const Entry &x);
  void drop(litestl::util::Map<uint32_t, Rec> &recs, uint32_t elem, bool face);
};

} // namespace ubertopo::topo
