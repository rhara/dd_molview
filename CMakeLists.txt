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

# conda-forge's qt6-main/qt6-webengine packages (an alternative to a
# system Qt6 install -- see README.md's Installation section) don't ship
# an activate.d script that registers themselves on CMAKE_PREFIX_PATH the
# way some other conda packages do; fall back to the active env's own
# prefix so `find_package(Qt6)` below can still find them there.
if(NOT DEFINED CMAKE_PREFIX_PATH AND DEFINED ENV{CONDA_PREFIX})
  list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets WebEngineWidgets WebChannel)

# --- Embedded Python (dd_viewer/dd_cview_core's own interpreter) ------
# dd_cview reuses its own vendored dd_viewer and dd_cview_core modules
# (see python/dd_cview_backend.py, python/dd_viewer/, and
# python/dd_cview_core/ -- both absorbed unmodified from now-retired
# sibling projects), rather than reimplementing PDB/SDF parsing, contact
# detection, scoring, etc. in C++. Neither needs installing into the
# interpreter -- python/ is added to sys.path directly at runtime (see
# PythonBridge.cpp) -- it just needs their own dependencies (RDKit,
# biopython, pandas, numpy, py3Dmol) present. Default to dd_cview's own
# dedicated `dd_cview` conda env (see README.md's
# install instructions; kept separate from the `dd` env other dd_* projects
# share, so rebuilding/upgrading those doesn't risk breaking this build),
# but let a caller override it (a differently-named env, a plain venv) via
# `-DDD_CVIEW_PYTHON=/path/to/python3` (or `...\python.exe` on Windows).
# A conda env lays its interpreter out differently per platform -- POSIX:
# "<prefix>/bin/python3"; Windows: "<prefix>/python.exe" directly, no
# "bin" subdirectory -- so the default path is platform-conditional too.
if(NOT DEFINED DD_CVIEW_PYTHON)
  if(WIN32)
    if(DEFINED ENV{CONDA_PREFIX})
      set(DD_CVIEW_PYTHON "$ENV{CONDA_PREFIX}/python.exe")
    else()
      set(DD_CVIEW_PYTHON "$ENV{USERPROFILE}/miniforge3/envs/dd_cview/python.exe")
    endif()
  else()
    if(DEFINED ENV{CONDA_PREFIX})
      set(DD_CVIEW_PYTHON "$ENV{CONDA_PREFIX}/bin/python3")
    else()
      set(DD_CVIEW_PYTHON "$ENV{HOME}/miniforge3/envs/dd_cview/bin/python3")
    endif()
  endif()
endif()
message(STATUS "dd_cview: embedding Python at ${DD_CVIEW_PYTHON}")

# PYTHONHOME for that same interpreter -- PythonBridge.cpp sets this
# explicitly before Py_Initialize so site-packages (rdkit, biopython,
# pandas, ...) resolve correctly regardless of what
# environment (if any) is active in the shell that launches dd_cview.
# Queried directly from the interpreter itself (`sys.prefix`) rather than
# derived via path arithmetic on DD_CVIEW_PYTHON -- the "how many
# directories up from the executable is the env root" answer differs by
# both platform *and* env flavor (conda-on-Windows: 0, i.e. the executable
# already sits in the env root; conda-on-POSIX / a POSIX venv's "bin":  1;
# a Windows venv's "Scripts": 1) -- sys.prefix sidesteps guessing which
# convention applies.
execute_process(
  COMMAND "${DD_CVIEW_PYTHON}" -c "import sys; print(sys.prefix)"
  OUTPUT_VARIABLE DD_CVIEW_PYTHON_HOME
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT DD_CVIEW_PYTHON_HOME)
  message(FATAL_ERROR "dd_cview: could not run '${DD_CVIEW_PYTHON}' to determine its sys.prefix -- "
                       "check that DD_CVIEW_PYTHON points at a working Python 3 interpreter.")
endif()

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

# Explicit rpath to the embedded Python's own lib/ directory (where
# libpython3.*.{so,dylib} actually lives), on both Linux and macOS. CMake
# adds this automatically for the *build-tree* binary (it can see where
# Python3::Python's library came from), but silently drops it again on
# `cmake --install` unless told to keep an (install-time-appropriate) one --
# without this, an installed dd_cview dies at startup with a dynamic-linker
# error before even reaching main() ("Library not loaded: @rpath/
# libpython3.12.dylib" / "error while loading shared libraries:
# libpython3.12.so.1.0"). A no-op on Windows (no rpath concept there --
# see README.md's Windows install notes for how python3*.dll is found
# instead).
if(UNIX)
  set_target_properties(dd_cview PROPERTIES
    BUILD_RPATH "${DD_CVIEW_PYTHON_HOME}/lib"
    INSTALL_RPATH "${DD_CVIEW_PYTHON_HOME}/lib"
  )
endif()

# Fallback location of dd_cview_backend.py for a binary run straight out of
# the build directory (never installed) -- baked in at compile time so that
# works regardless of the process's own current working directory. An
# *installed* binary instead finds it at a path relative to itself (see
# PythonBridge.cpp's resolvePythonDir()); this is only the fallback for the
# case that relative lookup misses.
target_compile_definitions(dd_cview PRIVATE
  DD_CVIEW_PYTHON_DIR="${CMAKE_SOURCE_DIR}/python"
  DD_CVIEW_PYTHON_HOME="${DD_CVIEW_PYTHON_HOME}"
)

# Installs as one self-contained, relocatable directory -- the executable
# plus a sibling "python/" (dd_cview_backend.py) and "data/" (sample
# structures) directory, all directly under the chosen --prefix, rather
# than spread across a traditional bin/share split -- so "install" is just
# "copy this whole directory wherever you like" on every platform (see
# README.md's "Installing the built binary" section). This does *not* make
# the embedded Python environment itself (DD_CVIEW_PYTHON_HOME, baked in
# above) relocatable -- that conda env/venv must still exist at its
# original path on whatever machine runs the installed binary.
install(TARGETS dd_cview RUNTIME DESTINATION .)
install(DIRECTORY python/ DESTINATION python PATTERN "__pycache__" EXCLUDE)
install(DIRECTORY data/ DESTINATION data)

# --- Tests ---------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
