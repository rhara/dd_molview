"""Tests for dd_cview_core.sequence: chain sequence extraction + HTML rendering."""
import pandas as pd
import dd_viewer as dv
from dd_viewer.io import ATOM_COLUMNS

from dd_cview_core.sequence import (
    HIGHLIGHT_COLOR,
    SELECTED_COLOR,
    UNKNOWN_RESIDUE_CODE,
    extract_sequences,
    residue_labels,
    sequence_to_html,
)

_STANDARD_AMINO_ACIDS = set("ACDEFGHIKLMNPQRSTVWY")


def _receptor_from_rows(rows) -> dv.Receptor:
    atoms = pd.DataFrame(rows, columns=ATOM_COLUMNS)
    return dv.Receptor(pdb_text="", atoms=atoms, source="<test>")


class TestExtractSequences:
    def test_single_chain_receptor(self, data_dir):
        receptor = dv.load_receptor(str(data_dir / "6W63_receptor.pdb"))
        sequences = extract_sequences(receptor)
        assert list(sequences.keys()) == ["A"]
        assert len(sequences["A"]) > 0
        resnums = [r.resnum for r in sequences["A"]]
        assert resnums == sorted(resnums)

    def test_two_chain_receptor(self, data_dir):
        receptor = dv.load_receptor(str(data_dir / "7L11.pdb"))
        sequences = extract_sequences(receptor)
        assert set(sequences.keys()) == {"A", "B"}
        assert len(sequences["A"]) > 0
        assert len(sequences["B"]) > 0

    def test_codes_are_standard_amino_acids(self, data_dir):
        receptor = dv.load_receptor(str(data_dir / "6W63_receptor.pdb"))
        sequences = extract_sequences(receptor)
        for residues in sequences.values():
            for res in residues:
                assert len(res.code) == 1
                assert res.code in _STANDARD_AMINO_ACIDS

    def test_hetero_skipped_by_default(self):
        receptor = _receptor_from_rows([
            ("A", 1, "ALA", "CA", "C", 0.0, 0.0, 0.0, False),
            ("A", 2, "HOH", "O", "O", 5.0, 0.0, 0.0, True),
        ])
        sequences = extract_sequences(receptor)
        assert [r.resnum for r in sequences["A"]] == [1]

    def test_include_hetero_marks_unknown_as_x(self):
        receptor = _receptor_from_rows([
            ("A", 1, "ALA", "CA", "C", 0.0, 0.0, 0.0, False),
            ("A", 2, "HOH", "O", "O", 5.0, 0.0, 0.0, True),
        ])
        sequences = extract_sequences(receptor, include_hetero=True)
        assert [r.resnum for r in sequences["A"]] == [1, 2]
        assert sequences["A"][1].code == UNKNOWN_RESIDUE_CODE


class TestSequenceToHtml:
    def _sequences(self):
        return {
            "A": [
                type("R", (), {"resnum": 1, "resname": "ALA", "code": "A"})(),
                type("R", (), {"resnum": 2, "resname": "GLY", "code": "G"})(),
                type("R", (), {"resnum": 3, "resname": "SER", "code": "S"})(),
            ]
        }

    def test_no_highlights_renders_without_color_spans(self):
        html = sequence_to_html(self._sequences())
        assert HIGHLIGHT_COLOR not in html
        assert SELECTED_COLOR not in html
        assert ">A<" in html and ">G<" in html and ">S<" in html

    def test_highlighted_residue_uses_highlight_color(self):
        html = sequence_to_html(self._sequences(), highlight_residues=[("A", 2)])
        assert HIGHLIGHT_COLOR in html

    def test_selection_wins_over_highlight(self):
        html = sequence_to_html(
            self._sequences(), highlight_residues=[("A", 2)], selected_residues=[("A", 2)],
        )
        assert SELECTED_COLOR in html
        # the doubly-tagged residue's own span should carry the selected
        # color, not the highlight color
        span_for_res2 = [line for line in html.split("<span") if 'title="GLY2"' in line][0]
        assert SELECTED_COLOR in span_for_res2

    def test_residue_numbers_shown_by_default_at_line_start(self):
        html = sequence_to_html(self._sequences())
        assert "&nbsp;1</span>" in html  # first residue's own resnum, line-start gutter

    def test_residue_numbers_can_be_disabled(self):
        html = sequence_to_html(self._sequences(), show_residue_numbers=False)
        assert "&nbsp;1</span>" not in html

    def test_not_clickable_by_default_renders_plain_spans(self):
        html = sequence_to_html(self._sequences())
        assert "<a href=" not in html
        assert '<span title="ALA1"' in html

    def test_clickable_renders_residue_links(self):
        html = sequence_to_html(self._sequences(), clickable=True)
        assert '<a href="residue:A/1"' in html
        assert '<a href="residue:A/2"' in html
        assert '<a href="residue:A/3"' in html
        assert "<span title=" not in html  # residues themselves are links, not spans

    def test_clickable_renders_chain_header_link(self):
        html = sequence_to_html(self._sequences(), clickable=True)
        assert '<a href="chain:A"' in html
        assert "<b>Chain A</b>" in html

    def test_not_clickable_chain_header_is_plain(self):
        html = sequence_to_html(self._sequences())
        assert '<a href="chain:A"' not in html
        assert "<b>Chain A</b>" in html

    def test_clickable_selection_and_highlight_colors_still_apply(self):
        html = sequence_to_html(
            self._sequences(), highlight_residues=[("A", 2)], selected_residues=[("A", 3)], clickable=True,
        )
        link_for_res2 = [line for line in html.split("<a ") if 'title="GLY2"' in line][0]
        link_for_res3 = [line for line in html.split("<a ") if 'title="SER3"' in line][0]
        assert HIGHLIGHT_COLOR in link_for_res2
        assert SELECTED_COLOR in link_for_res3

    def test_line_start_number_is_fixed_grid_position(self):
        # a gap from an unmodeled loop: residues 1-2, then a jump to 50
        sequences = {
            "A": [
                type("R", (), {"resnum": 1, "resname": "ALA", "code": "A"})(),
                type("R", (), {"resnum": 2, "resname": "GLY", "code": "G"})(),
                type("R", (), {"resnum": 50, "resname": "SER", "code": "S"})(),
            ]
        }
        html = sequence_to_html(sequences, residues_per_line=2)
        assert "&nbsp;1</span>" in html
        # resnum 50 sits on the grid line that starts at 49, not at 50
        assert "&nbsp;49</span>" in html
        assert "50</span>" not in html.replace("&nbsp;49</span>", "")

    def test_missing_resnum_renders_as_blank(self):
        # residues 1, 2, then 50 -- the grid line 49-50 should show a blank
        # slot for the missing 49, then the real residue at 50
        sequences = {
            "A": [
                type("R", (), {"resnum": 1, "resname": "ALA", "code": "A"})(),
                type("R", (), {"resnum": 2, "resname": "GLY", "code": "G"})(),
                type("R", (), {"resnum": 50, "resname": "SER", "code": "S"})(),
            ]
        }
        html = sequence_to_html(sequences, residues_per_line=2)
        last_line = html.split("<br>")[-2]  # last populated line, before the trailing </div></div>
        assert "&nbsp;49</span>&nbsp;&nbsp;" in last_line  # gutter, then blank slot for 49
        assert '<span title="SER50"' in last_line

    def test_default_residues_per_line_is_50(self):
        sequences = {"A": [type("R", (), {"resnum": n, "resname": "ALA", "code": "A"})() for n in range(1, 61)]}
        html = sequence_to_html(sequences)
        assert "&nbsp;1</span>" in html
        assert "&nbsp;51</span>" in html  # residue 51 starts a new line, not still on line 1

    def test_negative_resnum_grid_starts_at_negative_fifty(self):
        sequences = {
            "A": [
                type("R", (), {"resnum": -3, "resname": "ALA", "code": "A"})(),
                type("R", (), {"resnum": -2, "resname": "GLY", "code": "G"})(),
                type("R", (), {"resnum": -1, "resname": "SER", "code": "S"})(),
                type("R", (), {"resnum": 1, "resname": "THR", "code": "T"})(),
            ]
        }
        html = sequence_to_html(sequences, residues_per_line=50)
        assert "-50</span>" in html  # negative block start, no residue 0 in between
        assert "&nbsp;1</span>" in html  # positive side still starts its own grid at 1

    def test_single_blank_line_between_data_is_unaffected(self):
        # residues 1 and 25, 10/line -> exactly one fully-blank line (11-20)
        # between them: an isolated gap still renders as-is.
        sequences = {
            "A": [
                type("R", (), {"resnum": 1, "resname": "ALA", "code": "A"})(),
                type("R", (), {"resnum": 25, "resname": "SER", "code": "S"})(),
            ]
        }
        html = sequence_to_html(sequences, residues_per_line=10)
        ruler_11 = f"{11:>5}".replace(" ", "&nbsp;") + "</span>"
        assert ruler_11 in html
        assert html.count("<br>") == 3  # 1-10, 11-20 (blank), 21-30

    def test_multiple_consecutive_blank_lines_collapse_to_one(self):
        # residues 1 and 105, 10/line -> nine fully-blank lines (11-100) in
        # between: a deliberate numbering jump (e.g. a second domain started
        # at a round number) shouldn't render as a wall of blank rows.
        sequences = {
            "A": [
                type("R", (), {"resnum": 1, "resname": "ALA", "code": "A"})(),
                type("R", (), {"resnum": 105, "resname": "SER", "code": "S"})(),
            ]
        }
        html = sequence_to_html(sequences, residues_per_line=10)
        ruler_11 = f"{11:>5}".replace(" ", "&nbsp;") + "</span>"
        ruler_21 = f"{21:>5}".replace(" ", "&nbsp;") + "</span>"
        ruler_101 = f"{101:>5}".replace(" ", "&nbsp;") + "</span>"
        assert ruler_11 in html  # first blank line of the run kept
        assert ruler_21 not in html  # rest of the run dropped
        assert ruler_101 in html  # jumps straight to the block with real data
        assert html.count("<br>") == 3  # 1-10, 11-20 (blank stand-in), 101-110


class TestResidueLabels:
    def _sequences(self):
        return {
            "A": [
                type("R", (), {"resnum": 1, "resname": "ALA", "code": "A"})(),
                type("R", (), {"resnum": 2, "resname": "GLY", "code": "G"})(),
                type("R", (), {"resnum": 235, "resname": "SER", "code": "S"})(),
            ]
        }

    def test_formats_code_plus_resnum(self):
        labels = residue_labels(self._sequences(), [("A", 235)])
        assert labels == {("A", 235): "S235"}

    def test_multiple_residues(self):
        labels = residue_labels(self._sequences(), [("A", 1), ("A", 235)])
        assert labels == {("A", 1): "A1", ("A", 235): "S235"}

    def test_unknown_residue_key_is_dropped_not_raised(self):
        labels = residue_labels(self._sequences(), [("A", 999)])
        assert labels == {}

    def test_empty_residues_returns_empty_dict(self):
        assert residue_labels(self._sequences(), []) == {}
