/* Piecewise polynomials in station (fn1d.h). */

#include "ubertopo/curves/fn1d.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace ubertopo::curves {

namespace {

double factorial(int k)
{
  double f = 1;
  for (int i = 2; i <= k; i++) {
    f *= double(i);
  }
  return f;
}

} // namespace

const char *Fn1D::make(const util::Vector<double, 2> &breaks, const util::Vector<Poly, 1> &pieces, Fn1D &out)
{
  if (pieces.size() == 0 || breaks.size() != pieces.size() + 1) {
    return "Fn1D: need n pieces and n + 1 breakpoints";
  }
  if (uint32_t(pieces.size()) > kMaxPieces) {
    return "Fn1D: too many pieces";
  }
  Fn1D f;
  for (int i = 0; i < pieces.size(); i++) {
    const double a = breaks[i], b = breaks[i + 1];
    if (!std::isfinite(a) || !std::isfinite(b) || b < a) {
      return "Fn1D: breakpoints must be finite and increasing";
    }
    if (b == a) {
      continue; /* zero-length piece */
    }
    Poly p = pieces[i];
    for (double x : p.c) {
      if (!std::isfinite(x)) {
        return "Fn1D: non-finite coefficient";
      }
    }
    for (double &x : p.e) {
      x = 0;
    }
    p.canonicalize();
    if (p.degree() > kMaxDegree) {
      return "Fn1D: degree above kMaxDegree";
    }
    if (f.breaks.size() == 0) {
      f.breaks.append(a == 0 ? 0.0 : a);
    }
    f.breaks.append(b == 0 ? 0.0 : b);
    f.pieces.append(std::move(p));
  }
  if (f.pieces.size() == 0) {
    return "Fn1D: empty domain";
  }
  out = std::move(f);
  return nullptr;
}

Fn1D Fn1D::constant(double v, double s0, double s1)
{
  Fn1D f;
  util::Vector<double, 2> br;
  br.append(s0);
  br.append(s1);
  util::Vector<Poly, 1> ps;
  ps.append(Poly::exact({v}));
  make(br, ps, f);
  return f;
}

Fn1D Fn1D::linear(double v0, double v1, double s0, double s1)
{
  Fn1D f;
  util::Vector<double, 2> br;
  br.append(s0);
  br.append(s1);
  util::Vector<Poly, 1> ps;
  ps.append(Poly::exact({v0, (v1 - v0) / (s1 - s0)}));
  make(br, ps, f);
  return f;
}

bool Fn1D::is_zero() const
{
  for (const Poly &p : pieces) {
    if (p.size() != 0) {
      return false;
    }
  }
  return true;
}

int Fn1D::piece_at(double s, Side side) const
{
  const int n = pieces.size();
  if (s <= breaks[0]) {
    return 0;
  }
  if (s >= breaks[n]) {
    return n - 1;
  }
  /* first break > s (Above) or >= s (Below) */
  int lo = 0, hi = n; /* piece index in [lo, hi) */
  while (hi - lo > 1) {
    const int mid = (lo + hi) / 2;
    const bool right = side == Side::Above ? s >= breaks[mid] : s > breaks[mid];
    if (right) {
      lo = mid;
    }
    else {
      hi = mid;
    }
  }
  return lo;
}

Poly Fn1D::piece_deriv(int i, int k) const
{
  Poly p = pieces[i];
  for (int j = 0; j < k; j++) {
    p = p.deriv();
  }
  return p;
}

double Fn1D::eval(double s, int k, Side side, double *err) const
{
  const int i = piece_at(s, side);
  const double sigma = std::clamp(s, breaks[i], breaks[i + 1]) - breaks[i];
  const Poly &p = pieces[i];
  /* k-th derivative: sum_j j!/(j-k)! c_j sigma^(j-k), Horner (k = 0 included,
   * so the bound also covers the rounding of sigma) */
  const int n = p.size();
  if (k >= n) {
    if (err) {
      *err = 0;
    }
    return 0;
  }
  double r = 0, mag = 0;
  const double as = std::fabs(sigma);
  for (int j = n - 1; j >= k; j--) {
    const double coef = p.c[j] * (factorial(j) / factorial(j - k));
    r = r * sigma + coef;
    mag = mag * as + std::fabs(coef);
  }
  if (err) {
    /* sigma = s - break rounds too: one more relative u on sigma, absorbed by
     * the gamma (n + k + 4) margin */
    *err = up(gamma_n(2 * n + k + 4) * mag);
  }
  return r;
}

Range Fn1D::range(double s0, double s1, int k) const
{
  const int i0 = piece_at(s0, Side::Above), i1 = piece_at(s1, Side::Below);
  Range out{INFINITY, -INFINITY};
  for (int i = i0; i <= i1; i++) {
    const double a = std::max(s0, breaks[i]) - breaks[i], b = std::min(s1, breaks[i + 1]) - breaks[i];
    /* the subtraction rounds: widen by one ulp of the local interval */
    const double pad = (std::fabs(a) + std::fabs(b)) * kUnit;
    const Range r = piece_deriv(i, k).range(std::max(0.0, a - pad), b + pad, 2);
    out.lo = std::min(out.lo, r.lo);
    out.hi = std::max(out.hi, r.hi);
  }
  return out;
}

const char *Fn1D::add(const Fn1D &a, const Fn1D &b, Fn1D &out)
{
  if (a.empty() || b.empty() || a.lo() != b.lo() || a.hi() != b.hi()) {
    return "Fn1D::add: domains differ";
  }
  util::Vector<double, 2> br;
  int i = 0, j = 0;
  while (i < a.breaks.size() || j < b.breaks.size()) {
    double x;
    if (j >= b.breaks.size() || (i < a.breaks.size() && a.breaks[i] < b.breaks[j])) {
      x = a.breaks[i++];
    }
    else if (i >= a.breaks.size() || b.breaks[j] < a.breaks[i]) {
      x = b.breaks[j++];
    }
    else {
      x = a.breaks[i++];
      j++;
    }
    br.append(x);
  }
  util::Vector<Poly, 1> ps;
  for (int k = 0; k + 1 < br.size(); k++) {
    const double s = br[k];
    const int ia = a.piece_at(s, Side::Above), ib = b.piece_at(s, Side::Above);
    Poly pa = a.pieces[ia].compose_affine(s - a.breaks[ia], 1.0);
    Poly pb = b.pieces[ib].compose_affine(s - b.breaks[ib], 1.0);
    ps.append(pa + pb);
  }
  return make(br, ps, out);
}

void Fn1D::hash_into(hash::Hasher128 &h) const
{
  h.u32(uint32_t(pieces.size()));
  for (double x : breaks) {
    h.f64_bits(x);
  }
  for (const Poly &p : pieces) {
    h.u32(uint32_t(p.size()));
    for (double x : p.c) {
      h.f64_bits(x);
    }
  }
}

bool Fn1D::equal(const Fn1D &b) const
{
  if (pieces.size() != b.pieces.size()) {
    return false;
  }
  for (int i = 0; i < breaks.size(); i++) {
    if (std::bit_cast<uint64_t>(breaks[i]) != std::bit_cast<uint64_t>(b.breaks[i])) {
      return false;
    }
  }
  for (int i = 0; i < pieces.size(); i++) {
    const Poly &p = pieces[i], &q = b.pieces[i];
    if (p.size() != q.size()) {
      return false;
    }
    for (int k = 0; k < p.size(); k++) {
      if (std::bit_cast<uint64_t>(p.c[k]) != std::bit_cast<uint64_t>(q.c[k])) {
        return false;
      }
    }
  }
  return true;
}

void Fn1D::write(io::ByteWriter &w) const
{
  w.u32(uint32_t(pieces.size()));
  for (double x : breaks) {
    w.u64(std::bit_cast<uint64_t>(x));
  }
  for (const Poly &p : pieces) {
    w.u32(uint32_t(p.size()));
    for (double x : p.c) {
      w.u64(std::bit_cast<uint64_t>(x));
    }
  }
}

bool Fn1D::read(io::ByteReader &r, Fn1D &out)
{
  const uint32_t n = r.u32();
  if (r.failed() || n == 0 || n > kMaxPieces || uint64_t(n + 1) * 8 > r.remaining()) {
    return false;
  }
  util::Vector<double, 2> br;
  for (uint32_t i = 0; i <= n; i++) {
    br.append(std::bit_cast<double>(r.u64()));
  }
  util::Vector<Poly, 1> ps;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t m = r.u32();
    if (r.failed() || m > uint32_t(kMaxDegree + 1) || uint64_t(m) * 8 > r.remaining()) {
      return false;
    }
    Poly p;
    for (uint32_t k = 0; k < m; k++) {
      p.c.append(std::bit_cast<double>(r.u64()));
      p.e.append(0);
    }
    ps.append(std::move(p));
  }
  if (r.failed()) {
    return false;
  }
  Fn1D f;
  if (make(br, ps, f) || f.breaks.size() != br.size()) {
    return false;
  }
  Fn1D raw;
  raw.breaks = br;
  raw.pieces = ps;
  if (!f.equal(raw)) {
    return false; /* not canonical */
  }
  out = std::move(f);
  return true;
}

} // namespace ubertopo::curves
