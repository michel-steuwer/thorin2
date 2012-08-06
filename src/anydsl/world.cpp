#include "anydsl/world.h"

#include <queue>
#include <boost/unordered_set.hpp>

#include "anydsl/def.h"
#include "anydsl/primop.h"
#include "anydsl/lambda.h"
#include "anydsl/literal.h"
#include "anydsl/type.h"
#include "anydsl/fold.h"
#include "anydsl/util/array.h"

// debug includes
#include "anydsl/order.h"
#include "anydsl/dom.h"
#include "anydsl/printer.h"

#define ANYDSL_NO_U_TYPE \
    case PrimType_u1: \
    case PrimType_u8: \
    case PrimType_u16: \
    case PrimType_u32: \
    case PrimType_u64: ANYDSL_UNREACHABLE;

#define ANYDSL_NO_F_TYPE \
    case PrimType_f32: \
    case PrimType_f64: ANYDSL_UNREACHABLE;

namespace anydsl {

/*
 * helpers
 */

static bool isCommutative(ArithOpKind kind) {
    switch (kind) {
        case ArithOp_add:
        case ArithOp_mul:
            return true;
        default:
            return false;
    }
}

static RelOpKind normalizeRel(RelOpKind kind, bool& swap) {
    swap = false;
    switch (kind) {
        case RelOp_cmp_ugt: swap = true; return RelOp_cmp_ult;
        case RelOp_cmp_uge: swap = true; return RelOp_cmp_ule;
        case RelOp_cmp_sgt: swap = true; return RelOp_cmp_slt;
        case RelOp_cmp_sge: swap = true; return RelOp_cmp_sle;

        case RelOp_fcmp_ogt: swap = true; return RelOp_fcmp_olt;
        case RelOp_fcmp_oge: swap = true; return RelOp_fcmp_ole;
        case RelOp_fcmp_ugt: swap = true; return RelOp_fcmp_ult;
        case RelOp_fcmp_uge: swap = true; return RelOp_fcmp_ule;
        default: return kind;
    }
}

static void examineDef(const Def* def, FoldValue& v) {
    if (def->isa<Undef>())
        v.kind = FoldValue::Undef;
    else if (def->isa<Error>())
        v.kind = FoldValue::Error;
    if (const PrimLit* lit = def->isa<PrimLit>()) {
        v.kind = FoldValue::Valid;
        v.box = lit->box();
    }
}

/*
 * constructor and destructor
 */

World::World()
    : defs_(1031)
    , unit_ (find(new Sigma(*this, Array<const Type*>(0))))
    , pi0_  (find(new Pi   (*this, Array<const Type*>(0))))
#define ANYDSL_U_TYPE(T) ,T##_(find(new PrimType(*this, PrimType_##T)))
#define ANYDSL_F_TYPE(T) ANYDSL_U_TYPE(T)
#include "anydsl/tables/primtypetable.h"
{
    live_.insert(unit_);
    live_.insert(pi0_);
    for (size_t i = 0; i < Num_PrimTypes; ++i)
        live_.insert(primTypes_[i]);
}

World::~World() {
#ifdef NDEBUG
    for_all (def, defs_)
        delete def;
#else
    live_.clear();
    reachable_.clear();
    cleanup();
    anydsl_assert(defs_.empty(), "cleanup should catch everything");
#endif
}

/*
 * types
 */

Sigma* World::namedSigma(size_t num, const std::string& name /*= ""*/) {
    Sigma* s = new Sigma(*this, num);
    s->debug = name;

    anydsl_assert(defs_.find(s) == defs_.end(), "must not be inside");
    defs_.insert(s);

    return s;
}

/*
 * literals
 */

const PrimLit* World::literal(PrimTypeKind kind, Box value) {
    return find(new PrimLit(type(kind), value));
}

const PrimLit* World::literal(PrimTypeKind kind, int value) {
    switch (kind) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(T(value));
#define ANYDSL_F_TYPE(T) ANYDSL_U_TYPE(T)
#include "anydsl/tables/primtypetable.h"
    }
}

const Undef* World::undef(const Type* type) {
    return find(new Undef(type));
}

const Error* World::error(const Type* type) {
    return find(new Error(type));
}

/*
 * create
 */

const Def* World::tuple(ArrayRef<const Def*> args) {
    return find(new Tuple(*this, args));
}

const Def* World::arithop(ArithOpKind kind, const Def* a, const Def* b) {
    PrimTypeKind rtype = a->type()->as<PrimType>()->primtype_kind();

    // error op error -> error
    if (a->isa<Error>() || b->isa<Error>()) 
        return error(rtype);

    // a / undef -> error
    // a % undef -> error
    if (ArithOp::isDivOrRem(kind) && b->isa<Undef>())
        return error(rtype);

    // undef op undef -> undef
    if (a->isa<Undef>() && b->isa<Undef>())
        return undef(rtype);

    if (a->isa<Undef>() || b->isa<Undef>()) {
        switch (kind) {
            // a and undef -> 0
            // undef and b -> 0
            case ArithOp_and: return zero(rtype);
        }
    }


    // a op undef -> undef
    // undef op b -> undef
    if (a->isa<Undef>() || b->isa<Undef>())
        return undef(a->type());

    const PrimLit* llit = a->isa<PrimLit>();
    const PrimLit* rlit = b->isa<PrimLit>();

    if (llit && rlit) {
        Box l = llit->box();
        Box r = rlit->box();
        PrimTypeKind type = llit->primtype_kind();

        switch (kind) {
            case ArithOp_add:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(T(l.get_##T() + r.get_##T())));
#define ANYDSL_F_TYPE(T) ANYDSL_U_TYPE(T)
#include "anydsl/tables/primtypetable.h"
                }
            case ArithOp_sub:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(T(l.get_##T() - r.get_##T())));
#define ANYDSL_F_TYPE(T) ANYDSL_U_TYPE(T)
#include "anydsl/tables/primtypetable.h"
                }
            case ArithOp_mul:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(T(l.get_##T() * r.get_##T())));
#define ANYDSL_F_TYPE(T) ANYDSL_U_TYPE(T)
#include "anydsl/tables/primtypetable.h"
                }
            case ArithOp_udiv:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(T(l.get_##T() / r.get_##T())));
#include "anydsl/tables/primtypetable.h"
                    ANYDSL_NO_F_TYPE;
                }
            case ArithOp_sdiv:
                switch (type) {
#define ANYDSL_U_TYPE(T) \
                    case PrimType_##T: { \
                        typedef make_signed<T>::type S; \
                        return literal(type, Box(bcast<T , S>(bcast<S, T >(l.get_##T()) / bcast<S, T >(r.get_##T())))); \
                    }
#include "anydsl/tables/primtypetable.h"
                    ANYDSL_NO_F_TYPE;
                }
            case ArithOp_fdiv:
                switch (type) {
#define ANYDSL_F_TYPE(T) case PrimType_##T: return literal(type, Box(T(l.get_##T() / r.get_##T())));
#include "anydsl/tables/primtypetable.h"
                    ANYDSL_NO_U_TYPE;
                }
        }
    }

    if (isCommutative(kind))
        if (a > b)
            std::swap(a, b);

    return find(new ArithOp(kind, a, b));
}

const Def* World::relop(RelOpKind kind, const Def* a, const Def* b) {
    if (a->isa<Error>() || b->isa<Error>()) 
        return error(type_u1());

    // TODO undef

    const PrimLit* llit = a->isa<PrimLit>();
    const PrimLit* rlit = b->isa<PrimLit>();

    if (llit && rlit) {
        Box l = llit->box();
        Box r = rlit->box();
        PrimTypeKind type = llit->primtype_kind();

        switch (kind) {
            case Node_cmp_eq:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(l.get_##T() == r.get_##T()));
#include "anydsl/tables/primtypetable.h"
                    ANYDSL_NO_F_TYPE;
                }
            case Node_cmp_ne:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(l.get_##T() != r.get_##T()));
#include "anydsl/tables/primtypetable.h"
                    ANYDSL_NO_F_TYPE;
                }
            case Node_cmp_ult:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(l.get_##T() <  r.get_##T()));
#include "anydsl/tables/primtypetable.h"
                    ANYDSL_NO_F_TYPE;
                }
            case Node_cmp_ule:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(l.get_##T() <= r.get_##T()));
#include "anydsl/tables/primtypetable.h"
                    ANYDSL_NO_F_TYPE;
                }
            case Node_cmp_ugt:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(l.get_##T() >  r.get_##T()));
#include "anydsl/tables/primtypetable.h"
                    ANYDSL_NO_F_TYPE;
                }
            case Node_cmp_uge:
                switch (type) {
#define ANYDSL_U_TYPE(T) case PrimType_##T: return literal(type, Box(l.get_##T() >= r.get_##T()));
#include "anydsl/tables/primtypetable.h"
                    ANYDSL_NO_F_TYPE;
                }
        }
    }

    bool swap;
    kind = normalizeRel(kind, swap);
    if (swap)
        std::swap(a, b);

    return find(new RelOp(kind, a, b));
}

const Def* World::extract(const Def* tuple, size_t index) {
    // TODO folding
    return find(new Extract(tuple, index));
}

const Def* World::insert(const Def* tuple, size_t index, const Def* value) {
    // TODO folding
    return find(new Insert(tuple, index, value));
}


const Def* World::select(const Def* cond, const Def* a, const Def* b) {
    if (cond->isa<Error>() || a->isa<Error>() || b->isa<Error>())
        return error(a->type());

    if (a->isa<Undef>())
        if (b->isa<Undef>())
            return undef(a->type()); // select cond, undef, undef -> undef
        else
            return b;                // select cond, undef, b  -> b
    else if (b->isa<Undef>())
            return a;                // select cond, a,  undef -> a

    // don't do anything for select undef, a, b

    if (const PrimLit* lit = cond->isa<PrimLit>())
        return lit->box().get_u1().get() ? a : b;

    return find(new Select(cond, a, b));
}

const Lambda* World::finalize(Lambda*& lambda) {
    anydsl_assert(lambda->type(), "must be set");
    anydsl_assert(lambda->pi(),   "must be a set pi type");

    const Lambda* l = find<Lambda>(lambda);
    assert(l == lambda);
    assert(defs_.find(l) != defs_.end());
    // some day...
    //lambda = 0;

    return l;
}

const Param* World::param(const Type* type, const Lambda* parent, size_t index) {
    return find(new Param(type, parent, index));
}

void World::jump(Lambda*& lambda, const Def* to, ArrayRef<const Def*> args) {
    lambda->alloc(args.size() + 1);

    lambda->setOp(0, to);

    size_t x = 1;
    for_all (arg, args)
        lambda->setOp(x++, arg);

    finalize(lambda);
}

void World::branch(Lambda*& lambda, const Def* cond, const Def* tto, const Def*  fto) {
    return jump(lambda, select(cond, tto, fto), Array<const Def*>(0));
}


/*
 * optimizations
 */

void World::setLive(const Def* def) {
    live_.insert(def);
}

void World::setReachable(const Lambda* lambda) {
    assert(defs_.find(lambda) != defs_.end());
    reachable_.insert(lambda);
}

void World::dce() {
    // mark all as dead
    unmark();

    // find all live values
    for_all (def, live_)
        dce_insert(def);

    // kill the living dead
    DefSet::iterator i = defs_.begin();
    while (i != defs_.end()) {
        const Def* def = *i;
        if (!def->flag_) {
            destroy(def);
            i = defs_.erase(i);
        } else
            ++i;
    }
}

void World::dce_insert(const Def* def) {
    if (def->flag_)
        return;

    def->flag_ = true;

    if (const Type* type = def->type())
        dce_insert(type);

    for_all (op, def->ops())
        dce_insert(op);

    if (const Lambda* lambda = def->isa<Lambda>()) {
        // insert control-dependent lambdas
        for_all (caller, lambda->callers())
            dce_insert(caller);
    } else if (const Param* param = def->isa<Param>()) {
        for_all (op, param->phiOps()) {
            // look through "phi-args"
            dce_insert(op.def());
            dce_insert(op.from());
        }
    }
}

void World::uce() {
    Reachable reachable;

    // mark all as unreachable
    unmark();

    // find all reachable lambdas
    for_all (lambda, reachable_)
        uce_insert(reachable, lambda);

    // destroy all unreachable lambdas
    DefSet::iterator i = defs_.begin();
    while (i != defs_.end()) {
        if (const Lambda* lambda = (*i)->isa<Lambda>()) {
            if (!lambda->flag_) {
                destroy(lambda);
                i = defs_.erase(i);
                continue;
            }
        }
        ++i;
    }
}

void World::uce_insert(Reachable& reachable, const Lambda* lambda) {
    assert(defs_.find(lambda) != defs_.end());

    if (lambda->flag_)
        return;

    lambda->flag_ = true;
    reachable.insert(lambda);

    for_all (succ, lambda->succ())
        uce_insert(reachable, succ);
}

void World::cleanup() {
    uce();
    dce();
}

void World::unmark() {
    for_all (def, defs_)
        def->flag_ = false;
}

void World::destroy(const Def* def) {
    Live::iterator i = live_.find(def);
    if (i != live_.end())
        live_.erase(i);

    if (const Lambda* lambda = def->isa<Lambda>()) {
        Reachable::iterator i = reachable_.find(lambda);
        if (i != reachable_.end())
            reachable_.erase(i);
    }

    delete def;
}

const Def* World::findDef(const Def* def) {
    DefSet::iterator i = defs_.find(def);
    if (i != defs_.end()) {
        anydsl_assert(!def->isa<Lambda>(), "must not be a lambda");
        delete def;
        anydsl_assert(defs_.find(*i) != defs_.end(), "hash/equal function of def class incorrect");
        return *i;
    }

    defs_.insert(def);
    anydsl_assert(defs_.find(def) != defs_.end(), "hash/equal function of def class incorrect");

    return def;
}

/*
 * other
 */

void World::dump(bool fancy) {
    unmark();

    for_all (def, defs_) {
        if (const Lambda* lambda = def->isa<Lambda>()) {
            if (lambda->flag_)
                continue;

            std::queue<const Lambda*> queue;
            queue.push(lambda);
            lambda->flag_ = true;

            while (!queue.empty()) {
                const Lambda* cur = queue.front();
                queue.pop();

                cur->dump(fancy);
                std::cout << std::endl;

                for_all (succ, cur->succ()) {
                    if (!succ->flag_) {
                        succ->flag_ = true;
                        queue.push(succ);
                    }
                }
            }
        }
    }
}

Params World::findParams(const Lambda* lambda) {
    Params result;

    const Pi* pi = lambda->pi();
    size_t num = pi->elems().size();

    for (size_t i = 0; i < num; ++i) {
        Param param(pi->elem(i), lambda, i);

        DefSet::iterator j = defs_.find(&param);
        if (j != defs_.end())
            result.push_back((*j)->as<Param>());
    }

    return result;
}

Def* World::release(const Def* def) {
    DefSet::iterator i = defs_.find(def);
    anydsl_assert(i != defs_.end(), "must be found");
    assert(def == *i);
    defs_.erase(i);

    return const_cast<Def*>(*i);
}

/*
 * debug printing
 */

void World::printPostOrder() {
    PostOrder order(*defs_.begin());
    for(PostOrder::iterator it = order.begin(), e = order.end();
        it != e; ++it) {
        const Def* d = *it;
        if(d->isa<Lambda>()) {
            d->dump(false);
        }
    }
}

void World::printReversePostOrder() {
    PostOrder order(*defs_.begin());
    for(PostOrder::reverse_iterator it = order.rbegin(), e = order.rend();
        it != e; ++it) {
        const Def* d = *it;
        if(d->isa<Lambda>()) {
            d->dump(false);
        }
    }
}

void World::printDominators() {
    Dominators doms(*defs_.begin());
    for(Dominators::iterator it = doms.begin(), e = doms.end();
        it != e; ++it) {
        const Def* d = it->first;
        const Def* t = it->second;
        if(d->isa<Lambda>()) {
            Printer p(std::cout, false);
            t->vdump(p);
            std::cout << " --> ";
            d->vdump(p);
            std::cout << std::endl;
        }
    }
}

} // namespace anydsl
