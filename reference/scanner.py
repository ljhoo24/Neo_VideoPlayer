from pathlib import Path

VIDEO_EXTENSIONS: frozenset[str] = frozenset({
    ".mp4", ".mkv", ".avi", ".mov", ".wmv",
    ".flv", ".webm", ".m4v", ".ts", ".m2ts",
})

IMAGE_EXTENSIONS: frozenset[str] = frozenset({
    ".jpg", ".jpeg", ".png", ".gif",
    ".bmp", ".webp", ".tiff",
})


def scan_folder(root: Path) -> dict[Path, dict[str, list[Path]]]:
    """
    Recursively scan *root* and group every file by its parent directory.

    Returns
    -------
    dict mapping each directory that contains at least one file to::

        {
            "videos": [Path, ...],
            "images": [Path, ...],
            "others": [Path, ...],
        }

    The scan is performed eagerly so the returned snapshot is stable even
    while the caller subsequently modifies the file-system.
    """
    dirs: dict[Path, dict[str, list[Path]]] = {}

    for f in root.rglob("*"):
        if not f.is_file():
            continue
        parent = f.parent
        if parent not in dirs:
            dirs[parent] = {"videos": [], "images": [], "others": []}

        ext = f.suffix.lower()
        if ext in VIDEO_EXTENSIONS:
            dirs[parent]["videos"].append(f)
        elif ext in IMAGE_EXTENSIONS:
            dirs[parent]["images"].append(f)
        else:
            dirs[parent]["others"].append(f)

    return dirs
