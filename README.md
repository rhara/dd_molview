[Japanese version](README.jp.md)

# dd_cview

A native C++/Qt6 reimplementation of the retired `dd_molview-desktop`
(PySide6) app's GUI shell -- the same four-panel workbench (protein table /
ligand table / 3D view / sequence view) for interactive protein-ligand
structure analysis, with every window, dock, table, and event handler
rewritten in C++/Qt instead of PySide6. The computational logic underneath
(PDB/SDF parsing, contact/interaction detection, scoring, RMSD, per-chain
sequence extraction, 3D-scene HTML generation) is **not** reimplemented:
`dd_cview` embeds a Python interpreter (via
[pybind11](https://github.com/pybind/pybind11)) and calls straight into two
vendored Python modules under `python/`: `dd_viewer` (PDB/SDF parsing,
interaction detection, scoring, scene HTML -- absorbed unmodified from the
retired standalone `dd_viewer` project) and `dd_cview_core`
(multi-receptor/ligand collections, sequence extraction/HTML rendering,
dashboard tables -- absorbed unmodified from `dd_molview`'s core logic when
that project was retired in favor of this one), through one narrow
JSON-in/JSON-out module,
[`python/dd_cview_backend.py`](python/dd_cview_backend.py).

`dd_molview-desktop` (PySide6) was functionally complete but noticeably
sluggish for interactive use -- table selection, settings toggles, and
residue picks all route through Python's own Qt bindings and its
single-threaded interpreter for *every* Qt event, not just the RDKit/
biopython calls that actually need it. `dd_cview` keeps those calls (which
dominate the actual wall-clock cost -- contact detection, interaction
geometry, 3D-scene HTML generation) exactly as they were, and moves
everything else -- widget construction, dock layout, table models, event
wiring, the sequence panel, the settings panel, camera-state bookkeeping --
onto native Qt, so the UI thread itself is never waiting on a Python/Qt
binding round-trip it didn't need to.

## Architecture

```
┌─────────────────────────────── C++ / Qt6 ────────────────────────────────┐
│ MainWindow (docks, menus, tables, wiring)                                │
│  ├─ TableModel        (QAbstractTableModel, wraps TableData)             │
│  ├─ DisplaySettingsPanel                                                  │
│  ├─ SequencePanel      (QTextBrowser + clickable residue/chain links)     │
│  ├─ Viewer3D           (QWebEngineView wrapper, camera JS round-trip)     │
│  └─ PythonBridge  ───────────────────────┐                               │
└───────────────────────────────────────────┼──────────────────────────────┘
                                             │ pybind11::embed (JSON strings only)
┌────────────────────────────────────────────┼──────────────────────────────┐
│ python/dd_cview_backend.py :: Session       ▼                             │
│  load_all / *_table_json / sequence_html / build_view_html / ...          │
└──────────────────────┬──────────────────────────────────────────────────┘
                        │ plain function calls, unmodified
                        ▼
        dd_viewer (parsing, interactions, scoring, scene HTML --
                   vendored, absorbed from the retired dd_viewer project)
        dd_cview_core (multi-receptor/ligand collections, sequence, dashboard --
                       vendored, absorbed from the retired dd_molview project)
```

`PythonBridge` (`src/PythonBridge.h`/`.cpp`) is the only place `<pybind11/
embed.h>` (and therefore `Python.h`) is included -- every other C++ file
only sees plain Qt/std types (`QString`, `QJsonArray`, `TableData`,
`DisplaySettings`, ...), never a `py::object`. This also sidesteps a real
build hazard: `Python.h` and Qt's `qobjectdefs.h` both define a bare
`slots` symbol, so any translation unit that includes both breaks -- keeping
the pybind11 header confined to one non-`QObject` `.cpp` file avoids it
entirely, rather than fighting it with `QT_NO_KEYWORDS` everywhere.

`Session` (the Python-side counterpart) was modeled on the retired
`dd_molview-desktop`'s own `MainWindow` state and orchestration
(`receptor_entries`, `ligand_entries`, `active_receptor_idx`,
`active_ligand_idx`, `reference_mol`, and the `_refresh_view`-equivalent
recompute step) one-for-one; the C++ `MainWindow` owns only *display* state
instead (which dock is visible, the settings-panel checkboxes, the
sequence panel's selected residues, the live 3D-view camera position).
Every `PythonBridge` method takes/returns only strings (JSON) or
primitives, so the pybind11 boundary itself stays trivial -- no custom
type casters, no long-lived `py::object` handles anywhere outside
`PythonBridge::Impl`.

## Installation

Every platform needs the same three things: CMake ≥3.21 with a C++20
compiler, Qt6 (`Core`, `Widgets`, `WebEngineWidgets`, `WebChannel` -- the
`WebEngineWidgets` part specifically means a full Qt6 + Chromium-based
`qtwebengine` install is required, not just `qtbase`), and a Python
environment with `dd_viewer`/`dd_cview_core`'s own dependencies installed
(both modules themselves, under `python/`, need no separate install --
see below). Only the first two are platform-specific to set up.

`dd_cview` gets its own dedicated conda env, **`dd_cview`** -- kept
separate from the `dd` env other `dd_*` projects share, so rebuilding or
upgrading a package for one of those doesn't risk breaking this build (and
vice versa). It only needs the subset of `dd_viewer`/`dd_cview_core`'s
dependencies `dd_cview_backend.py` actually imports at runtime (RDKit,
biopython, pandas, numpy, py3Dmol) and `pybind11` -- *not* `dd_viewer`'s
own extra GUI/web dependencies (Streamlit), which `dd_cview`'s embedded
backend never touches:

```bash
mamba create -n dd_cview -c conda-forge \
    python=3.12 rdkit biopython pandas numpy py3dmol pybind11 pytest \
    qt6-main qt6-webengine
conda activate dd_cview
```

Neither `python/dd_viewer/` nor `python/dd_cview_core/` needs installing --
both are this project's own vendored modules, and `python/` is added to
sys.path directly at runtime (see `PythonBridge.cpp`).

`dd_viewer`/`dd_cview_core`'s pytest suites (`python/tests/`, ported from
each project's own test suite when it was absorbed) run standalone, no C++
build required:

```bash
PYTHONPATH=python pytest python/tests/
```

`qt6-main` + `qt6-webengine` above pull a complete Qt6 (including
WebEngineWidgets/WebChannel) straight from conda-forge, which is what's
actually been build-verified for this project on Linux (see
[Verified behavior](#verified-behavior)) -- no system/Homebrew/apt Qt6
install needed on top of it. If you'd rather use a system Qt6 install
instead (Homebrew on macOS, apt on Ubuntu, the Qt online installer on
Windows), drop `qt6-main qt6-webengine` from the `mamba create` line above
and follow the platform-specific Qt6 install steps below instead; either
way, CMake picks up whichever Qt6 it finds via `CMAKE_PREFIX_PATH`/the
active env.

By default, the build embeds whichever Python `$CONDA_PREFIX/bin/python3`
(`%CONDA_PREFIX%\python.exe` on Windows) points at when CMake is
configured, falling back to the `dd_cview` env under the platform's
default miniforge location if no conda env is active -- override with
`-DDD_CVIEW_PYTHON=/path/to/python3` (or `...\python.exe`) on any platform
to point at a different environment (a differently-named conda env, a
plain venv) that has `dd_viewer`/`dd_cview_core`'s dependencies installed.

### macOS (Homebrew Qt6)

```bash
brew install cmake ninja qt   # only if not using conda's qt6-main/qt6-webengine

conda activate dd_cview

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Homebrew's `qt` formula is keg-only (not linked onto the default search
path); `CMakeLists.txt` already runs `brew --prefix qt` itself and appends
it to `CMAKE_PREFIX_PATH`, so no manual `-DCMAKE_PREFIX_PATH` is needed
here (unlike Windows below).

### Ubuntu (22.04 / 24.04, apt Qt6)

```bash
sudo apt update
sudo apt install cmake ninja-build build-essential \
    qt6-base-dev qt6-webengine-dev qt6-webengine-dev-tools
# (skip the above if using conda's qt6-main/qt6-webengine instead)

conda activate dd_cview

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`qt6-webengine-dev` pulls in the WebEngineWidgets runtime/dev headers (and
their own large dependency chain -- same underlying Chromium cost as
Homebrew's `qt` formula) as a transitive dependency; apt-installed Qt6
lands on CMake's default search path already, so no `CMAKE_PREFIX_PATH`
override is needed the way it is on Windows. (Using conda's `qt6-main`/
`qt6-webengine` instead needs no override either -- CMake picks it up off
the active `dd_cview` env's own `CMAKE_PREFIX_PATH`.)

### Windows (MSVC + Qt online installer)

1. Install Visual Studio 2022 (or the standalone Build Tools) with the
   **"Desktop development with C++"** workload -- this provides the MSVC
   compiler and Windows SDK CMake needs.
2. Install [CMake](https://cmake.org/download/) and
   [Ninja](https://github.com/ninja-build/ninja/releases) (or use the ones
   bundled with the Visual Studio Installer's optional components).
3. Install Qt6 (6.5+) via the [Qt online
   installer](https://www.qt.io/download-qt-installer), selecting the
   **MSVC 2022 64-bit** kit *and* the **Qt WebEngine** module (unlike the
   Homebrew/apt packages above, it is not pulled in automatically) -- this
   installs to a path like `C:\Qt\6.7.2\msvc2022_64`.
4. Install [Miniforge](https://github.com/conda-forge/miniforge) for
   Windows and set up `dd_cview`'s own conda env (see the `mamba create`/
   `pip install -e` commands above; drop `qt6-main qt6-webengine` from the
   `mamba create` line since Qt6 comes from the online installer here
   instead).
5. Build from an **"x64 Native Tools Command Prompt for VS 2022"** (needed
   so `cl.exe` is on `PATH`; a plain `cmd.exe`/PowerShell window won't have
   it):

   ```bat
   cmake -S . -B build -G Ninja ^
     -DCMAKE_BUILD_TYPE=Release ^
     -DCMAKE_PREFIX_PATH=C:\Qt\6.7.2\msvc2022_64 ^
     -DDD_CVIEW_PYTHON=%USERPROFILE%\miniforge3\envs\dd_cview\python.exe
   cmake --build build
   ```

   Unlike Homebrew/apt, the Qt online installer never registers itself on
   CMake's search path -- `-DCMAKE_PREFIX_PATH` pointing at the exact
   installed kit directory is required every time, not just as a fallback.

## Installing the built binary

`cmake --install` copies the built executable plus a sibling `python/`
(the `dd_cview_backend.py` module) and `data/` (bundled sample structures)
directory into one self-contained, relocatable directory under whatever
`--prefix` you choose -- copy or move that whole directory anywhere
afterward and `dd_cview` still finds its own backend module next to
itself (see `PythonBridge.cpp`'s `resolvePythonDir()`). "Installing"
`dd_cview` means picking a stable home for that directory and, optionally,
putting the binary on your `PATH`.

**This does not make the *embedded Python environment* itself portable**:
the conda env/venv `-DDD_CVIEW_PYTHON` pointed at when the binary was
*built* is baked in as its `PYTHONHOME` at compile time, and must still
exist at that same path on whatever machine actually runs the installed
binary. This is a personal/local build, not a distributable installer for
other machines or other users' environments.

**Running the installed binary needs *no* `conda activate` step, though**
-- `PYTHONHOME` (compile-time, from `DD_CVIEW_PYTHON_HOME`) and the
dynamic linker `RUNPATH` to that same env's `lib/` (`BUILD_RPATH`/
`INSTALL_RPATH` in `CMakeLists.txt`, see [Design notes](#design-notes))
are both baked into the binary itself, not read from the shell's
environment at launch time. Verified directly: `readelf -d
build/dd_cview | grep RUNPATH` shows the env's `lib/` path baked in, and
running the installed binary under `env -i` (every environment variable
scrubbed, including `PATH`/`CONDA_PREFIX` -- no active conda env, no
`conda` even on `PATH`) still starts up and loads sample data correctly.
What genuinely *is* required is only that the conda env's files
themselves still physically exist, unmoved, at the path they were at when
`dd_cview` was built (`/opt/miniforge3/envs/dd_cview` in this project's
own build, for instance) -- deleting, renaming, or relocating that env
breaks the installed binary (a dynamic-linker error for
`libpython3.*.so`/`.dylib` before it even reaches `main()`), regardless of
where `dd_cview` itself was installed to.

```bash
cmake --install build --prefix <any-writable-directory>
```

### macOS / Ubuntu

```bash
cmake --install build --prefix ~/apps/dd_cview
ln -sf ~/apps/dd_cview/dd_cview ~/.local/bin/dd_cview   # or /usr/local/bin, if writable
```

(`~/.local/bin` is on `PATH` by default on most current Ubuntu/macOS
shells; add `export PATH="$HOME/.local/bin:$PATH"` to your shell profile
if not.) Run it as `dd_cview` from anywhere afterward, or skip the symlink
and just launch `~/apps/dd_cview/dd_cview` directly.

### Windows

```bat
cmake --install build --prefix C:\Tools\dd_cview
```

Then either add `C:\Tools\dd_cview` to your user `PATH` (Settings > System
> About > Advanced system settings > Environment Variables), or create a
shortcut to `C:\Tools\dd_cview\dd_cview.exe` (Desktop or Start Menu) --
there's no installer/uninstaller beyond that; it's a plain relocatable
directory.

**None of this is required for local development/testing** -- running
`./build/dd_cview` (or `build\dd_cview.exe`) directly, straight out of the
build directory, works identically; `cmake --install` only matters once
you want a stable location outside the build directory.

## Usage

```bash
./build/dd_cview \
  --receptor data/6W63_receptor.pdb --poses data/6W63_redock.sdf \
  --receptor data/7L10.pdb --receptor data/7L11.pdb \
  --reference data/6W63_ligand_ref.sdf   # optional: for RMSD calculation
```

`--receptor` and `--poses` are each **repeatable** (pass the flag once per
file); `--reference` and `--manifest` are each single. Files can also be
loaded afterward via the File menu/toolbar ("Open Receptor(s)...", "Add
Poses...", "Open Reference...", "Open Manifest..."), exactly as in
`dd_molview-desktop`.

**If no poses file is given at all**, every loaded receptor is scanned for
embedded ligands and the results merged into one ligand list -- same
all-or-nothing auto-extraction rule as the retired `dd_molview` (this logic
runs unmodified inside `dd_cview_core.load_all`, `dd_cview` just calls it).

### Ensemble manifest.json

```bash
./build/dd_cview --manifest data/sample_manifest.json
```

Same dd_docking-style `manifest.json` format `dd_cview_core` reads (a plain
JSON list of `{member_id, receptor_pdb, ...}` objects, duck-typed, no
`dd_docking` import) -- loaded additively alongside any `--receptor` paths.

## Features

Every panel behaves identically to the retired `dd_molview-desktop`'s own
(same underlying `dd_viewer`/`dd_cview_core` calls, just through native `QTableView`/
`QWebEngineView`/`QTextBrowser` widgets instead of PySide6's):

- **Protein-selection panel**: one row per loaded receptor (label,
  chain/residue counts, source path); selecting a row makes it the active
  receptor for the 3D view and sequence panel.
- **Ligand-selection panel**: one row per loaded ligand, with per-row
  score/RMSD/interaction-count columns computed against the *active*
  receptor only.
- **3D structure view**: cartoon/stick/surface toggles, secondary-structure
  coloring, an only-near-ligand display option, interaction overlays
  (H-bonds, hydrophobic contacts, salt bridges, electrostatics,
  pi-stacking, halogen bonds), floating residue-label text, and
  camera-position persistence across updates -- `Viewer3D` re-applies the
  last captured camera view to each freshly reloaded scene via a
  `page()->runJavaScript()` round-trip, the same technique
  `dd_molview-desktop` uses. **Highlight interacting residues** (yellow, on
  by default) marks every residue behind at least one currently-enabled
  interaction type above -- not merely every residue within the Contact
  residue cutoff below; that broader, purely distance-based set is a
  separate, independent concept (see the Contact-residue table below).
- **Sequence panel**: fixed 50-residues-per-line resnum grid with a
  line-start ruler, yellow/magenta two-tier highlighting matching the 3D
  view (the same interaction-based yellow set described above), clickable
  residues (multi-select with Ctrl/Cmd-click) and clickable "Chain X"
  headers (re-fits the camera to that chain).
- **Contact-residue table** (collapsed by default): every residue within
  the Contact residue cutoff slider's distance of the ligand, independent
  of the yellow highlight above -- selecting rows highlights those
  specific residues magenta in both the 3D view and the sequence panel
  (the usual manual-pick mechanism), regardless of whether they also
  happen to have a detected interaction.
- **Every panel is its own dock** -- movable, floatable, closable
  independently, restored across sessions via `QSettings`
  (`saveGeometry`/`saveState`).
- **Save 3D View Screenshot...** (File menu/toolbar): saves exactly what's
  currently on screen in the 3D View dock -- receptor, pose, interaction
  overlays, and whatever camera position it's framed at -- to a PNG file
  you pick via a save dialog, using a plain `QWidget::grab()` of the
  `QWebEngineView` (only meaningful with a real display; like every other
  3D-view capture in this project, `grab()` can't see WebGL content under
  `QT_QPA_PLATFORM=offscreen` -- see [Verified
  behavior](#verified-behavior)).
- **Quit** (File menu, bottom entry): closes the window (running the same
  `closeEvent` that saves dock/geometry state via `QSettings`, so layout
  persists across restarts) and exits the application. Keyboard shortcut
  is **Alt+Q** on Linux; **Cmd+Q** on macOS (and, per the platform's own
  convention, also relocated out of the File menu into the application
  menu there); **Ctrl+Q** on Windows.

**Camera behavior** and **multi-residue selection** follow the exact same
rules the retired `dd_molview-desktop` used -- switching
the active protein/ligand row or toggling a display setting never moves
the camera; only dragging/zooming it yourself, "Center on Ligand", or
"Zoom to Highlighted Residues" does.

**`dd_cview`-only addition** (not present in `dd_molview-desktop`): the
Settings dock has a second camera button, **Zoom to Highlighted
Residues**, next to "Center on Ligand" -- re-fits the camera to whatever
residues are currently highlighted yellow (a live `zoomTo({predicate:
...})` + `render()` call against the loaded scene, the same
direct-camera-move technique as a sequence-panel chain-header click; a
no-op if nothing is currently highlighted). Both camera buttons live only
in the Settings dock now -- there is no longer a top-level View menu/
toolbar "Center on Ligand" entry, since grouping both camera actions with
the rest of the display settings (which is what decides what's actually
highlighted) reads more clearly than duplicating one of them at the top
level.

## Module structure (`src/`)

| File | Contents |
|---|---|
| `PythonBridge.h`/`.cpp` | The only file that touches `<pybind11/embed.h>`. Owns the embedded interpreter (`py::scoped_interpreter`) and the one `Session` object; every method converts to/from `QJsonDocument` and plain Qt/std types. |
| `MainWindow.h`/`.cpp` | Dock construction, menu/toolbar, layout persistence, all Qt signal handlers, and the central `refreshView()` recompute-and-redraw method -- the C++ analog of the retired `dd_molview.desktop.main_window.MainWindow`. |
| `TableModel.h`/`.cpp` | One reusable read-only `QAbstractTableModel` wrapping a `TableData` (columns/display-rows/raw-rows), used for all three tables. |
| `DisplaySettingsPanel.h`/`.cpp` | The settings dock's controls -- one-to-one with the retired `dd_molview.desktop.controls.DisplaySettingsPanel`. |
| `SequencePanel.h`/`.cpp` | A `QTextBrowser` subclass with the sequence panel's font/link-handling setup. |
| `Viewer3D.h`/`.cpp` | Wraps the 3D view's `QWebEngineView`: loading a freshly built scene, the async `getView()` camera-capture round-trip, and `zoomTo({chain})` for chain-header clicks. |
| `main.cpp` | Entry point: CLI parsing (`--receptor`/`--poses`/`--reference`/`--manifest`), `QApplication` setup, and an optional `DD_CVIEW_SCREENSHOT` env-var hook for headless verification (see below). |

`python/dd_cview_backend.py` is the Python-side counterpart -- see the
[Architecture](#architecture) section above.

## Design notes

- **No computational logic is duplicated in C++.** Every PDB/SDF parse,
  distance calculation, RDKit call, or HTML-generation step happens in
  Python, inside `dd_viewer`/`dd_cview_core`, completely unmodified. `dd_cview`
  only ever passes primitives across the pybind11 boundary.
- **`PythonBridge` never leaks a `py::object`.** Its public API (see
  `PythonBridge.h`) is Qt/std types only, so no other file in the project
  needs to know pybind11 exists, and the `Python.h`/`qobjectdefs.h` `slots`
  clash never has a chance to occur outside `PythonBridge.cpp`.
- **PYTHONHOME is set explicitly, and `sys.path`'s cwd entry is stripped.**
  `dd_cview` lives under the same parent directory (`~/work`) as other,
  unrelated top-level checkouts that happen to share a name with a Python
  package it imports (e.g. a leftover `dd_viewer` checkout with no
  `__init__.py` at its own top level); if `dd_cview` is launched with that
  parent as its working directory, a bare `""` `sys.path` entry resolves
  `import dd_viewer` to a broken *namespace* package rooted at that
  directory instead of the real vendored `python/dd_viewer/` -- silently,
  with `dd_viewer.__file__` coming back `None` rather
  than raising. `PythonBridge`'s constructor strips any empty/cwd-derived
  `sys.path` entry before importing anything, so this can't happen
  regardless of where the binary is launched from.
- **The GIL is never released.** Every `PythonBridge` call happens
  synchronously on the Qt main thread, matching the original
  `dd_molview-desktop`'s own single-threaded Qt event-loop model -- no
  background thread ever touches the interpreter, so there's no need for
  `py::gil_scoped_release`/`acquire` anywhere.
- **Cross-platform environment plumbing avoids POSIX-only/layout
  assumptions.** `PYTHONHOME` is set via `qputenv` (not the POSIX-only
  `setenv`, which MSVC's CRT doesn't provide) so the same code builds on
  Windows; `PYTHONHOME`'s *value* is queried directly from the target
  interpreter via `sys.prefix` at CMake-configure time rather than derived
  by counting directories up from `DD_CVIEW_PYTHON`, since how many levels
  that takes differs by both platform *and* env flavor (conda-on-Windows:
  the interpreter already sits in the env root; conda-on-POSIX and a POSIX
  venv's `bin/`: one level up; a Windows venv's `Scripts/`: also one level
  up) -- `sys.prefix` sidesteps guessing which convention applies. On
  macOS/Linux, `dd_cview`'s CMake target also gets an explicit
  `INSTALL_RPATH` pointing at that same env's `lib/` directory -- without
  it, an installed (`cmake --install`) binary dies at startup with a
  dynamic-linker error (`Library not loaded: @rpath/libpython3.12.dylib` /
  `error while loading shared libraries: libpython3.so...`) before even
  reaching `main()`, since CMake only adds this automatically for the
  *build-tree* binary, not an installed one.
- **An installed binary finds its own backend module relative to itself,
  not the source checkout.** `PythonBridge.cpp`'s `resolvePythonDir()`
  first looks for `python/dd_cview_backend.py` next to the running
  executable (`QCoreApplication::applicationDirPath()`) -- the layout
  `cmake --install` produces (see [Installing the built
  binary](#installing-the-built-binary)) -- and only falls back to the
  compile-time source-tree path (`DD_CVIEW_PYTHON_DIR`) if that's not
  there, which is what makes a `cmake --install`'d directory genuinely
  relocatable (copy/move it anywhere) instead of permanently depending on
  the exact build directory it came from.

## Sample data (`data/`)

Same bundled SARS-CoV-2 Mpro sample set the retired `dd_molview` used
(`6W63_receptor.pdb` / `6W63_redock.sdf` / `6W63_ligand_ref.sdf`,
`7L10.pdb` / `7L11.pdb`, `sample_manifest.json`), also used by
`python/tests/`'s pytest suite.

## Verified behavior

`tests/bridge_test.cpp` (registered as the `bridge_smoke_test` CTest case)
is an integration smoke test of `PythonBridge` alone, no Qt GUI involved:
loading the bundled 6W63 + 7L10 receptors and 6W63's 9 redocked poses plus
its reference ligand, and checking every `PythonBridge` method against
them (table row/column counts, non-empty scene HTML and sequence HTML,
correct link schemes, a populated contact table, and a real score/RMSD)
returns sane values end to end through the actual embedded interpreter --
run it with `ctest --test-dir build`.

The full GUI was verified headless (`QT_QPA_PLATFORM=offscreen`, screenshots
via the `DD_CVIEW_SCREENSHOT` env var / `QWidget::grab()`, same technique
`dd_molview-desktop`'s own test suite uses): launching with the bundled
6W63 + 7L10 receptors, 6W63's poses, and its reference ligand populates the
Proteins/Ligands tables and the Chains panel (including yellow contact-
residue highlighting) correctly; launching with no arguments shows the
correct empty state (empty tables, "Show reference ligand" disabled); and
launching with the process's working directory set to the parent directory
shared by `dd_viewer`/`dd_cview` (the exact scenario the
`sys.path` fix above addresses) still loads data correctly. As with
`dd_molview-desktop`, real `QWebEngineView` WebGL rendering isn't
observable under `QT_QPA_PLATFORM=offscreen` -- the 3D view's actual
on-screen rendering and camera-preservation behavior needs a real display
to confirm visually (this also means "Save 3D View Screenshot..." itself
hasn't been visually confirmed to capture real rendered content, only that
it doesn't crash and writes *a* PNG); the interaction/camera *logic*
itself is exercised (unmodified) by `dd_viewer`'s own test suite and
`dd_cview_core`'s (`python/tests/`), which this project doesn't duplicate.

`cmake --install`'s relocatability was verified directly: installing to a
throwaway prefix (`cmake --install build --prefix /tmp/...`) and running
the installed binary from a different working directory than either the
build tree or the install prefix, with `--receptor`/`--poses` pointing at
the installed copy's own `data/` directory, loads correctly -- confirming
both `resolvePythonDir()`'s relative-to-executable lookup and the
`INSTALL_RPATH` fix (without which the installed binary fails immediately
with a dynamic-linker error before reaching `main()`, on macOS at least;
not separately confirmed on Linux, though the same `cmake --install`
rpath-stripping default applies there too). Only the macOS build itself,
and this macOS install flow, were actually exercised in this project --
the Ubuntu/Windows build and install instructions above are
standard-convention but unverified here (see
[Installation](#installation)), **except** for the
`mamba create`/conda-Qt6 configure+build+`ctest` path (using the dedicated
`dd_cview` env, `qt6-main`/`qt6-webengine`), which was exercised end to
end on Ubuntu (`bridge_smoke_test` passing) when that env was split off
from the shared `dd` one -- the full headless-GUI screenshot check and
`cmake --install` relocatability above were not re-run there, only on
macOS.

## License

MIT — see [LICENSE](LICENSE).
