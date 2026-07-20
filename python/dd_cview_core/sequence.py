"""Per-chain 1-letter residue sequences extracted from a `dd_viewer.Receptor`,
plus HTML rendering with the same two-tier highlight convention
(`highlight_residues` / `selected_residues`) used by `dd_viewer.build_view`,
so the sequence panel and the 3D view read as one consistent selection
state.
"""
from dataclasses import dataclass
from typing import Optional, Sequence

import dd_viewer as dv
from Bio.Data.IUPACData import protein_letters_3to1 as _PROTEIN_LETTERS_3TO1

# Bio.Data.IUPACData's keys are Title-case ("Ala"), not the upper-case PDB
# convention ("ALA") -- normalize once at import time. This dict predates
# and outlives the deprecated Bio.PDB.Polypeptide.three_to_one function, so
# it stays valid across Biopython versions without depending on a
# deprecated symbol.
_THREE_TO_ONE = {k.upper(): v for k, v in _PROTEIN_LETTERS_3TO1.items()}

UNKNOWN_RESIDUE_CODE = "X"

# Approximate the 3D view's yellowCarbon/magentaCarbon colorschemes
# (dd_viewer/dd_viewer/scene.py's HIGHLIGHT_COLORSCHEME / SELECTED_COLORSCHEME)
# with readable hex colors -- plain CSS "yellow" is nearly invisible against
# a white Streamlit background.
HIGHLIGHT_COLOR = "#c9a400"
SELECTED_COLOR = "#c800c8"


@dataclass
class SequenceResidue:
    """One residue in an extracted chain sequence.

    `resnum` matches what `dd_viewer.build_view`'s `highlight_residues`/
    `selected_residues` and `dd_viewer.find_contact_residues` use for
    (chain, resnum) keys, so sequence-panel highlighting can share the
    exact same residue keys as the 3D view.
    """

    resnum: int
    resname: str
    code: str


def extract_sequences(receptor: dv.Receptor, include_hetero: bool = False) -> dict[str, list[SequenceResidue]]:
    """Per-chain, residue-number-ordered sequence, keyed by chain id.

    Only standard amino acids get a real 1-letter code. A `Receptor` loaded
    via `dv.load_receptor` (unlike one produced by `dv.split_structure`,
    which is already protein-only) can still contain waters/ions/a bound
    ligand, since that loader keeps every residue in the file -- so this
    function can't assume hetero rows are absent. By default
    (`include_hetero=False`) such residues are dropped rather than shown
    as `X`, since a stray HETATM polluting a *protein sequence* panel is
    more confusing than useful; pass `include_hetero=True` to keep them
    (as `UNKNOWN_RESIDUE_CODE`) when that matters.
    """
    sequences: dict[str, list[SequenceResidue]] = {}
    atoms = receptor.atoms
    grouped = atoms.groupby(["chain", "resnum", "resname"], sort=False).size().reset_index()
    for chain, resnum, resname in zip(grouped["chain"], grouped["resnum"], grouped["resname"]):
        code = _THREE_TO_ONE.get(resname.strip().upper())
        if code is None:
            if not include_hetero:
                continue
            code = UNKNOWN_RESIDUE_CODE
        sequences.setdefault(chain, []).append(SequenceResidue(resnum=int(resnum), resname=resname.strip(), code=code))
    for chain_residues in sequences.values():
        chain_residues.sort(key=lambda r: r.resnum)
    return sequences


def residue_labels(
    sequences: dict[str, list[SequenceResidue]], residues: Sequence[tuple],
) -> dict[tuple, str]:
    """Map each (chain, resnum) in `residues` to a short label like `"S235"`
    (1-letter code + resnum), for `dv.build_view`'s `residue_labels`
    parameter -- so the 3D view can name a residue the same way the
    sequence panel already does, rather than a bare `resnum` or the raw
    3-letter `resname`. A `residues` entry with no match in `sequences`
    (e.g. a HETATM key that never got a code) is silently dropped, same
    convention as `dv.scene._add_residue_labels` skipping a key with no
    matching CA atom.
    """
    code_by_key = {(chain, res.resnum): res.code for chain, chain_residues in sequences.items() for res in chain_residues}
    return {key: f"{code_by_key[key]}{key[1]}" for key in residues if key in code_by_key}


RESIDUE_NUMBER_COLOR = "#888888"

# Scheme for the desktop app's clickable residue links -- an unregistered,
# opaque (no "//") custom scheme, so QUrl keeps the chain id's original case
# in `path()` (unlike `host()`, which is authority-position and therefore
# always lower-cased by QUrl). `main_window.py` reads these back with
# `setOpenLinks(False)` + `anchorClicked`, never actually navigating.
RESIDUE_LINK_SCHEME = "residue"

# Same convention as RESIDUE_LINK_SCHEME, for the clickable "Chain X" header
# of a multi-chain receptor -- lets `main_window.py` tell a chain click (zoom
# the 3D view to that chain, camera position intentionally changes) apart
# from a residue click (select the residue, camera position preserved).
CHAIN_LINK_SCHEME = "chain"


def _block_start(resnum: int, per_line: int) -> int:
    """First resnum of the fixed `per_line`-wide grid line that `resnum` falls
    on. Positive numbers grid from 1 (1..per_line, per_line+1..2*per_line, ...);
    negative numbers mirror that from -1 going down (-per_line..-1,
    -2*per_line..-per_line-1, ...), since PDB/UniProt numbering has no residue
    0 -- so a block never straddles zero.
    """
    if resnum > 0:
        return per_line * ((resnum - 1) // per_line) + 1
    if resnum < 0:
        return -per_line * ((-resnum - 1) // per_line + 1)
    return 0


def _line_blocks(min_resnum: int, max_resnum: int, per_line: int) -> list[tuple[int, int]]:
    """Ascending (block_start, block_end) grid lines covering `min_resnum`..
    `max_resnum`. The positive side always starts its grid at 1 regardless of
    `min_resnum` -- so two chains/structures numbered against the same
    canonical sequence but modeled starting at different residues still line
    up column-for-column -- while the negative side (present only if
    `min_resnum` < 0) starts at the block that actually contains `min_resnum`,
    since there's no fixed canonical lower bound to pad out to.
    """
    blocks: list[tuple[int, int]] = []
    if min_resnum < 0:
        start = _block_start(min_resnum, per_line)
        while start <= -per_line:
            blocks.append((start, start + per_line - 1))
            start += per_line
    if max_resnum > 0:
        start = 1
        last_start = _block_start(max_resnum, per_line)
        while start <= last_start:
            blocks.append((start, start + per_line - 1))
            start += per_line
    return blocks


def _collapse_empty_runs(
    blocks: list[tuple[int, int]], residue_by_num: dict[int, "SequenceResidue"],
) -> list[tuple[int, int]]:
    """Collapse a run of 2+ consecutive grid lines with no modeled residue at
    all down to just the first line of that run, dropping the rest. Some
    structures deliberately number in widely separated ranges (e.g. a
    construct with domains numbered 1-120 and 1001-1150) -- rendering every
    intervening blank grid line would bury the real sequence under a wall of
    empty rows. A single isolated blank line (a plain unmodeled-loop gap)
    still renders as before, unchanged.
    """
    def is_empty(block: tuple[int, int]) -> bool:
        start, end = block
        return not any(n in residue_by_num for n in range(start, end + 1))

    collapsed: list[tuple[int, int]] = []
    i = 0
    while i < len(blocks):
        if is_empty(blocks[i]):
            run_start = i
            while i < len(blocks) and is_empty(blocks[i]):
                i += 1
            collapsed.append(blocks[run_start])  # one blank line stands in for the whole run
        else:
            collapsed.append(blocks[i])
            i += 1
    return collapsed


def sequence_to_html(
    sequences: dict[str, list[SequenceResidue]],
    highlight_residues: Optional[Sequence[tuple]] = None,
    selected_residues: Optional[Sequence[tuple]] = None,
    residues_per_line: int = 50,
    show_residue_numbers: bool = True,
    clickable: bool = False,
) -> str:
    """Render `sequences` (as returned by `extract_sequences`) as an HTML
    block: one section per chain, each residue in its own `<span>`, colored
    to match `dd_viewer.build_view`'s two highlight tiers -- `highlight_residues`
    (auto contact-cutoff residues) in `HIGHLIGHT_COLOR` (yellow), and
    `selected_residues` (user table picks) in `SELECTED_COLOR` (magenta). Both
    take the same `Sequence[tuple[chain, resnum]]` shape as `build_view`'s
    parameters of the same name, so a caller can pass the identical two lists
    to both `build_view(...)` and `sequence_to_html(...)` without reshaping
    them. A residue present in both sets renders as selected (magenta) --
    selection takes visual priority over highlight, matching `build_view`'s
    draw order (selected-residue sticks are added after highlight sticks).

    Each chain renders on a fixed `residues_per_line`-wide resnum grid (see
    `_line_blocks`) rather than simply chunking whatever residues are present:
    a resnum with no modeled residue (an unresolved loop, or a structure that
    starts later than resnum 1) renders as blank space instead of shifting
    later residues left, so the same column always means the same resnum
    across chains and across structures compared against the same numbering.
    A run of 2+ consecutive fully-blank grid lines (some structures
    deliberately number in widely separated ranges, e.g. two domains
    numbered 1-120 and 1001-1150) collapses down to a single blank line
    instead of rendering a wall of empty rows -- see `_collapse_empty_runs`.
    A single isolated blank line (a plain unmodeled-loop gap) still renders
    as-is.

    `show_residue_numbers` prints each line's grid `block_start` at the start
    of the line, right-aligned in a fixed-width gutter (the classic FASTA-
    viewer "ruler" convention) -- the grid position, not the first actually-
    modeled residue's own resnum, since the grid (not the data) now decides
    where lines break.

    `clickable` wraps each residue in an `<a href="residue:{chain}/{resnum}">`
    link instead of a plain `<span>`, and each chain's "Chain X" header in an
    `<a href="chain:{chain}">` link, for callers that can react to clicks
    (the desktop app's `QTextBrowser.anchorClicked`) -- a residue click
    selects that residue, a chain click zooms the 3D view to that whole
    chain. Left off by default: in a real browser (`st.markdown`), clicking
    an unregistered `residue:`/`chain:` link just produces a dead-link
    prompt with nothing to catch it.

    Returns a plain string, meant for `st.markdown(html, unsafe_allow_html=True)`;
    this module has no Streamlit import, same convention as `dd_viewer.scene`.
    """
    highlight_set = set(highlight_residues or [])
    selected_set = set(selected_residues or [])

    # A concrete font stack (not the generic "monospace" family) -- harmless
    # in a browser (st.markdown), but resolving "monospace" inside a Qt
    # QTextBrowser (the desktop app) makes Qt populate its full font-family
    # alias table on macOS, which is slow and prints a qt.qpa.fonts warning.
    parts = ['<div style="font-family: Menlo, Consolas, \'DejaVu Sans Mono\', monospace; font-size: 13px; line-height: 1.6;">']
    for chain, residues in sequences.items():
        if clickable:
            chain_href = f"{CHAIN_LINK_SCHEME}:{chain}"
            chain_label = f'<a href="{chain_href}" title="Zoom to chain {chain}" style="text-decoration:none; color:inherit;"><b>Chain {chain}</b></a>'
        else:
            chain_label = f"<b>Chain {chain}</b>"
        parts.append(f"<div>{chain_label} ({len(residues)} residues)</div>")
        parts.append('<div style="word-break: break-all;">')
        residue_by_num = {res.resnum: res for res in residues}
        if residue_by_num:
            resnums = residue_by_num.keys()
            blocks = _line_blocks(min(resnums), max(resnums), residues_per_line)
            blocks = _collapse_empty_runs(blocks, residue_by_num)
            for block_start, block_end in blocks:
                if show_residue_numbers:
                    # Right-justify with &nbsp; padding rather than CSS
                    # (`display:inline-block` + `text-align:right`) -- Qt's
                    # QTextBrowser rich-text engine only supports a limited CSS
                    # subset and silently ignores `inline-block`, which left the
                    # numbers flush-left there even though it works in a real
                    # browser. Plain space-padding in a monospace font renders
                    # identically (right-aligned) in both.
                    number_text = f"{block_start:>5}".replace(" ", "&nbsp;")
                    parts.append(f'<span style="color:{RESIDUE_NUMBER_COLOR};">{number_text}</span>&nbsp;')
                for resnum in range(block_start, block_end + 1):
                    res = residue_by_num.get(resnum)
                    if res is None:
                        parts.append("&nbsp;")
                        continue
                    key = (chain, resnum)
                    if key in selected_set:
                        color, weight = SELECTED_COLOR, "bold"
                    elif key in highlight_set:
                        color, weight = HIGHLIGHT_COLOR, "bold"
                    else:
                        color, weight = "inherit", "normal"
                    title = f"{res.resname}{res.resnum}"
                    style = f"color:{color}; font-weight:{weight};"
                    if clickable:
                        href = f"{RESIDUE_LINK_SCHEME}:{chain}/{resnum}"
                        parts.append(f'<a href="{href}" title="{title}" style="{style} text-decoration:none;">{res.code}</a>')
                    else:
                        parts.append(f'<span title="{title}" style="{style}">{res.code}</span>')
                parts.append("<br>")
        parts.append("</div>")
    parts.append("</div>")
    return "".join(parts)
