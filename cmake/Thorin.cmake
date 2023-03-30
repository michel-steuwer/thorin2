# clear globals
SET(THORIN_PLUGIN_LIST   "" CACHE INTERNAL "THORIN_PLUGIN_LIST")
SET(THORIN_PLUGIN_LAYOUT "" CACHE INTERNAL "THORIN_PLUGIN_LAYOUT")

if(NOT THORIN_TARGET_NAMESPACE)
    set(THORIN_TARGET_NAMESPACE "")
endif()

#[=======================================================================[
add_thorin_plugin
-------------------

Registers a new Thorin plugin.

```
add_thorin_plugin(<name>
    [SOURCES <source>...]
    [DEPENDS <other_plugin_name>...]
    [HEADER_DEPENDS <other_plugin_name>...]
    [INSTALL])
```

The `<name>` is expected to be the name of the plugin. This means, there
should be (relative to your CMakeLists.txt) a file `<name>/<name>.thorin`
containing the axiom declarations.
This will generate a header `dialects/<name>/autogen.h` that can be used in normalizers
and passes to identify the axioms.

- `SOURCES`: The values to the `SOURCES` argument are the source files used
    to build the loadable plugin containing normalizers, passes and backends.
    One of the source files must export the `thorin_get_plugin` function.
    `add_thorin_plugin` creates a new target called `thorin_<name>` that builds
    the plugin.
    Custom properties can be specified in the using `CMakeLists.txt` file,
    e.g. adding include paths is done with `target_include_directories(thorin_<name> <path>..)`.
- `DEPENDS`: The `DEPENDS` arguments specify the relation between multiple
    plugins. This makes sure that the bootstrapping of the plugin is done
    whenever a depended-upon plugin description is changed.
    E.g. `core` depends on `mem`, therefore whenever `mem.thorin` changes,
    `core.thorin` has to be bootstrapped again as well.
- `HEADER_DEPENDS`: The `HEADER_DEPENDS` arguments specify dependencies
    of a plugin on the generated header of another plugin.
    E.g. `mem.thorin` does not import `core.thorin` but the plugin relies
    on the `%core.conv` axiom. Therefore `mem` requires `core`'s autogenerated
    header to be up-to-date.
- `INSTALL`: Specify, if the plugin description, plugin and headers shall
    be installed with `make install`.
    To export the targets, the export name `install_exports` has to be
    exported accordingly (see [install(EXPORT ..)](https://cmake.org/cmake/help/latest/command/install.html#export))


## Note: a copy of this text is in `docs/coding.md`. Please update!
#]=======================================================================]
function(add_thorin_plugin)
    set(PLUGIN ${ARGV0})
    list(SUBLIST ARGV 1 -1 UNPARSED)
    cmake_parse_arguments(
        PARSED                           # prefix of output variables
        "INSTALL"                        # list of names of the boolean arguments (only defined ones will be true)
        "THORIN"                         # list of names of mono-valued arguments
        "SOURCES;DEPENDS;HEADER_DEPENDS" # list of names of multi-valued arguments (output variables are lists)
        ${UNPARSED}                      # arguments of the function to parse, here we take the all original ones
    )

    set(THORIN_LIB_DIR ${CMAKE_BINARY_DIR}/lib/thorin)
    set(PLUGINS_INCLUDE_DIR ${CMAKE_BINARY_DIR}/include/dialects/)

    list(TRANSFORM PARSED_DEPENDS        PREPEND ${THORIN_LIB_DIR}/ OUTPUT_VARIABLE DEPENDS_THORIN_FILES)
    list(TRANSFORM DEPENDS_THORIN_FILES   APPEND .thorin)
    list(TRANSFORM PARSED_DEPENDS        PREPEND ${PLUGINS_INCLUDE_DIR} OUTPUT_VARIABLE DEPENDS_HEADER_FILES)
    list(TRANSFORM DEPENDS_HEADER_FILES   APPEND /autogen.h)
    list(TRANSFORM PARSED_HEADER_DEPENDS PREPEND ${PLUGINS_INCLUDE_DIR} OUTPUT_VARIABLE PARSED_HEADER_DEPENDS_FILES)
    list(TRANSFORM PARSED_HEADER_DEPENDS_FILES  APPEND /autogen.h)
    list(APPEND DEPENDS_HEADER_FILES ${PARSED_HEADER_DEPENDS_FILES})

    set(THORIN_FILE         ${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN}/${PLUGIN}.thorin)
    set(THORIN_FILE_LIB_DIR ${THORIN_LIB_DIR}/${PLUGIN}.thorin)
    set(PLUGIN_H            ${PLUGINS_INCLUDE_DIR}${PLUGIN}/autogen.h)
    set(PLUGIN_MD           ${CMAKE_BINARY_DIR}/docs/dialects/${PLUGIN}.md)

    list(APPEND THORIN_PLUGIN_LIST "${PLUGIN}")
    string(APPEND THORIN_PLUGIN_LAYOUT "<tab type=\"user\" url=\"@ref ${PLUGIN}\" title=\"${PLUGIN}\"/>")

    # populate to globals
    SET(THORIN_PLUGIN_LIST   "${THORIN_PLUGIN_LIST}"   CACHE INTERNAL "THORIN_PLUGIN_LIST")
    SET(THORIN_PLUGIN_LAYOUT "${THORIN_PLUGIN_LAYOUT}" CACHE INTERNAL "THORIN_PLUGIN_LAYOUT")

    # copy plugin thorin file to lib/thorin/${PLUGIN}.thorin
    add_custom_command(
        OUTPUT  ${THORIN_FILE_LIB_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THORIN_FILE} ${THORIN_FILE_LIB_DIR}
        DEPENDS ${THORIN_FILE} ${DEPENDS_THORIN_FILES}
    )

    add_custom_target(internal_thorin_${PLUGIN}_thorin DEPENDS ${THORIN_FILE_LIB_DIR})

    file(MAKE_DIRECTORY ${PLUGINS_INCLUDE_DIR}${PLUGIN})
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/docs/dialects/)

    add_custom_command(
        OUTPUT ${PLUGIN_MD} ${PLUGIN_H}
        COMMAND $<TARGET_FILE:${THORIN_TARGET_NAMESPACE}thorin> ${THORIN_FILE_LIB_DIR} -P ${THORIN_LIB_DIR} --bootstrap --output-h ${PLUGIN_H} --output-md ${PLUGIN_MD}
        DEPENDS ${THORIN_TARGET_NAMESPACE}thorin internal_thorin_${PLUGIN}_thorin ${THORIN_FILE_LIB_DIR}
        COMMENT "Bootstrapping Thorin plugin '${PLUGIN}' from '${THORIN_FILE}'"
    )
    add_custom_target(${PLUGIN} DEPENDS ${PLUGIN_H})

    add_library(thorin_${PLUGIN}
        MODULE
            ${PARSED_SOURCES}       # original sources passed to add_thorin_plugin
            ${PLUGIN_H}             # the generated header of this plugin
            ${DEPENDS_HEADER_FILES} # the generated headers of the plugins we depend on
    )

    add_dependencies(thorin_${PLUGIN} ${PLUGIN} ${PARSED_DEPENDS} ${PARSED_HEADER_DEPENDS})

    set_target_properties(thorin_${PLUGIN}
        PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN 1
            WINDOWS_EXPORT_ALL_SYMBOLS OFF
            PREFIX "lib" # always use "lib" as prefix regardless of OS/compiler
            LIBRARY_OUTPUT_DIRECTORY ${THORIN_LIB_DIR}
    )

    target_link_libraries(thorin_${PLUGIN} ${THORIN_TARGET_NAMESPACE}libthorin)

    target_include_directories(thorin_${PLUGIN}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include> # dialects/${PLUGIN}/autogen.h
            $<INSTALL_INTERFACE:include>
    )

    if(${PARSED_INSTALL})
        install(TARGETS thorin_${PLUGIN} EXPORT install_exports LIBRARY DESTINATION lib/thorin RUNTIME DESTINATION lib/thorin INCLUDES DESTINATION include)
        install(FILES ${THORIN_FILE_LIB_DIR} DESTINATION lib/thorin)
        install(FILES ${PLUGIN_H} DESTINATION include/dialects/${PLUGIN})
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN} DESTINATION include/dialects FILES_MATCHING PATTERN *.h)
    endif()
    if(TARGET thorin_all_plugins)
        add_dependencies(thorin_all_plugins thorin_${PLUGIN})
    endif()
endfunction()
