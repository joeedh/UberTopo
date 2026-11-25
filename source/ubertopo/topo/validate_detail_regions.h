#pragma once

/* validate_regions' two parts. Not a public API; tests compare them. */

#include "ubertopo/topo/validate.h"

namespace ubertopo::topo::vdetail {

/** A flat, parallel check (plan 2026-11-24 §5). True only if
 * validate_regions_walk reports nothing. It may be false for a valid complex
 * that build_regions doesn't write (a shell chain that isn't ascending, or
 * links the walk skips), and it checks dirty regions like clean ones. Like
 * the walk, it assumes nothing about links. */
bool regions_precheck(const ComplexBase &k, const VertexTopoView &v);

/** validate_regions without the pre-check: walks chains, and reports. */
void validate_regions_walk(const ComplexBase &k, const VertexTopoView &v, ValidationReport &rep);

} // namespace ubertopo::topo::vdetail
