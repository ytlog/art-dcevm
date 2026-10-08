"""Atomically allocate distinct evidence directories for concurrent device runs."""
from datetime import datetime, timezone
from pathlib import Path
import tempfile


def new_evidence_directory(base: Path, stamp: str | None = None) -> Path:
    base.mkdir(parents=True, exist_ok=True)
    prefix = stamp or datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    return Path(tempfile.mkdtemp(prefix=prefix + '-', dir=base))
