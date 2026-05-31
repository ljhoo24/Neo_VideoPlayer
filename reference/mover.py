from __future__ import annotations

import shutil
from pathlib import Path

from PyQt6.QtCore import QThread, pyqtSignal


class MoverWorker(QThread):
    """
    src 하위의 모든 인덱스 시트(*_index.jpg)를 찾아
    dst 안에 src와 동일한 폴더 구조를 만든 뒤 이동한다.
    """

    log_signal      = pyqtSignal(str)
    progress_signal = pyqtSignal(int)
    status_signal   = pyqtSignal(str)
    finished_signal = pyqtSignal()

    def __init__(self, src: Path, dst: Path) -> None:
        super().__init__()
        self._src     = src
        self._dst     = dst
        self._aborted = False

    def abort(self) -> None:
        self._aborted = True

    # ── QThread 진입점 ────────────────────────────────────────────────────────

    def run(self) -> None:
        try:
            self._process()
        except Exception as exc:  # pylint: disable=broad-except
            self.log_signal.emit(f"[치명적 오류] {exc}")
        finally:
            self.finished_signal.emit()

    # ── 핵심 로직 ─────────────────────────────────────────────────────────────

    def _process(self) -> None:
        self.status_signal.emit("인덱스 시트 검색 중…")
        self.log_signal.emit(f"소스: {self._src}")
        self.log_signal.emit(f"대상: {self._dst}\n")

        sheets = list(self._src.rglob("*_index.jpg"))

        if not sheets:
            self.log_signal.emit("인덱스 시트를 찾을 수 없습니다.")
            self.progress_signal.emit(100)
            return

        self.log_signal.emit(f"인덱스 시트 {len(sheets)}개 발견\n")
        total = len(sheets)
        done  = 0

        for sheet in sheets:
            if self._aborted:
                self.log_signal.emit("⚠ 작업이 중단되었습니다.")
                break

            rel       = sheet.relative_to(self._src)   # src 기준 상대 경로
            dest_file = self._dst / rel
            dest_dir  = dest_file.parent

            self.status_signal.emit(f"이동 중: {rel}")

            try:
                dest_dir.mkdir(parents=True, exist_ok=True)
                shutil.move(str(sheet), str(dest_file))
                self.log_signal.emit(f"   [이동] {rel}")
            except Exception as exc:  # pylint: disable=broad-except
                self.log_signal.emit(f"   [오류] {rel}: {exc}")

            done += 1
            self.progress_signal.emit(int(done / total * 100))

        if not self._aborted:
            self.log_signal.emit("\n모든 처리가 완료되었습니다.")
            self.progress_signal.emit(100)
