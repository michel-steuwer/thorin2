#include "thorin/world.h"

#include "thorin/plug/dpia/dpia.h"

namespace thorin::plug::dpia {

#if 0

Ref normalize_const(Ref type, Ref, Ref arg) {
    auto& world = type->world();
    return world.lit(world.type_idx(arg), 42);
}

THORIN_dpia_NORMALIZER_IMPL
#endif

} // namespace thorin::plug::dpia
