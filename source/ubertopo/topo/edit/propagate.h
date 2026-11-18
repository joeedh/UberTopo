#pragma once

/* Attribute propagation for edits (design 03 §6; plan P3 §3.3, C2, C10).
 *
 * A primitive turns each topology event into one simultaneous mix per
 * (group, event kind). Topo columns are never mixed (AttrGroup::mix skips
 * them; the primitive writes them), "pos" is never mixed (C2), and Temp /
 * Derived columns are reset to their defaults (they are recomputed). The
 * other columns follow their per-event rule, or the default:
 *
 *   Split   parts <- parent, weight = the part's measure fraction.
 *           Default CopyFirst. A float column may opt into Interpolate
 *           ("split by measure": part = fraction * parent).
 *   Merge   survivor <- (survivor, absorbed...), weights = raw measures.
 *           Default: WeightedMean for float columns, CopyFirst otherwise;
 *           CopyFirst whenever all sources are bitwise equal. Sum conserves.
 *   Interp  new element on a segment <- (end0, end1), weights (1 - t, t).
 *           Default: the column's own default policy.
 *   Reverse simultaneous swaps (face sides; corners of a flipped face).
 *           Default CopyFirst. */

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "litestl/attr/mix.h"
#include "litestl/util/vector.h"
#include "ubertopo/topo/edit/edit_types.h"

namespace ubertopo::topo {

enum class PropEvent : uint8_t { Split, Merge, Interp, Reverse };

/** Per-column overrides of the default policies. */
struct ColumnRule {
  Grp group;
  std::string name;
  std::optional<litestl::attr::MixPolicy> split, merge, interp, reverse;
};

class PropagationRules {
public:
  void add(ColumnRule r)
  {
    rules_.push_back(std::move(r));
  }
  const ColumnRule *find(Grp g, std::string_view name) const
  {
    for (const ColumnRule &r : rules_) {
      if (r.group == g && r.name == name) {
        return &r;
      }
    }
    return nullptr;
  }
  /** Convenience: a conserved float quantity (split by measure, sum on merge). */
  void conserved(Grp g, std::string_view name)
  {
    add(ColumnRule{g, std::string(name), litestl::attr::MixPolicy::Interpolate, litestl::attr::MixPolicy::Sum, {}, {}});
  }

private:
  std::vector<ColumnRule> rules_;
};

/** One event batch under construction (a MixBatch in owned storage). */
struct MixBuild {
  litestl::util::Vector<uint32_t, 16> targets;
  litestl::util::Vector<uint32_t, 17> offsets{0u};
  litestl::util::Vector<uint32_t, 32> sources;
  litestl::util::Vector<double, 32> weights;

  void add(uint32_t target, std::initializer_list<std::pair<uint32_t, double>> src)
  {
    targets.append(target);
    for (const auto &s : src) {
      sources.append(s.first);
      weights.append(s.second);
    }
    offsets.append(uint32_t(sources.size()));
  }
  bool empty() const
  {
    return targets.size() == 0;
  }
  litestl::attr::MixBatch batch() const
  {
    return {litestl::util::span<const uint32_t>(targets.data(), targets.size()),
            litestl::util::span<const uint32_t>(offsets.data(), offsets.size()),
            litestl::util::span<const uint32_t>(sources.data(), sources.size()),
            litestl::util::span<const double>(weights.data(), weights.size())};
  }
};

/** Operator data handed to Custom mixes (MixContext::op). */
struct PropEventInfo {
  PropEvent event;
  Grp group;
};

} // namespace ubertopo::topo
