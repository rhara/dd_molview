cmake_minimum_required(VERSION 3.21)
project(dd_cview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Qt6 -------------------------------------------------------------
# Homebrew's `qt` formula is keg-only; point CMake at it explicitly rather
# than relying on a global CMAKE_PREFIX_PATH the user may not have set.
if(APPLE AND NOT DEFINED CMAKE_PREFIX_PATH)
  execute_process(
    COMMAND brew --prefix qt
    OUTPUT_VARIABLE HOMEBREW_QT_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(HOMEBREW_QT_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_QT_PREFIX}")
  endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets WebEngineWidgets WebChannel)

# --- Embedded Python (dd_viewer/dd_molview's own interpreter) --------
# dd_cview reuses dd_viewer/dd_molview's existing Python logic as-is (see
# python/dd_cview_backend.py) rather than reimplementing PDB/SDF parsing,
# contact detection, scoring, etc. in C++. It therefore needs the *same*
# interpreter those packages are installed into -- default to the `dd`
# conda env (see ~/work/dd_molview/README.md's install instructions), but
# let a caller override it (a differently-named env, a plain venv) via
# `-DDD_CVIEW_PYTHON=/path/to/python3`.
if(NOT DEFINED DD_CVIEW_PYTHON)
  if(DEFINED ENV{CONDA_PREFIX})
    set(DD_CVIEW_PYTHON "$ENV{CONDA_PREFIX}/bin/python3")
  else()
    set(DD_CVIEW_PYTHON "$ENV{HOME}/miniforge3/envs/dd/bin/python3")
  endif()
endif()
message(STATUS "dd_cview: embedding Python at ${DD_CVIEW_PYTHON}")

# PYTHONHOME for that same interpreter (its venv/conda-env prefix, i.e. the
# parent of the "bin" directory DD_CVIEW_PYTHON sits in) -- PythonBridge.cpp
# sets this explicitly before Py_Initialize so site-packages (dd_viewer,
# dd_molview, rdkit, biopython, pandas, ...) resolve correctly regardless of
# what environment (if any) is active in the shell that launches dd_cview.
get_filename_component(DD_CVIEW_PYTHON_BINDIR "${DD_CVIEW_PYTHON}" DIRECTORY)
get_filename_component(DD_CVIEW_PYTHON_HOME "${DD_CVIEW_PYTHON_BINDIR}" DIRECTORY)

set(Python3_EXECUTABLE "${DD_CVIEW_PYTHON}")
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c "import pybind11; print(pybind11.get_cmake_dir())"
  OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
list(APPEND CMAKE_PREFIX_PATH "${PYBIND11_CMAKE_DIR}")
find_package(pybind11 REQUIRED)

# --- Sources -----------------------------------------------------------
qt_standard_project_setup()

add_executable(dd_cview
  src/main.cpp
  src/PythonBridge.cpp
  src/TableModel.cpp
  src/DisplaySettingsPanel.cpp
  src/SequencePanel.cpp
  src/Viewer3D.cpp
  src/MainWindow.cpp
  resources/resources.qrc
)

target_include_directories(dd_cview PRIVATE src)

target_link_libraries(dd_cview PRIVATE
  Qt6::Core
  Qt6::Widgets
  Qt6::WebEngineWidgets
  Qt6::WebChannel
  pybind11::embed
)

# Where dd_cview_backend.py lives -- baked in at compile time so the built
# binary works regardless of the process's current working directory (see
# PythonBridge.cpp's sys.path handling for why cwd can't be trusted here).
target_compile_definitions(dd_cview PRIVATE
  DD_CVIEW_PYTHON_DIR="${CMAKE_SOURCE_DIR}/python"
  DD_CVIEW_PYTHON_HOME="${DD_CVIEW_PYTHON_HOME}"
)

install(TARGETS dd_cview RUNTIME DESTINATION bin)

# --- Tests ---------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
