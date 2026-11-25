#pragma once

/* RegionBuilder: the writes. Included only by regions.cc.
 *
 * Face, face-side and container columns are compared first and written only
 * where they differ, so a rebuild that changes little leaves their versions
 * and their (shared) pages alone. The comparison is with whatever the complex
 * holds, however stale. The shell and region domains are replaced. */

#include "ubertopo/topo/regions/region_builder.h"

namespace ubertopo::topo::region_detail {

template <typename Col, typename T> void set_if_changed(Col &col, uint32_t i, const T &v)
{
  if (!(col[i] == v)) {
    col.set(i, v);
  }
}

template <geom::CoordType C> void RegionBuilder<C>::write()
{
  const uint32_t S = uint32_t(shell_.size());
  util::Vector<uint32_t> shell_region;
  shell_region.resize(S);
  for (uint32_t sh = 0; sh < S; sh++) {
    shell_region[sh] = preg_final_[shell_preg_[sh]];
  }
  util::Vector<uint32_t> region_first;
  write_faces(shell_region);
  write_shells(shell_region, region_first);
  write_regions(region_first);
  for (auto &[v, cr] : vcont_) {
    set_if_changed(k_.verts.template get<".v.container">(), v, cr);
  }
  for (auto &[e, cr] : econt_) {
    set_if_changed(k_.edges.template get<".e.container">(), e, cr);
  }
  k_.regions_dirty = false;
}

/* region and shell of each side; face-side shell chains */
template <geom::CoordType C> void RegionBuilder<C>::write_faces(const util::Vector<uint32_t> &shell_region)
{
  auto &freg = k_.faces.template get<".f.region">();
  auto &fsh = k_.faces.template get<".f.shell">();
  auto &sn = k_.faces.sides().template get<".fs.shell_next">();
  for (uint32_t f = 0; f < F_; f++) {
    if (!k_.faces.live(f)) {
      continue;
    }
    const uint32_t s0 = side_shell_[2 * f], s1 = side_shell_[2 * f + 1];
    set_if_changed(freg, f, U32x2{shell_region[s0], shell_region[s1]});
    set_if_changed(fsh, f, U32x2{s0, s1});
  }
  for (uint32_t sh = 0; sh < shell_.size(); sh++) {
    const auto &ss = shell_sides_[sh];
    for (uint32_t i = 0; i < ss.size(); i++) {
      set_if_changed(sn, ss[i], i + 1 < ss.size() ? ss[i + 1] : NONE);
    }
  }
}

/* generations: plan P3b §3.1; a shell is the same one if its first side is */
template <geom::CoordType C>
void RegionBuilder<C>::write_shells(const util::Vector<uint32_t> &shell_region, util::Vector<uint32_t> &region_first)
{
  const uint32_t S = uint32_t(shell_.size());
  const Domain<ShellSchema> old_shells = k_.shells;
  k_.shells = Domain<ShellSchema>();
  k_.shells.alloc_n(S);
  util::Vector<uint32_t> last;
  last.resize(R_);
  region_first.resize(R_);
  for (uint32_t r = 0; r < R_; r++) {
    last[r] = region_first[r] = NONE;
  }
  auto &sreg = k_.shells.template get<".s.region">();
  auto &sfirst = k_.shells.template get<".s.first">();
  auto &snext = k_.shells.template get<".s.next">();
  for (uint32_t sh = 0; sh < S; sh++) {
    const uint32_t r = shell_region[sh];
    sreg.set(sh, r);
    sfirst.set(sh, shell_sides_[sh].size() ? shell_sides_[sh][0] : NONE);
    if (last[r] == NONE) {
      region_first[r] = sh;
    }
    else {
      snext.set(last[r], sh);
    }
    last[r] = sh;
  }
  const auto &ofirst = old_shells.template get<".s.first">();
  k_.shells.replace_generations(old_shells, [&](uint32_t sh) { return old_shells.live(sh) && ofirst[sh] == sfirst[sh]; });
}

/* non-Topo columns are carried over by persistent ID */
template <geom::CoordType C> void RegionBuilder<C>::write_regions(const util::Vector<uint32_t> &region_first)
{
  const Domain<RegionSchema> old = k_.regions;
  k_.regions = Domain<RegionSchema>();
  k_.regions.alloc_n(R_);
  for (uint32_t r = 0; r < R_; r++) {
    k_.regions.template get<".id">().set(r, reg_id_[r]);
    k_.regions.template get<".r.shell">().set(r, region_first[r]);
  }
  k_.regions.template get<".r.flags">().set(0, region_flags::Unbounded);
  k_.regions.replace_generations(
      old, [&](uint32_t r) { return old.live(r) && old.template get<".id">()[r] == reg_id_[r]; });
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
  for (uint32_t r = 0; custom && r < R_; r++) {
    if (const uint32_t *src = old_by_id.lookup_ptr(reg_id_[r])) {
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
    k_.regions.group().mix_from(old.group(), mb, &pf);
  }
}

} // namespace ubertopo::topo::region_detail
