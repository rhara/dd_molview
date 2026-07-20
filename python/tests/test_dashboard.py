"""Tests for dd_cview_core.dashboard: protein/ligand summary tables."""
from dd_cview_core.collections import LigandEntry, load_all, load_poses_multi, load_receptors
from dd_cview_core.dashboard import ligands_dataframe, receptors_dataframe


class TestReceptorsDataframe:
    def test_columns_and_row_count(self, data_dir):
        entries = load_receptors([
            str(data_dir / "6W63_receptor.pdb"),
            str(data_dir / "7L10.pdb"),
            str(data_dir / "7L11.pdb"),
        ])
        df = receptors_dataframe(entries)
        assert len(df) == 3
        assert list(df.columns) == ["index", "label", "n_chains", "n_residues", "source"]
        # 7L11 has 2 chains (A/B), the other two have 1 (A only)
        n_chains_by_label = dict(zip(df["label"], df["n_chains"]))
        assert n_chains_by_label["7L11"] == 2
        assert n_chains_by_label["7L10"] == 1


class TestLigandsDataframe:
    def test_includes_source_and_index_columns(self, data_dir):
        entries = load_poses_multi([str(data_dir / "6W63_redock.sdf"), str(data_dir / "6W63_ligand_ref.sdf")])
        df = ligands_dataframe(entries)
        assert "index" in df.columns
        assert "source" in df.columns
        assert "pose_index" in df.columns
        assert list(df["index"]) == list(range(len(entries)))
        assert df["source"].tolist().count("6W63_redock") == 9
        assert df["source"].tolist().count("6W63_ligand_ref") == 1

    def test_interaction_columns_present_when_receptor_given(self, data_dir):
        receptor = load_receptors([str(data_dir / "6W63_receptor.pdb")])[0].receptor
        entries = load_poses_multi([str(data_dir / "6W63_redock.sdf")])
        df = ligands_dataframe(entries, receptor=receptor)
        assert "n_contact_residues" in df.columns
        assert "n_hbonds" in df.columns


class TestLigandsDataframeReceptorFiltering:
    def test_active_receptor_index_none_keeps_every_entry(self, data_dir):
        # default/pre-filtering behavior: no active_receptor_index given.
        _, ligand_entries = load_all(
            receptor_paths=[str(data_dir / "7L10.pdb"), str(data_dir / "6W63_receptor.pdb")],
            manifest_path=None, poses_paths=[],
        )
        df = ligands_dataframe(ligand_entries)
        assert len(df) == len(ligand_entries)

    def test_entry_from_a_different_receptor_is_filtered_out(self, data_dir):
        # 7L10.pdb's own co-crystal ligand (receptor_index=0) must not show
        # up once receptor 1 (6W63_receptor, no ligand) is the active one.
        _, ligand_entries = load_all(
            receptor_paths=[str(data_dir / "7L10.pdb"), str(data_dir / "6W63_receptor.pdb")],
            manifest_path=None, poses_paths=[],
        )
        assert len(ligand_entries) >= 1
        df = ligands_dataframe(ligand_entries, active_receptor_index=1)
        assert len(df) == 0

    def test_entry_matching_active_receptor_stays_visible(self, data_dir):
        _, ligand_entries = load_all(
            receptor_paths=[str(data_dir / "7L10.pdb"), str(data_dir / "6W63_receptor.pdb")],
            manifest_path=None, poses_paths=[],
        )
        df = ligands_dataframe(ligand_entries, active_receptor_index=0)
        assert len(df) == len(ligand_entries)

    def test_entry_with_no_receptor_index_always_stays_visible(self, data_dir):
        pose = load_poses_multi([str(data_dir / "6W63_redock.sdf")])[0].pose
        entries = [LigandEntry(pose=pose, source="x", receptor_index=None)]
        df = ligands_dataframe(entries, active_receptor_index=5)
        assert len(df) == 1

    def test_index_column_is_original_position_not_a_compacted_range(self, data_dir):
        # entries[1] (receptor_index=1) is dropped when active_receptor_index=0
        # -- the surviving row's "index" must still point at its real position
        # in the original list (2), not a freshly compacted 1.
        poses = [e.pose for e in load_poses_multi([str(data_dir / "6W63_redock.sdf")])][:3]
        entries = [
            LigandEntry(pose=poses[0], source="a", receptor_index=0),
            LigandEntry(pose=poses[1], source="b", receptor_index=1),
            LigandEntry(pose=poses[2], source="c", receptor_index=0),
        ]
        df = ligands_dataframe(entries, active_receptor_index=0)
        assert list(df["index"]) == [0, 2]
