"""Embedded-Python backend for dd_cview's native C++/Qt GUI.

dd_cview reimplements the GUI shell (windows, docks, tables, the sequence
widget, the 3D-view container, event wiring) that the retired PySide6
`dd_molview-desktop` app used to provide, in native C++/Qt, while reusing
`dd_viewer` (PDB/SDF parsing, contact/interaction detection, scoring, RMSD,
scene-HTML generation) and `dd_cview_core` (multi-receptor/ligand
collections, sequence extraction/HTML rendering, dashboard tables --
absorbed from `dd_molview`'s core logic, unmodified, when `dd_molview` was
retired in favor of this project) unmodified, through an embedded Python
interpreter (pybind11). This module is the one, narrow surface the C++ side
(`PythonBridge`, see `src/PythonBridge.cpp`) calls into: every method here
takes/returns only plain strings (JSON) or primitives -- never a `pandas
.DataFrame`, a `dd_viewer.Receptor`, or any other Python-only object crosses
into C++ -- so the pybind11 boundary itself stays trivial (no custom type
casters, no `py::object` bookkeeping on the C++ side beyond one `Session`
handle).

`Session` was modeled on `dd_molview-desktop`'s own `MainWindow`'s
Python-side state and orchestration (`receptor_entries`, `ligand_entries`,
`active_receptor_idx`, `active_ligand_idx`, `reference_mol`, and the
`_refresh_view`-equivalent recompute step) one-for-one -- the C++
`MainWindow` owns the *display* state instead (which dock is visible, the
display-settings checkboxes, the sequence panel's selected residues, the
live 3D-view camera position), and passes it into each call.
"""
from __future__ import annotations

import json
import math
from typing import Optional

import dd_viewer as dv
import dd_cview_core as dm


def _json_safe(value):
    """`json.dumps` chokes on numpy scalars and NaN/inf -- normalize both."""
    if value is None:
        return None
    if isinstance(value, float):
        return None if (math.isnan(value) or math.isinf(value)) else value
    if hasattr(value, "item"):  # numpy scalar (int64, float64, bool_, ...)
        return _json_safe(value.item())
    return value


def _df_to_table_json(df) -> str:
    """Same string-cast convention as the Python desktop app's
    `DataFrameTableModel.data()` (`"" if pd.isna(value) else str(value)`),
    so a `dd_cview` table renders identically to its `dd_molview` counterpart
    -- plus the raw (JSON-safe, non-stringified) values in `raw_rows`, which
    `Session`'s own callers need for keys like the `index`/`chain`/`resnum`
    columns (row -> `LigandEntry`/`ReceptorEntry` position, contact-row ->
    `(chain, resnum)`) without re-parsing a display string back into a
    number.
    """
    columns = [str(c) for c in df.columns]
    display_rows = []
    raw_rows = []
    for _, row in df.iterrows():
        display_rows.append(["" if _is_na(v) else str(v) for v in row])
        raw_rows.append([_json_safe(v) for v in row])
    return json.dumps({"columns": columns, "rows": display_rows, "raw_rows": raw_rows})


def _is_na(value) -> bool:
    try:
        return bool(value != value)  # NaN != NaN; works for pandas NA-likes too
    except Exception:
        return False


class Session:
    """One dd_cview window's worth of state. Not thread-safe; every method
    is meant to be called from the Qt main thread only (matching the
    original desktop app's single-threaded Qt event-loop model -- nothing
    here releases the GIL or expects to run off it).
    """

    def __init__(self) -> None:
        self.receptor_entries: list = []
        self.ligand_entries: list = []
        self.active_receptor_idx: int = 0
        self.active_ligand_idx: int = 0
        self.reference_mol = None

    # ------------------------------------------------------------------
    # Loading
    # ------------------------------------------------------------------
    def load_all(self, receptor_paths, poses_paths, reference_path, manifest_path) -> None:
        self.receptor_entries, self.ligand_entries = dm.load_all(
            list(receptor_paths), manifest_path or None, list(poses_paths),
        )
        self.reference_mol = dv.load_reference_ligand(reference_path) if reference_path else None
        self.active_receptor_idx = min(self.active_receptor_idx, max(len(self.receptor_entries) - 1, 0))
        self.active_ligand_idx = 0

    def receptor_count(self) -> int:
        return len(self.receptor_entries)

    def ligand_count(self) -> int:
        return len(self.ligand_entries)

    def has_reference(self) -> bool:
        return self.reference_mol is not None

    # ------------------------------------------------------------------
    # Active-row state
    # ------------------------------------------------------------------
    def set_active_receptor(self, index: int) -> None:
        self.active_receptor_idx = index

    def set_active_ligand(self, index: int) -> None:
        self.active_ligand_idx = index

    def active_receptor_index(self) -> int:
        return self.active_receptor_idx

    def active_ligand_index(self) -> int:
        return self.active_ligand_idx

    def _active_receptor(self):
        if not self.receptor_entries:
            return None
        return self.receptor_entries[self.active_receptor_idx].receptor

    def _active_pose(self):
        if not self.ligand_entries:
            return None
        idx = min(self.active_ligand_idx, len(self.ligand_entries) - 1)
        entry = self.ligand_entries[idx]
        # See LigandEntry.receptor_index: an auto-extracted co-crystal
        # ligand belongs only to the receptor it came from.
        if entry.receptor_index is not None and entry.receptor_index != self.active_receptor_idx:
            return None
        return entry.pose

    # ------------------------------------------------------------------
    # Tables
    # ------------------------------------------------------------------
    def protein_table_json(self) -> str:
        return _df_to_table_json(dm.receptors_dataframe(self.receptor_entries))

    def ligand_table_json(self) -> str:
        """Rebuilds against the *active* receptor only (dashboard.py's
        O(n_ligands), never O(n_receptors * n_ligands), contract) and, if
        the currently active ligand index isn't visible for this receptor
        any more, snaps it to the first visible entry -- same fallback
        `main_window.py._refresh_ligand_table` performs.
        """
        receptor = self._active_receptor()
        df = dm.ligands_dataframe(
            self.ligand_entries, receptor=receptor,
            active_receptor_index=self.active_receptor_idx, reference_mol=self.reference_mol,
        )
        if len(df):
            visible = df["index"].tolist()
            if self.active_ligand_idx not in visible:
                self.active_ligand_idx = int(visible[0])
        return _df_to_table_json(df)

    def contact_table_json(self, cutoff: float) -> str:
        receptor = self._active_receptor()
        pose = self._active_pose()
        if receptor is None or pose is None:
            return json.dumps({"columns": [], "rows": [], "raw_rows": []})
        df = dv.find_contact_residues(receptor, pose.mol, cutoff=cutoff)
        return _df_to_table_json(df)

    # ------------------------------------------------------------------
    # Sequence panel
    # ------------------------------------------------------------------
    def sequence_html(self, highlight_json: str, selected_json: str) -> str:
        receptor = self._active_receptor()
        if receptor is None:
            return ""
        sequences = dm.extract_sequences(receptor)
        highlight = [tuple(pair) for pair in json.loads(highlight_json)]
        selected = [tuple(pair) for pair in json.loads(selected_json)]
        return dm.sequence_to_html(
            sequences, highlight_residues=highlight, selected_residues=selected, clickable=True,
        )

    def residue_link_scheme(self) -> str:
        return dm.sequence.RESIDUE_LINK_SCHEME

    def chain_link_scheme(self) -> str:
        return dm.sequence.CHAIN_LINK_SCHEME

    # ------------------------------------------------------------------
    # 3D view
    # ------------------------------------------------------------------
    def build_view_html(self, settings_json: str, selected_residues_json: str, saved_camera_json: str) -> str:
        """Returns a JSON object: `{html, viewer_var, hbonds, hydrophobic,
        salt_bridges, electrostatic, pi_stacking, pi_halogen, sulfur_halogen,
        highlight_residues}` (`html`/`viewer_var` are `null` when nothing is
        loaded yet). The interaction counts and `highlight_residues` (the
        exact (chain, resnum) pairs rendered yellow -- see
        `show_interacting_residues` below) ride along here (rather than a
        separate call) so the C++ side's "Interaction summary" label, the
        rendered scene, and the "Zoom to Highlighted Residues" button all
        describe the exact same computed frame -- a second, separate call
        could race a settings change in between.
        """
        receptor = self._active_receptor()
        pose = self._active_pose()
        if receptor is None and pose is None:
            return json.dumps({
                "html": None, "viewer_var": None, "hbonds": 0, "hydrophobic": 0,
                "salt_bridges": 0, "electrostatic": 0, "pi_stacking": 0,
                "pi_halogen": 0, "sulfur_halogen": 0, "highlight_residues": [],
            })

        settings = json.loads(settings_json)
        selected = [tuple(pair) for pair in json.loads(selected_residues_json)]
        saved_camera = json.loads(saved_camera_json) if saved_camera_json else None

        sequences = dm.extract_sequences(receptor) if receptor is not None else {}
        highlight_residues = None
        hbonds = hydrophobic = salt_bridges = pi_stacking = electrostatic = pi_halogen = sulfur_halogen = []
        if receptor is not None and pose is not None:
            if settings["show_hbonds"]:
                hbonds = dv.find_hydrogen_bonds(receptor, pose.mol)
            if settings["show_hydrophobic"]:
                hydrophobic = dv.find_hydrophobic_contacts(receptor, pose.mol)
            if settings["show_salt_bridges"]:
                salt_bridges = dv.find_salt_bridges(receptor, pose.mol)
            if settings["show_pi_stacking"]:
                pi_stacking = dv.find_pi_stacking(receptor, pose.mol)
            if settings["show_electrostatic"]:
                electrostatic = dv.find_electrostatic_interactions(receptor, pose.mol)
            if settings["show_pi_halogen"]:
                pi_halogen = dv.find_pi_halogen_bonds(receptor, pose.mol)
            if settings["show_sulfur_halogen"]:
                sulfur_halogen = dv.find_sulfur_halogen_bonds(receptor, pose.mol)
            if settings["show_interacting_residues"]:
                # Every residue behind at least one *currently enabled*
                # interaction type -- not merely every residue within
                # `contact_cutoff` (that broader, purely distance-based set
                # stays available, unaffected by this flag, via the
                # separate contact_table_json/find_contact_residues call).
                # Contact/RingContact both carry chain/resnum identifying
                # the receptor residue, uniformly across every interaction
                # type.
                residues = {
                    (contact.chain, contact.resnum)
                    for contact in (*hbonds, *hydrophobic, *salt_bridges, *pi_stacking,
                                     *electrostatic, *pi_halogen, *sulfur_halogen)
                }
                highlight_residues = sorted(residues)

        view = dv.build_view(
            receptor=receptor, pose_mol=pose.mol if pose is not None else None,
            receptor_style=settings["receptor_style"],
            color_by_secondary_structure=settings["color_by_ss"],
            highlight_residues=highlight_residues, selected_residues=selected,
            residue_labels=dm.residue_labels(sequences, selected),
            hbonds=hbonds, hydrophobic_contacts=hydrophobic, salt_bridges=salt_bridges,
            pi_stacking=pi_stacking, electrostatic=electrostatic,
            pi_halogen_bonds=pi_halogen, sulfur_halogen_bonds=sulfur_halogen,
            reference_mol=self.reference_mol if settings.get("show_reference") else None,
            only_near_ligand=settings["only_near_ligand"],
        )
        html = dv.html_fill_container(view._make_html())
        html = dv.html_with_initial_view(html, saved_camera)
        viewer_var = dv.get_viewer_variable(html)
        return json.dumps({
            "html": html, "viewer_var": viewer_var,
            "hbonds": len(hbonds), "hydrophobic": len(hydrophobic),
            "salt_bridges": len(salt_bridges), "electrostatic": len(electrostatic),
            "pi_stacking": len(pi_stacking), "pi_halogen": len(pi_halogen),
            "sulfur_halogen": len(sulfur_halogen),
            "highlight_residues": [list(pair) for pair in (highlight_residues or [])],
        })

    # ------------------------------------------------------------------
    # Detail labels (score / RMSD)
    # ------------------------------------------------------------------
    def detail_info_json(self) -> str:
        pose = self._active_pose()
        score = _json_safe(dv.detect_score(pose)) if pose is not None else None
        rmsd = None
        if pose is not None and self.reference_mol is not None:
            raw = dv.rmsd_to_reference(pose.mol, self.reference_mol)
            rmsd = round(raw, 2) if raw is not None else None
        return json.dumps({"score": score, "rmsd": rmsd, "has_pose": pose is not None})


def create_session() -> Session:
    return Session()
