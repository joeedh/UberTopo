#include "ubertopo/topo/regions.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "litestl/math/wideint.h"
#include "litestl/util/map.h"
#include "litestl/util/sort.h"
#include "litestl/util/task.h"
#include "ubertopo/geom/predicates.h"
#include "ubertopo/topo/hint_votes.h"
#include "ubertopo/topo/radial.h"

namespace ubertopo::topo {

namespace util = litestl::util;
namespace task = litestl::task;
using ident::PersistentId;

const char *region_code_name(RegionCode c)
{
  switch (c) {
    case RegionCode::DegenerateShell:
      return "DegenerateShell";
    case RegionCode::NestingDegenerate:
      return "NestingDegenerate";
    case RegionCode::TouchingShell:
      return "TouchingShell";
    case RegionCode::NoTestPoint:
      return "NoTestPoint";
    case RegionCode::UnresolvedContainerHint:
      return "UnresolvedContainerHint";
    case RegionCode::AmbiguousContainerHint:
      return "AmbiguousContainerHint";
    case RegionCode::HintConflict:
      return "HintConflict";
    case RegionCode::DuplicateHint:
      return "DuplicateHint";
  }
  return "?";
}

bool region_code_is_error(RegionCode c)
{
  return c != RegionCode::HintConflict && c != RegionCode::DuplicateHint;
}

namespace {

struct Dsu {
  util::Vector<uint32_t> p;
  explicit Dsu(uint32_t n)
  {
    p.resize(n);
    for (uint32_t i = 0; i < n; i++) {
      p[i] = i;
    }
  }
  uint32_t find(uint32_t x)
  {
    while (p[x] != x) {
      p[x] = p[p[x]];
      x = p[x];
    }
    return x;
  }
  void unite(uint32_t a, uint32_t b)
  {
    a = find(a);
    b = find(b);
    if (a != b) {
      (a < b ? p[b] : p[a]) = a < b ? a : b;
    }
  }
};

/* Exact (Lattice) or filtered (floating) signed volume accumulator. */
template <geom::CoordType C> struct Volume {
  litestl::math::Int128 exact = litestl::math::Int128::from(0);
  double approx = 0.0, abs_sum = 0.0, perm_sum = 0.0;
  uint64_t terms = 0;

  void add(const geom::Point<C> &a, const geom::Point<C> &b, const geom::Point<C> &c, int sign)
  {
    if constexpr (geom::ExactCoord<C>) {
      /* a . (b x c): |b x c| <= 2^53, products <= 2^79, sum < 2^81 */
      const int64_t bx = b[0].raw, by = b[1].raw, bz = b[2].raw, cx = c[0].raw, cy = c[1].raw, cz = c[2].raw;
      const int64_t x = by * cz - bz * cy, y = bz * cx - bx * cz, z = bx * cy - by * cx;
      litestl::math::Int128 d = litestl::math::mul_wide(a[0].raw, x) + litestl::math::mul_wide(a[1].raw, y) +
                                litestl::math::mul_wide(a[2].raw, z);
      exact = sign > 0 ? exact + d : exact - d;
    }
    else {
      const auto A = geom::to_double3<C>(a), B = geom::to_double3<C>(b), Cc = geom::to_double3<C>(c);
      const double x = B[1] * Cc[2] - B[2] * Cc[1], y = B[2] * Cc[0] - B[0] * Cc[2], z = B[0] * Cc[1] - B[1] * Cc[0];
      const double d = A[0] * x + A[1] * y + A[2] * z;
      approx += sign > 0 ? d : -d;
      abs_sum += std::fabs(d);
      perm_sum += std::fabs(A[0]) * (std::fabs(B[1] * Cc[2]) + std::fabs(B[2] * Cc[1])) +
                  std::fabs(A[1]) * (std::fabs(B[2] * Cc[0]) + std::fabs(B[0] * Cc[2])) +
                  std::fabs(A[2]) * (std::fabs(B[0] * Cc[1]) + std::fabs(B[1] * Cc[0]));
    }
    terms++;
  }
  /** +1 / -1, or 0 if the sign can't be certified (or the volume is 0). */
  int sign() const
  {
    if constexpr (geom::ExactCoord<C>) {
      return exact.sign();
    }
    else {
      const double u = std::ldexp(1.0, -53);
      const double bound = 4.0 * u * perm_sum + 2.0 * double(terms) * u * abs_sum + 1e-300;
      return approx > bound ? 1 : (approx < -bound ? -1 : 0);
    }
  }
  /** Magnitude for ordering nested shells (exact comparisons where possible). */
  bool smaller_than(const Volume &o) const
  {
    if constexpr (geom::ExactCoord<C>) {
      const litestl::math::Int128 a = exact.sign() < 0 ? -exact : exact, b = o.exact.sign() < 0 ? -o.exact : o.exact;
      return a < b;
    }
    else {
      return std::fabs(approx) < std::fabs(o.approx);
    }
  }
};

struct BBox {
  double lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
  void add(const litestl::math::double3 &p)
  {
    for (int i = 0; i < 3; i++) {
      lo[i] = std::min(lo[i], p[i]);
      hi[i] = std::max(hi[i], p[i]);
    }
  }
  bool contains(const BBox &o) const
  {
    for (int i = 0; i < 3; i++) {
      if (o.lo[i] < lo[i] || o.hi[i] > hi[i]) {
        return false;
      }
    }
    return true;
  }
};

} // namespace

template <geom::CoordType C> bool build_regions(Complex<C> &k, RegionReport &report)
{
  using Point = geom::Point<C>;
  const uint32_t F = k.faces.capacity();
  const uint32_t NS2 = 2 * F;

  /* ---- face triangles in front winding (planar: loop fans; projected: T(f)) ---- */
  util::Vector<util::Vector<Point>> ftri;
  ftri.resize(F);
  util::Vector<uint8_t> ftri_ok;
  ftri_ok.resize(F);
  /* the refined skeleton (plan P4b §4.3), flattened up front: the parallel
   * loop below only reads it */
  Skeleton<C> sk(k);
  sk.warm();
  task::parallel_for_chunks(util::IndexRange(0, int(F)), 256, [&](int, util::IndexRange r) {
    for (int f : r) {
      ftri_ok[f] = 1;
      if (!k.faces.live(uint32_t(f))) {
        continue;
      }
      const FaceRef fr{uint32_t(f)};
      if (k.face_proj_axis(fr) >= 0) {
        FacePoints<C> fp;
        util::Vector<geom::Triangle> tris;
        if (!fp.gather(k, fr, sk) || geom::cdt_proj<C>(fp.view(), k.face_proj_axis(fr), tris) != geom::CdtStatus::Ok) {
          ftri_ok[f] = 0;
          continue;
        }
        for (const geom::Triangle &t : tris) {
          ftri[f].append(fp.pts[t[0]]);
          ftri[f].append(fp.pts[t[1]]);
          ftri[f].append(fp.pts[t[2]]);
        }
      }
      else {
        FacePoints<C> fp;
        fp.gather(k, fr, sk);
        const auto v = fp.view();
        for (uint32_t l = 0; l < v.loop_count(); l++) {
          const auto lp = v.loop(l);
          for (size_t i = 1; i + 1 < lp.size(); i++) {
            ftri[f].append(lp[0]);
            ftri[f].append(lp[i]);
            ftri[f].append(lp[i + 1]);
          }
        }
      }
    }
  });

  /* ---- 1. shells: union-find over wedges ---- */
  Dsu dsu(NS2);
  for (uint32_t c = 0; c < k.coedges.capacity(); c++) {
    if (!k.coedges.live(c)) {
      continue;
    }
    const CoedgeRef cr(c), cn = k.radial_next(cr);
    const uint32_t a = face_side_index(k.face_of(cr), k.side_toward_next(cr));
    const uint32_t b = face_side_index(k.face_of(cn), 1 - k.side_toward_next(cn));
    dsu.unite(a, b);
  }
  util::Vector<PersistentId> side_id;
  side_id.resize(NS2);
  task::parallel_for_chunks(util::IndexRange(0, int(F)), 2048, [&](int, util::IndexRange r) {
    for (int f : r) {
      if (k.faces.live(uint32_t(f))) {
        side_id[2 * f] = ident::face_side_id(k.face_id(FaceRef(uint32_t(f))), 0);
        side_id[2 * f + 1] = ident::face_side_id(k.face_id(FaceRef(uint32_t(f))), 1);
      }
    }
  });
  /* class label: smallest side ID per DSU root; shells indexed in label
   * order (side IDs are unique, so the order is total) */
  util::Vector<uint32_t> root;
  root.resize(NS2);
  util::Vector<PersistentId> label;
  label.resize(NS2);
  util::Vector<uint8_t> is_root;
  is_root.resize(NS2);
  for (uint32_t s = 0; s < NS2; s++) {
    is_root[s] = 0;
  }
  for (uint32_t s = 0; s < NS2; s++) {
    if (!k.faces.live(s / 2)) {
      root[s] = NONE;
      continue;
    }
    const uint32_t r = root[s] = dsu.find(s);
    if (!is_root[r] || side_id[s] < label[r]) {
      label[r] = side_id[s];
      is_root[r] = 1;
    }
  }
  util::Vector<std::pair<PersistentId, uint32_t>> labels;
  for (uint32_t r = 0; r < NS2; r++) {
    if (is_root[r]) {
      labels.append({label[r], r});
    }
  }
  std::sort(labels.begin(), labels.end());
  util::Vector<uint32_t> root_shell; /* by root side; set for roots only */
  root_shell.resize(NS2);
  for (uint32_t i = 0; i < labels.size(); i++) {
    root_shell[labels[i].second] = i;
  }
  const uint32_t S = uint32_t(labels.size());
  util::Vector<uint32_t> side_shell;
  side_shell.resize(NS2);
  for (uint32_t s = 0; s < NS2; s++) {
    side_shell[s] = root[s] == NONE ? NONE : root_shell[root[s]];
  }
  /* each shell's sides in ascending side order: the shell chains, the test
   * points, the volume sums and the nesting rays depend on it */
  util::KeyGroups shell_sides;
  util::group_by_key(
      NS2, S, [&](uint32_t s) { return side_shell[s] == NONE ? util::kNoGroup : side_shell[s]; }, shell_sides);

  /* ---- 2. shell kind, volume, bounding box, vertex set ---- */
  enum Kind : uint8_t { Sheet, Outer, Cavity, Degenerate };
  util::Vector<uint8_t> kind;
  kind.resize(S);
  util::Vector<Volume<C>> vol;
  vol.resize(S);
  util::Vector<BBox> box;
  box.resize(S);
  /* vertex sets, flat: shell sh's (with repeats) are allotted
   * vbuf[voff[sh] .. voff[sh + 1]), and the sorted distinct ones end up in
   * the first vlen[sh] */
  util::Vector<uint32_t> fcount;
  fcount.resize(F);
  task::parallel_for_chunks(util::IndexRange(0, int(F)), 2048, [&](int, util::IndexRange r) {
    for (int f : r) {
      uint32_t n = 0;
      if (k.faces.live(uint32_t(f))) {
        k.for_each_loop(FaceRef(uint32_t(f)), [&](LoopRef l) { n += k.loops.template get<".l.size">()[l.i]; });
      }
      fcount[f] = n;
    }
  });
  util::Vector<uint32_t> voff, vlen, vbuf;
  voff.resize(size_t(S) + 1);
  vlen.resize(S);
  voff[0] = 0;
  for (uint32_t sh = 0; sh < S; sh++) {
    uint32_t n = 0;
    for (uint32_t s : shell_sides[sh]) {
      n += fcount[s / 2];
    }
    voff[sh + 1] = voff[sh] + n;
  }
  vbuf.resize(voff[S]);
  auto shell_verts = [&](uint32_t sh) { return std::span<const uint32_t>(vbuf.data() + voff[sh], vlen[sh]); };
  task::parallel_for_chunks(util::IndexRange(0, int(S)), 64, [&](int, util::IndexRange r) {
    for (int sh : r) {
      bool sheet = true;
      for (uint32_t s : shell_sides[sh]) {
        sheet = sheet && side_shell[s ^ 1u] == uint32_t(sh);
      }
      bool tri_ok = true;
      uint32_t *sv = vbuf.data() + voff[sh];
      uint32_t nv = 0;
      for (uint32_t s : shell_sides[sh]) {
        const uint32_t f = s / 2;
        tri_ok = tri_ok && ftri_ok[f];
        const auto &t = ftri[f];
        for (uint32_t i = 0; i + 2 < t.size(); i += 3) {
          /* outward from the side's region: + for BACK sides, - for FRONT */
          vol[sh].add(t[i], t[i + 1], t[i + 2], (s & 1u) ? 1 : -1);
        }
        for (const Point &x : t) {
          box[sh].add(geom::to_double3<C>(x)); /* covers curved edges' skeletons */
        }
        k.for_each_loop(FaceRef(f), [&](LoopRef l) {
          k.for_each_coedge(l, [&](CoedgeRef c) {
            const VertRef v = k.start_vert(c);
            sv[nv++] = v.i;
            box[sh].add(geom::to_double3<C>(k.pos(v)));
          });
        });
      }
      std::sort(sv, sv + nv);
      uint32_t w = 0;
      for (uint32_t i = 0; i < nv; i++) {
        if (i == 0 || sv[i] != sv[i - 1]) {
          sv[w++] = sv[i];
        }
      }
      vlen[sh] = w;
      if (!tri_ok) {
        kind[sh] = Degenerate;
      }
      else if (sheet) {
        kind[sh] = Sheet;
      }
      else {
        const int sg = vol[sh].sign();
        kind[sh] = sg > 0 ? Outer : (sg < 0 ? Cavity : Degenerate);
      }
    }
  });
  report.shells = S;
  for (uint32_t sh = 0; sh < S; sh++) {
    if (kind[sh] == Degenerate) {
      report.add(RegionCode::DegenerateShell, sh);
    }
    report.sheet_shells += kind[sh] == Sheet;
    report.outer_shells += kind[sh] == Outer;
    report.cavity_shells += kind[sh] == Cavity;
  }
  if (!report.ok()) {
    return false;
  }

  /* ---- 3. nesting: innermost containing outer shell of cavity and sheet shells ---- */
  util::Vector<uint32_t> outers;
  for (uint32_t sh = 0; sh < S; sh++) {
    if (kind[sh] == Outer) {
      outers.append(sh);
    }
  }
  util::Vector<uint32_t> parent; /* containing outer shell, or NONE (region 0) */
  parent.resize(S);
  util::Vector<int32_t> nest_err;
  nest_err.resize(S);
  util::Vector<uint32_t> tests, retries;
  tests.resize(S);
  retries.resize(S);
  task::parallel_for_chunks(util::IndexRange(0, int(S)), 16, [&](int, util::IndexRange r) {
    util::Vector<Point> btris;
    for (int a : r) {
      parent[a] = NONE;
      nest_err[a] = -1;
      tests[a] = retries[a] = 0;
      if (kind[a] == Outer) {
        continue;
      }
      for (uint32_t b : outers) {
        if (!box[b].contains(box[a])) {
          continue;
        }
        /* (a) sharing a face on opposite sides: not inside */
        bool shared = false;
        for (uint32_t s : shell_sides[a]) {
          shared = shared || side_shell[s ^ 1u] == b;
        }
        if (shared) {
          continue;
        }
        /* (b) a test point of A off B */
        Point p;
        int scale = -1;
        const auto vb = shell_verts(b);
        for (uint32_t v : shell_verts(a)) {
          if (!std::binary_search(vb.begin(), vb.end(), v)) {
            p = k.pos(VertRef(v));
            scale = 0;
            break;
          }
        }
        for (uint32_t si = 0; scale < 0 && si < shell_sides[a].size(); si++) {
          const uint32_t f = shell_sides[a][si] / 2;
          if (side_shell[2 * f] == b || side_shell[2 * f + 1] == b) {
            continue;
          }
          const auto &t = ftri[f];
          for (uint32_t i = 0; scale < 0 && i + 2 < t.size(); i += 3) {
            if (geom::orient2d_proj<C>(t[i], t[i + 1], t[i + 2], 0) == 0 &&
                geom::orient2d_proj<C>(t[i], t[i + 1], t[i + 2], 1) == 0 &&
                geom::orient2d_proj<C>(t[i], t[i + 1], t[i + 2], 2) == 0)
            {
              continue; /* degenerate fan triangle */
            }
            if (geom::interior_point4<C>(t[i], t[i + 1], t[i + 2], p)) {
              scale = 2;
            }
          }
        }
        if (scale < 0) {
          nest_err[a] = int32_t(RegionCode::NoTestPoint);
          break;
        }
        /* B's triangles, outward from its enclosed volume */
        btris.clear();
        for (uint32_t s : shell_sides[b]) {
          const auto &t = ftri[s / 2];
          for (uint32_t i = 0; i + 2 < t.size(); i += 3) {
            btris.append(t[i]);
            if (s & 1u) {
              btris.append(t[i + 1]);
              btris.append(t[i + 2]);
            }
            else {
              btris.append(t[i + 2]);
              btris.append(t[i + 1]);
            }
          }
        }
        const geom::RayResult rr =
            geom::ray_winding<C>(p, std::span<const Point>(btris.data(), btris.size()), scale);
        tests[a]++;
        retries[a] += uint32_t(rr.retries);
        if (rr.status == geom::RayStatus::Boundary) {
          nest_err[a] = int32_t(RegionCode::TouchingShell);
          break;
        }
        if (rr.status != geom::RayStatus::Ok) {
          nest_err[a] = int32_t(RegionCode::NestingDegenerate);
          break;
        }
        if (rr.winding != 0 && (parent[a] == NONE || vol[b].smaller_than(vol[parent[a]]))) {
          parent[a] = b;
        }
      }
    }
  });
  for (uint32_t sh = 0; sh < S; sh++) {
    report.ray_tests += tests[sh];
    report.ray_retries += retries[sh];
    if (nest_err[sh] >= 0) {
      report.add(RegionCode(nest_err[sh]), sh);
    }
  }
  if (!report.ok()) {
    return false;
  }

  /* ---- 4/5. regions and their IDs ---- */
  /* provisional regions: 0 = unbounded, then one per outer shell in shell order */
  util::Vector<uint32_t> shell_preg;
  shell_preg.resize(S);
  util::Vector<uint32_t> outer_preg;
  outer_preg.resize(S);
  uint32_t PR = 1;
  for (uint32_t sh = 0; sh < S; sh++) {
    if (kind[sh] == Outer) {
      outer_preg[sh] = PR++;
    }
  }
  for (uint32_t sh = 0; sh < S; sh++) {
    shell_preg[sh] = kind[sh] == Outer ? outer_preg[sh] : (parent[sh] == NONE ? 0 : outer_preg[parent[sh]]);
  }
  const PersistentId unb = ident::unbounded_region_id();
  util::Vector<PersistentId> preg_id;
  preg_id.resize(PR);
  util::Vector<PersistentId> preg_hint;
  preg_hint.resize(PR);
  util::Vector<PersistentId> preg_min;
  preg_min.resize(PR);
  /* Per hint, over regions r > 0: how many win it, the final index of the
   * winner (hint_region, when exactly one wins), and the minority carrier
   * (P3b C4): the provisional region carrying it without winning it, NONE
   * if two do, absent if none. */
  struct HintInfo {
    uint32_t winners = 0, seen = 0, win = NONE, carrier = NONE;
    bool has_carrier = false;
  };
  util::Map<PersistentId, HintInfo> info;
  {
    util::KeyGroups preg_shells;
    util::group_by_key(S, PR, [&](uint32_t sh) { return shell_preg[sh]; }, preg_shells);
    const auto &hints = k.faces.sides().template get<".fs.hint">();
    HintVotes votes; /* unb votes like any hint */
    for (uint32_t r = 0; r < PR; r++) {
      votes.clear();
      PersistentId min_side{};
      bool have_min = false;
      for (uint32_t sh : preg_shells[r]) {
        for (uint32_t s : shell_sides[sh]) {
          if (!have_min || side_id[s] < min_side) {
            min_side = side_id[s];
            have_min = true;
          }
          const PersistentId h = hints[s];
          if (!(h == ident::kNoId)) {
            votes.add(h);
          }
        }
      }
      const PersistentId hint = votes.winner();
      const size_t nhints = votes.votes().size();
      if (nhints > 1 || (r == 0 && nhints > 0 && !(hint == unb)) || (r != 0 && hint == unb)) {
        report.add(RegionCode::HintConflict, r);
      }
      preg_hint[r] = (r == 0 || hint == unb) ? PersistentId{} : hint;
      preg_min[r] = min_side;
      if (r != 0) {
        for (const HintVotes::Vote &v : votes.votes()) {
          if (v.h == hint || v.h == unb) {
            continue;
          }
          HintInfo &hi = info[v.h];
          if (!hi.has_carrier) {
            hi.has_carrier = true;
            hi.carrier = r;
          }
          else {
            hi.carrier = NONE; /* a second region (a region's hints are distinct) */
          }
        }
      }
    }
    /* IDs (DEC-017): a winning hint no other region wins names the region on
     * its own; shared winners keep the minimal side to stay unique */
    for (uint32_t r = 1; r < PR; r++) {
      if (!(preg_hint[r] == ident::kNoId)) {
        info[preg_hint[r]].winners++;
      }
    }
    for (uint32_t r = 0; r < PR; r++) {
      if (r == 0) {
        preg_id[r] = unb;
      }
      else if (preg_hint[r] == ident::kNoId) {
        preg_id[r] = ident::region_id_from_sides(preg_min[r]);
      }
      else if (info.lookup(preg_hint[r]).winners == 1) {
        preg_id[r] = ident::region_id_from_hint(preg_hint[r]);
      }
      else {
        preg_id[r] = ident::region_id(preg_hint[r], preg_min[r]);
      }
    }
  }
  /* final order: region 0 first, then by ID (unique barring a hash
   * collision) */
  util::Vector<uint32_t> rorder;
  for (uint32_t r = 1; r < PR; r++) {
    rorder.append(r);
  }
  {
    const bool unique = util::sort_unique_keys(util::span<uint32_t>(rorder.data(), rorder.size()),
                                               [&](uint32_t a, uint32_t b) { return preg_id[a] < preg_id[b]; });
    assert(unique);
    (void)unique;
  }
  util::Vector<uint32_t> preg_final;
  preg_final.resize(PR);
  preg_final[0] = 0;
  for (uint32_t i = 0; i < rorder.size(); i++) {
    preg_final[rorder[i]] = i + 1;
  }
  const uint32_t R = PR;
  util::Vector<PersistentId> reg_id, reg_hint;
  reg_id.resize(R);
  reg_hint.resize(R);
  for (uint32_t r = 0; r < PR; r++) {
    reg_id[preg_final[r]] = preg_id[r];
    reg_hint[preg_final[r]] = preg_hint[r];
  }
  /* in final order: the second winner of a hint is reported (only it), and
   * a hint's sole winner is recorded */
  for (uint32_t r = 1; r < R; r++) {
    if (reg_hint[r] == ident::kNoId) {
      continue;
    }
    HintInfo &hi = info.lookup(reg_hint[r]);
    if (hi.seen++ == 1) {
      report.add(RegionCode::DuplicateHint, r);
    }
    hi.win = r;
  }

  /* ---- 7 (checks first). container hints ---- */
  /* unb, then the winning region (ambiguous if two win), then the minority
   * carrier (ambiguous if two carry it), else unresolved */
  auto resolve = [&](PersistentId h, uint32_t index, ident::ContainerRef &out) {
    if (h == unb) {
      return ident::ContainerRef::region(RegionRef(0), out);
    }
    const HintInfo *hi = info.lookup_ptr(h);
    if (hi == nullptr || hi->winners == 0) {
      if (hi == nullptr || !hi->has_carrier) {
        report.add(RegionCode::UnresolvedContainerHint, index);
        return false;
      }
      if (hi->carrier == NONE) {
        report.add(RegionCode::AmbiguousContainerHint, index);
        return false;
      }
      return ident::ContainerRef::region(RegionRef(preg_final[hi->carrier]), out);
    }
    if (hi->winners > 1) {
      report.add(RegionCode::AmbiguousContainerHint, index);
      return false;
    }
    return ident::ContainerRef::region(RegionRef(hi->win), out);
  };
  util::Vector<std::pair<uint32_t, ident::ContainerRef>> vcont, econt;
  {
    const auto &vh = k.verts.template get<".v.container_hint">();
    for (uint32_t v = 0; v < k.verts.capacity(); v++) {
      ident::ContainerRef cr;
      if (k.verts.live(v) && !(vh[v] == ident::kNoId) && resolve(vh[v], v, cr)) {
        vcont.append({v, cr});
      }
    }
    const auto &eh = k.edges.template get<".e.container_hint">();
    for (uint32_t e = 0; e < k.edges.capacity(); e++) {
      ident::ContainerRef cr;
      if (k.edges.live(e) && !(eh[e] == ident::kNoId) && resolve(eh[e], e, cr)) {
        econt.append({e, cr});
      }
    }
  }
  if (!report.ok()) {
    return false;
  }
  report.regions = R;

  /* ---- writes ---- */
  util::Vector<uint32_t> shell_region;
  shell_region.resize(S);
  for (uint32_t sh = 0; sh < S; sh++) {
    shell_region[sh] = preg_final[shell_preg[sh]];
  }
  /* faces: region and shell of each side; face-side shell chains */
  {
    auto &freg = k.faces.template get<".f.region">();
    auto &fsh = k.faces.template get<".f.shell">();
    auto &sn = k.faces.sides().template get<".fs.shell_next">();
    for (uint32_t f = 0; f < F; f++) {
      if (!k.faces.live(f)) {
        continue;
      }
      const uint32_t s0 = side_shell[2 * f], s1 = side_shell[2 * f + 1];
      freg.set(f, U32x2{shell_region[s0], shell_region[s1]});
      fsh.set(f, U32x2{s0, s1});
    }
    for (uint32_t sh = 0; sh < S; sh++) {
      const auto &ss = shell_sides[sh];
      for (uint32_t i = 0; i < ss.size(); i++) {
        sn.set(ss[i], i + 1 < ss.size() ? ss[i + 1] : NONE);
      }
    }
  }
  /* shells (generations: plan P3b §3.1; a shell is the same one if its
   * first side is) */
  const Domain<ShellSchema> old_shells = k.shells;
  k.shells = Domain<ShellSchema>();
  k.shells.alloc_n(S);
  util::Vector<uint32_t> region_first;
  region_first.resize(R);
  for (uint32_t r = 0; r < R; r++) {
    region_first[r] = NONE;
  }
  {
    util::Vector<uint32_t> last;
    last.resize(R);
    for (uint32_t r = 0; r < R; r++) {
      last[r] = NONE;
    }
    for (uint32_t sh = 0; sh < S; sh++) {
      const uint32_t r = shell_region[sh];
      k.shells.template get<".s.region">().set(sh, r);
      k.shells.template get<".s.first">().set(sh, shell_sides[sh].size() ? shell_sides[sh][0] : NONE);
      if (last[r] == NONE) {
        region_first[r] = sh;
      }
      else {
        k.shells.template get<".s.next">().set(last[r], sh);
      }
      last[r] = sh;
    }
  }
  k.shells.replace_generations(old_shells, [&](uint32_t sh) {
    return old_shells.live(sh) &&
           old_shells.template get<".s.first">()[sh] == k.shells.template get<".s.first">()[sh];
  });
  /* regions, carrying non-Topo columns over by persistent ID */
  {
    const Domain<RegionSchema> old = k.regions;
    k.regions = Domain<RegionSchema>();
    k.regions.alloc_n(R);
    for (uint32_t r = 0; r < R; r++) {
      k.regions.template get<".id">().set(r, reg_id[r]);
      k.regions.template get<".r.shell">().set(r, region_first[r]);
    }
    k.regions.template get<".r.flags">().set(0, region_flags::Unbounded);
    k.regions.replace_generations(
        old, [&](uint32_t r) { return old.live(r) && old.template get<".id">()[r] == reg_id[r]; });
    /* only custom columns are carried: without any, nothing to match. A
     * duplicate old ID keeps its last row (the old domain's order isn't
     * checked, so no merge by ID order) */
    const bool custom = old.group().column_count() > old.group().builtin_count();
    util::Map<PersistentId, uint32_t> old_by_id;
    for (uint32_t r = 0; custom && r < old.capacity(); r++) {
      if (old.live(r)) {
        old_by_id.add_overwrite(old.template get<".id">()[r], r);
      }
    }
    util::Vector<uint32_t> targets, offsets, sources;
    offsets.append(0);
    for (uint32_t r = 0; custom && r < R; r++) {
      if (const uint32_t *src = old_by_id.lookup_ptr(reg_id[r])) {
        targets.append(r);
        sources.append(*src);
        offsets.append(uint32_t(sources.size()));
      }
    }
    if (targets.size() > 0 && custom) {
      attr::MixBatch mb{litestl::util::span<const uint32_t>(targets.data(), targets.size()),
                        litestl::util::span<const uint32_t>(offsets.data(), offsets.size()),
                        litestl::util::span<const uint32_t>(sources.data(), sources.size()),
                        {}};
      auto copy_first = [](std::string_view, uint32_t, const attr::ColumnBase &) { return attr::MixPolicy::CopyFirst; };
      const attr::AttrGroup::PolicyFn pf(copy_first);
      k.regions.group().mix_from(old.group(), mb, &pf);
    }
  }
  for (auto &[v, cr] : vcont) {
    k.verts.template get<".v.container">().set(v, cr);
  }
  for (auto &[e, cr] : econt) {
    k.edges.template get<".e.container">().set(e, cr);
  }
  k.regions_dirty = false;
  return true;
}

template bool build_regions<double>(Complex<double> &, RegionReport &);
template bool build_regions<float>(Complex<float> &, RegionReport &);
template bool build_regions<geom::Lattice>(Complex<geom::Lattice> &, RegionReport &);

} // namespace ubertopo::topo
