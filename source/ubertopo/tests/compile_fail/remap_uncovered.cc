// A topology schema with a column that has no RemapRule must not compile.
#include "ubertopo/topo/remap.h"
using namespace ubertopo::topo;
using Unmapped = Link<".x.unmapped">;
using Extended = EdgeSchema::extend<Unmapped>;
static_assert(schema_covered<Extended>, "a column has no RemapRule");
int main()
{
  return 0;
}
