# VideoPlayer

Qt6 + libmpv 기반 데스크탑 비디오 플레이어. 모던 다크/라이트 UI, 플레이리스트 + 평점/메모 메타데이터, 인덱스 시트 썸네일, NVIDIA Image Scaling(NIS) 업스케일링, Windows 기본 재생기 통합을 지원합니다.

## 설치 (Windows x64)

[Releases](https://github.com/ljhoo24/Neo_VideoPlayer/releases/latest)에서 받습니다.

- **설치파일** — `VideoPlayer-x.y.z-Setup.exe` 실행. Program Files 설치, 시작메뉴/바탕화면 바로가기, Visual C++ 런타임, **기본앱 후보 등록**, 제거 프로그램 포함.
- **포터블** — `VideoPlayer-x.y.z-portable-x64.zip` 압축 해제 후 `VideoPlayer.exe` 실행. 설치 불필요, 런타임 DLL 동봉.

### 기본 재생기로 설정
설치 후 **설정 → 앱 → 기본 앱**에서 `VideoPlayer` 검색 → 확장자 지정, 또는 영상 우클릭 → **연결 프로그램 → VideoPlayer**.
지원: `mp4 mkv avi mov wmv flv webm m4v ts m2ts mpg mpeg 3gp ogv`

## 주요 기능

### 재생
- libmpv 임베드 (`vo=gpu`, `hwdec=auto-safe`). 자연 종료 시 자동 다음 곡, 1편/전체 반복.
- **재생 속도** 0.5×–2.0×, **A-B 구간 반복**, **프레임 단위 이동**(앞/뒤).
- **이어보기**: 영상별 마지막 위치 기억 후 재생 시 이어서 (옵션에서 on/off, 끝까지 본 영상은 처음부터).
- **볼륨**: 마지막 값 기억, Up/Down 키로 ±5, 음소거 토글.

### 플레이리스트 / 라이브러리
- SQLite 백엔드. 검색 텍스트 / 최소 평점 필터, 드래그 앤 드롭, 폴더 재귀 스캔.
- **리스트 / 썸네일 그리드** 보기 전환 (옵션).
- 재생 중 다른 항목을 선택해도 다음/이전·자동 다음은 **재생 중인 영상** 기준으로 동작.
- 항목 삭제 시 확인 다이얼로그.

### 메타데이터 / 썸네일
- 영상별 평점(0–100) + 자유 메모. 행 전환·종료 시 **자동 저장**.
- 사용자 지정 썸네일 또는 자동 생성 인덱스 시트. 더블클릭 팝업 → 휠 줌, 드래그 패닝.

### 화질 (업스케일)
- **끄기** — bilinear, 최대 속도
- **표준** — `ewa_lanczossharp` + sigmoid + deband + 가벼운 unsharp (모든 GPU)
- **NVIDIA NIS** — NIS GLSL 컴퓨트 셰이더 (RTX 권장). 옵션에서 샤프닝 강도 0.0–1.0 조절.

### UI / 시스템 통합
- **테마**: 다크 / 라이트 / **자동(시스템 추종, 실시간 전환)** + **강조 색 사용자 지정**. 벡터 아이콘 폰트(Material Icons) 임베드.
- **마우스 제스처**: 영상 위 휠 = 볼륨, 더블클릭 = 전체화면 토글.
- **단일 인스턴스**: 실행 중 영상을 더블클릭하면 새 창 대신 기존 창에서 재생목록 맨 앞에 추가·재생.
- 단축키 커스터마이즈(옵션), "기본값 복원"으로 단축키·테마·강조 색 일괄 초기화.

### 상태 영속화
마지막 재생 항목, 검색/평점 필터, 업스케일 모드 + NIS 샤프닝, 볼륨, 테마/강조 색, 재생목록 보기, 이어보기 설정, 윈도우 단축키 — 모두 `QSettings`로 재시작 시 복원.

## 기본 단축키

| 동작 | 키 | 동작 | 키 |
|---|---|---|---|
| 재생/일시정지 | Space | 5초 뒤로/앞으로 | ← / → |
| 볼륨 ±5 | ↑ / ↓ | 이전/다음 트랙 | Ctrl+← / Ctrl+→ |
| 프레임 뒤/앞 | , / . | 속도 −/+ | - / + |
| A 지점 / B 지점 / 해제 | [ / ] / \ | 반복 모드 | R |
| 전체화면 / 종료 | Enter / Esc | 썸네일 캡처 | F9 |
| 파일 추가 | Ctrl+O | 옵션 | Ctrl+, |

## 빌드

### 의존성
- **Qt 6.5+** (Widgets, Sql, **Network** 모듈)
- **libmpv** 2.x (Windows: shinchiro의 [mpv-dev-x86_64](https://sourceforge.net/projects/mpv-player-windows/files/libmpv/))
- **CMake 3.20+** 또는 Visual Studio 2022+

### CMake (권장)
```bash
# libmpv dev 패키지를 C:/tools/mpv-dev-x86_64에 압축 해제했다면:
cmake --preset windows-x64-release
cmake --build build/release
```
다른 위치면 `-DMPV_ROOT=<path>`. `windeployqt`가 Qt DLL을 자동 배포하고, `libmpv-2.dll` + `shaders/NVScaler.glsl`도 POST_BUILD에서 복사됩니다. `--compiler-runtime`으로 MSVC 런타임도 함께 배포.

### Visual Studio
`VideoPlayer.slnx` 열고 빌드. 또는 폴더를 열어 `CMakePresets.json`으로 빌드. `MpvRoot`/`Qt6*` 매크로는 `VideoPlayer/VideoPlayer.vcxproj` 상단에서 확인.

### 설치파일 (Inno Setup)
```powershell
ISCC.exe /DSrcDir="<build\release>" installer\VideoPlayer.iss
```

## 디렉터리 구조

```
VideoPlayer/
├── CMakeLists.txt               # 메인 빌드 스크립트
├── CMakePresets.json            # CMake 프리셋 (VS 폴더 빌드용)
├── VideoPlayer.slnx             # Visual Studio 솔루션
├── installer/VideoPlayer.iss    # Inno Setup 설치파일 스크립트
├── scripts/                     # 기본앱 등록/해제 PowerShell
├── icon.ico, icon.png           # 아이콘
├── reference/                   # 인덱스 시트 알고리즘 Python 레퍼런스
└── VideoPlayer/
    ├── main.cpp                 # 엔트리포인트 + 단일 인스턴스 IPC + 테마 로드
    ├── MainWindow.{h,cpp}       # 최상위 윈도우, UI, ImageViewerDialog
    ├── MpvPlayerWidget.{h,cpp}  # libmpv 임베드 + 업스케일/속도/프레임 + 제스처
    ├── PlaylistModel.{h,cpp}    # 필터/정렬 가능한 Qt 모델 + 그리드 썸네일
    ├── DatabaseManager.{h,cpp}  # SQLite I/O + 스키마 마이그레이션
    ├── OptionsDialog.{h,cpp}    # 단축키 + 일반/테마 옵션
    ├── IconFont.{h,cpp}         # Material Icons 폰트 로드 + 아이콘 렌더
    ├── ThemeManager.{h,cpp}     # 다크/라이트/자동 테마 + 강조 색
    ├── app.rc                   # 윈도우 아이콘 리소스
    ├── resources/               # theme.qss(템플릿) + Material Icons 폰트(qrc)
    └── shaders/NVScaler.glsl    # NVIDIA Image Scaling v1.0.2 (MIT)
```

## 사용자 데이터 저장 위치 (Windows)

- DB / 썸네일 캐시: `%APPDATA%/CustomMedia/VideoPlayer/`
- 설정: `QSettings` (`HKCU\Software\CustomMedia\VideoPlayer`)
- 로그: `%APPDATA%/CustomMedia/VideoPlayer/app.log`
- NIS 런타임 셰이더: `%APPDATA%/CustomMedia/VideoPlayer/shaders/NVScaler-runtime-XXX.glsl`

## 라이선스

본 저장소의 코드는 별도 명시 없는 한 비공개입니다. 외부 자산:

- `VideoPlayer/shaders/NVScaler.glsl` — NVIDIA Image Scaling v1.0.2 (MIT, © 2022 NVIDIA CORPORATION). agyild가 mpv용으로 포팅.
- `VideoPlayer/resources/fonts/MaterialIcons-Regular.ttf` — Material Icons (Apache License 2.0, © Google).
