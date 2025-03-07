#pragma once

#include <list>

#include "thorin/flags.h"
#include "thorin/plugin.h"
#include "thorin/world.h"

#include "thorin/util/log.h"

#include "absl/container/node_hash_map.h"

namespace thorin {

/// Some "global" variables needed all over the place.
/// Well, there are not really global - that's the point of this class.
class Driver : public fe::SymPool {
public:
    Driver();

    /// @name Getters
    ///@{
    Flags& flags() { return flags_; }
    Log& log() { return log_; }
    World& world() { return world_; }
    ///@}

    /// @name Manage Search Paths
    ///@{
    /// Search paths for plugins are in the following order:
    /// 1. The empty path. Used as prefix to look into current working directory without resorting to an absolute path.
    /// 2. All further user-specified paths via Driver::add_search_path; paths added first will also be searched first.
    /// 3. All paths specified in the environment variable `THORIN_PLUGIN_PATH`.
    /// 4. `path/to/thorin.exe/../../lib/thorin`
    /// 5. `CMAKE_INSTALL_PREFIX/lib/thorin`
    const auto& search_paths() const { return search_paths_; }
    void add_search_path(fs::path path) {
        if (fs::exists(path) && fs::is_directory(path)) search_paths_.insert(insert_, std::move(path));
    }
    ///@}

    /// @name Manage Imports
    ///@{
    /// This is a list of pairs where each pair contains:
    /// 1. The `fs::path` used during import,
    /// 2. The name as Sym%bol used in the `.import` directive or in Parser::import.
    const auto& imports() { return imports_; }
    /// Yields a `fs::path*` if not already added that you can use in Loc%ation; returns `nullptr` otherwise.
    const fs::path* add_import(fs::path, Sym);
    ///@}

    /// @name Load Plugin
    ///@{
    /// Finds and loads a shared object file that implements the Thorin Plugin @p name.
    /// If \a name is an absolute path to a `.so`/`.dll` file, this is used.
    /// Otherwise, "name", "libthorin_name.so" (Linux, Mac), "thorin_name.dll" (Win)
    /// are searched for in Driver::search_paths().
    void load(Sym name);
    void load(const std::string& name) { return load(sym(name)); }
    bool is_loaded(Sym sym) const { return lookup(plugins_, sym); }
    ///@}

    /// @name Manage Plugins
    ///@{
    /// All these lookups yield `nullptr` if the key has not been found.
    auto pass(flags_t flags) { return lookup(passes_, flags); }
    auto normalizer(flags_t flags) const { return lookup(normalizers_, flags); }
    auto normalizer(plugin_t d, tag_t t, sub_t s) const { return normalizer(d | flags_t(t << 8u) | s); }
    auto backend(std::string_view name) { return lookup(backends_, name); }
    ///@}

    /// @name Manage Annex
    ///@{
    const auto& plugin2annxes(Sym plugin) { return plugin2annexes_[plugin]; }
    std::pair<Annex&, bool> name2annex(Sym sym, Sym plugin, Sym tag, Loc loc);
    ///@}

private:
    Flags flags_;
    Log log_;
    World world_;
    std::list<fs::path> search_paths_;
    std::list<fs::path>::iterator insert_ = search_paths_.end();
    absl::node_hash_map<Sym, Plugin::Handle> plugins_;
    Backends backends_;
    Passes passes_;
    Normalizers normalizers_;
    std::deque<std::pair<fs::path, Sym>> imports_;
    fe::SymMap<fe::SymMap<Annex>> plugin2annexes_;
};

} // namespace thorin
