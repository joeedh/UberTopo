#include "ubertopo/topo/schema.h"

namespace ubertopo::topo {

void register_column_types()
{
  /* Column<T>::static_type() registers T's column class (idempotent). */
  (void)attr::Column<geom::Point<double>>::static_type();
  (void)attr::Column<geom::Point<float>>::static_type();
  (void)attr::Column<geom::Point<geom::Lattice>>::static_type();
  (void)attr::Column<ident::ContainerRef>::static_type();
  (void)attr::Column<GeomRef>::static_type();
  (void)attr::Column<U32x2>::static_type();
  (void)attr::Column<U32x4>::static_type();
  (void)attr::Column<ident::PersistentId>::static_type();
  (void)attr::BoolColumn::static_type();
}

} // namespace ubertopo::topo
