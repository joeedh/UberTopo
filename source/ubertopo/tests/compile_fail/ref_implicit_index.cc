// A raw index must not convert to a Ref implicitly.
#include "ubertopo/ident/ref.h"
using namespace ubertopo::ident;
int main()
{
  VertRef v = 5u;
  return int(v.i);
}
