# CLAUDE.md

Project: UberTopo — a CAD kernel + procedural modeling core for modeling human and
natural environments at multiple levels of detail, used to develop and test drone
defense systems. See `docs/requirements.md`.

## Rules

* **Temporary comments** must be prefixed with `CLAUDENOTE:` so they can be stripped
  later (e.g. `// CLAUDENOTE: remove once the BVH refit lands`).
* **Debugging guide:** keep a running debugging guide in `docs/debugging.md`. Whenever
  you diagnose a non-obvious problem, add the symptom, cause and fix/technique there.
* **Design docs** go in `docs/`.
* **Research docs** go in `docs/research/`. If a research doc grows large, split it
  into smaller, topic-focused files.
* **Plans** must be saved to the repo in `docs/plans/`. Before implementing a plan,
  pressure test it with an agent that has a fresh context (one that has not seen
  the discussion that produced the plan); record the review's findings and any plan
  changes in the plan file.
* **Docs index:** maintain `docs/index.md`. Every time a doc is added, moved, renamed
  or removed under `docs/`, update the index in the same change.
