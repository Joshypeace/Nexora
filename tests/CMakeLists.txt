find_package(Qt6 REQUIRED COMPONENTS Test)

# Qt's DLLs live outside the build tree, so put them on PATH while tests run.
set(_nexora_qt_bin "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")

function(nexora_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    nexora_configure_target(${name})
    target_link_libraries(${name} PRIVATE NexoraCore Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_nexora_qt_bin}"
        TIMEOUT 120)
endfunction()

nexora_add_test(tst_search)
nexora_add_test(tst_hotkey)
nexora_add_test(tst_cache)
nexora_add_test(tst_discovery)
nexora_add_test(tst_launcher)
nexora_add_test(tst_ui)

# The UI test shows a real window briefly; label it so it can be skipped (ctest -LE ui).
set_tests_properties(tst_ui PROPERTIES LABELS ui
    ENVIRONMENT "NEXORA_SNAPSHOT_DIR=${CMAKE_CURRENT_BINARY_DIR}/snapshots")
