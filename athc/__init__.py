from pathlib import Path


def runtime_artifact(mode: str, ext: str) -> Path:
    """Locate libath_<mode>.<ext>: bundled in the wheel, else the repo runtime/ (editable/dev)."""
    name = f"libath_{mode}.{ext}"
    here = Path(__file__).resolve().parent
    bundled = here / "_runtime" / name
    return bundled if bundled.exists() else here.parent / "runtime" / name
