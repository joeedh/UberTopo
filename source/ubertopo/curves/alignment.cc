/* alignment3 (alignment.h). */

#include "ubertopo/curves/alignment.h"

#include <algorithm>
#include <cmath>

#include "ubertopo/curves/cesaro.h"
#include "ubertopo/curves/offset.h"

namespace ubertopo::curves {

namespace {

class Alignment3View final : public Curve3 {
public:
  /* adopts h (alloc::New passes its arguments by value) */
  Alignment3View(Curve2 *h, const Alignment3 *a) : h_(h), z_(a->z)
  {
    e_[0] = up(h_->eval_error() + z_.eval_error(0) + z_.max_jump());
    for (int k = 1; k <= 2; k++) {
      e_[k] = up(h_->deriv_eval_error(k) + z_.eval_error(k));
    }
  }
  Interval domain() const override
  {
    return {z_.lo(), z_.hi()};
  }
  ParamKind param_kind() const override
  {
    return ParamKind::Station;
  }
  void eval(double t, int n, Vec<3> *out, Side side) const override
  {
    t = std::clamp(t, z_.lo(), z_.hi());
    Vec<2> hv[kMaxEvalDeriv + 1];
    h_->eval(t, n, hv, side);
    for (int k = 0; k <= n; k++) {
      out[k] = Vec<3>(hv[k][0], hv[k][1], z_.eval(t, k, side));
    }
  }
  double eval_error() const override
  {
    return e_[0];
  }
  double deriv_eval_error(int k) const override
  {
    return k <= 2 ? e_[k] : INFINITY;
  }
  double deriv_bound(double t0, double t1, int k) const override
  {
    for (int b = 1; b < z_.pieces.size(); b++) {
      if (z_.breaks[b] > t0 && z_.breaks[b] < t1 && std::max(0, z_.continuity_at(b)) < k) {
        return INFINITY;
      }
    }
    const double bh = h_->deriv_bound(t0, t1, k);
    const double bz = z_.range(t0, t1, k).abs_max();
    return up(std::sqrt(bh * bh + bz * bz) * (1 + gamma_n(4)));
  }
  void special_params(double t0, double t1, util::Vector<SpecialParam> &out) const override
  {
    util::Vector<SpecialParam> found;
    h_->special_params(t0, t1, found);
    for (int b = 1; b < z_.pieces.size(); b++) {
      if (z_.breaks[b] >= t0 && z_.breaks[b] <= t1) {
        found.append({z_.breaks[b], uint8_t(std::max(0, z_.continuity_at(b)))});
      }
    }
    std::sort(found.begin(), found.end(), [](const SpecialParam &x, const SpecialParam &y) { return x.t < y.t; });
    for (const SpecialParam &x : found) {
      if (out.size() > 0 && out.last().t == x.t) {
        out.last().continuity = std::min(out.last().continuity, x.continuity);
        continue;
      }
      out.append(x);
    }
  }

private:
  util::UniquePtr<Curve2> h_;
  const Fn1D &z_;
  double e_[3] = {0, 0, 0};
};

bool horizontal_kind(attr::PoolRef h)
{
  const KindOps *k = kind_of(h);
  return k == &cesaro2_kind() || k == &offset2_kind();
}

/* the horizontal's evaluator for a valid alignment entry, else null */
util::UniquePtr<Curve2> resolve(const attr::PoolStore &s, const Alignment3 &a)
{
  if (!horizontal_kind(a.horizontal)) {
    return {};
  }
  util::UniquePtr<Curve2> h = curve2(s, a.horizontal);
  if (!h) {
    return h;
  }
  const Interval d = h->domain();
  if (a.z.empty() || !(a.z.lo() >= d.lo) || !(a.z.hi() <= d.hi)) {
    return {};
  }
  return h;
}

class Alignment3Kind final : public KindOps {
public:
  const char *key() const override
  {
    return "curves.alignment3";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<Alignment3Pool>(key());
  }
  int dim(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Alignment3 *a = s.try_get<Alignment3Pool>(r);
    return a && resolve(s, *a) ? 3 : 0;
  }
  util::UniquePtr<Curve3> make3(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Alignment3 *a = s.try_get<Alignment3Pool>(r);
    if (!a) {
      return {};
    }
    util::UniquePtr<Curve2> h = resolve(s, *a);
    if (!h) {
      return {};
    }
    return util::UniquePtr<Curve3>(litestl::alloc::New<Alignment3View>("curves::Alignment3View", h.release(), a));
  }
};

} // namespace

const char *Alignment3::make(const attr::PoolStore &s, attr::PoolRef horizontal, const Fn1D &z, Alignment3 &out)
{
  Alignment3 a;
  a.horizontal = horizontal;
  a.z = z;
  if (!horizontal_kind(horizontal) || !curve2(s, horizontal)) {
    return "alignment3: the horizontal must be a live cesaro2 or offset2";
  }
  if (!resolve(s, a)) {
    return "alignment3: z's domain must lie inside the horizontal's";
  }
  for (int b = 1; b < z.pieces.size(); b++) {
    if (z.continuity_at(b) < 0) {
      return "alignment3: z jumps at a break";
    }
  }
  out = std::move(a);
  return nullptr;
}

void Alignment3::hash_into(hash::Hasher128 &h) const
{
  h.u32(horizontal.pool).u32(horizontal.slot);
  z.hash_into(h);
}

bool Alignment3::equal(const Alignment3 &b) const
{
  return horizontal == b.horizontal && z.equal(b.z);
}

void Alignment3::write(io::ByteWriter &w) const
{
  w.u32(horizontal.pool);
  w.u32(horizontal.slot);
  z.write(w);
}

bool Alignment3::read(io::ByteReader &r, Alignment3 &v, const attr::PoolReadLimits &)
{
  Alignment3 a;
  a.horizontal.pool = r.u32();
  a.horizontal.slot = r.u32();
  if (r.failed() || !attr::well_formed(a.horizontal) || a.horizontal.none() || !Fn1D::read(r, a.z)) {
    return false;
  }
  v = std::move(a);
  return true;
}

const KindOps &alignment3_kind()
{
  static const Alignment3Kind k;
  return k;
}

} // namespace ubertopo::curves
