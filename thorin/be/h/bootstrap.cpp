#include "thorin/be/h/bootstrap.h"

#include <ranges>
#include <sstream>

#include "thorin/axiom.h"
#include "thorin/driver.h"

namespace thorin {

void bootstrap(Driver& driver, Sym plugin, std::ostream& h) {
    Tab tab;
    tab.print(h, "#pragma once\n\n");
    tab.print(h, "#include \"thorin/axiom.h\"\n"
                 "#include \"thorin/plugin.h\"\n\n");

    tab.print(h, "/// @namespace thorin::{} @ref {} \n", plugin, plugin);
    tab.print(h, "namespace thorin {{\nnamespace {} {{\n\n", plugin);

    plugin_t plugin_id = *Annex::mangle(plugin);
    std::vector<std::ostringstream> normalizers, outer_namespace;

    tab.print(h << std::hex, "static constexpr plugin_t Plugin_Id = 0x{};\n\n", plugin_id);

    const auto& unordered = driver.plugin2annxes(plugin);
    std::deque<std::pair<Sym, Annex>> infos(unordered.begin(), unordered.end());
    std::ranges::sort(infos, [&](const auto& p1, const auto& p2) { return p1.second.tag_id < p2.second.tag_id; });

    // clang-format off
    for (const auto& [key, ax] : infos) {
        if (ax.plugin != plugin) continue; // this is from an import

        tab.print(h, "/// @name %%{}.{}\n///@{{\n", plugin, ax.tag);
        tab.print(h, "#ifdef DOXYGEN // see https://github.com/doxygen/doxygen/issues/9668\n");
        tab.print(h, "enum {} : flags_t {{\n", ax.tag);
        tab.print(h, "#else\n");
        tab.print(h, "enum class {} : flags_t {{\n", ax.tag);
        tab.print(h, "#endif\n");
        ++tab;
        flags_t ax_id = plugin_id | (ax.tag_id << 8u);

        auto& os = outer_namespace.emplace_back();
        print(os << std::hex, "template<> constexpr flags_t Annex::Base<{}::{}> = 0x{};\n", plugin, ax.tag, ax_id);

        if (auto& subs = ax.subs; !subs.empty()) {
            for (const auto& aliases : subs) {
                const auto& sub = aliases.front();
                tab.print(h, "{} = 0x{},\n", sub, ax_id++);
                for (size_t i = 1; i < aliases.size(); ++i) tab.print(h, "{} = {},\n", aliases[i], sub);

                if (ax.normalizer)
                    print(normalizers.emplace_back(), "normalizers[flags_t({}::{})] = &{}<{}::{}>;", ax.tag, sub,
                          ax.normalizer, ax.tag, sub);
            }
        } else {
            if (ax.normalizer)
                print(normalizers.emplace_back(), "normalizers[flags_t(Annex::Base<{}>)] = &{};", ax.tag, ax.normalizer);
        }
        --tab;
        tab.print(h, "}};\n\n");

        if (!ax.subs.empty()) tab.print(h, "THORIN_ENUM_OPERATORS({})\n", ax.tag);
        print(outer_namespace.emplace_back(), "template<> constexpr size_t Annex::Num<{}::{}> = {};\n", plugin, ax.tag, ax.subs.size());

        if (ax.normalizer) {
            if (auto& subs = ax.subs; !subs.empty()) {
                tab.print(h, "template<{}>\nRef {}(Ref, Ref, Ref);\n\n", ax.tag, ax.normalizer);
            } else {
                tab.print(h, "Ref {}(Ref, Ref, Ref);\n", ax.normalizer);
            }
        }
        tab.print(h, "///@}}\n\n");
    }
    // clang-format on

    if (!normalizers.empty()) {
        tab.print(h, "void register_normalizers(Normalizers& normalizers);\n\n");
        tab.print(h, "#define THORIN_{}_NORMALIZER_IMPL \\\n", plugin);
        ++tab;
        tab.print(h, "void register_normalizers(Normalizers& normalizers) {{\\\n");
        ++tab;
        for (const auto& normalizer : normalizers) tab.print(h, "{} \\\n", normalizer.str());
        --tab;
        tab.print(h, "}}\n");
        --tab;
    }

    tab.print(h, "}} // namespace {}\n\n", plugin);

    tab.print(h, "#ifndef DOXYGEN // don't include in Doxygen documentation\n");
    for (const auto& line : outer_namespace) tab.print(h, "{}", line.str());
    tab.print(h, "\n");

    // emit helpers for non-function axiom
    for (const auto& [tag, ax] : infos) {
        if (ax.pi || ax.plugin != plugin) continue; // from function or other plugin?
        tab.print(h, "template<> struct Axiom::Match<{}::{}> {{ using type = Axiom; }};\n", ax.plugin, ax.tag);
    }

    tab.print(h, "#endif\n");
    tab.print(h, "}} // namespace thorin\n");
}

} // namespace thorin
