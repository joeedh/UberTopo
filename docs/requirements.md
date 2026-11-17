I need to create a system to model human and natural environments at a variety of levels of detail.  This will require a decent CAD kernel and a procedural modeling core. 

Environments include:
* All building types.
* Forests, swamps, deserts, etc.
* Road networks
* Physical infrastructure.

Be skeptical of the design of existing systems, many are built on foundations written in the 90s.  At the very least we will need:

* A general non-manifold brep with holes in faces.  Investigate whether a 2-manifold is enough or will we need a 3-manifold.  BMesh with holes is a typical design.
* Arbitrary attribute data.  Note we may need to embedd plane/space curves and surface data into the brep so keep that in mind (e.g. to tesselate road networks).  Also note that curves in particular will likely be numerically integrated transcendental curves (e.g. spiros, polynomial clothoids, simply clothoids) tesselated into polynomial basis splines.
* Various geometry query mechanisms.
