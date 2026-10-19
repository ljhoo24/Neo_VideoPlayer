"""Export the Windows multi-resolution icon from icon.png (requires Pillow).

Run from any directory: python scripts/generate-icon.py
The PNG is the original transparent artwork; keep it as the source of truth.
"""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)


def main():
    with Image.open(ROOT / "icon.png") as source:
        if source.width != source.height or source.width < max(SIZES):
            raise ValueError("icon.png must be square and at least 256 pixels wide")
        source.convert("RGBA").save(
            ROOT / "icon.ico", format="ICO", sizes=[(size, size) for size in SIZES]
        )
    print("Exported icon.ico: " + ", ".join(f"{size}px" for size in SIZES))


if __name__ == "__main__":
    main()
