#include "ubertopo/csg/planar.h"

#include <algorithm>

namespace ubertopo::csg {

using litestl::util::Vector;

void PlanarGraph::clear()
{
  atoms_.clear();
  atom_of_.clear();
}

void PlanarGraph::add(uint32_t p, uint32_t q, uint32_t cut, uint8_t out)
{
  const uint32_t lo = std::min(p, q), hi = std::max(p, q);
  if (p != lo) {
    out = uint8_t((out & kLeftOut ? kRightOut : 0) | (out & kRightOut ? kLeftOut : 0));
  }
  if (const uint32_t *known = atom_of_.lookup_ptr(key(p, q))) {
    atoms_[int(*known)].out |= out;
    return;
  }
  int8_t dir = 0;
  if (cut != kNone) {
    const Dir d = cross(plane_.n, in_.planes[cut].n);
    const int x = !d.v[0].is_zero() ? 0 : !d.v[1].is_zero() ? 1 : 2;
    dir = int8_t(d.v[x].sign() * compare(in_.points[hi], in_.points[lo], x));
  }
  atom_of_.add(key(p, q), uint32_t(atoms_.size()));
  atoms_.append({lo, hi, cut, dir, out});
}

/* outgoing half edges about a vertex, counter-clockwise seen from where the
 * normal points, from the projection's +u axis */
int PlanarGraph::half(uint32_t h) const
{
  int du, dv;
  if (cut(h) != kNone) {
    const Dir d = cross(plane_.n, in_.planes[cut(h)].n);
    du = dir(h) * d.v[u_].sign();
    dv = dir(h) * orient_ * d.v[v_].sign();
  }
  else {
    du = compare(in_.points[to(h)], in_.points[from(h)], u_);
    dv = orient_ * compare(in_.points[to(h)], in_.points[from(h)], v_);
  }
  return dv > 0 || (dv == 0 && du > 0) ? 0 : 1;
}

bool PlanarGraph::before(uint32_t a, uint32_t b) const
{
  const int ha = half(a), hb = half(b);
  if (ha != hb) {
    return ha < hb;
  }
  if (cut(a) == kNone || cut(b) == kNone) {
    return turn(from(a), to(a), to(b)) > 0;
  }
  return dir(a) * dir(b) * triple(plane_.n, in_.planes[cut(a)].n, in_.planes[cut(b)].n).sign() > 0;
}

void PlanarGraph::trace()
{
  local_.clear();
  out_.clear();
  vertex_.clear();
  cycle_start_.clear();
  const uint32_t nh = 2 * uint32_t(atoms_.size());
  for (uint32_t h = 0; h < nh; h++) {
    const uint32_t p = from(h);
    if (!local_.contains(p)) {
      local_.add(p, uint32_t(out_.size()));
      out_.append({});
      vertex_.append(p);
    }
    out_[int(local(p))].append(h);
  }
  next_.resize(nh);
  cycle_.resize(nh);
  for (auto &o : out_) {
    std::sort(o.begin(), o.end(), [&](uint32_t a, uint32_t b) { return before(a, b); });
    const int n = int(o.size());
    for (int i = 0; i < n; i++) {
      /* arriving on o[i]'s twin, leave by the next one clockwise */
      next_[int(o[i] ^ 1)] = o[(i + n - 1) % n];
    }
  }
  for (uint32_t h = 0; h < nh; h++) {
    cycle_[int(h)] = kNone;
  }
  for (uint32_t h = 0; h < nh; h++) {
    if (cycle_[int(h)] != kNone) {
      continue;
    }
    const uint32_t c = uint32_t(cycle_start_.size());
    cycle_start_.append(h);
    for (uint32_t x = h; cycle_[int(x)] == kNone; x = next_[int(x)]) {
      cycle_[int(x)] = c;
    }
  }
}

/* whether a point off cycle c is inside it (even-odd along u; slits are run
 * twice and cancel) */
bool PlanarGraph::contains(uint32_t c, uint32_t p) const
{
  bool inside = false;
  const HPoint &x = in_.points[p];
  for (uint32_t h = cycle_start_[int(c)];;) {
    const int a = compare(x, in_.points[from(h)], v_), b = compare(x, in_.points[to(h)], v_);
    if ((a < 0) != (b < 0)) {
      /* the cut's normal points to the right of cross(plane, cut) */
      const int left = cut(h) != kNone ? -dir(h) * classify(x, in_.planes[cut(h)]) : turn(from(h), to(h), p);
      inside = inside != ((left > 0) == (b < 0));
    }
    h = next_[int(h)];
    if (h == cycle_start_[int(c)]) {
      return inside;
    }
  }
}

CsgCode PlanarGraph::cells(std::span<const uint32_t> dots, Vector<PolygonFacet> &facets)
{
  facets.clear();
  trace();
  /* components of the graph */
  const int nv = int(out_.size());
  Vector<uint32_t, 64> comp;
  for (int i = 0; i < nv; i++) {
    comp.append(kNone);
  }
  Vector<uint32_t, 8> leftmost; /* of each component */
  Vector<uint32_t, 64> stack;
  for (int i = 0; i < nv; i++) {
    if (comp[i] != kNone) {
      continue;
    }
    const uint32_t c = uint32_t(leftmost.size());
    leftmost.append(uint32_t(i));
    comp[i] = c;
    stack.append(uint32_t(i));
    while (stack.size() > 0) {
      const uint32_t x = stack.pop_back();
      if (compare(in_.points[vertex_[int(x)]], in_.points[vertex_[int(leftmost[int(c)])]], u_) < 0) {
        leftmost[int(c)] = x;
      }
      for (uint32_t h : out_[int(x)]) {
        const uint32_t y = local(to(h));
        if (comp[int(y)] == kNone) {
          comp[int(y)] = c;
          stack.append(y);
        }
      }
    }
  }
  /* at a leftmost vertex no edge runs to the left; the wedge that looks left
   * is on the left of the last one that runs up or to the right */
  const int nc = int(cycle_start_.size());
  Vector<uint8_t, 16> outer; /* faces away from its component */
  for (int c = 0; c < nc; c++) {
    outer.append(0);
  }
  Vector<uint32_t, 8> outer_of; /* by component */
  for (uint32_t x : leftmost) {
    const auto &o = out_[int(x)];
    uint32_t h = o[int(o.size()) - 1];
    for (int i = int(o.size()) - 1; i >= 0; i--) {
      if (half(o[i]) == 0) {
        h = o[i];
        break;
      }
    }
    outer_of.append(cycle_[int(h)]);
    outer[int(cycle_[int(h)])] = 1;
  }
  auto comp_of = [&](uint32_t c) { return comp[int(local(from(cycle_start_[int(c)])))]; };
  /* the bounded cycle directly around a point that isn't on component skip */
  auto around = [&](uint32_t p, uint32_t skip) {
    Vector<uint32_t, 8> in_cycles;
    for (int c = 0; c < nc; c++) {
      if (!outer[c] && comp_of(uint32_t(c)) != skip && contains(uint32_t(c), p)) {
        in_cycles.append(uint32_t(c));
      }
    }
    /* the innermost: contained in all the others */
    uint32_t best = kNone;
    int best_depth = -1;
    for (uint32_t c : in_cycles) {
      int depth = 0;
      for (uint32_t d : in_cycles) {
        depth += d != c && comp_of(d) != comp_of(c) && contains(d, vertex_[int(leftmost[int(comp_of(c))])]);
      }
      if (depth > best_depth) {
        best_depth = depth;
        best = c;
      }
    }
    return best;
  };
  Vector<uint32_t, 16> cell_of; /* by bounded cycle */
  Vector<uint8_t, 16> unwanted;
  for (int c = 0; c < nc; c++) {
    cell_of.append(kNone);
    if (!outer[c]) {
      cell_of[c] = uint32_t(facets.size());
      facets.append(PolygonFacet());
      facets[int(cell_of[c])].off.append(0);
      unwanted.append(0);
    }
  }
  auto add_cycle = [&](uint32_t f, uint32_t c) {
    PolygonFacet &pf = facets[int(f)];
    for (uint32_t h = cycle_start_[int(c)];;) {
      pf.edges.append({from(h), to(h), cut(h)});
      if (atoms_[int(h >> 1)].out & (h & 1 ? kRightOut : kLeftOut)) {
        unwanted[int(f)] = 1;
      }
      h = next_[int(h)];
      if (h == cycle_start_[int(c)]) {
        break;
      }
    }
    pf.off.append(uint32_t(pf.edges.size()));
  };
  for (int c = 0; c < nc; c++) {
    if (!outer[c]) {
      add_cycle(cell_of[c], uint32_t(c));
    }
  }
  for (int i = 0; i < int(outer_of.size()); i++) {
    const uint32_t c = around(vertex_[int(leftmost[i])], uint32_t(i));
    if (c != kNone) {
      add_cycle(cell_of[int(c)], outer_of[i]);
    }
  }
  for (uint32_t p : dots) {
    if (local_.contains(p)) {
      continue;
    }
    const uint32_t c = around(p, kNone);
    if (c == kNone) {
      return CsgCode::Inconsistent;
    }
    auto &d = facets[int(cell_of[int(c)])].dots;
    if (std::find(d.begin(), d.end(), p) == d.end()) {
      d.append(p);
    }
  }
  int kept = 0;
  for (int i = 0; i < int(facets.size()); i++) {
    if (!unwanted[i]) {
      if (kept != i) {
        facets[kept] = std::move(facets[i]);
      }
      kept++;
    }
  }
  facets.resize(kept);
  return CsgCode::Ok;
}

} // namespace ubertopo::csg
