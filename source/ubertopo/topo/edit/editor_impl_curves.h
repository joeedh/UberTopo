#pragma once

/* Editor<C>: curved edges and the geometry store (plan P4a W6, Â§5.2).
 * Template definitions, included only by editor.cc. */

#include <bit>
#include <cmath>

#include "ubertopo/topo/curve_geom.h"
#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/face_geom.h"
#include "ubertopo/topo/radial.h"

namespace ubertopo::topo {

namespace edit_detail {

/* doubles in op payloads: two words, low first */
inline void put_f64(litestl::util::Vector<uint32_t, 8> &u, double x)
{
  const uint64_t b = std::bit_cast<uint64_t>(x);
  u.append(uint32_t(b));
  u.append(uint32_t(b >> 32));
}
inline double get_f64(const uint32_t *w)
{
  return std::bit_cast<double>(uint64_t(w[0]) | (uint64_t(w[1]) << 32));
}
inline bool zero_bits(double x)
{
  return std::bit_cast<uint64_t>(x) == 0;
}

} // namespace edit_detail

template <geom::CoordType C>
EditCode Editor<C>::set_edge_curve(EdgeRef e, litestl::attr::PoolRef ref, double t0, double t1)
{
  Prim pr(*this, OpKind::SetEdgeCurve);
  if (pr.done) {
    return pr.code;
  }
  if (e.i >= k_.edges.capacity() || !k_.edges.live(e.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Edge, e.i);
  }
  if (ref.none()) {
    if (!edit_detail::zero_bits(t0) || !edit_detail::zero_bits(t1)) {
      return pr.fail(EditCode::BadArgument, ident::ElemKind::Edge, e.i, "a straight edge's range is (0, 0)");
    }
  }
  else {
    const char *why = nullptr;
    if (!edge_carrier(k_.geom, ref, t0, t1, nullptr, &why)) {
      return pr.fail(EditCode::BadArgument, ident::ElemKind::Edge, e.i, why);
    }
  }
  setE<".e.curve">(e, ref);
  setE<".e.range">(e, litestl::math::double2(t0, t1));
  /* the faces on e have a new skeleton (plan P4b §4.3): their coedges join
   * the commit filter, and the radial order is re-sorted about the new first
   * segment */
  if (k_.edge_coedge(e)) {
    k_.for_each_radial(e, [&](CoedgeRef c) { touch(Grp::Coedges, c.i); });
    set_dirty();
    const EditCode rc = resort_radial(e);
    if (rc != EditCode::Ok) {
      return pr.fail(rc, rep_.kind, rep_.index);
    }
  }
  pr.op.u.append(e.i);
  pr.op.u.append(ref.pool);
  pr.op.u.append(ref.slot);
  edit_detail::put_f64(pr.op.u, t0);
  edit_detail::put_f64(pr.op.u, t1);
  return pr.finish();
}

template <geom::CoordType C> EditCode Editor<C>::split_edge_at(EdgeRef e, double t, VertRef *v_out, EdgeRef *e_out)
{
  Prim pr(*this, OpKind::SplitEdgeAt);
  if (pr.done) {
    return pr.code;
  }
  if (e.i >= k_.edges.capacity() || !k_.edges.live(e.i)) {
    return pr.fail(EditCode::DeadElement, ident::ElemKind::Edge, e.i);
  }
  const litestl::attr::PoolRef ref = k_.edges.template get<".e.curve">()[e.i];
  if (ref.none()) {
    return pr.fail(EditCode::BadArgument, ident::ElemKind::Edge, e.i, "a straight edge: use split_edge");
  }
  const auto rg = k_.edges.template get<".e.range">()[e.i];
  const double t0 = rg[0], t1 = rg[1];
  if (!(std::fmin(t0, t1) < t && t < std::fmax(t0, t1))) {
    return pr.fail(EditCode::NotOnEdge, ident::ElemKind::Edge, e.i, "t is not strictly inside the range");
  }
  const auto c = edge_carrier(k_.geom, ref, t0, t1);
  if (!c) {
    return pr.fail(EditCode::BadArgument, ident::ElemKind::Edge, e.i, "the edge's carrier does not validate");
  }
  curves::Vec<3> x;
  c->eval(t, 0, &x);
  Point p;
  if (!round_point<C>(litestl::math::double3(x[0], x[1], x[2]), p)) {
    return pr.fail(EditCode::BadArgument, ident::ElemKind::Edge, e.i, "the point is not representable");
  }
  const auto vs = k_.edge_verts(e);
  if (geom::lex_compare<C>(p, k_.pos(vs[0])) == 0 || geom::lex_compare<C>(p, k_.pos(vs[1])) == 0) {
    return pr.fail(EditCode::NotOnEdge, ident::ElemKind::Edge, e.i, "the point rounds onto an end vertex");
  }
  const double frac = (t - t0) / (t1 - t0);
  VertRef v;
  EdgeRef e2;
  const EditCode sc = split_impl(pr, e, p, frac, &t, &v, &e2);
  if (sc != EditCode::Ok) {
    return sc;
  }
  if (e_out) {
    *e_out = e2;
  }
  /* the skeleton gained a corner at v: planar faces must stay planar */
  litestl::util::Vector<uint32_t, 8> faces;
  k_.for_each_radial(e, [&](CoedgeRef co) { faces.append(k_.face_of(co).i); });
  if (faces.size()) {
    set_dirty();
  }
  Skeleton<C> sk(k_);
  for (uint32_t f : faces) {
    if (k_.face_proj_axis(FaceRef(f)) < 0) {
      FacePoints<C> fv, fp;
      bool degenerate = false;
      if (!fv.gather(k_, FaceRef(f)) || !fp.gather(k_, FaceRef(f), sk) ||
          !face_planar_rule<C>(fv.view(), fp.view(), &degenerate) || degenerate)
      {
        return pr.fail(EditCode::NonPlanarFace, ident::ElemKind::Face, f);
      }
    }
  }
  /* both pieces sort about their own first skeleton segment */
  for (const EdgeRef x : {e, e2}) {
    if (x && k_.edge_coedge(x)) {
      const EditCode rc = resort_radial(x);
      if (rc != EditCode::Ok) {
        return pr.fail(rc, rep_.kind, rep_.index);
      }
    }
  }
  pr.op.u.append(e.i);
  edit_detail::put_f64(pr.op.u, t);
  if (v_out) {
    *v_out = v;
  }
  return pr.finish();
}

template <geom::CoordType C>
bool Editor<C>::collect_internal(std::span<litestl::attr::RefRoot *const> extra, const char **err)
{
  litestl::util::Vector<litestl::attr::RefRoot *, 4> roots;
  std::unique_ptr<JournalRoots<C>> jr;
  if (journal_) {
    jr = std::make_unique<JournalRoots<C>>(*journal_, k_);
    roots.append(jr.get());
  }
  for (litestl::attr::RefRoot *r : extra) {
    roots.append(r);
  }
  const GeomRoots<C> gr(k_, litestl::util::span<litestl::attr::RefRoot *const>(roots.data(), roots.size()));
  return k_.geom.collect(gr.roots(), err);
}

template <geom::CoordType C> EditCode Editor<C>::collect_geometry(std::span<litestl::attr::RefRoot *const> extra)
{
  rep_ = EditReport{};
  if (!sp_.empty()) {
    rep_.code = EditCode::OpenTransaction;
    return rep_.code;
  }
  /* a journal an outside write invalidated holds refs nothing may need */
  if (journal_ && (!journal_->done_.empty() || !journal_->undone_.empty()) &&
      k_.version() != journal_->expected_version_)
  {
    journal_->clear();
  }
  const char *err = nullptr;
  if (!collect_internal(extra, &err)) {
    rep_.code = EditCode::Invalid;
    rep_.detail = err;
    return rep_.code;
  }
  return EditCode::Ok;
}

template <geom::CoordType C> void Editor<C>::restore_snapshot(Complex<C> &&snapshot)
{
  litestl::attr::PoolStore live = std::move(k_.geom);
  k_ = std::move(snapshot);
  k_.geom = std::move(live);
}

} // namespace ubertopo::topo
