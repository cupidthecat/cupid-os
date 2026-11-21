## @file
#  @brief Share checked seed and JPEG fixtures for Hostbuild tests.
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SEED_MANIFEST = REPO_ROOT / "bootstrap" / "seeds" / "i386-linux" / "manifest.json"
BASELINE_JPEG = (
    b"\xff\xd8"
    b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
    b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
    b"\xff\xd9"
)
PROGRESSIVE_JPEG = BASELINE_JPEG.replace(b"\xff\xc0", b"\xff\xc2", 1)
