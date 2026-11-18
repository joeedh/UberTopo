/* Editor<C> instantiations (plan P3a). The member definitions live in the
 * editor_impl_*.h headers, which only this file includes. */

#include "ubertopo/topo/edit/editor.h"

#include "ubertopo/topo/edit/editor_impl_cycles.h"
#include "ubertopo/topo/edit/editor_impl_face.h"
#include "ubertopo/topo/edit/editor_impl_imprint.h"
#include "ubertopo/topo/edit/editor_impl_unimprint.h"
#include "ubertopo/topo/edit/editor_impl_weld.h"
#include "ubertopo/topo/edit/editor_impl_txn.h"
#include "ubertopo/topo/edit/editor_impl_vertex_edge.h"

namespace ubertopo::topo {

const char *edit_code_name(EditCode c)
{
  switch (c) {
#define UT_CASE(x) \
  case EditCode::x: \
    return #x;
    UT_CASE(Ok)
    UT_CASE(Frozen)
    UT_CASE(NoTransaction)
    UT_CASE(OpenTransaction)
    UT_CASE(SchemaChanged)
    UT_CASE(NothingToUndo)
    UT_CASE(NothingToRedo)
    UT_CASE(JournalStale)
    UT_CASE(DeadElement)
    UT_CASE(DuplicateId)
    UT_CASE(BadArgument)
    UT_CASE(NotIsolated)
    UT_CASE(NotWire)
    UT_CASE(SameVertex)
    UT_CASE(EdgeExists)
    UT_CASE(NotOnEdge)
    UT_CASE(NotJoinable)
    UT_CASE(Incompatible)
    UT_CASE(NotInLoop)
    UT_CASE(Adjacent)
    UT_CASE(NotOuterLoop)
    UT_CASE(BadLoop)
    UT_CASE(EdgeUsedTwice)
    UT_CASE(ContainedElement)
    UT_CASE(FaceHasInclusions)
    UT_CASE(HintInUse)
    UT_CASE(UnhintedRegion)
    UT_CASE(RegionsDirty)
    UT_CASE(ReservedFaceFlags)
    UT_CASE(InclusionOnChord)
    UT_CASE(HoleOnChord)
    UT_CASE(BadRule)
    UT_CASE(NonPlanarFace)
    UT_CASE(NotProjectable)
    UT_CASE(HoleOrientation)
    UT_CASE(DegenerateLoop)
    UT_CASE(RadialTie)
    UT_CASE(Invalid)
    UT_CASE(RegionsFailed)
    UT_CASE(PropagationFailed)
    UT_CASE(ChainNotSimple)
    UT_CASE(NotEmbedded)
    UT_CASE(HoleInside)
    UT_CASE(EdgeShared)
    UT_CASE(Coincident)
#undef UT_CASE
  }
  return "?";
}

template class Editor<double>;
template class Editor<float>;
template class Editor<geom::Lattice>;

} // namespace ubertopo::topo
