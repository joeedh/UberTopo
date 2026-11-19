/* road3 (road.h). */

#include "ubertopo/curves/road.h"

#include "ubertopo/curves/cesaro.h"

namespace ubertopo::curves {

namespace {

/* the structural rules shared by make and valid: sizes, one common domain */
const char *check_shape(const util::Vector<Fn1D, 2> &borders, const util::Vector<Fn1D, 2> &heights)
{
  if (borders.size() < 2 || borders.size() > Road3::kMaxBorders || heights.size() != borders.size()) {
    return "road3: need 2..kMaxBorders borders and as many heights";
  }
  const double lo = borders[0].lo(), hi = borders[0].hi();
  for (int i = 0; i < borders.size(); i++) {
    for (const Fn1D *f : {&borders[i], &heights[i]}) {
      if (f->empty() || f->lo() != lo || f->hi() != hi) {
        return "road3: borders and heights must share one domain";
      }
    }
  }
  return nullptr;
}

/* the reference of a well-formed entry, else null */
const Cesaro2 *resolve(const attr::PoolStore &s, const Road3 &d)
{
  if (kind_of(d.reference) != &cesaro2_kind()) {
    return nullptr;
  }
  const Cesaro2 *c = s.try_get<Cesaro2Pool>(d.reference);
  if (!c || check_shape(d.borders, d.heights) || !(d.borders[0].lo() >= 0) ||
      !(d.borders[0].hi() <= c->length()))
  {
    return nullptr;
  }
  return c;
}

class Road3Kind final : public SurfaceKindOps {
public:
  const char *key() const override
  {
    return "curves.road3";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<Road3Pool>(key());
  }
  bool valid(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Road3 *d = s.try_get<Road3Pool>(r);
    return d && resolve(s, *d);
  }
  util::UniquePtr<SurfaceEval> make_surface(const attr::PoolStore &, attr::PoolRef) const override
  {
    return {}; /* CLAUDENOTE: the evaluator lands in W3 */
  }
};

void hash_fns(hash::Hasher128 &h, const util::Vector<Fn1D, 2> &v)
{
  h.u32(uint32_t(v.size()));
  for (const Fn1D &f : v) {
    f.hash_into(h);
  }
}

} // namespace

const char *Road3::make(const attr::PoolStore &s,
                        attr::PoolRef reference,
                        const util::Vector<Fn1D, 2> &borders,
                        const util::Vector<Fn1D, 2> &heights,
                        Road3 &out)
{
  if (const char *e = check_shape(borders, heights)) {
    return e;
  }
  Road3 d;
  d.reference = reference;
  d.borders = borders;
  d.heights = heights;
  if (kind_of(reference) != &cesaro2_kind() || !s.try_get<Cesaro2Pool>(reference)) {
    return "road3: the reference must be a live cesaro2";
  }
  if (!resolve(s, d)) {
    return "road3: the domain must lie inside the reference's";
  }
  out = std::move(d);
  return nullptr;
}

void Road3::hash_into(hash::Hasher128 &h) const
{
  h.u32(reference.pool).u32(reference.slot);
  hash_fns(h, borders);
  hash_fns(h, heights);
}

bool Road3::equal(const Road3 &b) const
{
  if (!(reference == b.reference) || borders.size() != b.borders.size() || heights.size() != b.heights.size()) {
    return false;
  }
  for (int i = 0; i < borders.size(); i++) {
    if (!borders[i].equal(b.borders[i])) {
      return false;
    }
  }
  for (int i = 0; i < heights.size(); i++) {
    if (!heights[i].equal(b.heights[i])) {
      return false;
    }
  }
  return true;
}

void Road3::write(io::ByteWriter &w) const
{
  w.u32(reference.pool);
  w.u32(reference.slot);
  w.u32(uint32_t(borders.size()));
  for (const Fn1D &f : borders) {
    f.write(w);
  }
  for (const Fn1D &f : heights) {
    f.write(w);
  }
}

bool Road3::read(io::ByteReader &r, Road3 &v, const attr::PoolReadLimits &)
{
  Road3 d;
  d.reference.pool = r.u32();
  d.reference.slot = r.u32();
  const uint32_t n = r.u32();
  if (r.failed() || !attr::well_formed(d.reference) || d.reference.none() || n < 2 ||
      n > uint32_t(kMaxBorders))
  {
    return false;
  }
  for (int pass = 0; pass < 2; pass++) {
    for (uint32_t i = 0; i < n; i++) {
      Fn1D f;
      if (!Fn1D::read(r, f)) {
        return false;
      }
      (pass == 0 ? d.borders : d.heights).append(std::move(f));
    }
  }
  if (check_shape(d.borders, d.heights)) {
    return false;
  }
  v = std::move(d);
  return true;
}

const KindOps &road3_kind()
{
  static const Road3Kind k;
  return k;
}

} // namespace ubertopo::curves
