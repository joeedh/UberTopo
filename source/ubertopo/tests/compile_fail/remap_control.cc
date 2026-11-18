// Positive control for remap_uncovered: the builtin schemas are covered.
#include "ubertopo/topo/remap.h"
using namespace ubertopo::topo;
static_assert(schema_covered<EdgeSchema>);
int main()
{
  return 0;
}
