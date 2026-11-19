/* graph2 and segment2 (pcurve.h). */

#include "ubertopo/curves/pcurve.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace ubertopo::curves {

namespace {

class Graph2Eval final : public Curve2 {
public:
  explicit Graph2Eval(const Graph2 *g) : f_(g->f) {}
  Interval domain() const override
  {
    return {f_.lo(), f_.hi()};
  }
  ParamKind param_kind() const override
  {
    return ParamKind::Station;
  }
  void eval(double t, int n, Vec<2> *out, Side side) const override
  {
    t = std::clamp(t, f_.lo(), f_.hi());
    out[0] = Vec<2>(t, f_.eval(t, 0, side));
    if (n >= 1) {
      out[1] = Vec<2>(1.0, f_.eval(t, 1, side));
    }
    for (int k = 2; k <= n; k++) {
      out[k] = Vec<2>(0.0, f_.eval(t, k, side));
    }
  }
  double eval_error() const override
  {
    return f_.eval_error(0); /* u = lambda is exact */
  }
  double deriv_eval_error(int k) const override
  {
    return f_.eval_error(k);
  }
  double deriv_bound(double t0, double t1, int k) const override
  {
    t0 = std::max(t0, f_.lo());
    t1 = std::min(t1, f_.hi());
    /* a break strictly inside where f is not C^k */
    for (int b = 1; b < f_.pieces.size(); b++) {
      if (f_.breaks[b] > t0 && f_.breaks[b] < t1 && f_.continuity_at(b) < k) {
        return INFINITY;
      }
    }
    const double m = f_.range(t0, t1, k).abs_max();
    if (k == 0) {
      const double u = std::max(std::fabs(t0), std::fabs(t1));
      return up(std::sqrt(u * u + m * m) * (1 + gamma_n(4)));
    }
    if (k == 1) {
      return up(std::sqrt(1 + m * m) * (1 + gamma_n(4)));
    }
    return up(m);
  }
  void special_params(double t0, double t1, util::Vector<SpecialParam> &out) const override
  {
    for (int b = 1; b < f_.pieces.size(); b++) {
      if (f_.breaks[b] >= t0 && f_.breaks[b] <= t1) {
        out.append({f_.breaks[b], uint8_t(std::max(0, f_.continuity_at(b)))});
      }
    }
  }

private:
  const Fn1D &f_;
};

class Segment2Eval final : public Curve2 {
public:
  explicit Segment2Eval(const Segment2 *s) : s_(*s)
  {
    for (double x : s_.p) {
      max_abs_ = std::max(max_abs_, std::fabs(x));
    }
  }
  Interval domain() const override
  {
    return {0.0, 1.0};
  }
  ParamKind param_kind() const override
  {
    return ParamKind::General;
  }
  void eval(double t, int n, Vec<2> *out, Side) const override
  {
    t = std::clamp(t, 0.0, 1.0);
    for (int c = 0; c < 2; c++) {
      const double d = s_.p[2 + c] - s_.p[c];
      out[0][c] = s_.p[c] + t * d;
      if (n >= 1) {
        out[1][c] = d;
      }
    }
    for (int k = 2; k <= n; k++) {
      out[k] = Vec<2>(0.0, 0.0);
    }
  }
  double eval_error() const override
  {
    /* a + t (b - a), t in [0, 1]: per coordinate gamma(3) (|a| + |b - a|) */
    return up(std::sqrt(2.0) * gamma_n(3) * 3 * max_abs_);
  }
  double deriv_eval_error(int k) const override
  {
    return k == 1 ? up(std::sqrt(2.0) * kUnit * 2 * max_abs_) : 0.0;
  }
  double deriv_bound(double, double, int k) const override
  {
    if (k >= 2) {
      return 0.0;
    }
    if (k == 1) {
      double s = 0;
      for (int c = 0; c < 2; c++) {
        const double x = std::fabs(s_.p[2 + c] - s_.p[c]) * (1 + kUnit);
        s += x * x;
      }
      return up(std::sqrt(s) * (1 + gamma_n(4)));
    }
    /* convex: the endpoints bound the segment */
    const double a = s_.p[0] * s_.p[0] + s_.p[1] * s_.p[1], b = s_.p[2] * s_.p[2] + s_.p[3] * s_.p[3];
    return up(std::sqrt(std::max(a, b)) * (1 + gamma_n(4)));
  }
  void special_params(double, double, util::Vector<SpecialParam> &) const override {}

private:
  const Segment2 &s_;
  double max_abs_ = 0;
};

class Graph2Kind final : public KindOps {
public:
  const char *key() const override
  {
    return "curves.graph2";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<Graph2Pool>(key());
  }
  int dim(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    return s.try_get<Graph2Pool>(r) ? 2 : 0;
  }
  util::UniquePtr<Curve2> make2(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Graph2 *g = s.try_get<Graph2Pool>(r);
    return g ? g->evaluator() : util::UniquePtr<Curve2>();
  }
};

class Segment2Kind final : public KindOps {
public:
  const char *key() const override
  {
    return "curves.segment2";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<Segment2Pool>(key());
  }
  int dim(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    return s.try_get<Segment2Pool>(r) ? 2 : 0;
  }
  util::UniquePtr<Curve2> make2(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Segment2 *g = s.try_get<Segment2Pool>(r);
    return g ? g->evaluator() : util::UniquePtr<Curve2>();
  }
};

} // namespace

/* ---- graph2 ---- */

const char *Graph2::make(const Fn1D &f, Graph2 &out)
{
  if (f.empty()) {
    return "graph2: empty function";
  }
  out.f = f;
  return nullptr;
}

void Graph2::hash_into(hash::Hasher128 &h) const
{
  f.hash_into(h);
}

bool Graph2::equal(const Graph2 &b) const
{
  return f.equal(b.f);
}

void Graph2::write(io::ByteWriter &w) const
{
  f.write(w);
}

bool Graph2::read(io::ByteReader &r, Graph2 &v, const attr::PoolReadLimits &)
{
  Graph2 g;
  if (!Fn1D::read(r, g.f) || g.f.empty()) {
    return false;
  }
  v = std::move(g);
  return true;
}

util::UniquePtr<Curve2> Graph2::evaluator() const
{
  return util::UniquePtr<Curve2>(litestl::alloc::New<Graph2Eval>("curves::Graph2Eval", this));
}

/* ---- segment2 ---- */

const char *Segment2::make(double u0, double v0, double u1, double v1, Segment2 &out)
{
  const double p[4] = {u0, v0, u1, v1};
  Segment2 s;
  for (int i = 0; i < 4; i++) {
    if (!std::isfinite(p[i])) {
      return "segment2: non-finite coordinate";
    }
    s.p[i] = p[i] == 0 ? 0.0 : p[i];
  }
  if (s.p[0] == s.p[2] && s.p[1] == s.p[3]) {
    return "segment2: equal endpoints";
  }
  out = s;
  return nullptr;
}

void Segment2::hash_into(hash::Hasher128 &h) const
{
  for (double x : p) {
    h.f64_bits(x);
  }
}

bool Segment2::equal(const Segment2 &b) const
{
  for (int i = 0; i < 4; i++) {
    if (std::bit_cast<uint64_t>(p[i]) != std::bit_cast<uint64_t>(b.p[i])) {
      return false;
    }
  }
  return true;
}

void Segment2::write(io::ByteWriter &w) const
{
  for (double x : p) {
    w.u64(std::bit_cast<uint64_t>(x));
  }
}

bool Segment2::read(io::ByteReader &r, Segment2 &v, const attr::PoolReadLimits &)
{
  double p[4];
  for (double &x : p) {
    x = std::bit_cast<double>(r.u64());
  }
  Segment2 s;
  if (r.failed() || make(p[0], p[1], p[2], p[3], s)) {
    return false;
  }
  Segment2 raw;
  for (int i = 0; i < 4; i++) {
    raw.p[i] = p[i];
  }
  if (!s.equal(raw)) {
    return false; /* not canonical (-0.0) */
  }
  v = s;
  return true;
}

util::UniquePtr<Curve2> Segment2::evaluator() const
{
  return util::UniquePtr<Curve2>(litestl::alloc::New<Segment2Eval>("curves::Segment2Eval", this));
}

const KindOps &graph2_kind()
{
  static const Graph2Kind k;
  return k;
}

const KindOps &segment2_kind()
{
  static const Segment2Kind k;
  return k;
}

} // namespace ubertopo::curves
