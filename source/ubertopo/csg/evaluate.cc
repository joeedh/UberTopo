#include "ubertopo/csg/evaluate.h"

namespace ubertopo::csg {

PersistentId evaluation_key(const CsgTree &t, const BoolOptions &o)
{
  ident::Hasher128 h(ident::tag::BoolTree);
  h.u32(kEngineVersion);
  h.h128(csg_hash(t));
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      h.f64_canonical(o.placement.r[i][j]);
    }
    h.f64_canonical(o.placement.t[i]);
    h.f64_canonical(o.frame.origin[i]);
  }
  h.i32(o.frame.quantum_log2).u64(o.frame.frame_id).i32(o.max_quantum_shift);
  /* (in the order they were given: the first rule of a column counts) */
  for (const topo::ColumnRule &r : o.rules ? o.rules->rules() : std::span<const topo::ColumnRule>()) {
    h.u32(uint32_t(r.group)).str(r.name).i32(r.split ? int32_t(*r.split) : -1);
  }
  return h.finish();
}

CsgCode evaluate(const CsgTree &t, const OperandStore &store, const BoolOptions &options,
                 topo::Complex<geom::Lattice> &out, BoolReport &report)
{
  report = BoolReport();
  auto done = [&](CsgCode c) {
    report.code = c;
    return c;
  };
  Compiled c;
  CsgCode code = compile(t, store, c, &report.node);
  if (code != CsgCode::Ok) {
    return done(code);
  }
  report.leaf_ids = c.leaf_ids;
  report.labels = c.labels;
  report.label_source = c.label_source;
  for (uint32_t i = 0; i < c.operands.size(); i++) {
    const PersistentId id = c.leaf_ids[int(i)];
    const topo::Complex<geom::Lattice> &k = *c.operands[int(i)].k;
    if (options.validate_operands && !store.is_valid(t.leaf(id)->operand)) {
      report.node = id;
      return done(CsgCode::BadOperand);
    }
    if (k.frame.quantum_log2 != options.frame.quantum_log2) {
      report.node = id;
      return done(CsgCode::MixedFrames);
    }
  }
  Arrangement a;
  uint32_t bad = kNone;
  code = arrange({c.operands.data(), size_t(c.operands.size())}, a, &bad);
  if (code != CsgCode::Ok) {
    report.node = bad == kNone ? ident::kNoId : c.leaf_ids[int(bad)];
    return done(code);
  }
  report.arrangement = a.stats;
  report.points = a.in.points.size();
  report.facets = uint32_t(a.facets.size());
  litestl::util::Vector<Selected> selected;
  select_facets(a, c.expr, selected);
  const NameSource names = c.names();
  for (int shift = 0;; shift++) {
    OutputOptions o;
    o.placement = options.placement;
    o.frame = options.frame;
    o.quantum_shift = shift;
    o.validate = options.validate_result;
    o.names = &names;
    o.rules = options.rules;
    report.output = OutputReport();
    report.quantum_shift = shift;
    code = build_output(a, c.expr, selected, o, out, report.output);
    if (report.output.bad_leaf != kNone) {
      report.node = c.leaf_ids[int(report.output.bad_leaf)];
    }
    if (code != CsgCode::RoundingFailed || shift >= options.max_quantum_shift) {
      return done(code);
    }
  }
}

} // namespace ubertopo::csg
