# VideoPlayer

Qt6 + libmpv 기반 데스크탑 비디오 플레이어. 인덱스 시트 썸네일, 평점/메모 메타데이터, NVIDIA Image Scaling(NIS) 업스케일링, 단축키 커스터마이즈를 지원합니다.

## 주요 기능

- **재생**: libmpv 임베드 (`vo=gpu`, `hwdec=auto-safe`). 자연 종료 시 자동 다음 곡, 1편/전체 반복.
- **플레이리스트**: SQLite 백엔드. 검색 텍스트 / 최소 평점 필터, 드래그 앤 드롭, 폴더 재귀 스캔.
- **메타데이터**: 영상별 평점(0–100) + 자유 메모, 사용자 지정 썸네일 또는 자동 생성 인덱스 시트(4×3 그리드).
- **인덱스 시트 미리보기**: 더블클릭으로 팝업 → 마우스 휠로 커서 기준 줌, 좌클릭 드래그 패닝.
- **업스케일 모드**:
  - **끄기** — bilinear, 최대 속도
  - **표준** — `ewa_lanczossharp` + sigmoid + deband + 가벼운 unsharp (모든 GPU)
  - **NVIDIA NIS** — NIS GLSL 컴퓨트 셰이더 (RTX 권장). 옵션 → 일반 탭에서 샤프닝 강도 0.0–1.0 조절.
- **상태 영속화**: 마지막 재생 항목, 검색/평점 필터, 업스케일 모드 + NIS 샤프닝, 윈도우 단축키 — 모두 `QSettings`로 재시작 시 복원.

## 빌드

### 의존성
- **Qt 6.5+** (Widgets, Sql 모듈)
- **libmpv** 2.x (Windows: shinchiro의 [mpv-dev-x86_64](https://sourceforge.net/projects/mpv-player-windows/files/libmpv/) 사용)
- **CMake 3.20+** 또는 Visual Studio 2022

### CMake (권장)
```bash
# libmpv dev 패키지를 C:/tools/mpv-dev-x86_64에 압축 해제했다면:
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt6 install dir>
cmake --build build --config Release
```

다른 위치에 libmpv가 있으면 `-DMPV_ROOT=<path>` 추가. `windeployqt`가 Qt DLL을 exe 옆에 자동 배포하고, `libmpv-2.dll` + `shaders/NVScaler.glsl`도 POST_BUILD 단계에서 복사됩니다.

### Visual Studio
`VideoPlayer.slnx`를 열고 빌드. `MpvRoot` / `Qt6*` 매크로 경로는 `VideoPlayer/VideoPlayer.vcxproj` 상단의 환경 변수에서 확인.

## 디렉터리 구조

```
VideoPlayer/
├── CMakeLists.txt               # 메인 빌드 스크립트
├── VideoPlayer.slnx             # Visual Studio 솔루션
├── VideoPlayer.pro              # qmake 프로젝트 (legacy, 참고용)
├── icon.ico, icon.png           # 윈도우 / 작업표시줄 아이콘
├── reference/                   # 인덱스 시트 알고리즘 Python 레퍼런스
└── VideoPlayer/
    ├── main.cpp                 # 엔트리포인트
    ├── MainWindow.{h,cpp}       # 최상위 윈도우, UI 와이어링, ImageViewerDialog
    ├── MpvPlayerWidget.{h,cpp}  # libmpv 임베드 + 업스케일 프로필
    ├── PlaylistModel.{h,cpp}    # 필터/정렬 가능한 Qt 모델
    ├── DatabaseManager.{h,cpp}  # SQLite I/O
    ├── OptionsDialog.{h,cpp}    # 단축키 + 일반 옵션 다이얼로그
    ├── app.rc                   # 윈도우 아이콘 리소스
    └── shaders/
        └── NVScaler.glsl        # NVIDIA Image Scaling v1.0.2 (MIT)
```

## 사용자 데이터 저장 위치 (Windows)

- DB / 썸네일 캐시: `%APPDATA%/<OrgName>/VideoPlayer/`
- 설정: `QSettings` 기본 경로 (`HKCU\Software\<OrgName>\VideoPlayer`)
- NIS 런타임 셰이더: `%APPDATA%/<OrgName>/VideoPlayer/shaders/NVScaler-runtime-XXX.glsl`

## 라이선스

본 저장소의 코드는 별도 명시 없는 한 비공개입니다. 다음 외부 자산은 각자의 라이선스로 포함됩니다:

- `VideoPlayer/shaders/NVScaler.glsl` — NVIDIA Image Scaling v1.0.2 (MIT License, © 2022 NVIDIA CORPORATION). agyild에 의해 mpv용으로 포팅됨.
