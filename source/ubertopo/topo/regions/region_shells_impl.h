#pragma once

/* RegionBuilder: faces and shells. Included only by regions.cc. */

#include <algorithm>

#include "ubertopo/topo/regions/region_builder.h"

namespace ubertopo::topo::region_detail {

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

template <geom::CoordType C> void RegionBuilder<C>::summarise_faces()
{
  face_.resize(F_);
  side_id_.resize(2 * size_t(F_));
  sk_.warm();
  task::parallel_for_chunks(util::IndexRange(0, int(F_)), 256, [&](int, util::IndexRange r) {
    FaceTriangles<C> ft;
    for (int f : r) {
      if (!k_.faces.live(uint32_t(f))) {
        continue;
      }
      const FaceRef fr{uint32_t(f)};
      FaceSummary<C> &fs = face_[f];
      fs.ok = ft.gather(k_, fr, sk_);
      for (uint32_t i = 0; i + 2 < ft.tris.size(); i += 3) {
        fs.vol.add(ft.tris[i], ft.tris[i + 1], ft.tris[i + 2]);
      }
      for (const Point &x : ft.tris) {
        fs.box.add(geom::to_double3<C>(x)); /* covers curved edges' skeletons */
      }
      k_.for_each_loop(fr, [&](LoopRef l) {
        k_.for_each_coedge(l, [&](CoedgeRef c) { fs.box.add(geom::to_double3<C>(k_.pos(k_.start_vert(c)))); });
      });
      side_id_[2 * f] = ident::face_side_id(k_.face_id(fr), 0);
      side_id_[2 * f + 1] = ident::face_side_id(k_.face_id(fr), 1);
    }
  });
}

template <geom::CoordType C> void RegionBuilder<C>::make_shells()
{
  const uint32_t NS2 = 2 * F_;
  Dsu dsu(NS2);
  for (uint32_t c = 0; c < k_.coedges.capacity(); c++) {
    if (!k_.coedges.live(c)) {
      continue;
    }
    const CoedgeRef cr(c), cn = k_.radial_next(cr);
    const uint32_t a = face_side_index(k_.face_of(cr), k_.side_toward_next(cr));
    const uint32_t b = face_side_index(k_.face_of(cn), 1 - k_.side_toward_next(cn));
    dsu.unite(a, b);
  }
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
    if (!k_.faces.live(s / 2)) {
      root[s] = NONE;
      continue;
    }
    const uint32_t r = root[s] = dsu.find(s);
    if (!is_root[r] || side_id_[s] < label[r]) {
      label[r] = side_id_[s];
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
  const uint32_t S = uint32_t(labels.size());
  shell_.resize(S);
  util::Vector<uint32_t> root_shell; /* by root side; set for roots only */
  root_shell.resize(NS2);
  for (uint32_t i = 0; i < S; i++) {
    shell_[i].label = labels[i].first;
    root_shell[labels[i].second] = i;
  }
  side_shell_.resize(NS2);
  for (uint32_t s = 0; s < NS2; s++) {
    side_shell_[s] = root[s] == NONE ? NONE : root_shell[root[s]];
  }
  util::group_by_key(
      NS2, S, [&](uint32_t s) { return side_shell_[s] == NONE ? util::kNoGroup : side_shell_[s]; }, shell_sides_);
}

template <geom::CoordType C> bool RegionBuilder<C>::summarise_shells()
{
  const uint32_t S = uint32_t(shell_.size());
  const auto &hints = k_.faces.sides().template get<".fs.hint">();
  task::parallel_for_chunks(util::IndexRange(0, int(S)), 64, [&](int, util::IndexRange r) {
    HintVotes votes;
    for (int sh : r) {
      ShellSummary<C> &ss = shell_[sh];
      bool sheet = true, ok = true;
      votes.clear();
      for (uint32_t s : shell_sides_[sh]) {
        sheet = sheet && side_shell_[s ^ 1u] == uint32_t(sh);
        ok = ok && face_[s / 2].ok;
        ss.box.add(face_[s / 2].box);
        if (!(hints[s] == ident::kNoId)) {
          votes.add(hints[s]);
        }
      }
      for (const HintVotes::Vote &v : votes.votes()) {
        ss.hints.append(v);
      }
      if (!ok) {
        ss.kind = Degenerate;
      }
      else if (sheet) {
        ss.kind = Sheet;
      }
      else {
        for (uint32_t s : shell_sides_[sh]) {
          /* outward from the side's region: + for BACK sides, - for FRONT */
          ss.vol.add(face_[s / 2].vol, (s & 1u) ? 1 : -1);
        }
        const int sg = ss.vol.sign();
        ss.kind = sg > 0 ? Outer : (sg < 0 ? Cavity : Degenerate);
      }
    }
  });
  report_.shells = S;
  for (uint32_t sh = 0; sh < S; sh++) {
    const uint8_t kind = shell_[sh].kind;
    if (kind == Degenerate) {
      report_.add(RegionCode::DegenerateShell, sh);
    }
    report_.sheet_shells += kind == Sheet;
    report_.outer_shells += kind == Outer;
    report_.cavity_shells += kind == Cavity;
  }
  return report_.ok();
}

template <geom::CoordType C> void RegionBuilder<C>::shell_verts(uint32_t sh, util::Vector<uint32_t> &out) const
{
  out.clear();
  for (uint32_t s : shell_sides_[sh]) {
    k_.for_each_loop(FaceRef(s / 2), [&](LoopRef l) {
      k_.for_each_coedge(l, [&](CoedgeRef c) { out.append(k_.start_vert(c).i); });
    });
  }
  std::sort(out.begin(), out.end());
  out.resize(size_t(std::unique(out.begin(), out.end()) - out.begin()));
}

} // namespace ubertopo::topo::region_detail
