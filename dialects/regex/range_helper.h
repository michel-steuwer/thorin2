#pragma once
#include <optional>
#include <vector>

#include "thorin/world.h"

namespace thorin::regex {

struct RangeCompare {
    inline bool operator()(const std::pair<nat_t, nat_t>& a, const std::pair<nat_t, nat_t>& b) const noexcept {
        return a.first < b.first;
    }
};

inline std::optional<std::pair<nat_t, nat_t>> merge_ranges(std::pair<nat_t, nat_t> a,
                                                           std::pair<nat_t, nat_t> b) noexcept {
    if (!(a.second + 1 < b.first || b.second + 1 < a.first)) {
        return {
            {std::min(a.first, b.first), std::max(a.second, b.second)}
        };
    }
    return {};
}

// precondition: ranges are sorted by increasing lower bound
template<class LogF>
Vector<std::pair<nat_t, nat_t>> merge_ranges(const Vector<std::pair<nat_t, nat_t>>& old_ranges, LogF&& log) {
    Vector<std::pair<nat_t, nat_t>> new_ranges;
    for (auto it = old_ranges.begin(); it != old_ranges.end(); ++it) {
        auto current_range = *it;
        log("old range: {}-{}", current_range.first, current_range.second);
        for (auto inner = it + 1; inner != old_ranges.end(); ++inner)
            if (auto merged = merge_ranges(current_range, *inner)) current_range = *merged;

        Vector<Vector<std::pair<nat_t, nat_t>>::iterator> de_duplicate;
        for (auto inner = new_ranges.begin(); inner != new_ranges.end(); ++inner) {
            if (auto merged = merge_ranges(current_range, *inner)) {
                current_range = *merged;
                de_duplicate.push_back(inner);
            }
        }
        for (auto dedup : de_duplicate) {
            log("dedup {}-{}", current_range.first, current_range.second);
            new_ranges.erase(dedup);
        }
        log("new range: {}-{}", current_range.first, current_range.second);
        new_ranges.push_back(std::move(current_range));
    }
    return new_ranges;
}

// precondition: ranges are sorted by increasing lower bound
inline Vector<std::pair<nat_t, nat_t>> merge_ranges(const Vector<std::pair<nat_t, nat_t>>& old_ranges) {
    return merge_ranges(old_ranges, [](auto&&...) {});
}

} // namespace thorin::regex
