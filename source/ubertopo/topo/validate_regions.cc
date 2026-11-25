/* validate_regions: a flat, parallel pre-check, and the walk that decides and
 * reports when it fails (validate.cc). */

#include <atomic>

#include "litestl/util/task.h"
#include "ubertopo/topo/validate.h"
#include "ubertopo/topo/validate_detail_regions.h"

namespace ubertopo::topo {

namespace util = litestl::util;
namespace task = litestl::task;

namespace vdetail {

namespace {

struct Precheck {
  const ComplexBase &k;
  const VertexTopoView &v;

  /** Whether check(i) holds for every i < n. */
  template <typename F> static bool all(uint32_t n, F &&check)
  {
    std::atomic<bool> ok{true};
    task::parallel_for_chunks(util::IndexRange(0, int(n)), 4096, [&](int, util::IndexRange r) {
      for (int i : r) {
        if (!check(uint32_t(i))) {
          ok.store(false, std::memory_order_relaxed);
          return;
        }
      }
    });
    return ok.load(std::memory_order_relaxed);
  }

  bool container_ok(ident::ContainerRef c) const
  {
    switch (c.kind()) {
      case ident::ContainerKind::None:
        return true;
      case ident::ContainerKind::Face:
        return k.faces.live(c.index());
      case ident::ContainerKind::Region:
        return k.regions.live(c.index());
      default:
        return false;
    }
  }

  /* containers are live, and no vertex of a face edge has one */
  bool containers() const
  {
    const auto &ec = k.edges.get<".e.container">();
    const auto &evs = k.edges.get<".e.vs">();
    const auto &ecoedge = k.edges.get<".e.c">();
    return all(v.capacity, [&](uint32_t i) { return !attr::get_bool(v.live, i) || container_ok(v.container[i]); }) &&
           all(k.edges.capacity(), [&](uint32_t e) {
             if (!k.edges.live(e)) {
               return true;
             }
             if (!container_ok(ec[e])) {
               return false;
             }
             if (ecoedge[e] != NONE) {
               const U32x2 vs = evs[e];
               for (uint32_t j = 0; j < 2; j++) {
                 if (vs[j] < v.capacity && !v.container[vs[j]].is_none()) {
                   return false;
                 }
               }
             }
             return true;
           });
  }

  /* region 0 is the one unbounded region; the others ascend by ID, and none
   * has region 0's */
  bool regions() const
  {
    const auto &id = k.regions.get<".id">();
    const auto &flags = k.regions.get<".r.flags">();
    if (!k.regions.live(0) || !(flags[0] & region_flags::Unbounded)) {
      return false;
    }
    uint32_t last = 0;
    for (uint32_t r = 1; r < k.regions.capacity(); r++) {
      if (!k.regions.live(r)) {
        continue;
      }
      if ((flags[r] & region_flags::Unbounded) || id[r] == id[0] || (last != 0 && !(id[last] < id[r]))) {
        return false;
      }
      last = r;
    }
    return true;
  }

  /* a region's chain lists shells of that region. Such chains are disjoint,
   * so unless one is a cycle, they have at most one entry per shell */
  bool region_chains() const
  {
    const auto &first = k.regions.get<".r.shell">();
    const auto &region = k.shells.get<".s.region">();
    const auto &next = k.shells.get<".s.next">();
    uint32_t left = k.shells.capacity();
    for (uint32_t r = 0; r < k.regions.capacity(); r++) {
      if (!k.regions.live(r)) {
        continue;
      }
      for (uint32_t s = first[r]; s != NONE; s = next[s]) {
        if (!k.shells.live(s) || region[s] != r || left-- == 0) {
          return false;
        }
      }
    }
    return true;
  }

  /* a shell's region is live, its sides are in that region, and its chain is
   * its sides in ascending order. A chain in another order is valid too, but
   * nothing writes one */
  bool shells() const
  {
    const auto &sregion = k.shells.get<".s.region">();
    const auto &sfirst = k.shells.get<".s.first">();
    const auto &fshell = k.faces.get<".f.shell">();
    const auto &fregion = k.faces.get<".f.region">();
    const auto &next = k.faces.sides().get<".fs.shell_next">();
    const uint32_t NS = k.shells.capacity(), NF = k.faces.capacity();
    if (!all(NS, [&](uint32_t s) { return !k.shells.live(s) || k.regions.live(sregion[s]); }) ||
        !all(NF, [&](uint32_t f) {
          if (!k.faces.live(f)) {
            return true;
          }
          const U32x2 sh = fshell[f], reg = fregion[f];
          return k.shells.live(sh[0]) && k.shells.live(sh[1]) && sregion[sh[0]] == reg[0] && sregion[sh[1]] == reg[1];
        }))
    {
      return false;
    }
    util::Vector<uint32_t> want; /* by shell: the side its chain has next */
    want.resize(NS);
    for (uint32_t s = 0; s < NS; s++) {
      want[s] = k.shells.live(s) ? sfirst[s] : NONE;
    }
    for (uint32_t f = 0; f < NF; f++) {
      if (!k.faces.live(f)) {
        continue;
      }
      const U32x2 sh = fshell[f];
      for (uint32_t j = 0; j < 2; j++) {
        uint32_t &w = want[sh[j]];
        if (w != 2 * f + j) {
          return false;
        }
        w = next[2 * f + j];
      }
    }
    for (uint32_t s = 0; s < NS; s++) {
      if (want[s] != NONE) {
        return false;
      }
    }
    return true;
  }

  /* invariant W, with every live coedge's loop, face and radial next live */
  bool wedges() const
  {
    const auto &loop = k.coedges.get<".c.l">();
    const auto &rnext = k.coedges.get<".c.radial_next">();
    const auto &flags = k.coedges.get<".c.flags">();
    const auto &lface = k.loops.get<".l.f">();
    const auto &fregion = k.faces.get<".f.region">();
    /* by live coedge: the regions of its face's sides toward radial next and
     * previous */
    util::Vector<U32x2> reg;
    reg.resize(k.coedges.capacity());
    return all(uint32_t(reg.size()),
               [&](uint32_t c) {
                 if (!k.coedges.live(c)) {
                   return true;
                 }
                 if (!k.loops.live(loop[c]) || !k.faces.live(lface[loop[c]])) {
                   return false;
                 }
                 const U32x2 fr = fregion[lface[loop[c]]];
                 const uint32_t s = side_toward_next((flags[c] & coedge_flags::Sense) != 0);
                 reg[c] = {fr[s], fr[1 - s]};
                 return true;
               }) &&
           all(uint32_t(reg.size()), [&](uint32_t c) {
             return !k.coedges.live(c) || (k.coedges.live(rnext[c]) && reg[c][0] == reg[rnext[c]][1]);
           });
  }
};

} // namespace

bool regions_precheck(const ComplexBase &k, const VertexTopoView &v)
{
  const Precheck p{k, v};
  return p.regions() && p.region_chains() && p.shells() && p.containers() && p.wedges();
}

} // namespace vdetail

void validate_regions(const ComplexBase &k, const VertexTopoView &v, ValidationReport &rep)
{
  if (!vdetail::regions_precheck(k, v)) {
    vdetail::validate_regions_walk(k, v, rep);
  }
}

} // namespace ubertopo::topo
