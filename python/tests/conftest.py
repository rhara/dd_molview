"""Shared fixtures for dd_cview_core's pytest suite."""
from pathlib import Path

import pytest

DATA_DIR = Path(__file__).resolve().parent.parent.parent / "data"


@pytest.fixture
def data_dir() -> Path:
    """Directory holding the bundled sample PDB/SDF/manifest files."""
    return DATA_DIR
