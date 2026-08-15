"""Shared discovery and invocation for UnrealBridge project synchronization."""
from __future__ import annotations

import pathlib
import subprocess
import sys


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
SKILL_ROOT = SCRIPT_DIR.parent
CONTAINING_ROOT = SCRIPT_DIR.parents[3]
SOURCE_ROOT_MARKER = (
    CONTAINING_ROOT / "Saved" / "UnrealBridge" / "source-root.txt"
)


def detect_project_root() -> pathlib.Path | None:
    """Return the project containing this installed skill, if any."""
    if any(CONTAINING_ROOT.glob("*.uproject")):
        return CONTAINING_ROOT
    return None


def resolve_sync_script(source_dir: str | None = None) -> pathlib.Path | None:
    """Find the canonical sync_project.bat without consumer-specific names."""
    candidates: list[pathlib.Path] = []
    if source_dir:
        candidates.append(pathlib.Path(source_dir).expanduser())

    try:
        recorded_source = SOURCE_ROOT_MARKER.read_text(
            encoding="utf-8", errors="strict"
        ).strip()
    except (OSError, UnicodeError):
        recorded_source = ""
    if recorded_source:
        candidates.append(pathlib.Path(recorded_source))

    # Supports invoking the helper directly from an UnrealBridge source checkout.
    candidates.append(CONTAINING_ROOT)

    seen: set[pathlib.Path] = set()
    for candidate in candidates:
        try:
            root = candidate.resolve()
        except OSError:
            continue
        if root in seen:
            continue
        seen.add(root)
        script = root / "sync_project.bat"
        if script.is_file():
            return script
    return None


def run_project_sync(
    project_dir: pathlib.Path,
    *,
    source_dir: str | None = None,
    verbose: bool = False,
    label: str = "sync",
) -> int:
    """Deploy the plugin and both skill copies from one source revision."""
    sync_script = resolve_sync_script(source_dir)
    if sync_script is None:
        sys.stderr.write(
            "Could not locate UnrealBridge sync_project.bat. "
            "Pass --sync-source <UnrealBridge repo root>, or run "
            "sync_project.bat once so the target project's "
            "Saved/UnrealBridge/source-root.txt marker is created.\n"
        )
        return 3

    print(f"[{label}] syncing plugin and skills via {sync_script} ...")
    process = subprocess.run(
        ["cmd.exe", "/d", "/c", str(sync_script), str(project_dir.resolve())],
        capture_output=not verbose,
        text=True,
    )
    if process.returncode != 0:
        if not verbose and process.stdout:
            sys.stderr.write(process.stdout)
        if not verbose and process.stderr:
            sys.stderr.write(process.stderr)
        sys.stderr.write(
            f"UnrealBridge project sync failed (rc={process.returncode})\n"
        )
        return 3
    return 0
