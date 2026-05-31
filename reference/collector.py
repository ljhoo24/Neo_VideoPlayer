from __future__ import annotations

import shutil
from pathlib import Path

from PyQt6.QtCore import QThread, pyqtSignal


class CollectorWorker(QThread):
    """
    src 하위의 모든 인덱스 시트(*_index.jpg)를 찾아
    dst 폴더 한 곳에 평탄하게 복사한다.
    파일명 충돌 시 _1, _2, … 접미사를 붙인다.
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

    def run(self) -> None:
        try:
            self._process()
        except Exception as exc:  # pylint: disable=broad-except
            self.log_signal.emit(f"[치명적 오류] {exc}")
        finally:
            self.finished_signal.emit()

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

        try:
            self._dst.mkdir(parents=True, exist_ok=True)
        except Exception as exc:  # pylint: disable=broad-except
            self.log_signal.emit(f"[오류] 대상 폴더 생성 실패: {exc}")
            return

        for sheet in sheets:
            if self._aborted:
                self.log_signal.emit("⚠ 작업이 중단되었습니다.")
                break

            dest_file = self._resolve_dest(sheet.name)
            self.status_signal.emit(f"복사 중: {sheet.name}")

            try:
                shutil.copy2(str(sheet), str(dest_file))
                rel = sheet.relative_to(self._src)
                if dest_file.name != sheet.name:
                    self.log_signal.emit(
                        f"   [복사] {rel}  →  {dest_file.name}  (이름 충돌 해결)"
                    )
                else:
                    self.log_signal.emit(f"   [복사] {rel}  →  {dest_file.name}")
            except Exception as exc:  # pylint: disable=broad-except
                self.log_signal.emit(f"   [오류] {sheet.name}: {exc}")

            done += 1
            self.progress_signal.emit(int(done / total * 100))

        if not self._aborted:
            self.log_signal.emit("\n모든 처리가 완료되었습니다.")
            self.progress_signal.emit(100)

    def _resolve_dest(self, filename: str) -> Path:
        """충돌 없는 대상 경로를 반환. 충돌 시 stem_1.jpg, stem_2.jpg … 순으로 시도."""
        candidate = self._dst / filename
        if not candidate.exists():
            return candidate

        stem = Path(filename).stem
        suffix = Path(filename).suffix
        for i in range(1, 10_000):
            candidate = self._dst / f"{stem}_{i}{suffix}"
            if not candidate.exists():
                return candidate

        # 극단적 충돌 — 거의 발생하지 않음
        return self._dst / filename
