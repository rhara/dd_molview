[Japanese version](README.jp.md)

# dd_cview

A native C++/Qt6 reimplementation of
[`dd_molview`](../dd_molview)'s desktop GUI shell -- the same four-panel
workbench (protein table / ligand table / 3D view / sequence view) for
interactive protein-ligand structure analysis, with every window, dock,
table, and event handler rewritten in C++/Qt instead of PySide6. The
computational logic underneath (PDB/SDF parsing, contact/interaction
detection, scoring, RMSD, per-chain sequence extraction, 3D-scene HTML
generation) is **not** reimplemented: `dd_cview` embeds a Python
interpreter (via [pybind11](https://github.com/pybind/pybind11)) and calls
straight into `dd_viewer`/`dd_molview`'s existing, unmodified Python
packages for all of it, through one narrow JSON-in/JSON-out module,
[`python/dd_cview_backend.py`](python/dd_cview_backend.py).

`dd_molview-desktop` (PySide6) is functionally complete but noticeably
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
        dd_viewer (parsing, interactions, scoring, scene HTML)
        dd_molview (multi-receptor/ligand collections, sequence, dashboard)
```

`PythonBridge` (`src/PythonBridge.h`/`.cpp`) is the only place `<pybind11/
embed.h>` (and therefore `Python.h`) is included -- every other C++ file
only sees plain Qt/std types (`QString`, `QJsonArray`, `TableData`,
`DisplaySettings`, ...), never a `py::object`. This also sidesteps a real
build hazard: `Python.h` and Qt's `qobjectdefs.h` both define a bare
`slots` symbol, so any translation unit that includes both breaks -- keeping
the pybind11 header confined to one non-`QObject` `.cpp` file avoids it
entirely, rather than fighting it with `QT_NO_KEYWORDS` everywhere.

`Session` (the Python-side counterpart) mirrors
`dd_molview.desktop.main_window.MainWindow`'s own state and orchestration
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

Requires Qt6 (`Core`, `Widgets`, `WebEngineWidgets`, `WebChannel`), CMake
≥3.21, a C++20 compiler, and the same Python environment `dd_molview`
itself runs in (`dd_viewer` + `dd_molview` installed, editable or not, plus
their own dependencies -- RDKit, biopython, pandas, numpy -- and
`pybind11`, which only needs to be importable from that environment, not
separately installed as a system package).

```bash
# Qt6 + build tooling (macOS/Homebrew)
brew install cmake qt ninja

# the same conda env dd_molview/dd_viewer already run in (see
# ../dd_molview/README.md for the full, from-scratch setup)
conda activate dd
pip install pybind11   # if not already present

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

By default, the build embeds whichever Python `$CONDA_PREFIX/bin/python3`
points at when CMake is configured (falling back to `~/miniforge3/envs/dd`
if no conda env is active) -- override with
`-DDD_CVIEW_PYTHON=/path/to/python3` to point at a different environment
(a differently-named conda env, a plain venv) that has `dd_viewer`/
`dd_molview` installed.

```bash
cmake -S . -B build -G Ninja -DDD_CVIEW_PYTHON=/path/to/venv/bin/python3
```

**Note**: like `dd_molview-desktop`, the 3D view is a `QWebEngineView`
(Qt's `WebEngineWidgets` module), so Homebrew's full `qt` formula (which
pulls in `qtwebengine`, a Chromium-based ~38-package dependency chain) is
required -- the smaller `qtbase`-only install is not enough.

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
all-or-nothing auto-extraction rule as `dd_molview` (see its README for the
full contract); this logic runs unmodified inside `dd_molview.load_all`,
`dd_cview` just calls it.

### Ensemble manifest.json

```bash
./build/dd_cview --manifest data/sample_manifest.json
```

Same dd_docking-style `manifest.json` format `dd_molview` reads (a plain
JSON list of `{member_id, receptor_pdb, ...}` objects, duck-typed, no
`dd_docking` import) -- loaded additively alongside any `--receptor` paths.

## Features

Every panel behaves identically to `dd_molview-desktop`'s own (same
underlying `dd_viewer`/`dd_molview` calls, just through native `QTableView`/
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
  `dd_molview-desktop` uses.
- **Sequence panel**: fixed 50-residues-per-line resnum grid with a
  line-start ruler, yellow/magenta two-tier highlighting matching the 3D
  view, clickable residues (multi-select with Ctrl/Cmd-click) and
  clickable "Chain X" headers (re-fits the camera to that chain).
- **Contact-residue table** (collapsed by default): selecting rows
  highlights the same residues in both the 3D view and the sequence panel.
- **Every panel is its own dock** -- movable, floatable, closable
  independently, restored across sessions via `QSettings`
  (`saveGeometry`/`saveState`).

**Camera behavior** and **multi-residue selection** follow the exact same
rules as `dd_molview-desktop` (see [its
README](../dd_molview/README.md#usage) for the full writeup) -- switching
the active protein/ligand row or toggling a display setting never moves
the camera; only dragging/zooming it yourself or an explicit "Center on
Ligand" does.

## Module structure (`src/`)

| File | Contents |
|---|---|
| `PythonBridge.h`/`.cpp` | The only file that touches `<pybind11/embed.h>`. Owns the embedded interpreter (`py::scoped_interpreter`) and the one `Session` object; every method converts to/from `QJsonDocument` and plain Qt/std types. |
| `MainWindow.h`/`.cpp` | Dock construction, menu/toolbar, layout persistence, all Qt signal handlers, and the central `refreshView()` recompute-and-redraw method -- the C++ analog of `dd_molview.desktop.main_window.MainWindow`. |
| `TableModel.h`/`.cpp` | One reusable read-only `QAbstractTableModel` wrapping a `TableData` (columns/display-rows/raw-rows), used for all three tables. |
| `DisplaySettingsPanel.h`/`.cpp` | The settings dock's controls -- one-to-one with `dd_molview.desktop.controls.DisplaySettingsPanel`. |
| `SequencePanel.h`/`.cpp` | A `QTextBrowser` subclass with the sequence panel's font/link-handling setup. |
| `Viewer3D.h`/`.cpp` | Wraps the 3D view's `QWebEngineView`: loading a freshly built scene, the async `getView()` camera-capture round-trip, and `zoomTo({chain})` for chain-header clicks. |
| `main.cpp` | Entry point: CLI parsing (`--receptor`/`--poses`/`--reference`/`--manifest`), `QApplication` setup, and an optional `DD_CVIEW_SCREENSHOT` env-var hook for headless verification (see below). |

`python/dd_cview_backend.py` is the Python-side counterpart -- see the
[Architecture](#architecture) section above.

## Design notes

- **No computational logic is duplicated in C++.** Every PDB/SDF parse,
  distance calculation, RDKit call, or HTML-generation step happens in
  Python, inside `dd_viewer`/`dd_molview`, completely unmodified. `dd_cview`
  only ever passes primitives across the pybind11 boundary.
- **`PythonBridge` never leaks a `py::object`.** Its public API (see
  `PythonBridge.h`) is Qt/std types only, so no other file in the project
  needs to know pybind11 exists, and the `Python.h`/`qobjectdefs.h` `slots`
  clash never has a chance to occur outside `PythonBridge.cpp`.
- **PYTHONHOME is set explicitly, and `sys.path`'s cwd entry is stripped.**
  `dd_viewer`, `dd_molview`, and `dd_cview` all live as sibling directories
  under the same parent (`~/work`); if `dd_cview` is launched with that
  parent as its working directory, a bare `""` `sys.path` entry resolves
  `import dd_viewer` to a broken *namespace* package rooted at the
  directory literally named `dd_viewer` sitting right there (no
  `__init__.py` at that level) instead of the real editable-installed
  package -- silently, with `dd_viewer.__file__` coming back `None` rather
  than raising. `PythonBridge`'s constructor strips any empty/cwd-derived
  `sys.path` entry before importing anything, so this can't happen
  regardless of where the binary is launched from.
- **The GIL is never released.** Every `PythonBridge` call happens
  synchronously on the Qt main thread, matching the original
  `dd_molview-desktop`'s own single-threaded Qt event-loop model -- no
  background thread ever touches the interpreter, so there's no need for
  `py::gil_scoped_release`/`acquire` anywhere.

## Sample data (`data/`)

Same bundled SARS-CoV-2 Mpro sample set as `dd_molview` (`6W63_receptor.pdb`
/ `6W63_redock.sdf` / `6W63_ligand_ref.sdf`, `7L10.pdb` / `7L11.pdb`,
`sample_manifest.json`) -- see [its README](../dd_molview/README.md#sample-data-data)
for what each file is for.

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
shared by `dd_viewer`/`dd_molview`/`dd_cview` (the exact scenario the
`sys.path` fix above addresses) still loads data correctly. As with
`dd_molview-desktop`, real `QWebEngineView` WebGL rendering isn't
observable under `QT_QPA_PLATFORM=offscreen` -- the 3D view's actual
on-screen rendering and camera-preservation behavior needs a real display
to confirm visually; the interaction/camera *logic* itself is exercised
(unmodified) by `dd_viewer`/`dd_molview`'s own test suites, which this
project doesn't duplicate.

## License

MIT — see [LICENSE](LICENSE).
