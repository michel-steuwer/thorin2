#pragma once

#include <queue>

#include "thorin/phase/phase.h"

#include "dialects/clos/clos.h"
#include "dialects/mem/mem.h"


namespace thorin::clos {

using DefQueue = std::deque<const Def*>;

class HigherOrderScalerize : public RWPass<HigherOrderScalerize, Lam> {
public:
    enum Mode{
        Flat, Arg
    };

    HigherOrderScalerize(PassMan& man, Mode mode)
        : RWPass(man, "higher_order_scalerize"),
            mode_(mode) {}

    void enter() override;

private:
    /// Recursively rewrites a Def.
    const Def* rewrite(const Def* def);
    const Def* rewrite_convert(const Def* def);

    const Def* convert(const Def* def);
    const Def* convert_ty(const Def* ty);
    const Def* flatten_ty(const Def* ty);
    const Def* flatten_ty_convert(const Def* ty);
    void aggregate_sigma(const Def* ty, DefQueue& ops);
    //const Def* make_scalar_inv(const Def* def, const Def* ty);

    Def2Def old2new_;
    std::stack<Lam*> worklist_;
    Mode mode_;
    Def2Def old2flatten_;
};

} // namespace thorin::clos
