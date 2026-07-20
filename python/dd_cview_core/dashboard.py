"""Summary tables for the protein- and ligand-selection panels."""
from pathlib import Path
from typing import Optional, Sequence

import dd_viewer as dv
import pandas as pd
from rdkit import Chem

from .collections import LigandEntry, ReceptorEntry
from .sequence import extract_sequences


def receptors_dataframe(entries: Sequence[ReceptorEntry]) -> pd.DataFrame:
    """One row per loaded receptor: index, label, chain/residue counts
    (via `sequence.extract_sequences`, standard amino acids only), source
    path. For the protein-selection panel.
    """
    rows = []
    for i, entry in enumerate(entries):
        seqs = extract_sequences(entry.receptor)
        rows.append({
            "index": i,
            "label": entry.label or Path(entry.receptor.source).stem,
            "n_chains": len(seqs),
            "n_residues": sum(len(r) for r in seqs.values()),
            "source": entry.receptor.source,
        })
    return pd.DataFrame(rows)


def ligands_dataframe(
    entries: Sequence[LigandEntry],
    receptor: Optional[dv.Receptor] = None,
    active_receptor_index: Optional[int] = None,
    reference_mol: Optional[Chem.Mol] = None,
) -> pd.DataFrame:
    """One row per loaded ligand *visible for the active receptor*. Delegates
    the score/RMSD/interaction-count columns to `dd_viewer.poses_dataframe`
    (reusing its logic rather than reimplementing interaction counting),
    then merges in `index` (this list's own position -- the stable
    row-selection key) and `source` (which file this ligand came from)
    columns.

    `receptor` should be the currently *active* receptor only -- passing
    all loaded receptors and looping here would make interaction-count
    computation O(n_receptors * n_ligands) instead of O(n_ligands).

    `active_receptor_index` drops any entry whose `receptor_index` names a
    *different* receptor than this one -- an embedded co-crystal ligand
    belongs only to the structure it was extracted from (see
    `LigandEntry.receptor_index`), so it shouldn't still show up, selectable,
    once a different receptor becomes active. An entry with
    `receptor_index=None` (an explicitly-loaded pose, not auto-extracted)
    always stays visible, regardless of which receptor is active. Passing
    `None` here (the default) disables this filtering entirely -- every
    entry stays visible, the pre-filtering behavior.

    `dv.poses_dataframe` already returns an `index` column, but it's
    `Pose.index` -- the pose's position *within its own source file*, which
    is not unique once poses from several files are combined here. That
    column is kept (renamed `pose_index`) for reference, and replaced as
    the row-selection key by each kept entry's own position in `entries`
    (globally unique across every loaded source, by construction) -- not a
    freshly counted 0..N range, so a caller keying off this column (e.g.
    `ligand_entries[row["index"]]`) still lands on the right entry even
    though some rows were filtered out above.
    """
    visible = [
        (i, e) for i, e in enumerate(entries)
        if active_receptor_index is None or e.receptor_index is None or e.receptor_index == active_receptor_index
    ]
    poses_df = dv.poses_dataframe([e.pose for _, e in visible], receptor=receptor, reference_mol=reference_mol)
    poses_df = poses_df.rename(columns={"index": "pose_index"})
    poses_df.insert(0, "index", [i for i, _ in visible])
    poses_df["source"] = [e.source for _, e in visible]
    return poses_df
