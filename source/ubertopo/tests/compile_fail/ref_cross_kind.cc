// A Ref of one domain must not be accepted where another domain is expected.
#include "ubertopo/ident/ref.h"
using namespace ubertopo::ident;
static void take_face(FaceRef) {}
int main()
{
  take_face(EdgeRef(1));
  return 0;
}
