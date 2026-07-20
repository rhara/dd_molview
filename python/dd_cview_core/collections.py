"""Multi-receptor / multi-ligand collections built on top of `dd_viewer`.

`dd_viewer.Receptor` has no display-label field and `dd_viewer.Pose` has no
source-file field -- and neither is modified here (dd_viewer is a separate,
untouched dependency, see the project README). Instead, `ReceptorEntry` /
`LigandEntry` wrap them with the extra bookkeeping a multi-item workbench
needs: a short display label for the protein table, and source-file
provenance for the ligand table.
"""
import json
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Sequence

import dd_viewer as dv

logger = logging.getLogger(__name__)


@dataclass
class ReceptorEntry:
    """One row of the protein-selection panel.

    `label` is the short display name (defaults to the file stem; a
    manifest.json-driven load uses the manifest's `member_id` instead,
    since that's a more meaningful name than a generic PDB filename).
    """

    receptor: dv.Receptor
    label: str


@dataclass
class LigandEntry:
    """One row of the ligand-selection panel.

    `source` is which file/upload this pose came from (file stem), so
    ligands loaded from several docking runs can be told apart in the
    combined table.

    `receptor_index` is which `receptor_entries` position this ligand was
    auto-extracted from (see `load_all`'s no-`poses_paths` branch), or
    `None` if it came from an explicit poses SDF instead -- a co-crystal
    ligand pulled out of one specific structure belongs only to that
    structure and should disappear once a different receptor is active,
    unlike an explicitly-loaded docking pose, which is meant to stay
    selectable against every receptor in an ensemble review.
    """

    pose: dv.Pose
    source: str
    receptor_index: Optional[int] = None


def _stem(path: str) -> str:
    return Path(str(path)).stem


def load_receptors(paths: Sequence[str]) -> list[ReceptorEntry]:
    """Load one `ReceptorEntry` per path, in order, each labeled by its
    file stem.
    """
    return [ReceptorEntry(receptor=dv.load_receptor(p), label=_stem(p)) for p in paths]


def load_poses_multi(paths: Sequence[str]) -> list[LigandEntry]:
    """Load poses from multiple SDF files and concatenate them into one
    flat list of `LigandEntry`, each tagged with its source file's stem.

    The list's own position is the stable "global index" for UI row
    selection -- unlike `dd_viewer.Pose.index` (which restarts at 0 for
    each source file and is left untouched here), there's no need to
    re-index anything: `dd_cview_core` owns this list outright.
    """
    entries = []
    for path in paths:
        source = _stem(path)
        for pose in dv.load_poses(path):
            entries.append(LigandEntry(pose=pose, source=source))
    return entries


def load_receptors_from_manifest(path: str) -> list[ReceptorEntry]:
    """Batch-load receptors from a dd_docking-style `manifest.json` -- a
    plain JSON list of objects with at least `member_id` and
    `receptor_pdb` keys. Duck-typed (`json.load` + `.get(...)`), no
    `dd_docking` import, so `dd_cview_core` stays independent of it.

    A relative `receptor_pdb` path is resolved against the manifest
    file's own directory (real dd_docking output uses absolute paths, but
    a bundled sample manifest shipped alongside its PDBs is more portable
    with relative ones -- both work). An entry whose `receptor_pdb` is
    missing/unreadable is skipped (with a warning), not treated as a hard
    failure, so one bad entry doesn't take down the whole batch.
    """
    manifest_path = Path(path)
    entries = json.loads(manifest_path.read_text())

    receptors = []
    for entry in entries:
        member_id = entry.get("member_id", "")
        pdb_path = entry.get("receptor_pdb")
        if not pdb_path:
            logger.warning("Skipping manifest entry %r: no receptor_pdb", member_id)
            continue
        resolved = Path(pdb_path)
        if not resolved.is_absolute():
            resolved = manifest_path.parent / resolved
        if not resolved.exists():
            logger.warning("Skipping manifest entry %r: receptor_pdb not found at %s", member_id, resolved)
            continue
        receptor = dv.load_receptor(str(resolved))
        receptors.append(ReceptorEntry(receptor=receptor, label=member_id or _stem(str(resolved))))
    return receptors


def load_all(
    receptor_paths: Sequence[str], manifest_path: Optional[str], poses_paths: Sequence[str],
) -> tuple[list[ReceptorEntry], list[LigandEntry]]:
    """Load every receptor (from explicit paths and/or a manifest.json)
    and build the combined ligand list.

    All-or-nothing auto-extraction: if any `poses_paths` are given, ligands
    come only from those explicit SDFs (no receptor auto-extracts embedded
    ligands). If none are given, every receptor is scanned via
    `dv.split_structure` (which both produces the protein-only Receptor AND
    extracts any bound ligand(s) in one pass -- `dv.load_receptor` alone
    doesn't strip/extract HETATMs) and its extracted ligands merged into
    one global list.

    Framework-agnostic (no Streamlit/Qt import) so every UI built on top of
    `dd_cview_core` shares this exact branching logic instead of duplicating
    it; a caller wanting caching (e.g. Streamlit's `st.cache_data`) wraps
    this function rather than reimplementing it.
    """
    manifest_entries = load_receptors_from_manifest(manifest_path) if manifest_path else []
    direct_entries = load_receptors(receptor_paths) if receptor_paths else []

    if poses_paths:
        receptor_entries = direct_entries + manifest_entries
        ligand_entries = load_poses_multi(poses_paths)
        return receptor_entries, ligand_entries

    receptor_entries = []
    ligand_entries = []
    for path in receptor_paths:
        receptor, extracted = dv.split_structure(path)
        stem = _stem(path)
        receptor_entries.append(ReceptorEntry(receptor=receptor, label=stem))
        receptor_idx = len(receptor_entries) - 1
        ligand_entries.extend(LigandEntry(pose=p, source=stem, receptor_index=receptor_idx) for p in extracted)
    for entry in manifest_entries:
        receptor, extracted = dv.split_structure(entry.receptor.source)
        receptor_entries.append(ReceptorEntry(receptor=receptor, label=entry.label))
        receptor_idx = len(receptor_entries) - 1
        ligand_entries.extend(LigandEntry(pose=p, source=entry.label, receptor_index=receptor_idx) for p in extracted)
    return receptor_entries, ligand_entries
