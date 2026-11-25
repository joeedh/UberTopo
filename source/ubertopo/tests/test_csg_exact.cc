/* Exact numbers of the Boolean engine (plan 2026-11-25-exact-booleans §3),
 * against arbitrary-precision integers, on random inputs of three kinds
 * (small, full range, extreme corners) that reach the bounds. */

#include <cstdio>
#include <vector>

#include "litestl/util/alloc.h"
#include "support/bigint_oracle.h"
#include "support/splitmix64.h"
#include "ubertopo/csg/points.h"
#include "ut_test.h"

using namespace ubertopo::csg;
using litestl::test::BigInt;
using litestl::test::SplitMix64;

namespace {

template <int N> BigInt big(const lmath::WideInt<N> &v)
{
  return BigInt::from_twos(v.w, N);
}
BigInt big(int64_t v)
{
  return BigInt::from_i64(v);
}

struct BigPoint {
  BigInt x[3], w;
};

BigPoint big(const HPoint &p)
{
  return {{big(p.x[0]), big(p.x[1]), big(p.x[2])}, big(p.w)};
}

/* n . p + d, over the point's denominator */
BigInt eval(const BigPoint &p, const Plane &q)
{
  return big(q.n[0]) * p.x[0] + big(q.n[1]) * p.x[1] + big(q.n[2]) * p.x[2] + big(q.d) * p.w;
}

int64_t coord(SplitMix64 &r, int kind)
{
  switch (kind) {
    case 0:
      return int64_t(r.below(3)) - 1;
    case 1:
      return int64_t(r.below(uint64_t(2 * kCoordLimit) + 1)) - kCoordLimit;
    default: /* corners of the range, and their neighbours */
      return r.below(2) ? kCoordLimit - int64_t(r.below(3)) : int64_t(r.below(3)) - kCoordLimit;
  }
}

I3 point(SplitMix64 &r, int kind)
{
  return I3(coord(r, kind), coord(r, kind), coord(r, kind));
}

bool plane(SplitMix64 &r, int kind, Plane &out)
{
  return plane_through(point(r, kind), point(r, kind), point(r, kind), out) != 0;
}

bool in_box(const BigPoint &p)
{
  const BigInt lim = big(kCoordLimit) * p.w;
  for (int i = 0; i < 3; i++) {
    if ((p.x[i] - lim).sign() > 0 || (p.x[i] + lim).sign() < 0) {
      return false;
    }
  }
  return true;
}

void test_planes()
{
  SplitMix64 r(1);
  uint32_t flat = 0;
  for (int n = 0; n < 30000; n++) {
    const int kind = int(r.below(3));
    const I3 a = point(r, kind), b = point(r, kind), c = point(r, kind), d = point(r, kind);
    Plane p, q;
    const int s = plane_through(a, b, c, p);
    const I3 u = sub(b, a), v = sub(c, a);
    BigInt m[3];
    for (int i = 0; i < 3; i++) {
      const int j = (i + 1) % 3, k = (i + 2) % 3;
      m[i] = big(u[j]) * big(v[k]) - big(u[k]) * big(v[j]);
    }
    const bool zero = m[0].sign() == 0 && m[1].sign() == 0 && m[2].sign() == 0;
    UT_CHECK((s == 0) == zero);
    if (s == 0) {
      flat++;
      continue;
    }
    UT_CHECK(classify(a, p) == 0 && classify(b, p) == 0 && classify(c, p) == 0);
    UT_CHECK(std::gcd(std::gcd(p.n[0], p.n[1]), p.n[2]) == 1);
    UT_CHECK((p.n[0] ? p.n[0] : p.n[1] ? p.n[1] : p.n[2]) > 0);
    /* the sign: the volume of (a, b, c, d) */
    const I3 e = sub(d, a);
    const BigInt vol = m[0] * big(e[0]) + m[1] * big(e[1]) + m[2] * big(e[2]);
    UT_CHECK(classify(d, p) * s == vol.sign());
    /* the same plane from its points in other orders */
    UT_CHECK(plane_through(b, a, c, q) == -s && q == p && q.computeHash() == p.computeHash());
    UT_CHECK(plane_through(c, a, b, q) == s && q == p);
    for (int axis = 0; axis < 3; axis++) {
      I3 up = a;
      up[axis] += 1;
      const bool ok = edge_plane(a, b, axis, q);
      UT_CHECK(ok == (a[(axis + 1) % 3] != b[(axis + 1) % 3] || a[(axis + 2) % 3] != b[(axis + 2) % 3]));
      UT_CHECK(!ok || (classify(a, q) == 0 && classify(b, q) == 0 && classify(up, q) == 0));
    }
  }
  UT_CHECK(flat > 100);
}

void test_points()
{
  SplitMix64 r(2);
  uint32_t made = 0, none = 0, on = 0, equal = 0;
  int bits_w = 0, bits_x = 0, bits_s = 0;
  std::vector<HPoint> pts, small;
  const auto bits = [](BigInt v) {
    int n = 0;
    BigInt p = big(int64_t(1));
    const BigInt a = v.sign() < 0 ? -v : v;
    while ((a - p).sign() >= 0) {
      p += p;
      n++;
    }
    return n;
  };
  for (int n = 0; n < 20000; n++) {
    const int kind = int(r.below(3));
    Plane p, q, s, t;
    if (!plane(r, kind, p) || !plane(r, kind, q) || !plane(r, kind, s) || !plane(r, kind, t)) {
      continue;
    }
    std::vector<std::vector<BigInt>> m;
    for (const Plane *x : {&p, &q, &s}) {
      m.push_back({big(x->n[0]), big(x->n[1]), big(x->n[2])});
    }
    const BigInt det = BigInt::det(m);
    UT_CHECK(big(triple(p.n, q.n, s.n)) == det);
    HPoint h;
    const bool ok = meet(p, q, s, h);
    UT_CHECK(ok == (det.sign() != 0));
    if (!ok) {
      none++;
      continue;
    }
    const BigPoint b = big(h);
    UT_CHECK(b.w.sign() > 0 && (b.w == det || b.w == -det));
    if (!in_box(b)) {
      continue; /* the bounds hold inside the box only */
    }
    made++;
    UT_CHECK(eval(b, p).sign() == 0 && eval(b, q).sign() == 0 && eval(b, s).sign() == 0);
    const BigInt e = eval(b, t);
    UT_CHECK(classify(h, t) == e.sign());
    on += e.sign() == 0;
    bits_w = std::max(bits_w, bits(b.w));
    bits_x = std::max(bits_x, bits(b.x[0]));
    bits_s = std::max(bits_s, bits(e));
    for (int i = 0; i < 3; i++) {
      for (const int shift : {0, 4, 20}) {
        const BigInt scaled = b.x[i] * big(int64_t(1) << shift);
        const BigInt f = big(floor_at(h, i, shift));
        UT_CHECK((scaled - f * b.w).sign() >= 0 && (scaled - f * b.w - b.w).sign() < 0);
        /* nearest, ties up: 2 x - w <= 2 k w < 2 x + w, shifted by one */
        const BigInt k = big(round_at(h, i, shift)), two = big(int64_t(2));
        UT_CHECK((two * scaled + b.w - two * k * b.w).sign() >= 0);
        UT_CHECK((two * scaled - b.w - two * k * b.w).sign() < 0);
      }
    }
    /* the same point from other planes of its own pencil: the order, and a
     * scaled representation */
    HPoint h2, h3;
    UT_CHECK(meet(s, p, q, h2) && same_point(h, h2));
    h3.w = h.w * Int256::from(3);
    for (int i = 0; i < 3; i++) {
      h3.x[i] = h.x[i] * Int256::from(3);
      UT_CHECK(floor_at(h3, i, 20) == floor_at(h, i, 20) && round_at(h3, i, 0) == round_at(h, i, 0));
    }
    UT_CHECK(same_point(h, h3));
    pts.push_back(h);
    if (kind == 0) {
      small.push_back(h);
    }
  }
  printf("points %u, parallel %u, on a fourth plane %u; bits: w %d, x %d, classify %d\n", made, none, on, bits_w, bits_x,
         bits_s);
  UT_CHECK(made > 5000 && none > 50 && on > 50);
  UT_CHECK(bits_w > 155 && bits_w <= 167 && bits_x <= 193 && bits_s > 230 && bits_s <= 251);
  /* comparisons and orientations of pairs and triples */
  for (int n = 0; n < 20000; n++) {
    const std::vector<HPoint> &from = n % 2 ? pts : small;
    const HPoint &a = from[r.below(from.size())], &b = from[r.below(from.size())], &c = from[r.below(from.size())];
    const BigPoint ba = big(a), bb = big(b), bc = big(c);
    int lex = 0;
    for (int i = 0; i < 3; i++) {
      const int want = (ba.x[i] * bb.w - bb.x[i] * ba.w).sign();
      UT_CHECK(compare(a, b, i) == want);
      lex = lex ? lex : want;
    }
    UT_CHECK(lex_compare(a, b) == lex && same_point(a, b) == (lex == 0));
    equal += lex == 0;
    for (int axis = 0; axis < 3; axis++) {
      const int u = (axis + 1) % 3, v = (axis + 2) % 3;
      const std::vector<std::vector<BigInt>> m = {
          {ba.x[u], ba.x[v], ba.w}, {bb.x[u], bb.x[v], bb.w}, {bc.x[u], bc.x[v], bc.w}};
      UT_CHECK(orient2d(a, b, c, axis) == BigInt::det(m).sign());
    }
  }
  printf("equal pairs %u; ", equal);
  UT_CHECK(equal > 20);
}

void test_directions()
{
  SplitMix64 r(3);
  for (int n = 0; n < 20000; n++) {
    const int kind = int(r.below(3));
    Plane p, q, s;
    if (!plane(r, kind, p) || !plane(r, kind, q) || !plane(r, kind, s)) {
      continue;
    }
    const Dir d = cross(p.n, q.n), e = cross(s.n, q.n);
    BigInt b[3], c[3];
    for (int i = 0; i < 3; i++) {
      const int j = (i + 1) % 3, k = (i + 2) % 3;
      b[i] = big(p.n[j]) * big(q.n[k]) - big(p.n[k]) * big(q.n[j]);
      c[i] = big(s.n[j]) * big(q.n[k]) - big(s.n[k]) * big(q.n[j]);
      UT_CHECK(big(d.v[i]) == b[i]);
    }
    UT_CHECK(big(dot(d, e)) == b[0] * c[0] + b[1] * c[1] + b[2] * c[2]);
    UT_CHECK(big(dot(s.n, d)) == big(s.n[0]) * b[0] + big(s.n[1]) * b[1] + big(s.n[2]) * b[2]);
    UT_CHECK(d.is_zero() == (p.n[0] == q.n[0] && p.n[1] == q.n[1] && p.n[2] == q.n[2]));
    const I3 u = point(r, kind), v = point(r, kind);
    const Dir f = direction(u, v);
    UT_CHECK(big(f.v[0]) == big(v[0]) - big(u[0]) && big(f.v[2]) == big(v[2]) - big(u[2]));
  }
}

void test_rays()
{
  SplitMix64 r(4);
  uint32_t zero = 0, made = 0;
  while (made < 8000) {
    const int kind = int(r.below(3));
    Plane p[6], t;
    bool ok = plane(r, kind, t);
    for (Plane &x : p) {
      ok = ok && plane(r, kind, x);
    }
    HPoint a, b;
    if (!ok || !meet(p[0], p[1], p[2], a) || !meet(p[3], p[4], p[5], b) || !in_box(big(a)) || !in_box(big(b))) {
      continue;
    }
    made++;
    const Mid m(a, b);
    const BigPoint ba = big(a), bb = big(b);
    BigPoint o;
    o.w = big(int64_t(2)) * ba.w * bb.w;
    for (int i = 0; i < 3; i++) {
      o.x[i] = ba.x[i] * bb.w + bb.x[i] * ba.w;
    }
    UT_CHECK(big(m.w) == o.w && big(m.x[1]) == o.x[1]);
    UT_CHECK(classify(m, t) == eval(o, t).sign());
    const I3 u = point(r, kind), v = point(r, kind);
    const int64_t ts[3] = {1, 1 + int64_t(r.below(50)), kMaxRay};
    for (const int64_t tt : ts) {
      const I3 d = ray_direction(tt);
      std::vector<std::vector<BigInt>> rows = {{big(d[0]), big(d[1]), big(d[2])}, {}, {}};
      for (int i = 0; i < 3; i++) {
        rows[1].push_back(big(u[i]) * o.w - o.x[i]);
        rows[2].push_back(big(v[i]) * o.w - o.x[i]);
      }
      const int want = BigInt::det(rows).sign();
      UT_CHECK(ray_side(m, d, u, v) == want);
      zero += want == 0;
    }
  }
  UT_CHECK(zero > 20);
}

void test_tables()
{
  SplitMix64 r(5);
  PlaneTable planes;
  PointTable points;
  std::vector<Plane> all;
  for (int n = 0; n < 3000; n++) {
    Plane p;
    if (!plane(r, 0, p)) {
      continue;
    }
    const uint32_t i = planes.add(p);
    UT_CHECK(planes[i] == p && planes.add(p) == i);
    all.push_back(p);
  }
  UT_CHECK(planes.size() < all.size());
  std::vector<std::pair<HPoint, uint32_t>> seen;
  for (int n = 0; n < 3000; n++) {
    HPoint h;
    if (meet(all[r.below(all.size())], all[r.below(all.size())], all[r.below(all.size())], h) && in_box(big(h))) {
      seen.push_back({h, points.add(h)});
    }
  }
  uint32_t shared = 0;
  for (size_t i = 0; i < seen.size(); i++) {
    UT_CHECK(same_point(points[seen[i].second], seen[i].first));
    for (size_t j = i + 1; j < seen.size() && j < i + 300; j++) {
      const bool same = same_point(seen[i].first, seen[j].first);
      UT_CHECK(same == (seen[i].second == seen[j].second));
      shared += same;
    }
  }
  UT_CHECK(shared > 50 && points.size() < seen.size());
  /* an integer point, and the same one as three planes */
  Plane x, y, z;
  HPoint h;
  UT_CHECK(edge_plane(I3(3, 0, 0), I3(3, 7, 0), 2, x) && edge_plane(I3(0, -5, 0), I3(1, -5, 0), 2, y) &&
           edge_plane(I3(0, 0, 9), I3(1, 0, 9), 1, z) && meet(x, y, z, h));
  UT_CHECK(points.add(h) == points.add(I3(3, -5, 9)));
  /* points closer than the key's grid are told apart */
  Plane y1, z0;
  UT_CHECK(edge_plane(I3(0, 1, 0), I3(1, 1, 0), 2, y1) && edge_plane(I3(0, 0, 0), I3(1, 0, 0), 1, z0));
  std::vector<HPoint> near;
  std::vector<uint32_t> at;
  for (int64_t i = 0; i < 5; i++) {
    Plane a;
    HPoint g;
    UT_CHECK(plane_from(I3(0, 0, 0), I3((int64_t(1) << 54) - 1 - 2 * i, 1, 0), a) == 1 && meet(a, y1, z0, g));
    UT_CHECK(floor_at(g, 0, 20) == -1);
    near.push_back(g);
    at.push_back(points.add(g));
  }
  for (size_t i = 0; i < near.size(); i++) {
    UT_CHECK(points.add(near[i]) == at[i] && (i == 0 || at[i] != at[i - 1]));
  }
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_planes();
  test_points();
  test_directions();
  test_rays();
  test_tables();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
