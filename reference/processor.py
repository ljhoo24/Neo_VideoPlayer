from __future__ import annotations

from pathlib import Path

from PyQt6.QtCore import QThread, pyqtSignal

from core.index_sheet import generate_index_sheet
from core.scanner import scan_folder


class ProcessorWorker(QThread):
    """Background worker: scans a folder and generates index sheets for every video."""

    log_signal      = pyqtSignal(str)
    progress_signal = pyqtSignal(int)
    status_signal   = pyqtSignal(str)
    finished_signal = pyqtSignal()

    def __init__(self, folder: str) -> None:
        super().__init__()
        self._root    = Path(folder)
        self._aborted = False

    def abort(self) -> None:
        self._aborted = True

    def run(self) -> None:
        try:
            self._process()
        except Exception as exc:  # pylint: disable=broad-except
            self.log_signal.emit(f"[치명적 오류] {exc}")
        finally:
            self.finished_signal.emit()

    def _process(self) -> None:
        self.log_signal.emit(f"스캔 시작: {self._root}")
        self.status_signal.emit("파일 스캔 중…")

        dirs = scan_folder(self._root)

        # Collect only directories that have at least one video
        video_dirs = {d: f["videos"] for d, f in dirs.items() if f["videos"]}

        total = sum(len(v) for v in video_dirs.values())
        if total == 0:
            self.log_signal.emit("처리할 영상 파일이 없습니다.")
            self.progress_signal.emit(100)
            return

        self.log_signal.emit(f"총 {total}개 영상 파일 발견\n")
        done = 0

        for dir_path, videos in video_dirs.items():
            if self._aborted:
                self.log_signal.emit("⚠ 작업이 중단되었습니다.")
                break

            try:
                rel = dir_path.relative_to(self._root)
            except ValueError:
                rel = dir_path
            self.log_signal.emit(f"── 디렉토리: {rel if str(rel) != '.' else '(루트)'}")
            self.status_signal.emit(f"처리 중: {rel}")

            for video in videos:
                if self._aborted:
                    break

                index_path = video.parent / f"{video.stem}_index.jpg"
                if index_path.exists():
                    self.log_signal.emit(f"   [스킵] {video.name} (이미 존재: {index_path.name})")
                else:
                    self.log_signal.emit(f"   [인덱스] {video.name} 생성 중…")
                    try:
                        generate_index_sheet(video, video.parent)
                        self.log_signal.emit(f"   [완료] {index_path.name}")
                    except Exception as exc:  # pylint: disable=broad-except
                        self.log_signal.emit(f"   [오류] {video.name}: {exc}")

                done += 1
                self.progress_signal.emit(int(done / total * 100))

        if not self._aborted:
            self.log_signal.emit("\n모든 처리가 완료되었습니다.")
            self.progress_signal.emit(100)
