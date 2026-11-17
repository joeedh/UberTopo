# 03 — The Open Attribute System

Part of the [core data model design](README.md). Decisions: DEC-007 makes the attribute
system open, with no central switch over all types. DEC-003 puts the general-purpose
pieces in litestl. Research basis:
[attributes-and-queries/recommendations](../../research/attributes-and-queries/recommendations.md),
[attribute-propagation](../../research/attributes-and-queries/attribute-propagation.md),
[Sculptcore reference](../../research/sculptcore-brep-reference.md).

## 1. Goals and the change from Sculptcore

Sculptcore's attributes come from a closed `AttrType` bitmask (float/int vectors, bool,
byte, short, weights). They are dispatched by `type_dispatch` switches and
specialisation trampolines to avoid virtual calls. UberTopo needs:

- **Open extension:** new column classes such as per-frequency-band spectra, material
  handles, geometry references, persistent IDs and variable-length lists, added
  **without editing core code**.
- **Schema metadata** per attribute: unit, role, frame, quantity kind and policies.
- **Copy-on-write pages** for versioned snapshots ([04](04-identity-versions-journal.md)).
- **Per-element hot loops that stay non-virtual.**

What carries over from Sculptcore:
- **Paged storage** with **lazily materialised** pages.
- **Builtin typed member columns** (`BuiltinAttr<T, "name", Flags>`).
- **Packed bools.**
- The **flag** vocabulary (`TOPO`, `DERIVED`, `TEMP`, `NOCOPY`, `NOINTERP`).
- **Merge policies** at topology operations.
- The interned **side-table pool** pattern ([05](05-geometry-pools.md)).

## 2. Layering

```
litestl::attr  (generic, reusable; DEC-003)
  ColumnBase            virtual per-column interface
  Column<T, Policy>     paged COW storage for a value type T
  BoolColumn            bit-packed bools
  ColumnTypeRegistry    type key -> factory + binding descriptor
  AttrGroup             named set of columns for one domain (+ builtin members)
  InternPool<T>         interned, refcounted side table

ubertopo::attr  (domain-specific)
  Domain enum           Vertex, Edge, Coedge, Loop, Face, FaceSide, Shell, Region, Complex
  SchemaRegistry        "acoustic.absorption@2" -> column type, unit, role, kind, policies
  Propagator            applies schema policies to topology events (§6)
  column classes        Spectrum<N>, MaterialRef, GeomRef, PersistentId, ContainerRef, ...
```

## 3. Paged copy-on-write storage

```cpp
namespace litestl::attr {

template <typename T> struct Page {
  enum class State : uint8_t { Absent, Constant, Dense };
  State state = State::Absent;     // Absent: the column default
  T constant;                      // valid when State::Constant
  util::shared_ptr<T[]> data;      // valid when Dense; shared between snapshots
};

template <typename T> class PagedStore {
  util::Vector<Page<T>> pages_;    // page p holds elements [p*kPage, (p+1)*kPage)
  T default_;
public:
  const T &get(uint32_t i) const;            // never allocates
  T *mutable_page(uint32_t p);               // materialise; copy if shared (COW)
  util::Span<const T> page_span(uint32_t p); // read-only span (materialises a view of Constant/Absent lazily)
  void share_from(const PagedStore &other);  // O(pages) refcount bumps
  uint64_t page_hash(uint32_t p) const;      // content hash, cached per page
};

}  // namespace litestl::attr
```

**How it behaves:**
- **Page states.** Three page states, extending Sculptcore's `exists` flag:
  - **Absent** means the column default, and costs nothing.
  - **Constant** gives free storage for piecewise-constant attributes, such as a
    material that is the same over a whole generated building.
  - **Dense** means shared and refcounted.
- **Copy-on-write** happens at page granularity. Any write through `mutable_page` makes
  the page unique first, so a snapshot copy of a `Complex` costs O(total pages) refcount
  increments.
- **Page size** (OI-3) is `kPage = 4096` initially, matching Sculptcore, and is a
  compile-time constant. Measure 1K–16K in Spike 1: smaller pages mean cheaper
  copy-on-write for scattered edits, larger pages mean less overhead.
- **Page hashes** are cached and invalidated on write. They give the procedural cache
  content keys and cheap version diffs.
- **Determinism.** Hashes use the stable 128-bit hash ([04 §3](04-identity-versions-journal.md#3-persistent-ids))
  over a **canonical byte encoding** supplied by the column class, not raw struct bytes,
  so padding never leaks into hashes.

## 4. The column interface

```cpp
namespace litestl::attr {

struct ColumnType;                         // registry entry (name, factory, binding, size)

class ColumnBase {
public:
  virtual ~ColumnBase() = default;
  virtual const ColumnType &type() const = 0;

  // structure (called once per column per bulk operation, not per element)
  virtual void resize(uint32_t n) = 0;
  virtual void swap(uint32_t a, uint32_t b) = 0;
  virtual void permute(util::Span<const uint32_t> old_to_new) = 0;
  virtual void set_default(uint32_t i) = 0;
  virtual void copy_elem(const ColumnBase &src, uint32_t from, uint32_t to) = 0;

  // propagation: batch interface (see section 6)
  virtual void apply(const PropagationBatch &batch, const Policy &policy) = 0;

  // versions
  virtual util::UniquePtr<ColumnBase> share() const = 0;   // COW clone
  virtual uint64_t page_hash(uint32_t page) const = 0;

  // io
  virtual void write(io::Writer &w) const = 0;
  virtual bool read(io::Reader &r, int format_version) = 0;
};

template <typename T, typename Ops = ValueOps<T>>
class Column final : public ColumnBase {
public:
  PagedStore<T> store;
  // typed, non-virtual accessors used by hot loops:
  const T &operator[](uint32_t i) const { return store.get(i); }
  T &write(uint32_t i);                    // mutable_page + index
  template <typename F> void for_each_page(F &&f);         // f(Span<T>, base_index)
  template <typename F> void parallel_for_each(F &&f);     // litestl::task
  /* ...ColumnBase overrides implemented via Ops... */
};

}  // namespace litestl::attr
```

- **`ValueOps<T>`** is the per-type behaviour bundle: default, interpolate, merge and
  canonical encode. It is selected **by concept**, not by a switch:

```cpp
template <typename T> concept Lerpable = requires(T a, T b, float t) { { a + (b - a) * t } -> std::convertible_to<T>; };
template <typename T> concept TriviallyEncodable = std::is_trivially_copyable_v<T> && /* no padding */ ...;

template <typename T> struct ValueOps;                 // primary: must be specialised or match below
template <Lerpable T> struct ValueOps<T> { /* lerp-based interpolate, weighted-mean merge */ };
template <typename T> requires (!Lerpable<T>) struct ValueOps<T> { /* copy-src0 interpolate */ };
```

- **Custom column classes** subclass `ColumnBase` directly when paged-`T` storage
  doesn't fit. Examples: variable-length lists (offsets plus values), and
  struct-of-sub-columns (`name.field` addressing).
- **Dispatch cost.** Virtual calls happen per column per operation. A topology operation
  touching *k* columns makes *k* virtual calls, not *k × elements*. Element loops use the
  typed `Column<T>` they obtained once (§5).

## 5. Groups, registries and typed access

```cpp
class AttrGroup {
  util::Map<util::string, util::UniquePtr<ColumnBase>> by_name_;
  util::Vector<ColumnBase *> ordered_;          // deterministic iteration order (insertion)
public:
  template <typename Col> Col *find(const util::string &name);      // checked downcast
  template <typename Col> Col &ensure(const util::string &name, const ColumnType &t = Col::static_type());
  ColumnBase *find_any(const util::string &name);
  // structural ops forward to every column (resize, swap, permute, share...)
};
```

- **Checked downcast.** `find<Col>` compares `&col->type() == &Col::static_type()`. This
  is a pointer comparison against a registry singleton, with no RTTI dependency (litestl
  doesn't rely on RTTI). It returns `nullptr` on a type mismatch.
- **Builtin columns** use Sculptcore's pattern as typed members registered into the
  group: `BuiltinColumn<uint32_t, ".c.next", Flag::TOPO> next;`. Topology code accesses
  them as plain members, with no lookup.
- **`ColumnTypeRegistry`** maps a stable type key (e.g. `"f32x8"`,
  `"ubertopo.MaterialRef"`) to a factory and the litestl binding descriptor
  `Bind<T>()`. That gives serialisation (by type key) and TS/Python exposure without a
  central type switch. New column classes self-register with a static registrar object
  in their `.cc` file.
- **Iteration order** is insertion order (`ordered_`), never hash order, so
  serialisation and hashing are deterministic.

## 6. Propagation

Topology primitives ([01 §7](01-cell-complex.md#7-primitive-operations)) do not touch
user attributes. They emit **events**, buffered into batches per operation:

```cpp
struct PropagationEvent {
  enum Kind : uint8_t { Create, Split, Merge, Reverse, Delete } kind;
  Domain domain;
  uint32_t target;                      // new or surviving element
  util::Span<const uint32_t> sources;   // parents
  util::Span<const float> weights;      // interpolation weights or measures (area/length)
};
```

`ubertopo::attr::Propagator` walks the batch once per column and calls
`col->apply(batch, policy)`. The **policy comes from the column's schema**, with the
operator able to override it per column. Policies:

| Policy | Meaning | Default for |
|---|---|---|
| `interp` | Weighted interpolation (lerp, barycentric) | Lerpable intensive quantities (UV, temperature) |
| `copy_src0` | Copy the first parent | Non-lerpable: enums, references, IDs (IDs are handled by the identity service) |
| `area_weighted_mean` / `length_weighted_mean` | Merge by measure | **Acoustic, optical and RF coefficients** (a plain mean silently corrupts reverberation) |
| `sum` / `split_by_measure` | Extensive quantities | Mass, emission power |
| `swap_sides` | On `Reverse`, swap the FRONT and BACK values | All FaceSide columns |
| `none` | Leave the default | `TEMP` and `DERIVED` columns (derived ones are recomputed) |
| `custom` | Column-class hook | Spectra with special merge rules, weights |

Measures (areas and lengths) are computed by the operator, which has the geometry, and
passed in `weights`. The propagator never touches geometry.

## 7. Domains

| Domain | Size | Notes |
|---|---|---|
| Vertex, Edge, Coedge, Loop, Face, Shell, Region | Element capacity | Topology plus user columns |
| **FaceSide** | `2 × face capacity` | Index `2f+s`. Per-side material, absorption, region-facing annotations. Resized and permuted *with* the Face domain: face permutation π induces `2π(f)+s` |
| Complex | 1 | Global metadata, generator parameters, tile frame |

## 8. Schemas (UberTopo level)

```cpp
struct Schema {
  util::string name;          // "acoustic.absorption"
  uint16_t version;           // 2
  const ColumnType *type;     // f32x8 (octave bands 63 Hz..8 kHz)
  Unit unit;                  // dimensionless
  Role role;                  // none | position | normal | direction | color | id ...
  Kind kind;                  // intensive | extensive | categorical | identity | derived
  DomainMask domain_hint;     // FaceSide
  PolicySet policies;         // interp / split / merge / lod / reverse
  DefaultSource default_src;  // literal | via material table | computed
  util::Vector<Migration> migrations;
};
```

- **Namespaces** follow the research: `geom.*`, `topo.*`, `sem.*`, `mat.*`,
  `acoustic.*`, `optical.*`, `ir.*`, `rf.*`, `medium.*`, `road.*`, `proc.*`, `survey.*`,
  `src.*`.
- **Material lookup.** Material properties live in a **material table**, which is
  itself a columnar table. Elements carry a `MaterialRef` on the FaceSide or Region.
  Per-element spectra are overrides only. The schema's `default_src` resolves
  element → material → default.
- **Units** live in the schema, never per value. They are checked when procedural
  graphs connect columns, in a later design.
- **Computed attributes** (area, normal, dihedral angle, wedge angle for diffraction)
  are registered with a compute function and dependency versions. They live in the
  same page store and are invalidated by the topology or geometry version.

## 9. Bindings

- **Binding descriptors.** Every column type registers its binding descriptor, so the
  TS layer can enumerate a domain's columns and get typed-array views of dense pages.
  This is Sculptcore's `napi_vector_bridge` idea, applied to pages.
- **Absent and constant pages** are exposed as `{state, constant}` and are not
  materialised for a read.
- **Writes from TS** go through `write(i)`/`mutable_page`, so copy-on-write is
  preserved.
- **Timing.** The N-API work is deferred (DEC-006). This section sets the requirement
  the column interface must meet; the binding work itself comes later.

## 10. Testing

- **Propagation invariants** (from the research): extensive conservation; intensive
  integral conservation under splits; `swap_sides` involution; identity stability under
  identical regeneration.
- **Copy-on-write:** a snapshot is unchanged after any sequence of writes to the
  original; page sharing counts are as expected; page hashes are equal for equal
  content and independent of padding.
- **Open extension:** a test-only custom column class registered from a test
  translation unit round-trips through serialise, permute, propagate and share without
  any change to core code. This guards DEC-007.
