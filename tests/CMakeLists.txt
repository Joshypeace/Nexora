find_package(Qt6 REQUIRED COMPONENTS Test)

# Qt's DLLs live outside the build tree, so put them on PATH while tests run.
set(_omnipane_qt_bin "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")

function(omnipane_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    omnipane_configure_target(${name})
    target_link_libraries(${name} PRIVATE OmnipaneCore Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_omnipane_qt_bin}"
        TIMEOUT 120)
endfunction()

omnipane_add_test(tst_search)
omnipane_add_test(tst_hotkey)
omnipane_add_test(tst_cache)
omnipane_add_test(tst_discovery)
omnipane_add_test(tst_launcher)
omnipane_add_test(tst_watcher)
omnipane_add_test(tst_ui)

# The UI test shows a real window briefly; label it so it can be skipped (ctest -LE ui).
set_tests_properties(tst_ui PROPERTIES LABELS ui
    ENVIRONMENT "OMNIPANE_SNAPSHOT_DIR=${CMAKE_CURRENT_BINARY_DIR}/snapshots")
