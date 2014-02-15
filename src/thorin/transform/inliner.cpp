#include "thorin/lambda.h"
#include "thorin/world.h"
#include "thorin/analyses/scope.h"
#include "thorin/analyses/verify.h"
#include "thorin/analyses/top_level_scopes.h"
#include "thorin/transform/mangle.h"

namespace thorin {

void inliner(World& world) {
    for (auto scope : top_level_scopes(world)) {
        auto top = scope->entry();
        if (!top->empty() && top->num_uses() <= 2) {
            for (auto use : top->uses()) {
                if (Lambda* ulambda = use->isa_lambda()) {
                    if (ulambda->to() == top) {
                        if (!scope->contains(ulambda))
                            ulambda->jump(drop(*scope, ulambda->args()), {});
                    }
                }
            }
        }
    }

    debug_verify(world);
}

}
