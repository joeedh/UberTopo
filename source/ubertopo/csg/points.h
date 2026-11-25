#pragma once

/* Tables with one entry per plane and per point of an evaluation (plan
 * 2026-11-25-exact-booleans §3). Indices follow the order of insertion, so
 * whoever wants a canonical order sorts. */

#include <cstdint>

#include "litestl/util/map.h"
#include "litestl/util/vector.h"
#include "ubertopo/csg/exact.h"

namespace ubertopo::csg {

class PlaneTable {
public:
  uint32_t add(const Plane &p)
  {
    if (const uint32_t *i = index_.lookup_ptr(p)) {
      return *i;
    }
    const uint32_t i = uint32_t(planes_.size());
    planes_.append(p);
    index_.add(p, i);
    return i;
  }
  const Plane &operator[](uint32_t i) const
  {
    return planes_[int(i)];
  }
  uint32_t size() const
  {
    return uint32_t(planes_.size());
  }

private:
  litestl::util::Vector<Plane> planes_;
  litestl::util::Map<Plane, uint32_t> index_;
};

class PointTable {
public:
  /** Fractional bits of the hash key. */
  static constexpr int kKeyBits = 20;

  uint32_t add(const HPoint &p)
  {
    const Key key = key_of(p);
    const uint32_t i = uint32_t(points_.size());
    uint32_t *head = heads_.lookup_ptr(key);
    if (head) {
      for (uint32_t j = *head; j != kNone; j = next_[int(j)]) {
        if (same_point(points_[int(j)], p)) {
          return j;
        }
      }
    }
    points_.append(p);
    next_.append(head ? *head : kNone);
    if (head) {
      *head = i;
    }
    else {
      heads_.add(key, i);
    }
    return i;
  }
  uint32_t add(const I3 &p)
  {
    return add(HPoint::from(p));
  }
  const HPoint &operator[](uint32_t i) const
  {
    return points_[int(i)];
  }
  uint32_t size() const
  {
    return uint32_t(points_.size());
  }

private:
  static constexpr uint32_t kNone = 0xffffffffu;
  struct Key {
    int64_t k[3];
    bool operator==(const Key &b) const = default;
    uint64_t computeHash() const
    {
      uint64_t h = 0;
      for (const int64_t v : k) {
        h = (h ^ uint64_t(v)) * 0x9e3779b97f4a7c15ull;
        h ^= h >> 29;
      }
      return h;
    }
  };
  static Key key_of(const HPoint &p)
  {
    Key r;
    for (int i = 0; i < 3; i++) {
      r.k[i] = floor_at(p, i, kKeyBits);
    }
    return r;
  }

  litestl::util::Vector<HPoint> points_;
  litestl::util::Vector<uint32_t> next_;
  litestl::util::Map<Key, uint32_t> heads_;
};

} // namespace ubertopo::csg
