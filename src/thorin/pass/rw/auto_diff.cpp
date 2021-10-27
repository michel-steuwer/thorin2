#include "thorin/pass/rw/auto_diff.h"

#include <algorithm>
#include <string>

#include "thorin/analyses/scope.h"

namespace thorin {

template<class... Args> auto msg (const char* fmt, Args&&... args) {
#if 1
    outln(fmt,std::forward<Args&&>(args)...);
#endif
}

void debug_dump(const char* name, const Def* d) {
    msg("{} {} : {}",name,d,d->type());
}

// Sadly, we need to "unpack" the type
const Def* lit_of_type(World& world, const Def* type, u64 lit) {

    if (auto real = isa<Tag::Real>(type))
        return world.lit_real(as_lit(real->arg()), lit);
    if (auto a = type->isa<Arr>()) {
        msg("Arr");
        auto dim = a->shape()->as<Lit>()->get<uint8_t>();
        Array<const Def*> ops{dim, [&](auto i) {
            return lit_of_type(world,a->body(),lit);
                              }};
        return world.tuple(ops);
    }
//        return world.lit_real(as_lit(real->arg()), lit);
//        msg("LIT TY {}",type);
    return world.lit_int(as_lit(as<Tag::Int>(type)), lit);
}

const Def* ONE(World& world, const Def* def) { return lit_of_type(world, def, 1); }
const Def* ZERO(World& world, const Def* def) { return lit_of_type(world, def, 0); }

namespace {

class AutoDiffer {
public:
    AutoDiffer(World& world, const Def2Def src_to_dst, const Def* A, const Def* B)
        : world_{world}
        , src_to_dst_{src_to_dst}
        , idpb{}
        , A{A}
        , B{B}
    {
//        auto idpi = world_.cn_mem_flat(B, A);
        auto idpi = world_.cn_mem_ret(B, A);
        msg("IDPI {} ",idpi);
        idpb = world_.nom_lam(idpi, world_.dbg("id"));
        idpb->set_filter(world_.lit_true());
        debug_dump("A",A);
        msg("Node {} ",A->node_name());
//        debug_dump("Shape",A->as<Arr>()->shape());
//        debug_dump("Body",A->as<Arr>()->body());
        debug_dump("B",B);
//        msg("A {} ",A); // r32 or <<2::nat, r32>>
        msg("IDPB Var {} : {}",idpb->var(),idpb->var()->type());
        msg("IDPB RVar {} : {}",idpb->ret_var(),idpb->ret_var()->type());

        // use type A directly instead of doms().back()
        const Def* inner;
        if (auto a = A->isa<Arr>()) {
            dim = a->shape()->as<Lit>()->get<uint8_t>();
            msg("Arr Dim {} ",dim);
            inner=a->body();
        }else {
            dim=1;
            inner=A;
        }
        msg("Dim {} ",dim);
        Array<const Def*> ops{dim, [&](auto i) {
            return idpb->var(1, world_.dbg("a")); // z
        }};
        // msg("Nums: {}",idpi->doms().back()->as<Pi>()->num_doms());
        // msg("Nums: {}",idpi->doms().back()->as<Pi>());
        // msg("Nums: {}",idpi->codom());
        // msg("Nums: {}",idpi->num_doms());
        // msg("Nums: {}",idpi->num_codoms());
        // msg("Nums: {}",num_args);


        const Def* opArr = world_.tuple(ops);
//        debug_dump("Arr: ",world_.pack(A->arity(),world_.tuple(ops)));
        debug_dump("Arr: ",opArr);

//        idpb->set_body(world_.app(idpb->ret_var(), world_.tuple(ops)));
        idpb->set_body(world_.app(idpb->ret_var(), {idpb->mem_var(),opArr}));
//        idpb->set_body(world_.app(idpb->ret_var(), world_.tuple(merge(idpb->mem_var(),ops))));
        // idpb->set_body(world_.app(idpb->ret_var(), {idpb->mem_var(), idpb->var(1, world.dbg("a"))}));
        // idpb->set_body(world_.app(idpb->ret_var(), {idpb->mem_var(), idpb->var(1, world.dbg("a")),idpb->var(1, world.dbg("b"))}));

        ind_idpb={
            dim,
            [&](auto i) {
                Lam* ipb=world_.nom_lam(idpi, world_.dbg("id"));
                ipb->set_filter(world_.lit_true());
                Array<const Def*> ops{dim, [&](auto j) {
                    if(i==j)
                        return ipb->var(1, world_.dbg("a")); // z
                    else
                        return ZERO(world_,inner);
                }};
                const Def* opArr = world_.tuple(ops);
                ipb->set_body(world_.app(ipb->ret_var(), {ipb->mem_var(),opArr}));
                return ipb;
            }
        };
    }

    const Def* reverse_diff(Lam* src);
    const Def* forward_diff(const Def*) { throw "not implemented"; }
private:
    const Def* j_wrap(const Def* def);
    const Def* j_wrap_rop(ROp op, const Def* a, const Def* b);

    const Def* seen(const Def* src);

    // chains cn[:mem, A, cn[:mem, B]] and cn[:mem, B, cn[:mem, C]] to a toplevel cn[:mem, A, cn[:mem, C]]
    Lam* chain(Lam* a, Lam* b);

    World& world_;
    Def2Def src_to_dst_;
    Lam* idpb;
    Array<const Def*> ind_idpb; // TODO: specialize Def* to Lam*, inline in reverse_diff
    DefMap<const Def*> pullbacks_;  // <- maps a *copied* src term to its pullback function
    const Def* A;
    const Def* B;
    size_t dim;
};

// unused
Lam* AutoDiffer::chain(Lam* a, Lam* b) {
    // chaining with identity is neutral
    if (a == idpb) return b;
    if (b == idpb) return a;

    auto at = a->type()->as<Pi>();
    auto bt = b->type()->as<Pi>();

    auto A = at->doms()[1];
    auto B = bt->doms()[1];
    auto C = bt->doms()[2]->as<Pi>()->doms()[1];

    auto pi = world_.cn_mem_flat(A, C);
    auto toplevel = world_.nom_lam(pi, world_.dbg("chain"));

    auto middlepi = world_.cn({world_.type_mem(), B});
    auto middle = world_.nom_lam(middlepi, world_.dbg("chain"));

    toplevel->set_body(world_.app(a, {toplevel->mem_var(), toplevel->var(1), middle}));
    middle->set_body(world_.app(b, {middle->mem_var(), middle->var(1), toplevel->ret_var()}));

    toplevel->set_filter(world_.lit_true());
    middle->set_filter(world_.lit_true());

    return toplevel;
}


const Def* AutoDiffer::reverse_diff(Lam* src) {
    // For each param, create an appropriate pullback. It is just the identity function for each of those.
//    msg("Src Num Vars {} ",src->num_vars());
    debug_dump("src",src); // ignore 0 and 2 => only 2 (might be an array)
    for(size_t i = 0, e = src->num_vars(); i < e; ++i) {
        auto src_param = src->var(i);
        if(src_param == src->ret_var() || src_param == src->mem_var()) {
            msg("Src Not Count {} ",i);
            continue;
        }
        auto dst = src_to_dst_[src_param];
        debug_dump("start pb for ",dst);
        pullbacks_[dst] = idpb;

        // or use dim
        if (auto a = dst->type()->isa<Arr>()) {
//            auto idpi = world_.cn_mem_ret(B, A);
//            Array<const Def*> ind_idpb={
//                a->shape()->as<Lit>()->get<uint8_t>(),
//                [&](auto i) {
//                    Lam* ipb=world_.nom_lam(idpi, world_.dbg("id"));
//                    ipb->set_filter(world_.lit_true());
//                    Array<const Def*> ops{dim, [&](auto j) {
//                        if(i==j)
//                            return ipb->var(1, world_.dbg("a")); // z
//                        else
//                            return ZERO(world_,inner);
//                    }};
//                    const Def* opArr = world_.tuple(ops);
//                    ipb->set_body(world_.app(ipb->ret_var(), {ipb->mem_var(),opArr}));
//                    return ipb;
//                }
//            };
            pullbacks_[dst] = world_.tuple(ind_idpb);

//            if (auto extract = dst->isa<Extract>()) {
//                debug_dump("dst",dst);
// //                msg("dst tuple size {} ",extract->tuple()->num_ops());
// //                debug_dump("dst arg ex tuple",extract->tuple()->op(0));
//            }

//            msg("dst Node {} ",dst->node_name());
//            if (auto tuple = dst->isa<Tuple>()) {
//                // or use dim
//                for(size_t j = 0; j < tuple->num_ops(); ++j) {
//                    pullbacks_[tuple->op(j)] = ind_idpb[j];
//                }
//            }else{
//                msg("No Tuple?!");
//            }
        }else {
            pullbacks_[dst] = idpb;
        }



//        pullbacks_[dst] = world_.tuple(ind_idpb);
        debug_dump("pb is ",pullbacks_[dst]);

//        pullbacks_[dst] = ind_idpb[i];
    }
    auto dst = j_wrap(src->body());
    return dst;
}

// We implement AD in a similar way as described by Brunel et al., 2020
//  <x², λa. x'(a * 2*x)>
//       ^^^^^^^^^- pullback. The intuition is as follows:
//                            Each value x has a pullback pb_x.
//                            pb_x receives a value that was differentiated with respect to x.
//                  Thus, the "initial" pullback for parameters must be the identity function.
// Here is a very brief example of what should happen in `j_wrap` and `j_wrap_rop`:
//
//      SOURCE             |  PRIMAL VERSION OF SOURCE
//   ----------------------+-----------------------------------------------------------------------
//     // x is parameter   | // <x,x'> is parameter. x' should be something like λz.z
//    let y = 3 * x * x;   | let <y,y'> = <3 * x * x, λz. x'(z * (6 * x))>;
//    y * x                | <y * x, λz. y'(z * x) + x'(z * y)>
//
// Instead of explicitly putting everything into a pair, we just use the pullbacks freely
//  Each `x` gets transformed to a `<x, λδz. δz * (δz / δx)>`
const Def* AutoDiffer::j_wrap(const Def* def) {
//    if(isa<Tag::Mem>(def->type())) {
//        debug_dump("mem",def);
//        return def; // and pb is idbp
//    }

    if (auto dst = seen(def)) {
        debug_dump("seen",def);
        return dst;
    }

    if (auto var = def->isa<Var>()) {
        msg("Out of scope var: {} Not differentiable", var);
        THORIN_UNREACHABLE;
    }
    if (auto axiom = def->isa<Axiom>()) {
        msg("Axioms are not differentiable. Found axiom: {}", axiom);
        THORIN_UNREACHABLE;
    }
    if (auto lam = def->isa_nom<Lam>()) {
        // FIXME: pb type correct? might not be able to just use idpb->type() here
        auto old_pi = lam->type()->as<Pi>();
        auto pi = world_.cn({world_.type_mem(), old_pi->doms()[1], idpb->type()});
        auto dst = world_.nom_lam(pi, world_.dbg(lam->name()));
        src_to_dst_[lam->var()] = dst->var();
        pullbacks_[dst->var()] = dst->var(dst->num_vars() - 1);
        dst->set_filter(lam->filter());

        auto bdy = j_wrap(lam->body());
        dst->set_body(bdy);
        src_to_dst_[lam] = dst;
        pullbacks_[dst] = pullbacks_[bdy];
        return dst;
    }
    if (auto app = def->isa<App>()) {
        auto callee = app->callee();
        auto arg = app->arg();

        debug_dump("App Callee: ",callee);
        debug_dump("App Arg: ",arg);

        // remove
        // msg("Diff app: {}", app);
        // msg("Diff args: {}", arg);

        // Handle binary operations
        if (auto inner = callee->isa<App>()) {
            // Take care of binary operations
            if (auto axiom = inner->callee()->isa<Axiom>()) {
                if (axiom->tag() == Tag::ROp) {
                    // msg("Op: {}",axiom->flags());
                    msg("Arg {}",arg);
                    auto ab = j_wrap(arg);
                    auto [a, b] = ab->split<2>();
                    if(!pullbacks_.count(a) || !pullbacks_.count(b)){
                        // necessary for non-extracted components of main function argument
                        // => the array function argument has a pullback (tuple)
                        //    but the components do not (not registered)
                        // TODO: maybe move up to reverse_diff?
                        auto [pa,pb]=pullbacks_[ab]->split<2>();
                        pullbacks_[a]=pa;
                        pullbacks_[b]=pb;
                    }
                    auto dst = j_wrap_rop(ROp(axiom->flags()), a, b);
                    src_to_dst_[app] = dst;
                    return dst;
                }

                if (axiom->tag() == Tag::RCmp) {
                    auto [a, b] = j_wrap(arg)->split<2>();
                    auto dst = world_.op(RCmp(axiom->flags()), nat_t(0), a, b);
                    src_to_dst_[app] = dst;
                    // TODO: tuple or app
                    return world_.tuple({inner, dst});
                }
            }
        }

        debug_dump("arg in call",arg);
        auto ad = j_wrap(arg);
        // remove
        debug_dump("args were in call",arg);
        msg("callee: {} : {}",callee, callee->type());
        msg("ad (arg jwrap): {} : {}",ad, ad->type());
        msg("ad node type: {}",ad->node_name());
        // msg("Num outs: {}", ad->num_outs());
        const Def* ad_mem;
        const Def* ad_arg;
        if(ad->isa<Tuple>()) {
            ad_mem = world_.extract(ad, (u64)0, world_.dbg("mem"));
            ad_arg = world_.extract(ad, (u64)1, world_.dbg("arg")); // TODO: error with relu.impala
        } else {
            // TODO: if only mem
            ad_mem = ad;
            ad_arg= nullptr;
        }
            // call to then/else branch only takes memory

        auto cpi = (src_to_dst_.count(callee) ? src_to_dst_[callee]->type()->as<Pi>() : nullptr);
        if(cpi != nullptr) {
            msg("cpi is not null (callee in mapping)");
            // check if our functions returns a pullback already
            if (auto rett = cpi->doms().back()->isa<Pi>(); rett && rett->is_returning()) {
                msg("callee has node type: {}",callee->node());
//                msg("callee is Extract: {}",callee->isa<Extract>());
//                msg("callee is App: {}",callee->isa<App>());
//                msg("callee is Lam: {}",callee->isa<Lam>());
//                msg("callee is nom Lam: {}",callee->isa_nom<Lam>());
                auto cd = j_wrap(callee);
                msg("cd (callee jwrap): {} : {}",cd, cd->type());

                if (pullbacks_.count(ad)) {
                    auto dst = world_.app(cd, {ad_mem, ad_arg, pullbacks_[ad]});
                    // remove
                    // auto dst = world_.app(cd, {ad_mem, pullbacks_[ad]});
                    src_to_dst_[app] = dst;

                    pullbacks_[dst] = pullbacks_[ad];
                    return dst;
                }
                else {
                    assert(ad->num_outs() == arg->num_outs() + 1 && "Pullback must have been added here.");

                    auto dst = world_.app(cd, ad);
                    src_to_dst_[app] = dst;

                    return dst;
                }
            }
        }
        msg("No translation of callee found or pullback not available");
        if (!callee->isa_nom<Lam>() && src_to_dst_.count(callee)) {
            msg("No Lam and found in mapping");
            auto dstcallee = src_to_dst_[callee];

            auto dst = world_.app(dstcallee, {ad_mem, ad_arg, pullbacks_[ad]});
            // remove
            // auto dst = world_.app(dstcallee, {ad_mem, pullbacks_[ad]});
            pullbacks_[dst] = pullbacks_[ad]; // <- chain pullback of dstcallee?

            return dst;
        }
        msg("Nothing found for app");
        debug_dump("callee in question:",callee);
        debug_dump("ad args in question:",ad);
        auto dst_callee = world_.op_rev_diff(callee);
        auto dst = world_.app(dst_callee, ad);
        pullbacks_[dst] = pullbacks_[ad];

        return dst;
    }

    if (auto tuple = def->isa<Tuple>()) {
        debug_dump("Tuple",tuple);
        msg("Tuple NumOps {}",tuple->num_outs());
        Array<const Def*> ops{tuple->num_ops(), [&](auto i) { return j_wrap(tuple->op(i)); }};
        auto dst = world_.tuple(ops);
        src_to_dst_[tuple] = dst;

        Array<const Def*> pbs{tuple->num_ops(),
                [&](auto i) { return pullbacks_[ops[i]]; }};
        debug_dump("tuple dst",dst);
        // distinguish [mem, r32] from <<2::nat,r32>>
        // TODO: multiple arguments
        // TODO: double diff? [mem, r32,
        //      cn[mem, r32, cn[mem, r32, cn[mem, r32]]]]
        if(isa<Tag::Mem>(tuple->op(0)->type())) { // ops.size() == 2 &&
            msg("tuple mem arg");
            pullbacks_[dst] = pbs[1];
//            pullbacks_[dst] = world_.tuple(
//                {tuple->num_ops()-1, [&](auto i) { return pullbacks_[ops[i+1]]; }}
//            );
        }else{
            pullbacks_[dst] = world_.tuple(pbs);
        }
        debug_dump("pb",pullbacks_[dst]);
//        else {
//            // fallback
//            pullbacks_[dst] = idpb;
//            for (auto i : ops) {
//                if (pullbacks_.contains(i))
//                    pullbacks_[dst] = pullbacks_[i];
//            }
//        }
        return dst;
    }


    if (auto pack = def->isa<Pack>()) {
        auto dst = world_.pack(pack->type()->arity(), j_wrap(pack->body()));
        src_to_dst_[pack] = dst;
        pullbacks_[dst] = idpb;
        return dst;
    }

    if (auto extract = def->isa<Extract>()) {
        msg("ex {} : {}",extract,extract->type());
        auto jtup = j_wrap(extract->tuple());
        msg("jtup {} : {}",jtup,jtup->type());
        auto dst = world_.extract_unsafe(jtup, extract->index());
        msg("dst {} : {}",dst,dst->type());
        src_to_dst_[extract] = dst;
        // do not extract diff
        // but tuple => tuple of diffs
        // no lambda

        // everywhere else zero?
//        pullbacks_[dst] = pullbacks_[jtup];
        debug_dump("ex pb",pullbacks_[jtup]);
        pullbacks_[dst] = world_.extract_unsafe(pullbacks_[jtup], extract->index());
        debug_dump("ex pb dst",pullbacks_[dst]);
        return dst;
    }

    if (auto insert = def->isa<Insert>()) {
        auto dst = world_.insert(j_wrap(insert->tuple()), insert->index(), j_wrap(insert->value()));
        src_to_dst_[insert] = dst;
        pullbacks_[dst] = idpb;
        return dst;
    }

    if (auto lit = def->isa<Lit>()) {
        // The derivative of a literal is ZERO
//        auto zeropi = world_.cn_mem_flat(lit->type(), lit->type());
        auto zeropi = world_.cn_mem_ret(lit->type(), A);
//        msg("ZPi {}",zeropi);
        auto zeropb = world_.nom_lam(zeropi, world_.dbg("id"));
        debug_dump("zero PB",zeropb);
        zeropb->set_filter(world_.lit_true());
//        auto zero = ZERO(world_, lit->type());
        auto zero = ZERO(world_, A);// or use dim directly
        zeropb->set_body(world_.app(zeropb->ret_var(), {zeropb->mem_var(), zero}));
        pullbacks_[lit] = zeropb;
        return lit;
    }

    msg("Not handling: {}", def);
    THORIN_UNREACHABLE;
}

const Def* vec_add(World& world, size_t dim, const Def* a, const Def* b) {
    Array<const Def*> ops{dim, [&](auto i) {
            return world.op(ROp::add,(nat_t)0,
                                  world.extract(a,i),
                                  world.extract(b,i)
                                  );
        }};
    return world.tuple(ops);
//    return {a.size(), [&](auto i) {
//        return world.op(ROp::add,(nat_t)0,a[i],b[i]);
//    }};
}

Array<const Def*> collect_arguments(Def* lam) {
    return {lam->num_vars()-1, [&](auto i) { return lam->var(i+1); }};
}

const Def* AutoDiffer::j_wrap_rop(ROp op, const Def* a, const Def* b) {
    // build up pullback type for this expression
    // auto r_type = a->type();
    auto o_type = a->type();
    auto r_type = A;
//    auto pbpi = world_.cn_mem_flat(B, A);
    auto pbpi = world_.cn_mem_ret(B, A);
    msg("o_type {} ",o_type);
    msg("r_type {} ",r_type);
//    msg("apb last {} ",pullbacks_[a]->type()->as<Pi>()->doms().back());
    debug_dump("apb",pullbacks_[a]);
    debug_dump("bpb",pullbacks_[b]);
    // auto pbT = pullbacks_[a]->type()->as<Pi>()->doms().back()->as<Pi>();
    auto pbT = pullbacks_[a]->type()->as<Pi>()->doms().back()->as<Pi>(); // TODO: create using A
    auto pb = world_.nom_lam(pbpi, world_.dbg("φ"));
    msg("pbT {} ",pbT);
    msg("pbpi {} ",pbpi);
    msg("pb ret var {} : {} ",pb->ret_var(),pb->ret_var()->type());

    auto middle = world_.nom_lam(pbT, world_.dbg("φmiddle"));
    auto end = world_.nom_lam(pbT, world_.dbg("φend"));
    // auto middle = world_.nom_lam(world_.cn({world_.type_mem(), r_type}), world_.dbg("φmiddle"));
    // auto end = world_.nom_lam(world_.cn({world_.type_mem(), r_type}), world_.dbg("φend"));

    msg("middle type {}",middle->type());

    pb->set_filter(world_.lit_true());
    middle->set_filter(world_.lit_true());
    end->set_filter(world_.lit_true());

    auto one = ONE(world_, o_type);

    // Grab argument pullbacks
    assert(pullbacks_.count(a) && "Pullbacks for ROp arguments should already be created");
    assert(pullbacks_.count(b) && "Pullbacks for ROp arguments should already be created");
    auto apb = pullbacks_[a];
    auto bpb = pullbacks_[b];
    msg("ROp Pullback {} => {}",a, apb);
    msg("ROp Pullback {} : {}",apb,apb->type());
    // msg("ROp {} Pullback {} & {}",op,apb,bpb);
    switch (op) {
        // ∇(a + b) = λz.∂a(z * (1 + 0)) + ∂b(z * (0 + 1))
        case ROp::add: {
            auto dst = world_.op(ROp::add, (nat_t)0, a, b);
            pb->set_dbg(world_.dbg(pb->name() + "+"));

            pb->set_body(world_.app(apb, {pb->mem_var(), world_.op(ROp::mul, (nat_t)0, pb->var(1), one), middle}));
            middle->set_body(world_.app(bpb, {middle->mem_var(), world_.op(ROp::mul, (nat_t)0, pb->var(1), one), end}));
            auto adiff = middle->var(1);
            auto bdiff = end->var(1);

            auto sum = vec_add(world_, dim, adiff, bdiff);
            end->set_body(world_.app(pb->ret_var(), { end->mem_var(), sum}));
            pullbacks_[dst] = pb;
//            end->set_body(world_.app(pb->ret_var(), {end->mem_var(), world_.op(ROp::add, (nat_t)0, adiff, bdiff)}));

            return dst;
        }
        // ∇(a - b) = λz.∂a(z * (0 + 1)) - ∂b(z * (0 + 1))
        case ROp::sub: {
            auto dst = world_.op(ROp::sub, (nat_t)0, a, b);
            pb->set_dbg(world_.dbg(pb->name() + "-"));

            pb->set_body(world_.app(apb, {pb->mem_var(), world_.op(ROp::mul, (nat_t)0, pb->var(1), one), middle}));
            middle->set_body(world_.app(bpb, {middle->mem_var(), world_.op(ROp::mul, (nat_t)0, pb->var(1), world_.op_rminus((nat_t)0, one)), end}));
            // all args 1..n as tuple => vector for addition
             auto adiff = middle->var(1);
                // proj((const Def*) var(), num_vars(), 1, nullptr)
             auto bdiff = end->var(1);
//            auto adiffV = middle->vars().skip_front();
                // Array<const Def*>(num_vars(), [&](auto i) { return var(i); });
//            auto bdiffV = end->vars().skip_front();
//            auto adiff2=adiffV[0];
                // ptr_[0] = var(1)
//            auto adiffV = Array<const Def*>(middle->num_vars()-1, [&](auto i) { return middle->var(i+1); });
//            auto bdiffV = Array<const Def*>(end->num_vars()-1, [&](auto i) { return end->var(i+1); });
//            auto adiff=adiffV.front();
//            auto bdiff=bdiffV.front();

            // dim = middle->num_vars()-1=end.num_vars()-1
//            Array<const Def*> sum{dim, [&](auto i) {
//                return world_.op(ROp::add,(nat_t)0,adiffV[i],bdiffV[i]);
//            }};

//            msg("middle->vars {} = 1+ {}",middle->num_vars(),adiffV.size());
//            msg("sum size {}",sum.size());
//            intuitively adiff==adiff2
//            intuitively bdiff==bdiff2
//            adiff=adiff2;

//            debug_dump("adiff",adiff);
//            msg("adiff {}",adiff);
//            msg("adiff {}",adiff->type());
            // msg("adiff {}",adiff->type()->as<Sigma>());
            // msg("adiff {}",adiff->type()->as<Sigma>()->ops());

             auto sum = vec_add(world_, dim, adiff, bdiff);
             debug_dump("sum",sum);
//            end->set_body(world_.app(pb->ret_var(), {end->mem_var(), world_.op(ROp::add, (nat_t)0, adiff, bdiff)}));
            end->set_body(world_.app(pb->ret_var(), { end->mem_var(), sum}));
                           pullbacks_[dst] = pb;

            return dst;
        }
        // ∇(a * b) = λz.∂a(z * (1 * b + a * 0)) + ∂b(z * (0 * b + a * 1))
        //          potential opt: if ∂a = ∂b, do: ∂a(z * (a + b))
        //             do this in the future. We need to make sure the pb is linear.
        //             This should be doable without additional tracking if we change
        //             their types from `R -> R` to `R -> ⊥`
        case ROp::mul: {
            auto dst = world_.op(ROp::mul, (nat_t)0, a, b);
            pb->set_dbg(world_.dbg(pb->name() + "*"));

            pb->set_body(world_.app(apb, {pb->mem_var(), world_.op(ROp::mul, (nat_t)0, pb->var(1), b), middle}));
            middle->set_body(world_.app(bpb, {middle->mem_var(), world_.op(ROp::mul, (nat_t)0, pb->var(1), a), end}));
            auto adiff = middle->var(1);
            auto bdiff = end->var(1);

//            end->set_body(world_.app(pb->ret_var(), world_.tuple(merge(
//                end->mem_var(),
//                vec_add(world_,
//                        collect_arguments(middle),
//                        collect_arguments(end))))));
            auto sum = vec_add(world_, dim, adiff, bdiff);
            end->set_body(world_.app(pb->ret_var(), { end->mem_var(), sum}));
//            end->set_body(world_.app(pb->ret_var(), {end->mem_var(), world_.op(ROp::add, (nat_t)0, adiff, bdiff)}));
            pullbacks_[dst] = pb;
            return dst;
        }
        // ∇(a / b) = λz. (g* (z * h) - h* (z * g))/h²
        case ROp::div: {
            //    a*(1/b * z)          => a*(z/b)
            //  + b*(a * -b^(-2) * z)  => b*(z*a/(b*b))
            auto dst = world_.op(ROp::div, (nat_t)0, a, b);
            pb->set_dbg(world_.dbg(pb->name() + "/"));

            pb->set_body(world_.app(apb, {pb->mem_var(), world_.op(ROp::div, (nat_t)0, pb->var(1), b), middle}));
            auto za=world_.op(ROp::mul, (nat_t)0, pb->var(1), a);
            auto bsq=world_.op(ROp::mul, (nat_t)0, b, b);
            middle->set_body(world_.app(bpb, {middle->mem_var(), world_.op_rminus((nat_t)0, world_.op(ROp::div, (nat_t)0, za, bsq)), end}));
            auto adiff = middle->var(1);
            auto bdiff = end->var(1);

            auto sum = vec_add(world_, dim, adiff, bdiff);

            end->set_body(world_.app(pb->ret_var(), { end->mem_var(), sum}));
            pullbacks_[dst] = pb;
            return dst;
        }
        default:
            THORIN_UNREACHABLE;
    }
}

const Def* AutoDiffer::seen(const Def* def) { return src_to_dst_.contains(def) ? src_to_dst_[def] : nullptr; }

} // namespace

const Def* AutoDiff::rewrite(const Def* def) {
    if (auto app = def->isa<App>()) {
        if (auto type_app = app->callee()->isa<App>()) {
            if (auto axiom = type_app->callee()->isa<Axiom>(); axiom && axiom->tag() == Tag::RevDiff) {
                auto src_lam = app->arg(0)->as_nom<Lam>();
                // this should be something like `cn[:mem, r32, cn[:mem, r32]]`
                auto& world = src_lam->world();

                // We get for `A -> B` the type `A -> (B * (B -> A))`.
                //  i.e. cn[:mem, A, [:mem, B]] ---> cn[:mem, A, cn[:mem, B, cn[:mem, B, A]]]
                auto dst_pi = app->type()->as<Pi>(); // multi dim as array
                debug_dump("dst_pi",dst_pi);
                auto dst_lam = world.nom_lam(dst_pi, world.dbg("top_level_rev_diff_" + src_lam->name()));
                dst_lam->set_filter(src_lam->filter());
                auto A = dst_pi->dom(1);
                auto B = src_lam->ret_var()->type()->as<Pi>()->dom(1);

                msg("A: {}",A);
                msg("B: {}",B);
                debug_dump("src_lam",src_lam);
                debug_dump("dst_lam",dst_lam);

                // The actual AD, i.e. construct "sq_cpy"
                Def2Def src_to_dst;
                for (size_t i = 0, e = src_lam->num_vars(); i < e; ++i) {
                    auto src_param = src_lam->var(i);
                    auto dst_param = dst_lam->var(i, world.dbg(src_param->name()));
                    src_to_dst[src_param] = i == e - 1 ? dst_lam->ret_var() : dst_param;
                }
                auto differ = AutoDiffer{world, src_to_dst, A, B};
                dst_lam->set_body(world.lit_true());
                dst_lam->set_body(differ.reverse_diff(src_lam));

                // debug_dump(src_lam);
                // debug_dump(dst_lam);

                return dst_lam;
            }
        }
    }

    return def;
}

}
