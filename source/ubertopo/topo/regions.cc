#include "ubertopo/topo/regions.h"

#include "ubertopo/topo/regions/region_builder.h"
#include "ubertopo/topo/regions/region_naming_impl.h"
#include "ubertopo/topo/regions/region_nesting_impl.h"
#include "ubertopo/topo/regions/region_shells_impl.h"
#include "ubertopo/topo/regions/region_write_impl.h"

namespace ubertopo::topo {

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

namespace region_detail {

template <geom::CoordType C> bool RegionBuilder<C>::build()
{
  summarise_faces();
  make_shells();
  if (!summarise_shells() || !nest()) {
    return false;
  }
  name_regions();
  if (!resolve_containers()) {
    return false;
  }
  report_.regions = R_;
  write();
  return true;
}

} // namespace region_detail

template <geom::CoordType C> bool build_regions(Complex<C> &k, RegionReport &report)
{
  region_detail::RegionBuilder<C> b(k, report);
  return b.build();
}

template bool build_regions<double>(Complex<double> &, RegionReport &);
template bool build_regions<float>(Complex<float> &, RegionReport &);
template bool build_regions<geom::Lattice>(Complex<geom::Lattice> &, RegionReport &);

} // namespace ubertopo::topo
