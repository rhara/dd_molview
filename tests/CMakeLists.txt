# Integration smoke test for PythonBridge (the embedded-Python <-> Qt
# boundary) -- deliberately not a Qt GUI test (no QApplication/QWidget
# involved): it just proves the whole pybind11 + CMake + conda-env wiring
# actually loads dd_viewer/dd_cview_core and round-trips real sample data.
add_executable(dd_cview_bridge_test
  bridge_test.cpp
  ${CMAKE_SOURCE_DIR}/src/PythonBridge.cpp
)
target_include_directories(dd_cview_bridge_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dd_cview_bridge_test PRIVATE Qt6::Core pybind11::embed)
target_compile_definitions(dd_cview_bridge_test PRIVATE
  DD_CVIEW_PYTHON_DIR="${CMAKE_SOURCE_DIR}/python"
  DD_CVIEW_PYTHON_HOME="${DD_CVIEW_PYTHON_HOME}"
)

add_test(NAME bridge_smoke_test COMMAND dd_cview_bridge_test "${CMAKE_SOURCE_DIR}/data")
