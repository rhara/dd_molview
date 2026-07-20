"""Tests for dd_cview_core.collections: multi-receptor / multi-ligand loading."""
from dd_cview_core.collections import (
    LigandEntry,
    ReceptorEntry,
    load_all,
    load_poses_multi,
    load_receptors,
    load_receptors_from_manifest,
)


class TestLoadReceptors:
    def test_loads_one_entry_per_path_with_stem_label(self, data_dir):
        entries = load_receptors([str(data_dir / "6W63_receptor.pdb"), str(data_dir / "7L10.pdb")])
        assert len(entries) == 2
        assert all(isinstance(e, ReceptorEntry) for e in entries)
        assert entries[0].label == "6W63_receptor"
        assert entries[1].label == "7L10"
        assert entries[0].receptor.source == str(data_dir / "6W63_receptor.pdb")

    def test_empty_input_gives_empty_list(self):
        assert load_receptors([]) == []


class TestLoadPosesMulti:
    def test_concatenates_and_tags_source(self, data_dir):
        # 6W63_redock.sdf has 9 poses; 6W63_ligand_ref.sdf has 1.
        entries = load_poses_multi([str(data_dir / "6W63_redock.sdf"), str(data_dir / "6W63_ligand_ref.sdf")])
        assert all(isinstance(e, LigandEntry) for e in entries)
        sources = [e.source for e in entries]
        assert sources.count("6W63_redock") == 9
        assert sources.count("6W63_ligand_ref") == 1

    def test_list_position_is_a_stable_global_index(self, data_dir):
        # Each source SDF's own Pose.index restarts at 0; the combined
        # list's position must not collide across sources.
        entries = load_poses_multi([str(data_dir / "6W63_redock.sdf"), str(data_dir / "6W63_ligand_ref.sdf")])
        assert len(entries) == len(set(range(len(entries))))  # trivially true; real check is pose.index below
        pose_indices_by_source = {}
        for i, entry in enumerate(entries):
            pose_indices_by_source.setdefault(entry.source, []).append(entry.pose.index)
        # both sources' own Pose.index sequences independently start at 0 --
        # confirming list position (not Pose.index) is what callers must use.
        assert pose_indices_by_source["6W63_redock"][0] == 0
        assert pose_indices_by_source["6W63_ligand_ref"][0] == 0


class TestLoadReceptorsFromManifest:
    def test_loads_labeled_entries_with_relative_paths(self, data_dir):
        entries = load_receptors_from_manifest(str(data_dir / "sample_manifest.json"))
        assert len(entries) == 2
        labels = {e.label for e in entries}
        assert labels == {"7L10", "7L11"}

    def test_skips_missing_receptor_pdb_without_raising(self, tmp_path, data_dir):
        manifest = tmp_path / "manifest.json"
        manifest.write_text(
            '[{"member_id": "ok", "receptor_pdb": "%s"}, '
            '{"member_id": "missing", "receptor_pdb": "does_not_exist.pdb"}]'
            % (data_dir / "7L10.pdb")
        )
        entries = load_receptors_from_manifest(str(manifest))
        assert len(entries) == 1
        assert entries[0].label == "ok"


class TestLoadAll:
    def test_poses_given_disables_auto_extraction_for_every_receptor(self, data_dir):
        receptor_entries, ligand_entries = load_all(
            receptor_paths=[str(data_dir / "6W63_receptor.pdb"), str(data_dir / "7L10.pdb")],
            manifest_path=None,
            poses_paths=[str(data_dir / "6W63_redock.sdf")],
        )
        assert len(receptor_entries) == 2
        assert len(ligand_entries) == 9  # only the explicit SDF's poses, no auto-extraction
        assert all(e.source == "6W63_redock" for e in ligand_entries)

    def test_no_poses_triggers_auto_extraction_per_receptor(self, data_dir):
        # 7L10.pdb is a raw co-crystal PDB (receptor + bound ligand in one
        # file); 6W63_receptor.pdb has already had its ligand stripped, so
        # it wouldn't exercise auto-extraction.
        receptor_entries, ligand_entries = load_all(
            receptor_paths=[str(data_dir / "7L10.pdb")],
            manifest_path=None,
            poses_paths=[],
        )
        assert len(receptor_entries) == 1
        assert len(ligand_entries) >= 1  # embedded co-crystal ligand auto-extracted

    def test_auto_extracted_ligands_are_tagged_with_their_own_receptor_index(self, data_dir):
        # 7L10.pdb and 7L11.pdb are both raw co-crystal PDBs; 6W63_receptor.pdb
        # has no embedded ligand at all -- its receptor position contributes
        # zero ligand_entries, so no entry should ever be tagged with it.
        receptor_entries, ligand_entries = load_all(
            receptor_paths=[str(data_dir / "7L10.pdb"), str(data_dir / "6W63_receptor.pdb"), str(data_dir / "7L11.pdb")],
            manifest_path=None,
            poses_paths=[],
        )
        assert len(receptor_entries) == 3
        receptor_indices = {e.receptor_index for e in ligand_entries}
        assert receptor_indices == {0, 2}  # never 1 (6W63_receptor has no ligand)
        assert all(e.receptor_index == 0 for e in ligand_entries if e.source == "7L10")
        assert all(e.receptor_index == 2 for e in ligand_entries if e.source == "7L11")

    def test_explicit_poses_have_no_receptor_index(self, data_dir):
        # An explicitly-loaded pose isn't tied to any one receptor -- it
        # stays selectable against every receptor in the ensemble.
        _, ligand_entries = load_all(
            receptor_paths=[str(data_dir / "6W63_receptor.pdb"), str(data_dir / "7L10.pdb")],
            manifest_path=None,
            poses_paths=[str(data_dir / "6W63_redock.sdf")],
        )
        assert all(e.receptor_index is None for e in ligand_entries)

    def test_manifest_and_direct_receptors_are_combined_additively(self, data_dir):
        receptor_entries, ligand_entries = load_all(
            receptor_paths=[str(data_dir / "6W63_receptor.pdb")],
            manifest_path=str(data_dir / "sample_manifest.json"),
            poses_paths=[str(data_dir / "6W63_redock.sdf")],
        )
        labels = {e.label for e in receptor_entries}
        assert labels == {"6W63_receptor", "7L10", "7L11"}
