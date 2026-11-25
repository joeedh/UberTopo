#include "ubertopo/csg/diff.h"

#include <algorithm>

#include "litestl/util/map.h"

namespace ubertopo::csg {

namespace {

using ident::kNoId;
using litestl::util::Vector;

struct Faces {
  const BoolReport &rep;
  Vector<Vector<PersistentId, 8>> verts; /* by face, sorted */

  explicit Faces(const BoolReport &r) : rep(r)
  {
    for (const ResultFace &f : r.output.result.faces) {
      Vector<PersistentId, 8> v;
      for (uint32_t p : f.pts) {
        v.append(r.output.names.vertex[int(p)]);
      }
      std::sort(v.begin(), v.end());
      verts.append(std::move(v));
    }
  }
  bool same_source(uint32_t f, const Faces &b, uint32_t g) const
  {
    const FaceSource &x = rep.output.sources[int(f)], &y = b.rep.output.sources[int(g)];
    return x.leaf == y.leaf && x.face == y.face;
  }
  uint32_t common(uint32_t f, const Faces &b, uint32_t g) const
  {
    uint32_t n = 0;
    const auto &x = verts[int(f)], &y = b.verts[int(g)];
    for (int i = 0, j = 0; i < int(x.size()) && j < int(y.size());) {
      if (x[i] == y[j]) {
        n++, i++, j++;
      }
      else {
        x[i] < y[j] ? i++ : j++;
      }
    }
    return n;
  }
  PersistentId name(uint32_t f) const
  {
    return rep.output.names.face[int(f)];
  }
};

void compare(Vector<PersistentId> x, Vector<PersistentId> y, NameChanges &out)
{
  std::sort(x.begin(), x.end());
  std::sort(y.begin(), y.end());
  int i = 0, j = 0;
  while (i < int(x.size()) || j < int(y.size())) {
    if (j == int(y.size()) || (i < int(x.size()) && x[i] < y[j])) {
      out.gone.append(x[i++]);
    }
    else if (i == int(x.size()) || y[j] < x[i]) {
      out.added.append(y[j++]);
    }
    else {
      out.kept.append(x[i++]);
      j++;
    }
  }
}

Vector<PersistentId> vertex_names(const BoolReport &r)
{
  Vector<PersistentId> v;
  for (const PersistentId &x : r.output.names.vertex) {
    if (!(x == kNoId)) {
      v.append(x);
    }
  }
  return v;
}

} // namespace

bool csg_diff(const BoolReport &before, const BoolReport &now, CsgDiff &out)
{
  out = CsgDiff();
  if (before.code != CsgCode::Ok || now.code != CsgCode::Ok) {
    return false;
  }
  compare(vertex_names(before), vertex_names(now), out.vertices);
  compare(before.output.names.face, now.output.names.face, out.faces);
  compare(before.output.names.region, now.output.names.region, out.regions);
  const Faces x(before), y(now);
  for (uint32_t f = 0; f < y.verts.size(); f++) {
    uint32_t best = kNone, n = 0;
    bool same = false;
    for (uint32_t g = 0; g < x.verts.size(); g++) {
      if (!y.same_source(f, x, g)) {
        continue;
      }
      const uint32_t c = y.common(f, x, g);
      same = same || (c > 0 && x.name(g) == y.name(f));
      if (c > n || (c == n && c > 0 && x.name(g) < x.name(best))) {
        best = g;
        n = c;
      }
    }
    if (best != kNone && !same) {
      out.moved.append({y.name(f), x.name(best)});
    }
  }
  std::sort(out.moved.begin(), out.moved.end(), [](const FaceLineage &p, const FaceLineage &q) { return p.now < q.now; });
  return true;
}

} // namespace ubertopo::csg
