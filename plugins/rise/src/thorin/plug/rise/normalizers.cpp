#include "thorin/world.h"

#include "thorin/plug/rise/rise.h"

namespace thorin::plug::rise {

#if 0
Ref normalize_const(Ref type, Ref, Ref arg) {
    auto& world = type->world();
    return world.lit(world.type_idx(arg), 42);
}
#endif

// THORIN_rise_NORMALIZER_IMPL

} // namespace thorin::plug::rise
