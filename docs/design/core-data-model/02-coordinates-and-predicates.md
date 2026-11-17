# 02 — Coordinates and Predicates

Part of the [core data model design](README.md). Decisions: DEC-007 makes the coordinate
type a template parameter and requires concepts. Research basis:
[cad-kernels/robustness-and-exact-arithmetic](../../research/cad-kernels/robustness-and-exact-arithmetic.md),
[attributes-and-queries/large-world-coordinates-and-tiling](../../research/attributes-and-queries/large-world-coordinates-and-tiling.md).

## 1. Goals

1. A single topology implementation serves:
   - generated content with **exact** decisions;
   - imported or scanned content in **double**;
   - light tools and tests in **float**.
2. **No epsilon in any topological decision.** Every branch that affects topology goes
   through a predicate that is exact, or adaptive-exact, for its type.
3. **Bit-identical results across platforms** for the exact instantiations. The `double`
   instantiation gets the same guarantee on its *decisions*, because adaptive predicates
   always return the exact sign of the computation on the stored doubles.
4. **Template bloat stays in the geometric layer** (see [01 §10](01-cell-complex.md#10-template-shape)).

## 2. Concepts

```cpp
namespace ubertopo::geom {

// Per-type customisation point; specialised for each supported scalar.
template <typename T> struct CoordTraits;   // primary undefined: unsupported types fail to compile

template <typename T>
concept CoordScalar = requires(T a, T b) {
  { a + b } -> std::convertible_to<T>;
  { a - b } -> std::convertible_to<T>;
  { -a } -> std::convertible_to<T>;
  { a < b } -> std::convertible_to<bool>;
  { a == b } -> std::convertible_to<bool>;
};

template <typename T>
concept CoordType = CoordScalar<T> && requires {
  typename CoordTraits<T>::Point;            // usually math::Vec<T, 3>
  typename CoordTraits<T>::Point2;           // usually math::Vec<T, 2>
  typename CoordTraits<T>::Wide;             // type that holds exact products (may equal T)
  { CoordTraits<T>::is_exact } -> std::convertible_to<bool>;
  { CoordTraits<T>::to_double(std::declval<T>()) } -> std::same_as<double>;
} && requires(const typename CoordTraits<T>::Point &p,
              const typename CoordTraits<T>::Point2 &q) {
  // Predicates return the sign (-1, 0, +1) of the exact determinant.
  { CoordTraits<T>::orient2d(q, q, q) } -> std::same_as<int>;
  { CoordTraits<T>::orient3d(p, p, p, p) } -> std::same_as<int>;
  { CoordTraits<T>::incircle(q, q, q, q) } -> std::same_as<int>;
  { CoordTraits<T>::compare(p, p, int{}) } -> std::same_as<int>;   // per-axis ordering
};

template <typename T>
concept ExactCoord = CoordType<T> && CoordTraits<T>::is_exact;

}  // namespace ubertopo::geom
```

**Notes:**
- `CoordTraits` follows the **customisation-point pattern** that litestl's `Binder<T>`
  uses. An unsupported type is caught at the first instantiation.
- Predicates are **static functions on the traits**, not free functions found by ADL. That
  keeps `float` and `double` overloads unambiguous and makes the dispatch explicit.
- Algorithms constrain themselves on the weakest concept they need. For example, CDT
  needs `CoordType`; a Boolean that must snap needs `ExactCoord` or a
  `RoundingPolicy<T>`.
- **Derived predicates** are built from the primitives *once*, generically: radial
  comparison about an axis, segment intersection classification, point-in-polygon,
  point-in-shell parity, and orientation of a point to a plane through three points.

## 3. Instantiations

| `Coord` | Point | `Wide` | Predicates | Use |
|---|---|---|---|---|
| `double` | `Vec<double,3>` | `double` (adaptive expansions internally) | **Adaptive exact** (Shewchuk-style floating-point expansions, re-implemented per DEC-002) | Imported, scanned and BIM content; default for interactive tools |
| `float` | `Vec<float,3>` | `double` | Promote to `double`, then adaptive. This is exact, because every float is a double | Tests, small fixtures, GPU-adjacent tools |
| `Lattice` (int64 fixed point) | `Vec<Lattice,3>` | `Int128` / `Int256` (litestl) | **Exact integer** determinants | Generated planar content: buildings, parcels, footprints, extrusions |
| *(option, OI-1)* `PlaneExact` | homogeneous point from three planes | `Int256`+ | Exact plane-based | Exact Booleans without vertex rounding (§5) |

### 3.1 `Lattice`

```cpp
struct Lattice {            // a strong typedef; no implicit conversions
  int64_t raw;              // value = raw * tile.quantum  (quantum stored per tile frame)
};
template <> struct CoordTraits<Lattice> {
  using Point = math::Vec<Lattice, 3>;
  using Point2 = math::Vec<Lattice, 2>;
  using Wide = litestl::Int256;
  static constexpr bool is_exact = true;
  static constexpr int input_bits = 26;     // validated on construction (see below)
  /* With 27-bit coordinate differences:
     orient3d: 3x3 det, 3 x 27-bit factors per term, 6 terms -> ~84 bits -> Int128
     incircle: 3x3 det with one 54-bit lifted column -> ~111 bits -> Int128
     insphere: 4x4 det with one 54-bit lifted column -> ~140 bits -> Int256
     (re-derive exactly in Spike 0; plane-based predicates need more, see section 5) */
};
```

- **Bit budget.** Inputs are limited to **±2²⁶ quanta** about the tile origin. The
  cad-kernels research estimate is that 26-bit inputs keep plane-based predicates inside
  256 bits. This needs re-deriving during Spike 0 for each predicate actually used.
- **Coverage.** At a 0.1 mm quantum, ±2²⁶ covers about ±6.7 km per tile. At 1 mm it
  covers about ±67 km.
- **Range checking.** `Lattice` construction from a double is range-checked. Overflow is
  a hard error, never wrap-around.
- **Arithmetic.** `Lattice` deliberately does not support multiplication or division of
  two lattice values. Those happen only inside predicates, in `Wide`. Construction
  operations that create new points, such as intersection, go through a
  `RoundingPolicy` (snap rounding) or through `PlaneExact`.

### 3.2 Why `double` is not "tolerant"

The `double` instantiation still has **no tolerance in decisions**. Its predicates answer
exactly for the stored values. Tolerance enters only through explicit, journaled
operations:
- **`weld(tol)`** on import;
- **`snap_to(Lattice frame)`** when converting;
- **LOD simplification.**

These operations record the tolerance they used in a `geom.tol` attribute, so downstream
code can see it. This is how the research's "local journaled tolerances" from
brep-topology and the "no ε" rule from cad-kernels are reconciled (D1).

## 4. Predicates

**Primitive set:**
- `orient2d`, `orient3d`: orientation of points against a line or plane.
- `incircle`, `insphere`: the Delaunay tests.
- `compare(p, q, axis)`: per-axis ordering of two points.

**Generic derived set**, written once over `CoordType`:

| Derived predicate | Used by |
|---|---|
| `side_of_plane(p, a, b, c)` | Point-in-shell, splitting, classification |
| `radial_compare(axis_a, axis_b, dir1_pts, dir2_pts)` | Radial order ([01 §4](01-cell-complex.md#4-radial-order-and-the-wedge-rule)) |
| `segment_intersection_kind(...)` | 2D arrangements, loop self-intersection checks |
| `point_in_polygon_with_holes(...)` | Containers, validation, CDT |
| `ray_parity(point, shell)` | `build_regions()` nesting |
| `lexicographic_compare` | Canonical sorting for determinism |

**Implementation home** (DEC-003): adaptive `double` predicates, wide integers and the
exact integer determinants are general-purpose, so they go into **litestl** (`math/`).
UberTopo supplies only the `CoordTraits` glue and the derived predicates.

## 5. The plane-based option (D2)

The exact plane-based Boolean line (Campen & Kobbelt; Nehring-Wirxel; EMBER) represents
each vertex as the **intersection of three planes**. Intersection vertices are then
*exact* without rounding. Two ways to fit that in, to be decided by Spike 1 (OI-1):

- **(a) A `PlaneExact` coordinate type.** `Point` is a homogeneous point with `Int256`
  components, computed from three integer planes. Every topology algorithm then works
  unchanged. The costs are large points (4×32 bytes) and slow predicates on non-Boolean
  paths.
- **(b) A plane table beside `Lattice` vertices.** Faces reference planes in a geometry
  pool ([05](05-geometry-pools.md)). Booleans run on planes internally and **round**
  their output vertices to the lattice with snap rounding. That gives smaller storage
  but reintroduces rounding (handled, but still a failure source).

The current lean is **(b) for storage, with (a) as an internal representation inside
the Boolean engine only**. That keeps `Complex<Lattice>` compact. The concept design
supports either.

## 6. Frames and conversion between instantiations

```
WGS84 / geodetic anchor  ──(double, exact formulas)──►  site ENU frame (double, metres)
site ENU  ──(integer offset + quantum)──►  tile frame
tile frame values:  Complex<double> (metres, tile-local)
                    Complex<Lattice> (raw * quantum, tile-local)
GPU: camera-relative float, computed at upload only
```

- **One `TileFrame` per complex:** origin (an integer multiple of a site grid step),
  quantum (for `Lattice`) and frame ID. Positions are always **tile-local**. Values are
  never stored in geodetic or UTM coordinates, because UTM scale error reaches 1 m/km
  ([large-world research](../../research/attributes-and-queries/large-world-coordinates-and-tiling.md)).
- **Conversions are explicit operations:**

| From → To | Operation | Exactness |
|---|---|---|
| `Lattice` → `double` | `to_double` per vertex | Exact if the quantum is a power of two; otherwise correctly rounded |
| `double` → `Lattice` | `snap_to(frame)` with weld and validation | Lossy; records the tolerance; may merge vertices (journaled) |
| `float` ↔ `double` | Widen or narrow | Narrowing is lossy and test-only |
| Tile A → tile B (same type) | Integer frame offset | Exact for `Lattice` with equal quanta |

- Converting a complex **re-runs the predicates** it depends on (radial order, regions)
  in the target type, because a lossy conversion can change orientations. The converter
  validates, then repairs or reports.

## 7. What is *not* templated on `Coord`

- **Topology links and attributes** ([03](03-attributes.md)).
- **Persistent IDs** ([04](04-identity-versions-journal.md)).
- **Analytic geometry pools** ([05](05-geometry-pools.md)). Curves and surfaces are
  evaluated in `double` in the tile frame. They are carriers whose *certified error
  bound* covers the gap to the exact vertex positions. Only the plane table in option (b)
  is `Coord`-dependent.

## 8. Testing

- **Predicate correctness:** randomised and degenerate inputs (collinear, cospherical,
  near-overflow lattice values) compared against an independent big-rational evaluator
  written for tests only.
- **Cross-instantiation:** build the same generated model in `Lattice` and in `double`
  (from exact-representable inputs) and assert identical topology and regions.
- **Platform determinism:** CI hashes predicate outcomes on fixed corpora across
  compilers and platforms. `-ffast-math` is banned in predicate translation units, and
  FMA contraction is disabled there (`-ffp-contract=off`).
