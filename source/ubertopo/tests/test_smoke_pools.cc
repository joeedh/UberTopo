/* Smoke test (P1b W7): UberTopo builds against litestl's value pools and can
 * follow the owner contract P4's Complex will use (design 07 "Pool owner
 * contract"): a compile-time schema with a Column<PoolRef> builtin, a
 * PoolStore held by value (defaulted copy and move), diverging snapshots,
 * collect at operation boundaries, canonical owner write, strict owner read,
 * import between owners, and a kind check through Roots::accept. */

#include "litestl/attr/pool_store.h"
#include "litestl/attr/schema.h"

#include "ut_test.h"

#include <cstring>
#include <vector>

using namespace litestl;
using namespace litestl::attr;

namespace {

/* A curve-like entry: points plus an optional source (approximation cache). */
struct Curve {
  util::Vector<math::double3> pts;
  PoolRef source;
  static constexpr bool has_refs = true;

  void hash_into(hash::Hasher128 &h) const
  {
    h.u32(uint32_t(pts.size()));
    for (const auto &p : pts) {
      h.raw_bytes(&p, sizeof(p));
    }
    h.u32(source.pool).u32(source.slot);
  }
  bool equal(const Curve &b) const
  {
    return pts.size() == b.pts.size() && source == b.source &&
           (pts.size() == 0 || memcmp(pts.data(), b.pts.data(), sizeof(math::double3) * size_t(pts.size())) == 0);
  }
  void write(io::ByteWriter &w) const
  {
    w.u32(uint32_t(pts.size()));
    if (pts.size()) {
      w.put(pts.data(), sizeof(math::double3) * size_t(pts.size()));
    }
    w.u32(source.pool);
    w.u32(source.slot);
  }
  static bool read(io::ByteReader &r, Curve &v, const PoolReadLimits &)
  {
    const uint32_t n = r.u32();
    if (r.failed() || uint64_t(n) * sizeof(math::double3) > r.remaining()) {
      return false;
    }
    v.pts.resize(int(n));
    if (n) {
      r.get(v.pts.data(), sizeof(math::double3) * n);
    }
    v.source.pool = r.u32();
    v.source.slot = r.u32();
    return !r.failed();
  }
  template <typename F> void for_each_ref(F &&f) const
  {
    f(source);
  }
  template <typename F> void remap_refs(F &&f)
  {
    source = f(source);
  }
};
using Curves = InternPool<Curve>;
using Materials = InternPool<util::string>;

using EdgeCurve = AttrDef<".e.curve", Column<PoolRef>>;
using EdgeMaterial = AttrDef<".e.material", Column<PoolRef>>;
using EdgeSchema = Schema<EdgeCurve, EdgeMaterial>;

/* The owner contract: store by value, kinds registered in a fixed order. */
struct Owner {
  PoolStore store;
  TypedGroup<EdgeSchema> edges;
  uint32_t curves = 0, materials = 0;

  Owner()
  {
    curves = store.add_pool<Curves>("ut.curve.polyline");
    materials = store.add_pool<Materials>("ut.material");
  }

  const AttrGroup *g[1];
  AttrGroup *mg[1];
  PoolStore::Roots roots(const PoolStore::AcceptFn *accept = nullptr)
  {
    g[0] = &edges.group();
    return PoolStore::Roots{util::span<const AttrGroup *const>(g, 1), {}, accept};
  }
  PoolStore::MutRoots mroots()
  {
    mg[0] = &edges.group();
    return PoolStore::MutRoots{util::span<AttrGroup *const>(mg, 1), {}, nullptr};
  }

  PoolRef curve(std::initializer_list<double> xs, PoolRef src = kNoRef)
  {
    Curve c;
    for (double x : xs) {
      c.pts.append(math::double3(x, 0, 0));
    }
    c.source = src;
    return store.intern<Curves>(curves, std::move(c));
  }

  bool write(io::ByteWriter &w) const
  {
    Owner c(*this);
    return c.store.canonicalize(c.mroots()) && c.store.write(w, c.roots()) && (c.edges.group().write(w), true);
  }
  static bool read(io::ByteReader &r, Owner &out)
  {
    Owner f;
    if (!f.store.read(r) || !f.edges.group().read(r) || !f.store.audit(f.roots(), true)) {
      return false;
    }
    bool changed = true;
    if (!f.store.canonicalize(f.mroots(), &changed) || changed) {
      return false;
    }
    out = std::move(f);
    return true;
  }
};

std::vector<uint8_t> bytes_of(const Owner &o)
{
  io::ByteWriter w;
  UT_CHECK(o.write(w));
  return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

} // namespace

int main()
{
  const size_t baseline = alloc::getMemorySize();
  {
    Owner o;
    o.edges.resize(6);
    const PoolRef base = o.curve({0, 1, 2});
    o.edges.get<".e.curve">().set(0, o.curve({0, 1}, base)); /* cache referencing its source */
    o.edges.get<".e.curve">().set(1, base);
    o.edges.get<".e.material">().fill(0, 6, o.store.intern<Materials>(o.materials, util::string("brick")));
    o.curve({99}); /* garbage */
    UT_CHECK(o.store.collect(o.roots()));
    UT_CHECK(o.store.live_count() == 3);

    /* defaulted copy is a snapshot; snapshots diverge */
    Owner s = o;
    s.edges.get<".e.curve">().set(1, s.curve({7}));
    UT_CHECK(s.store.collect(s.roots()));
    UT_CHECK(o.store.get<Curves>(o.edges.get<".e.curve">()[1]).pts.size() == 3);
    UT_CHECK(s.store.get<Curves>(s.edges.get<".e.curve">()[1]).pts[0][0] == 7);
    UT_CHECK(o.store.audit(o.roots(), true) && s.store.audit(s.roots(), true));

    /* kind check: curves only on .e.curve */
    auto curve_only = [&](const RefSite &site, PoolRef r) {
      return !(site.kind == RefSite::Column && site.b == TypedGroup<EdgeSchema>::slot<".e.curve">() &&
               r.pool != o.curves);
    };
    const PoolStore::AcceptFn acc = curve_only;
    UT_CHECK(o.store.audit(o.roots(&acc), true));
    o.edges.get<".e.curve">().set(5, o.edges.get<".e.material">()[0]);
    UT_CHECK(!o.store.audit(o.roots(&acc), true));
    o.edges.get<".e.curve">().set(5, kNoRef);

    /* canonical owner write; strict read; identical re-write */
    const auto b = bytes_of(o);
    Owner back;
    io::ByteReader r(b.data(), b.size());
    UT_CHECK(Owner::read(r, back));
    UT_CHECK(bytes_of(back) == b);

    /* moved owners keep working (nothing points into the store) */
    Owner moved = std::move(back);
    UT_CHECK(moved.store.get<Curves>(moved.store.get<Curves>(moved.edges.get<".e.curve">()[0]).source).pts.size() == 3);

    /* import an edge's curve (with its source) into an unrelated owner */
    Owner other;
    other.edges.resize(1);
    PoolStore::ImportMemo memo;
    const PoolRef imp = other.store.import(o.store, o.edges.get<".e.curve">()[0], memo);
    other.edges.get<".e.curve">().set(0, imp);
    UT_CHECK(other.store.collect(other.roots()) && other.store.live_count() == 2);
  }
  UT_CHECK(alloc::getMemorySize() == baseline);
  return ut_test_end();
}
