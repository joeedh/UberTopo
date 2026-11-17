# Academic foundations of procedural modeling

Part of the [procedural-systems research set](README.md). This file reviews the formalisms
behind procedural modeling (grammars, L-systems, generative languages), inverse procedural
modeling, neural program synthesis for shapes, and LLM-driven procedural generation as of
late 2026. The emphasis is on what each formalism implies for an *engine* design: what state
a rule can see, how derivations are identified, and how edits interact with re-derivation.

---

## 1. Grammars

### 1.1 Shape grammars (Stiny & Gips 1972)
Shape grammars operate directly on shapes: a rule `A → B` matches any (sub)shape geometrically
equal to `A` under a similarity transform and replaces it with `B`. Matching is *emergent* —
shapes created by one rule can be recognized as unintended sub-shapes by another. This is
expressive (architectural style analysis: Palladian villas, Prairie houses) but
computationally intractable for general use (subshape matching), and essentially no
production system implements it. Reference:
[Stiny & Gips 1972](http://www.shapegrammar.org/ifip/SGBestPapers72.pdf).

### 1.2 Split grammars (Wonka et al. 2003, "Instant Architecture")
Restrict shapes to labeled boxes (with a scope) and rules to **splits** (subdivide along an
axis) and **repeats**, plus a separate **control grammar** that distributes attributes
(e.g. which floor is the ground floor) and an attribute matching system that selects rules.
Restriction makes derivation fast and predictable, and the derivation is a **tree** — a key
property for identity, since each terminal has a unique path from the root
([ACM](https://dl.acm.org/doi/10.1145/882262.882324)).

### 1.3 CGA shape (Müller et al. 2006)
CGA combines split grammars with **mass modeling** (extrude, union of volumes, roofs),
**component splits** (`comp(f)` turns a volume into faces, each a new shape with its own
scope — moving from 3D to 2D), and **occlusion queries** (a rule can ask whether its scope is
inside/overlapping other geometry — the first, limited, context sensitivity). It is sequential
in rule application but each derivation is a **shape tree** whose leaves are terminals
([ACM](https://dl.acm.org/doi/10.1145/1141911.1141931)). CGA is the language behind CityEngine
(see [survey-dcc-and-engine-systems.md](survey-dcc-and-engine-systems.md)).

### 1.4 Visual editing and locators (Lipp, Wonka, Wimmer 2008)
"Interactive visual editing of grammars for procedural architecture" introduced direct
manipulation of grammar output and the problem of **persisting local edits**: an edit to one
window must be stored against an identifier that still resolves after the grammar is changed.
They proposed *exact* and *semantic* locators (path in the derivation tree vs. a semantic
query such as "the 3rd window on the 2nd floor of the front facade")
([ACM](https://doi.org/10.1145/1360612.1360701),
[PDF](https://www.cg.tuwien.ac.at/research/publications/2008/LIPP-2008-IEV/LIPP-2008-IEV-paper.pdf)).
Follow-ups: **layers of procedural content** with **anchored assignments** that survive whole
regeneration of a city layout (Lipp et al. 2011,
[EG](https://diglib.eg.org/handle/10.1111/v30i2pp345-354),
[draft](https://www.cg.tuwien.ac.at/research/publications/2011/lipp2011a/lipp2011a-draft.pdf));
**local editing of procedural models** via *meta-locators* that analyze local spatial context
(Lipp et al. 2019, [EG](https://diglib.eg.org:443/handle/10.1111/cgf13615)). This line of work
is the most important academic input to UberTopo's override design; see
[determinism-and-identity.md](determinism-and-identity.md).

### 1.5 Generalized non-terminals and interconnected structures (Krecklau & Kobbelt)
Krecklau, Pavic and Kobbelt (2010) generalize non-terminals beyond boxes (free-form deformed
volumes, arbitrary meshes) in a grammar framework (often referred to as G²); Krecklau &
Kobbelt (2011) add **interconnected structures** — rules can create connections (beams, cables,
bridges) between non-terminals in *different* branches of the derivation, breaking the strict
tree ([CGF 2010](https://doi.org/10.1111/j.1467-8659.2010.01782.x)). Implication: once rules
can refer to other branches, evaluation order and dependency tracking matter and the
derivation becomes a DAG.

### 1.6 Shape grammars on convex polyhedra (Thaller et al. 2013)
Replace box scopes with arbitrary convex polyhedra so splits work on non-rectangular floor
plans and roofs without approximation
(Computers & Graphics, 2013 †). Relevant because UberTopo will have a real
B-rep kernel: grammar operations should be defined on general planar faces/solids, not scopes.

### 1.7 CGA++ (Schwarz & Müller 2015)
CGA++ makes **shapes and shape trees first-class values**: rules can access other shapes,
operate on groups of shapes, rewrite subtrees, spawn new trees, and use rules as first-class
values instantiated on the fly. It adds **events** and **synchronization** so that, e.g., all
floors of a building can agree on a floor height, windows can align across facades, and
context-sensitive decisions can be made after a partial derivation
([SIGGRAPH history](https://history.siggraph.org/?p=112748), DOI 10.1145/2766956). In engine
terms, CGA++ turns a grammar into a **functional program with first-class functions and
staged evaluation** — the same direction Blender reached in 5.0 with closures and bundles.

### 1.8 Procedural extrusions and generative mesh modeling
- Kelly & Wonka 2011, **procedural extrusions**: building massing and roofs from footprints
  using a weighted straight skeleton with profile curves per edge — robust for real,
  irregular GIS footprints (ACM TOG 30(2), 2011 †).
- Havemann 2005, **Generative Modeling Language (GML)**: a stack-based (PostScript-like)
  language whose primitives are **Euler operators on a B-rep** (combined B-reps with smooth/sharp
  edges), with the modeling history being the program
  (PhD thesis, TU Braunschweig, 2005 †). The closest academic precedent
  to "procedural modeling on top of a real B-rep kernel" — which is what UberTopo is.

### 1.9 Floor plans
Merrell, Schkufza & Koltun 2010: Bayesian network for room program + stochastic optimization
for layout ([ACM](https://doi.org/10.1145/1882261.1866203)). Like Infinigen Indoors, it pairs a
generative model with an optimizer — non-local, see the WFC discussion in the domain survey.

---

## 2. L-systems and growth models

- Prusinkiewicz & Lindenmayer, *The Algorithmic Beauty of Plants* (1990): parallel string
  rewriting with turtle interpretation; parametric, stochastic, context-sensitive variants
  ([ABOP](http://algorithmicbotany.org/papers/#abop)).
- Měch & Prusinkiewicz 1996, **open L-systems**: the plant exchanges information with an
  environment model (light, collision, water) each derivation step
  ([ACM](https://doi.org/10.1145/237170.237279)). Engine implication: the generator and the
  environment are in a feedback loop — evaluation is *iterative*, not a single pass, which a
  pure DAG cannot express without explicit loop/simulation constructs.
- Parish & Müller 2001, **Procedural Modeling of Cities**: an extended (self-sensitive) L-system
  grows road networks, with **global goals** (population density, road patterns) and **local
  constraints** (water, elevation, snapping to existing intersections) applied as rule
  modifiers; then lots and buildings are derived ([ACM](https://doi.org/10.1145/383259.383292)).
  The split between "proposal by a grammar" and "acceptance/adjustment by constraint functions"
  is a durable design pattern.

## 3. Surveys

- Smelik, Tutenel, Bidarra, Benes 2014, **A survey on procedural modelling for virtual worlds**
  (CGF) — terrain, vegetation, water, roads, cities, buildings; argues for integrating
  procedural methods into *declarative*, semantics-driven workflows
  ([DOI](https://doi.org/10.1111/cgf.12276)).
- Vanegas et al. 2010, *Modelling the appearance and behaviour of urban spaces* (CGF STAR)
  ([DOI](https://doi.org/10.1111/j.1467-8659.2009.01535.x)).
- Ritchie et al. 2023, **Neurosymbolic Models for Computer Graphics** (Eurographics STAR) — the
  best overview connecting procedural programs, program synthesis and learning
  ([arXiv 2304.10320](https://arxiv.org/abs/2304.10320)).

## 4. Inverse procedural modeling and controllability

Forward procedural models are easy to write and hard to control. Inverse procedural modeling
(IPM) finds rules or parameters that reproduce a target.

- **Grammar/parameter inference from examples**: Šťava et al. 2010 inferred L-systems from 2D
  vector images ([DOI](https://doi.org/10.1111/j.1467-8659.2009.01636.x)); Bokeloh et al. 2010
  linked partial symmetry to inverse procedural modeling
  ([ACM](https://doi.org/10.1145/1778765.1778841)); Martinovic & Van Gool 2013 learned facade
  grammars with Bayesian model merging
  ([CVPR](https://openaccess.thecvf.com/content_cvpr_2013/html/Martinovic_Bayesian_Grammar_Learning_2013_CVPR_paper.html)).
- **Goal-directed sampling**: Talton et al. 2011 **Metropolis procedural modeling** — MCMC over
  derivations to satisfy high-level targets (silhouette, volume)
  ([ACM](https://doi.org/10.1145/1944846.1944851)); Ritchie et al. 2015 stochastically-ordered
  sequential Monte Carlo for better control ([ACM](https://doi.org/10.1145/2766895)).
- **Urban inverse design**: Vanegas et al. 2012 optimize urban procedural model parameters to
  hit indicator targets (sun exposure, density) ([ACM](https://doi.org/10.1145/2366145.2366187)).
- **Differentiable procedural models**: inverse procedural *materials* (Hu et al. 2019; Shi et
  al. 2020 "MATch") fit node-graph parameters by gradient descent; same idea is emerging for
  geometry ([MATch](https://doi.org/10.1145/3414685.3417781)).

Relevance: UberTopo's "survey a real site" use case is an IPM problem — given GIS footprints,
LiDAR, photos, estimate procedural parameters (storey count, roof type, window rhythm, canopy
density) so the generated model matches reality. The engine should make parameters
**addressable and differentiable-or-searchable** (typed parameter spaces with bounds, no hidden
state), which is cheap to design in and expensive to retrofit.

## 5. Neural program synthesis for shapes

- **CSGNet** (Sharma et al. 2018): CNN encoder + RNN decoder emits CSG programs reproducing a
  2D/3D shape; trained with policy gradients without program labels
  ([arXiv 1712.08290](https://arxiv.org/abs/1712.08290),
  [extended 1912.11393](https://arxiv.org/abs/1912.11393)).
- **ShapeAssembly** (Jones et al. 2020): a DSL of cuboid parts attached to each other; a
  hierarchical VAE writes programs learned from PartNet
  ([arXiv 2009.08026](https://arxiv.org/abs/2009.08026),
  [project](https://rkjones4.github.io/shapeAssembly)).
- **ShapeMOD** (Jones et al. 2021): discovers reusable macros (abstractions) from program
  corpora ([arXiv 2104.06392](https://arxiv.org/abs/2104.06392)).
- **PLAD** (Jones et al. 2022): pseudo-labeling for shape-program inference across CSG and
  ShapeAssembly ([arXiv 2011.13045](https://arxiv.org/abs/2011.13045)).
- **GeoCode** (Pearl et al. 2023): maps point clouds/sketches into the parameter space of a
  hand-written Blender Geometry Nodes program — interpretable, editable reconstruction
  ([arXiv 2212.01735](https://arxiv.org/abs/2212.01735)).

Lesson: learned systems work best when the target language is **small, typed, and
compositional** with good macros — big imperative APIs (Blender Python) are hard targets.

## 6. LLM-driven procedural generation (state as of late 2026)

Generations of work:

1. **LLM drives an existing procedural generator via parameters/API calls.**
   *3D-GPT* (2023) — agents choose Infinigen function parameters
   ([arXiv 2310.12945](https://arxiv.org/abs/2310.12945)); *SceneX* / *CityX* — LLM planners
   call Blender PCG APIs for terrain, roads, buildings
   ([arXiv 2403.15698](https://arxiv.org/abs/2403.15698)); *Scene Co-pilot* — LLM produces
   Infinigen scene commands with human-in-the-loop refinement
   ([arXiv 2411.18644](https://arxiv.org/abs/2411.18644)); *UnrealLLM* (ACL Findings 2025)
   targets Unreal PCG ([ACL](https://preview.aclanthology.org/display_plenaries/2025.findings-acl.994.pdf)).
2. **LLM writes scene/asset code.** *SceneCraft* (ICML 2024) writes Blender Python with a
   scene-graph plan and a vision-feedback loop ([arXiv 2403.01248](https://arxiv.org/abs/2403.01248));
   *LayoutGPT* ([arXiv 2305.15393](https://arxiv.org/abs/2305.15393)) and *Holodeck* (CVPR 2024;
   LLM proposes spatial-relation constraints, a solver places objects,
   [arXiv 2312.09067](https://arxiv.org/abs/2312.09067)); *LL3M* (2025) multi-agent
   plan/retrieve/write/debug of Blender scripts, noting that naive LLM Blender scripting fails
   often ([arXiv 2508.08228](https://arxiv.org/abs/2508.08228)); *MeshCoder* (2025) trains an
   LLM to emit structured Blender code from point clouds
   ([arXiv 2508.14879](https://arxiv.org/abs/2508.14879)); *ShapeCraft* (NeurIPS 2025) uses a
   graph-based procedural shape representation ([arXiv 2510.17603](https://arxiv.org/abs/2510.17603)).
3. **LLM writes a compact procedural IR, compiled to a target.** *Proc3D* (Jan 2026) generates a
   compact graph representation compiled to Blender Geometry Nodes or Unity; reported compile
   rates are much higher than direct-script baselines
   ([arXiv 2601.12234](https://arxiv.org/abs/2601.12234)). *Procedura* (2026) has an LLM write
   CSG "procedural assemblies" with typed, machine-checkable mates between named parts
   ([arXiv 2608.26238](https://arxiv.org/abs/2608.26238)) *(recent; verify)*.

Survey: *3D Scene Generation: A Survey* (2025) covers LLM-to-procedural approaches
([arXiv 2505.05474](https://arxiv.org/abs/2505.05474)).

Consistent findings across this literature:
- LLMs are good at **high-level structure, semantic intent, and parameter selection**; they are
  poor at low-level geometric correctness. Pairing them with **constraint solvers** and
  **typed, validated IRs** dramatically raises success rates (Holodeck, Proc3D, Procedura).
- Verbose, side-effectful host APIs (bpy) are brittle targets; small typed DSLs with good
  error messages and named, semantically meaningful parts are good targets.
- Visual or programmatic **feedback loops** (render → critique → edit) need fast, incremental
  re-evaluation and stable identities so that an edit "make the third building taller" can be
  addressed and applied without regenerating everything.

Implication for UberTopo: design the procedural IR to have a **canonical textual form** that is
compact, typed, diffable and validated with precise error messages; expose semantic, stable
names for generated parts; and keep the constraint solver as a separate declarative layer. This
makes the system LLM-authorable as a by-product of being well-designed.

---

## References

Entries marked † were supplied from memory after this session's web-search budget ran out;
verify the DOI before citing. Everything else was located by web search during this research.

- Stiny & Gips 1972, Shape Grammars and the Generative Specification of Painting and Sculpture: http://www.shapegrammar.org/ifip/SGBestPapers72.pdf †
- Wonka et al. 2003, Instant Architecture: https://dl.acm.org/doi/10.1145/882262.882324
- Müller et al. 2006, Procedural Modeling of Buildings: https://dl.acm.org/doi/10.1145/1141911.1141931
- Lipp, Wonka, Wimmer 2008, Interactive Visual Editing of Grammars for Procedural Architecture: https://doi.org/10.1145/1360612.1360701 †
- Lipp et al. 2011, Interactive Modeling of City Layouts using Layers of Procedural Content: https://diglib.eg.org/handle/10.1111/v30i2pp345-354
- Lipp et al. 2019, Local Editing of Procedural Models: https://diglib.eg.org:443/handle/10.1111/cgf13615
- Krecklau, Pavic, Kobbelt 2010, Generalized Use of Non-Terminal Symbols for Procedural Modeling: https://doi.org/10.1111/j.1467-8659.2010.01782.x †
- Thaller et al. 2013, Shape grammars on convex polyhedra, Computers & Graphics 37(6) † (DOI not verified)
- Schwarz & Müller 2015, Advanced Procedural Modeling of Architecture (CGA++): https://history.siggraph.org/?p=112748
- Kelly & Wonka 2011, Interactive Architectural Modeling with Procedural Extrusions, ACM TOG 30(2) † (likely https://doi.org/10.1145/1944846.1944854)
- Havemann 2005, Generative Mesh Modeling, PhD thesis, TU Braunschweig † (no verified URL)
- Merrell, Schkufza, Koltun 2010, Computer-Generated Residential Building Layouts: https://doi.org/10.1145/1882261.1866203 †
- Prusinkiewicz & Lindenmayer 1990, The Algorithmic Beauty of Plants: http://algorithmicbotany.org/papers/#abop
- Měch & Prusinkiewicz 1996, Visual Models of Plants Interacting with Their Environment: https://doi.org/10.1145/237170.237279 †
- Parish & Müller 2001, Procedural Modeling of Cities: https://doi.org/10.1145/383259.383292
- Smelik et al. 2014, A Survey on Procedural Modelling for Virtual Worlds: https://doi.org/10.1111/cgf.12276
- Vanegas et al. 2010, Modelling the Appearance and Behaviour of Urban Spaces: https://doi.org/10.1111/j.1467-8659.2009.01535.x †
- Ritchie et al. 2023, Neurosymbolic Models for Computer Graphics: https://arxiv.org/abs/2304.10320
- Šťava et al. 2010, Inverse Procedural Modeling by Automatic Generation of L-systems: https://doi.org/10.1111/j.1467-8659.2009.01636.x †
- Bokeloh, Wand, Seidel 2010, A Connection between Partial Symmetry and Inverse Procedural Modeling: https://doi.org/10.1145/1778765.1778841 †
- Martinovic & Van Gool 2013, Bayesian Grammar Learning for Inverse Procedural Modeling: https://openaccess.thecvf.com/content_cvpr_2013/html/Martinovic_Bayesian_Grammar_Learning_2013_CVPR_paper.html
- Talton et al. 2011, Metropolis Procedural Modeling: https://doi.org/10.1145/1944846.1944851 †
- Ritchie et al. 2015, Controlling Procedural Modeling Programs with SOSMC: https://doi.org/10.1145/2766895 †
- Vanegas et al. 2012, Inverse Design of Urban Procedural Models: https://doi.org/10.1145/2366145.2366187 †
- Shi et al. 2020, MATch: https://doi.org/10.1145/3414685.3417781 †
- Sharma et al., CSGNet: https://arxiv.org/abs/1712.08290 ; https://arxiv.org/abs/1912.11393
- Jones et al. 2020, ShapeAssembly: https://arxiv.org/abs/2009.08026
- Jones et al. 2021, ShapeMOD: https://arxiv.org/abs/2104.06392
- Jones et al., PLAD: https://arxiv.org/abs/2011.13045
- Pearl et al. 2023, GeoCode: https://arxiv.org/abs/2212.01735
- Sun et al. 2023, 3D-GPT: https://arxiv.org/abs/2310.12945
- SceneX: https://arxiv.org/abs/2403.15698
- Scene Co-pilot: https://arxiv.org/abs/2411.18644
- UnrealLLM (ACL Findings 2025): https://preview.aclanthology.org/display_plenaries/2025.findings-acl.994.pdf
- Hu et al. 2024, SceneCraft: https://arxiv.org/abs/2403.01248
- Feng et al. 2023, LayoutGPT: https://arxiv.org/abs/2305.15393
- Yang et al. 2024, Holodeck: https://arxiv.org/abs/2312.09067
- LL3M: https://arxiv.org/abs/2508.08228
- MeshCoder: https://arxiv.org/abs/2508.14879
- ShapeCraft: https://arxiv.org/abs/2510.17603
- Proc3D: https://arxiv.org/abs/2601.12234
- Procedura: https://arxiv.org/abs/2608.26238
- 3D Scene Generation: A Survey: https://arxiv.org/abs/2505.05474
