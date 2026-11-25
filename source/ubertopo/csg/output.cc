#include "ubertopo/csg/output.h"

#include <algorithm>
#include <cmath>

#include "ubertopo/topo/builder.h"
#include "ubertopo/topo/face_geom.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::csg {

using geom::Lattice;
using litestl::util::Vector;
using LComplex = topo::Complex<Lattice>;

bool ExactPlacement::set(const RigidPlacement &p)
{
  constexpr double tol = 0x1p-40;
  for (int i = 0; i < 3; i++) {
    for (int j = i; j < 3; j++) {
      const double d = p.r[i][0] * p.r[j][0] + p.r[i][1] * p.r[j][1] + p.r[i][2] * p.r[j][2];
      if (!(std::abs(d - (i == j ? 1.0 : 0.0)) <= tol)) {
        return false;
      }
    }
  }
  const double det = p.r[0][0] * (p.r[1][1] * p.r[2][2] - p.r[1][2] * p.r[2][1]) -
                     p.r[0][1] * (p.r[1][0] * p.r[2][2] - p.r[1][2] * p.r[2][0]) +
                     p.r[0][2] * (p.r[1][0] * p.r[2][1] - p.r[1][1] * p.r[2][0]);
  if (!(det > 0)) {
    return false;
  }
  for (int i = 0; i < 3; i++) {
    if (!(std::abs(p.t[i]) <= 0x1p40)) {
      return false;
    }
    t_[i] = int64_t(std::floor(std::ldexp(p.t[i], kTransBits) + 0.5));
    for (int j = 0; j < 3; j++) {
      r_[i][j] = int64_t(std::ldexp(p.r[i][j], kRotBits));
    }
  }
  return true;
}

I3 ExactPlacement::round(const Input &in, const HPoint &p, int shift, const I3 &bias) const
{
  /* the operands' coordinates, over w */
  Int512 x[3];
  const Int512 w = Int512::from(p.w);
  for (int j = 0; j < 3; j++) {
    x[j] = Int512::from(p.x[j]) - Int512::from(in.shift[j]) * w;
  }
  I3 out;
  const Int512 one = w << kRotBits;
  for (int i = 0; i < 3; i++) {
    Int512 s = Int512::from(t_[i]) * (w << (kRotBits - kTransBits));
    for (int j = 0; j < 3; j++) {
      s += Int512::from(r_[i][j]) * x[j];
    }
    /* floor(s 2^shift / one + 1/2 + bias / 8) */
    out[i] = floor_div((s << (shift + 3)) + Int512::from(4 + bias[i]) * one, one << 3);
  }
  return out;
}

namespace {

geom::Point<Lattice> lattice(const I3 &p)
{
  return geom::Point<Lattice>(Lattice(p[0]), Lattice(p[1]), Lattice(p[2]));
}

struct Writer {
  const Arrangement &a;
  const Expression &e;
  const Vector<Selected> &sel;
  const OutputOptions &options;
  OutputReport &rep;
  ExactPlacement place;
  int shift;
  I3 bias;
  Vector<uint32_t> key;    /* by point: builder key */
  Vector<uint8_t> used;    /* by point */
  Vector<uint32_t> region_of; /* by result face and side: region of the complex */

  /* outlines whose triangulation is another one after rounding */
  bool check_whole(Vector<uint8_t> &split)
  {
    bool any = false;
    split.resize(a.in.outlines.size());
    for (uint8_t &s : split) {
      s = 0;
    }
    Vector<geom::Point<Lattice>> pts;
    Vector<geom::Triangle> tris;
    for (const ResultFace &f : rep.result.faces) {
      if (!f.whole) {
        continue;
      }
      const uint32_t outline = a.in.polygons[int(f.source)].outline;
      const Outline &o = a.in.outlines[int(outline)];
      pts.clear();
      for (uint32_t p : f.pts) {
        pts.append(lattice(rep.positions[int(p)]));
      }
      const geom::PolyView<Lattice> view{{pts.data(), size_t(pts.size())}, {f.off.data(), size_t(f.off.size())}};
      int8_t axis = -1;
      bool same = topo::classify_face<Lattice>(view, true, axis, tris) == topo::FaceGeom::Ok;
      if (same && axis >= 0) {
        /* (a planar face has one surface) */
        same = tris.size() == o.count;
        for (uint32_t i = 0; i < o.count && same; i++) {
          const Polygon &t = a.in.polygons[int(o.first + i)];
          bool found = false;
          for (const geom::Triangle &u : tris) {
            int n = 0;
            for (int x = 0; x < 3; x++) {
              n += std::find(t.pts.begin(), t.pts.end(), f.pts[int(u[x])]) != t.pts.end();
            }
            found = found || n == 3;
          }
          same = found;
        }
      }
      if (!same) {
        split[int(outline)] = 1;
        rep.split_whole++;
        any = true;
      }
    }
    return any;
  }

  CsgCode round_points()
  {
    used.resize(a.in.points.size());
    rep.positions.resize(a.in.points.size());
    for (uint32_t p = 0; p < a.in.points.size(); p++) {
      used[int(p)] = 0;
      rep.positions[int(p)] = I3(0, 0, 0);
    }
    for (const ResultFace &f : rep.result.faces) {
      for (uint32_t p : f.pts) {
        used[int(p)] = 1;
      }
    }
    for (uint32_t p = 0; p < a.in.points.size(); p++) {
      if (used[int(p)]) {
        rep.positions[int(p)] = place.round(a.in, a.in.points[p], shift, bias);
        if (!in_range(rep.positions[int(p)])) {
          return CsgCode::OutOfRange;
        }
      }
    }
    return CsgCode::Ok;
  }

  CsgCode fail(RoundCode c)
  {
    rep.round = c;
    return CsgCode::RoundingFailed;
  }

  PersistentId vertex_id(uint32_t i, uint32_t point) const
  {
    return options.names ? rep.names.vertex[int(point)] : ident::derive(ident::tag::OpDerived, uint32_t(0), i);
  }
  PersistentId face_id(uint32_t f) const
  {
    return options.names ? rep.names.face[int(f)] : ident::derive(ident::tag::OpDerived, uint32_t(1), f);
  }

  uint8_t flags(uint32_t f) const
  {
    const Provenance::Face &g = rep.provenance.faces[int(f)];
    return options.names->operands[g.leaf].k->faces.get<".f.flags">()[g.face] &
           (topo::face_flags::Virtual | topo::face_flags::Aperture);
  }

  /* hints: of regions, by the sides that rep.regions was made of (may be empty) */
  CsgCode write(LComplex &k, std::span<const PersistentId> hints)
  {
    Vector<uint32_t> order;
    key.clear();
    for (uint32_t p = 0; p < used.size(); p++) {
      key.append(kNone);
      if (used[int(p)]) {
        order.append(p);
      }
    }
    auto less = [&](uint32_t x, uint32_t y) {
      const I3 &p = rep.positions[int(x)], &q = rep.positions[int(y)];
      return p[0] != q[0] ? p[0] < q[0] : p[1] != q[1] ? p[1] < q[1] : p[2] < q[2];
    };
    std::sort(order.begin(), order.end(), less);
    for (int i = 1; i < int(order.size()); i++) {
      if (!less(order[i - 1], order[i])) {
        return fail(RoundCode::SamePosition);
      }
    }
    geom::TileFrame frame = options.frame;
    frame.quantum_log2 -= options.quantum_shift;
    topo::Builder<Lattice> b(frame);
    for (uint32_t i = 0; i < order.size(); i++) {
      key[int(order[int(i)])] =
          b.vertex(vertex_id(i, order[int(i)]), lattice(rep.positions[int(order[int(i)])]));
    }
    Vector<uint32_t> keys, sizes;
    for (uint32_t i = 0; i < rep.result.faces.size(); i++) {
      const ResultFace &f = rep.result.faces[int(i)];
      keys.clear();
      sizes.clear();
      for (uint32_t p : f.pts) {
        keys.append(key[int(p)]);
      }
      for (int l = 0; l + 1 < int(f.off.size()); l++) {
        sizes.append(f.off[l + 1] - f.off[l]);
      }
      PersistentId hint[2];
      for (uint32_t side = 0; side < 2 && hints.size() > 0; side++) {
        hint[side] = hints[size_t(region_of[int(2 * i + side)])];
      }
      b.face(face_id(i), {keys.data(), size_t(keys.size())}, {sizes.data(), size_t(sizes.size())}, hint[0], hint[1],
             options.names ? flags(i) : uint8_t(0));
    }
    topo::BuildPolicy policy;
    policy.allow_projectable_faces = true;
    topo::BuildReport br;
    if (!b.build(k, br, policy)) {
      return fail(RoundCode::Build);
    }
    rep.projected = br.projected_faces;
    topo::RegionReport rr;
    if (!topo::build_regions(k, rr)) {
      return fail(RoundCode::Regions);
    }
    return CsgCode::Ok;
  }

  /* the rounded faces are arranged as the exact ones */
  CsgCode check(const LComplex &k)
  {
    litestl::util::Map<ident::PersistentId, uint32_t> vert, face;
    k.verts.for_each_live([&](uint32_t v) { vert.add(k.vert_id(topo::VertRef(v)), v); });
    k.faces.for_each_live([&](uint32_t f) { face.add(k.face_id(topo::FaceRef(f)), f); });
    rep.face_of.clear();
    for (uint32_t i = 0; i < rep.result.faces.size(); i++) {
      rep.face_of.append(*face.lookup_ptr(face_id(i)));
    }
    rep.vert_of.clear();
    rep.edge_of.clear();
    for (uint32_t p = 0; p < used.size(); p++) {
      rep.vert_of.append(used[int(p)] ? *vert.lookup_ptr(vertex_id(key[int(p)], p)) : kNone);
    }
    litestl::util::Map<uint64_t, uint32_t> edge;
    auto pair = [](uint32_t x, uint32_t y) { return (uint64_t(std::min(x, y)) << 32) | std::max(x, y); };
    k.edges.for_each_live([&](uint32_t x) {
      const auto vs = k.edge_verts(topo::EdgeRef(x));
      edge.add(pair(vs[0].i, vs[1].i), x);
    });
    if (edge.size() != rep.result.edges.size()) {
      return CsgCode::Inconsistent;
    }
    Vector<std::pair<uint32_t, bool>, 8> got, want;
    for (const ResultEdge &x : rep.result.edges) {
      const uint32_t va = rep.vert_of[int(x.a)], vb = rep.vert_of[int(x.b)];
      const uint32_t *ke = edge.lookup_ptr(pair(va, vb));
      if (!ke) {
        return CsgCode::Inconsistent;
      }
      const topo::EdgeRef er(*ke);
      rep.edge_of.append(*ke);
      const bool along = k.edge_verts(er)[0].i == va; /* vs[0] -> vs[1] is a -> b */
      want.clear();
      got.clear();
      for (int i = 0; i < int(x.uses.size()); i++) {
        const EdgeUse &u = x.uses[along ? i : int(x.uses.size()) - 1 - i];
        want.append({rep.face_of[int(u.facet)], (u.dir > 0) == along});
      }
      const topo::CoedgeRef first = k.edge_coedge(er);
      topo::CoedgeRef c = first;
      do {
        got.append({k.face_of(c).i, k.sense(c)});
        c = k.radial_next(c);
      } while (c.i != first.i && got.size() <= want.size());
      if (got.size() != want.size()) {
        return CsgCode::Inconsistent;
      }
      bool same = false;
      for (int s = 0; s < int(want.size()) && !same; s++) {
        same = true;
        for (int i = 0; i < int(want.size()) && same; i++) {
          same = got[i] == want[(i + s) % int(want.size())];
        }
      }
      if (!same) {
        return fail(RoundCode::Radial);
      }
    }
    Vector<uint32_t> label;
    for (uint32_t i = 0; i < k.regions.capacity(); i++) {
      label.append(i == 0 ? kVoid : kNone);
    }
    for (uint32_t i = 0; i < rep.result.faces.size(); i++) {
      for (uint32_t side = 0; side < 2; side++) {
        const uint32_t s = rep.result.faces[int(i)].solid[side];
        uint32_t &l = label[int(k.region(topo::FaceRef(rep.face_of[int(i)]), side).i)];
        l = l == kNone ? s : l;
        if (l != s) {
          return fail(RoundCode::Labels);
        }
      }
    }
    note_regions(k);
    if (options.validate && !topo::validate(k, topo::ValidateLevel::Global).ok()) {
      return fail(RoundCode::Invalid);
    }
    return CsgCode::Ok;
  }

  void note_regions(const LComplex &k)
  {
    rep.regions.clear();
    region_of.clear();
    for (uint32_t i = 0; i < k.regions.capacity(); i++) {
      rep.regions.append({});
    }
    for (uint32_t i = 0; i < rep.result.faces.size(); i++) {
      const ResultFace &f = rep.result.faces[int(i)];
      const PersistentId id = face_id(i);
      for (uint32_t side = 0; side < 2; side++) {
        const uint32_t region = k.region(topo::FaceRef(rep.face_of[int(i)]), side).i;
        region_of.append(region);
        RegionSides &r = rep.regions[int(region)];
        r.solid = f.solid[side];
        if (f.label[side] != f.solid[side] && (r.air == kVoid || f.label[side] < r.air)) {
          r.air = f.label[side];
        }
        /* (the first leaf's, so that a cutter that moves from one part to
         * another doesn't exchange their names) */
        const uint32_t leaf = rep.provenance.faces[int(i)].leaf;
        if (r.leaf == kNone || leaf < r.leaf || (leaf == r.leaf && id < r.min_face)) {
          r.leaf = leaf;
          r.min_face = id;
        }
      }
    }
  }

  /* builds again, with names for the regions */
  CsgCode write_regions(LComplex &k)
  {
    Vector<PersistentId> hints;
    name_regions({rep.regions.data(), size_t(rep.regions.size())}, *options.names, hints);
    const CsgCode c = write(k, {hints.data(), size_t(hints.size())});
    return c == CsgCode::Ok ? check(k) : c;
  }

  CsgCode attributes(LComplex &k)
  {
    const NameSource &n = *options.names;
    k.regions.for_each_live([&](uint32_t r) {
      while (rep.names.region.size() <= r) {
        rep.names.region.append(ident::kNoId);
      }
      rep.names.region[int(r)] = k.regions.get<".id">()[r];
    });
    for (const Provenance::Face &f : rep.provenance.faces) {
      rep.sources.append({n.leaf_ids[f.leaf], n.operands[f.leaf].k->face_id(topo::FaceRef(f.face)), f.reversed});
    }
    const ElementMap map{{rep.vert_of.data(), size_t(rep.vert_of.size())},
                         {rep.edge_of.data(), size_t(rep.edge_of.size())},
                         {rep.face_of.data(), size_t(rep.face_of.size())},
                         {rep.regions.data(), size_t(rep.regions.size())}};
    return copy_attributes(rep.provenance, n, map, options.rules, k, &rep.bad_leaf);
  }

  CsgCode merge(std::span<const uint8_t> split)
  {
    rep.result = Result();
    const CsgCode c = merge_facets(a, e, sel, split, rep.result);
    return c == CsgCode::Ok ? round_points() : c;
  }

  CsgCode merge_whole()
  {
    CsgCode c = merge({});
    Vector<uint8_t> split;
    if (c == CsgCode::Ok && check_whole(split)) {
      c = merge({split.data(), size_t(split.size())});
    }
    return c;
  }

  CsgCode build(LComplex &k)
  {
    CsgCode c = write(k, {});
    c = c == CsgCode::Ok ? check(k) : c;
    if (c == CsgCode::Ok && options.names) {
      c = write_regions(k);
      c = c == CsgCode::Ok ? attributes(k) : c;
    }
    return c;
  }
};

/* An attempt: the exact result, rounded, then repaired in rounds. Its points are
 * rounded with one bias. */
struct Attempt {
  const OutputOptions &options;
  const ExactPlacement &place;
  I3 bias;
  OutputReport &rep;
  LComplex &out;

  uint32_t roots = 0, face_limit = 0;
  Vector<PersistentId> root_names; /* of the exact result's faces */
  Provenance first;
  Vector<PersistentId> seen;       /* hashes of the rounds' polygons */
  bool hopeless = false;           /* a round found nothing to repair */

  static constexpr int kExactFrom = 3; /* round */

  CsgCode start(const Arrangement &a, const Expression &e, const Vector<Selected> &sel)
  {
    Writer w{a, e, sel, options, rep, place, options.quantum_shift, bias, {}, {}, {}};
    CsgCode c = w.merge_whole();
    if (c == CsgCode::Ok && options.names) {
      name_elements(a, e, sel, rep.result, *options.names, rep.names);
    }
    LComplex k;
    if (c == CsgCode::Ok) {
      find_provenance(a, rep.result, rep.names, rep.provenance);
      c = w.build(k);
    }
    if (c == CsgCode::Ok) {
      out = std::move(k);
    }
    if (c != CsgCode::RoundingFailed || options.max_repair_rounds <= 0) {
      return c;
    }
    if (rep.repair.first == RoundCode::None) {
      rep.repair.first = rep.round;
    }
    roots = uint32_t(rep.result.faces.size());
    face_limit = 4 * roots + 300;
    root_names = rep.names.face;
    first = rep.provenance;
    Vector<uint32_t> root;
    for (uint32_t f = 0; f < roots; f++) {
      root.append(f);
    }
    return round(w, root, 1);
  }

  /* repairs what w has (a result that failed); root: by its faces, the exact
   * result's face */
  CsgCode round(const Writer &w, const Vector<uint32_t> &root, int number)
  {
    if (number > options.max_repair_rounds) {
      return CsgCode::RoundingFailed;
    }
    rep.repair.rounds = uint32_t(number);
    const Result before = rep.result;
    const Vector<I3> positions = rep.positions;
    const Names names = std::move(rep.names);
    const Provenance provenance = std::move(rep.provenance);
    const Rounded rounded{w.a,
                          before,
                          {positions.data(), size_t(positions.size())},
                          {w.used.data(), size_t(w.used.size())},
                          names,
                          provenance};
    Arrangement a;
    Vector<uint32_t> point_of, solid;
    RepairCounts &counts = rep.repair.counts;
    CsgCode c = read_rounded(rounded, number >= kExactFrom, a.in, point_of, counts);
    if (c != CsgCode::Ok) {
      return c;
    }
    const PersistentId hash = polygons_hash(a.in);
    if (number > kExactFrom && std::any_of(seen.begin(), seen.end(), [&](const PersistentId &x) { return x == hash; })) {
      return CsgCode::RoundingFailed;
    }
    seen.append(hash);
    a.leaf_count = 1;
    c = build_arrangement(a);
    std::span<const PersistentId> labels;
    if (options.names) {
      labels = options.names->labels;
    }
    /* (thin faces that cross along their length can multiply) */
    c = c == CsgCode::Ok && a.facets.size() > 4 * face_limit ? CsgCode::RoundingFailed : c;
    c = c == CsgCode::Ok ? label_by_winding(a, labels, solid, counts) : c;
    if (c != CsgCode::Ok) {
      return c;
    }
    if (number == 1 && counts.merged == 0 && counts.broken == 0 && unchanged(a, solid)) {
      hopeless = true;
      return CsgCode::RoundingFailed;
    }
    Vector<Selected> sel;
    select_repaired(a, solid, rounded, w.e, sel);
    Expression e;
    e.kinds = w.e.kinds;
    Vector<uint32_t> ids;
    for (uint32_t l = 0; l < e.kinds.size(); l++) {
      ids.append(l);
    }
    e.add_leaf(true, {ids.data(), size_t(ids.size())});
    ExactPlacement same;
    same.set(RigidPlacement());
    Writer x{a, e, sel, options, rep, same, 0, bias, {}, {}, {}};
    c = x.merge_whole();
    c = c == CsgCode::Ok && rep.result.faces.size() > face_limit ? CsgCode::RoundingFailed : c;
    if (c != CsgCode::Ok) {
      return c;
    }
    Vector<uint32_t> next_root;
    for (const ResultFace &f : rep.result.faces) {
      next_root.append(root[int(a.in.polygons[int(f.source)].face)]);
    }
    const PreviousNames previous{before,
                                 {names.vertex.data(), size_t(names.vertex.size())},
                                 {names.face.data(), size_t(names.face.size())},
                                 {point_of.data(), size_t(point_of.size())}};
    const PersistentId no_leaf = ident::kNoId;
    auto name = [&](const Vector<uint32_t> &of) {
      if (options.names) {
        NameSource n = *options.names;
        n.previous = &previous;
        n.leaf_ids = {&no_leaf, 1};
        name_elements(a, e, sel, rep.result, n, rep.names);
        keep_names(of);
      }
      compose_provenance(provenance, first, {of.data(), size_t(of.size())}, a, rep.result, rep.names, rep.provenance);
    };
    auto done = [&](LComplex &k, const Vector<uint32_t> &of) {
      Vector<uint8_t> there;
      there.resize(roots);
      std::fill(there.begin(), there.end(), uint8_t(0));
      for (uint32_t f : of) {
        there[int(f)] = 1;
      }
      rep.repair.vanished = uint32_t(std::count(there.begin(), there.end(), uint8_t(0)));
      rep.round = RoundCode::None;
      out = std::move(k);
      return CsgCode::Ok;
    };
    name(next_root);
    /* whole faces again, if that does */
    Result joined;
    Vector<Vector<uint32_t, 2>> members;
    if (join_faces(rep.result, {rep.positions.data(), size_t(rep.positions.size())},
                   {next_root.data(), size_t(next_root.size())}, joined, members))
    {
      const Result pieces = std::move(rep.result);
      Vector<uint32_t> joined_root;
      for (int f = 0; f < int(members.size()); f++) {
        joined_root.append(next_root[int(members[f][0])]);
        for (uint32_t m : members[f]) {
          /* it goes on as the piece that has the face's name */
          const ResultFace &piece = pieces.faces[int(m)];
          const uint32_t src = a.in.polygons[int(piece.source)].face;
          if (options.names && names.face[int(src)] == root_names[int(joined_root[f])]) {
            joined.faces[f].source = piece.source;
            break;
          }
        }
      }
      rep.result = std::move(joined);
      name(joined_root);
      LComplex k;
      c = x.build(k);
      if (c == CsgCode::Ok) {
        rep.repair.joined = uint32_t(pieces.faces.size() - members.size());
        return done(k, joined_root);
      }
      if (c != CsgCode::RoundingFailed) {
        return c;
      }
      rep.result = pieces;
      name(next_root);
    }
    LComplex k;
    c = x.build(k);
    if (c == CsgCode::Ok) {
      return done(k, next_root);
    }
    return c == CsgCode::RoundingFailed ? round(x, next_root, number + 1) : c;
  }

  /* a face that has pieces left, but not the one with its name: the piece with
   * the smallest name takes it */
  void keep_names(const Vector<uint32_t> &root)
  {
    Vector<uint32_t> best;
    best.resize(root_names.size());
    std::fill(best.begin(), best.end(), kNone);
    auto &face = rep.names.face;
    for (uint32_t f = 0; f < root.size(); f++) {
      uint32_t &b = best[int(root[int(f)])];
      const PersistentId &name = root_names[int(root[int(f)])];
      if (b == kNone || face[int(f)] == name || (!(face[int(b)] == name) && face[int(f)] < face[int(b)])) {
        b = f;
      }
    }
    for (uint32_t i = 0; i < best.size(); i++) {
      if (best[int(i)] != kNone) {
        face[int(best[int(i)])] = root_names[int(i)];
      }
    }
  }

  /* the rounded faces meet as the exact ones do */
  static bool unchanged(const Arrangement &a, const Vector<uint32_t> &solid)
  {
    if (a.facets.size() != a.in.polygons.size()) {
      return false;
    }
    for (uint32_t f = 0; f < a.facets.size(); f++) {
      const Facet &fc = a.facets[int(f)];
      if (fc.sources.size() != 1) {
        return false;
      }
      const Polygon &g = a.in.polygons[int(fc.sources[0])];
      const uint32_t front = g.sign > 0 ? 0 : 1;
      if (solid[int(2 * f + front)] != g.region[0] || solid[int(2 * f + 1 - front)] != g.region[1]) {
        return false;
      }
    }
    return true;
  }
};

} // namespace

CsgCode build_output(const Arrangement &a, const Expression &e, const Vector<Selected> &selected,
                     const OutputOptions &options, LComplex &out, OutputReport &report)
{
  ExactPlacement place;
  if (!place.set(options.placement)) {
    return CsgCode::BadPlacement;
  }
  /* of what is added before rounding: nothing that moves an integer */
  constexpr int8_t kBias[8][3] = {{0, 0, 0},   {1, 1, 1},  {-1, -1, -1}, {1, -1, 1},
                                  {-1, 1, -1}, {1, 1, -1}, {-1, -1, 1},  {1, -1, -1}};
  RoundCode first = RoundCode::None;
  CsgCode c = CsgCode::RoundingFailed;
  const int attempts = options.max_repair_rounds > 0 ? 8 : 1;
  for (int i = 0; i < attempts; i++) {
    OutputReport rep;
    rep.repair.first = first;
    rep.repair.attempts = uint32_t(i);
    Attempt x{options, place, I3(3 * kBias[i][0], 3 * kBias[i][1], 3 * kBias[i][2]), rep, out, 0, 0, {}, {}, {}, false};
    c = x.start(a, e, selected);
    first = rep.repair.first;
    if (i > 0 && c == CsgCode::OutOfRange) {
      c = CsgCode::RoundingFailed; /* (the first attempt was in range) */
      continue;
    }
    report = std::move(rep);
    if (c != CsgCode::RoundingFailed || x.hopeless) {
      break;
    }
  }
  return c;
}

} // namespace ubertopo::csg
