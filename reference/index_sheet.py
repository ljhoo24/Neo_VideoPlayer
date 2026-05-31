from __future__ import annotations

from pathlib import Path
from typing import Union

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

# ── constants ─────────────────────────────────────────────────────────────────

TARGET_WIDTH = 1920
COLS         = 4
ROWS         = 3
FRAME_COUNT  = COLS * ROWS  # 12

HEADER_H     = 96           # pixels reserved for the header strip
BG_COLOR     = (24, 24, 24)
HEADER_FG    = (240, 240, 240)
META_FG      = (170, 170, 170)
SEPARATOR_FG = (55, 55, 55)

# ── font helpers ──────────────────────────────────────────────────────────────

_FontType = Union["ImageFont.FreeTypeFont", "ImageFont.ImageFont"]

_FONT_CANDIDATES = [
    Path("C:/Windows/Fonts/malgun.ttf"),   # Malgun Gothic — Korean support
    Path("C:/Windows/Fonts/gulim.ttc"),    # Gulim — Korean fallback
    Path("C:/Windows/Fonts/arial.ttf"),    # ASCII-only last resort
]


def _load_font(size: int) -> _FontType:
    for path in _FONT_CANDIDATES:
        if path.exists():
            try:
                return ImageFont.truetype(str(path), size)
            except Exception:
                continue
    return ImageFont.load_default()


# ── formatting helpers ────────────────────────────────────────────────────────

def _fmt_duration(seconds: float) -> str:
    h  = int(seconds // 3600)
    m  = int((seconds % 3600) // 60)
    s  = int(seconds % 60)
    cs = int((seconds % 1) * 100)
    if h:
        return f"{h:02d}:{m:02d}:{s:02d}.{cs:02d}"
    return f"{m:02d}:{s:02d}.{cs:02d}"


def _fmt_size(byte_count: int) -> str:
    value = float(byte_count)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024:
            return f"{value:.1f} {unit}"
        value /= 1024
    return f"{value:.1f} TB"


# ── text drawing with black outline ──────────────────────────────────────────

_OUTLINE_OFFSETS = ((-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1))


def _draw_outlined_text(
    draw: ImageDraw.ImageDraw,
    xy: tuple[int, int],
    text: str,
    font: _FontType,
    anchor: str = "rb",
) -> None:
    x, y = xy
    for dx, dy in _OUTLINE_OFFSETS:
        draw.text((x + dx, y + dy), text, fill=(0, 0, 0), font=font, anchor=anchor)
    draw.text(xy, text, fill=(255, 255, 255), font=font, anchor=anchor)


# ── main function ─────────────────────────────────────────────────────────────

def generate_index_sheet(video_path: Path, output_dir: Path) -> Path:
    """
    Extract FRAME_COUNT frames from *video_path*, arrange them in a
    COLS × ROWS grid, annotate each frame with a timestamp, add a header
    with file metadata, and save the result as a JPEG.

    Parameters
    ----------
    video_path:  Path to the video file (already inside its destination folder).
    output_dir:  Directory where the index sheet JPEG will be written.

    Returns
    -------
    Path of the saved JPEG file.

    Raises
    ------
    RuntimeError if OpenCV cannot open or read frames from the video.
    """
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        raise RuntimeError(f"영상 파일을 열 수 없습니다: {video_path.name}")

    try:
        fps          = cap.get(cv2.CAP_PROP_FPS) or 25.0
        total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        vid_w        = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        vid_h        = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fourcc_int   = int(cap.get(cv2.CAP_PROP_FOURCC))
        codec = "".join(
            chr((fourcc_int >> (8 * i)) & 0xFF) for i in range(4)
        ).strip("\x00") or "N/A"

        if total_frames <= 0:
            raise RuntimeError(
                f"총 프레임 수를 확인할 수 없습니다: {video_path.name}"
            )

        duration = total_frames / fps

        # Evenly distributed positions inside [5 %, 95 %] of the timeline
        start  = int(total_frames * 0.05)
        end    = int(total_frames * 0.95)
        span   = max(end - start, 1)
        positions = [
            start + int(span * i / (FRAME_COUNT - 1))
            for i in range(FRAME_COUNT)
        ]

        frames: list[tuple[np.ndarray, float]] = []
        for pos in positions:
            cap.set(cv2.CAP_PROP_POS_FRAMES, float(pos))
            ok, bgr = cap.read()
            if ok:
                frames.append((
                    cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB),
                    pos / fps,
                ))
    finally:
        cap.release()

    if not frames:
        raise RuntimeError(
            f"프레임을 추출할 수 없습니다: {video_path.name}"
        )

    # ── layout ────────────────────────────────────────────────────────────────
    cell_w = TARGET_WIDTH // COLS
    cell_h = (
        int(cell_w * vid_h / vid_w)
        if vid_w > 0 and vid_h > 0
        else cell_w * 9 // 16
    )
    canvas_h = HEADER_H + ROWS * cell_h

    # ── fonts ─────────────────────────────────────────────────────────────────
    font_title = _load_font(26)
    font_meta  = _load_font(18)
    font_stamp = _load_font(15)

    # ── canvas ────────────────────────────────────────────────────────────────
    canvas = Image.new("RGB", (TARGET_WIDTH, canvas_h), BG_COLOR)
    draw   = ImageDraw.Draw(canvas)

    # ── header ────────────────────────────────────────────────────────────────
    resolution = f"{vid_w}×{vid_h}" if vid_w and vid_h else "N/A"
    meta_line  = (
        f"해상도: {resolution}   "
        f"재생 시간: {_fmt_duration(duration)}   "
        f"파일 크기: {_fmt_size(video_path.stat().st_size)}   "
        f"코덱: {codec}"
    )
    draw.text((16, 10), video_path.name, fill=HEADER_FG, font=font_title)
    draw.text((16, 52), meta_line,        fill=META_FG,   font=font_meta)
    draw.line(
        [(0, HEADER_H - 1), (TARGET_WIDTH, HEADER_H - 1)],
        fill=SEPARATOR_FG, width=1,
    )

    # ── grid of frames ────────────────────────────────────────────────────────
    for idx, (rgb_array, timestamp) in enumerate(frames):
        row = idx // COLS
        col = idx % COLS
        x   = col * cell_w
        y   = HEADER_H + row * cell_h

        cell_img  = Image.fromarray(rgb_array).resize(
            (cell_w, cell_h), Image.Resampling.LANCZOS
        )
        cell_draw = ImageDraw.Draw(cell_img)
        _draw_outlined_text(
            cell_draw,
            (cell_w - 8, cell_h - 8),
            _fmt_duration(timestamp),
            font_stamp,
            anchor="rb",
        )
        canvas.paste(cell_img, (x, y))

    # ── save ──────────────────────────────────────────────────────────────────
    output_path = output_dir / f"{video_path.stem}_index.jpg"
    canvas.save(str(output_path), "JPEG", quality=92)
    return output_path
