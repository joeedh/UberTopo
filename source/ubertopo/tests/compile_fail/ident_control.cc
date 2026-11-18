// Positive control: must compile (proves the include setup of the negative tests).
#include "ubertopo/ident/ref.h"
using namespace ubertopo::ident;
static void take_face(FaceRef) {}
int main()
{
  take_face(FaceRef(1));
  return 0;
}
