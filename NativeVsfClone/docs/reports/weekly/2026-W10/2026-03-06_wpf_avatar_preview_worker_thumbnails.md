# WPF Avatar Preview Worker Thumbnails (2026-03-06)

## Scope
- WPF 호스트의 아바타 선택 UX를 개선하기 위해, 불러오기 전(pre-load) 실시간 3D 썸네일 미리보기 경로를 추가했다.
- 범위는 `WpfHost` 우선이며, `WinUiHost`에는 동일 기능을 아직 적용하지 않았다.
- 구현은 메인 런타임 상태를 보호하기 위해 백그라운드 워커 프로세스 방식으로 제한했다.

## Implemented Changes
- NativeCore 썸네일 API 추가:
  - `NcThumbnailRequest` 구조체와 `nc_render_avatar_thumbnail_png(...)` API를 추가했다.
  - 오프스크린 D3D11 렌더 타겟에 단일 아바타를 렌더하고 BGRA 캡처 후 PNG 파일로 인코딩한다.
  - 관련 경로:
    - `include/vsfclone/nativecore/api.h`
    - `src/nativecore/native_core.cpp`
- HostCore interop 확장:
  - C# P/Invoke에 `NcThumbnailRequest`, `nc_render_avatar_thumbnail_png`를 노출했다.
  - 경로: `host/HostCore/NativeCoreInterop.cs`
- 세션 저장 모델 확장(v7):
  - `RecentAvatarEntry` 타입과 `SessionPersistenceModel.RecentAvatars`를 추가했다.
  - 최근 항목 정규화 정책을 추가했다:
    - 최대 12개
    - 최신순 정렬
    - 경로 중복 제거
    - 누락 파일 자동 제외
  - 경로: `host/HostCore/PlatformFeatures.cs`
- HostController 최근 아바타/썸네일 업데이트 경로 추가:
  - `GetRecentAvatars()`
  - `RecordAvatarSelection(path)`
  - `UpdateRecentAvatarThumbnail(path, thumbnailPath, status, lastError)`
  - `LoadAvatar(...)` 경로에서 `RecordAvatarSelection(...)` 호출하도록 연동했다.
  - 경로:
    - `host/HostCore/HostController.MvpFeatures.cs`
    - `host/HostCore/HostController.cs`
- WPF 워커 모드 추가:
  - 앱 시작 인자 `--thumbnail-worker`를 처리하는 분기 추가.
  - 워커 전용 진입점에서 `init -> load -> create_render_resources -> render_thumbnail -> shutdown` 순으로 동작한다.
  - 경로:
    - `host/WpfHost/App.xaml`
    - `host/WpfHost/App.xaml.cs`
    - `host/WpfHost/ThumbnailWorker.cs`
- WPF UI/동작 추가:
  - 아바타 섹션에 프리뷰 패널(`Image`, 상태 텍스트, 재생성 버튼)과 최근 목록(`ListBox`)을 추가했다.
  - 경로 입력/선택 시 유효 파일이면 즉시 썸네일 큐에 등록한다.
  - 단일 워커 실행(queue 기반)으로 동시 실행을 막고, 상태(`pending/ready/failed`)를 세션에 반영한다.
  - 썸네일 경로는 `%LOCALAPPDATA%/VsfCloneHost/thumbnails/<sha256>.png`를 사용한다.
  - 경로:
    - `host/WpfHost/MainWindow.xaml`
    - `host/WpfHost/MainWindow.xaml.cs`

## Verification Summary
- 실행 커맨드:
  - `cmake -S NativeVsfClone -B NativeVsfClone/build-thumb -DVSFCLONE_BUILD_CONSOLE=ON`
  - `cmake --build NativeVsfClone/build-thumb --config Release --target nativecore`
  - `dotnet build NativeVsfClone/host/WpfHost/WpfHost.csproj -c Release`
- 결과:
  - `nativecore` C++ 빌드 성공.
  - WPF/.NET 빌드는 현재 환경의 NuGet 네트워크 제한으로 실패:
    - `NU1301` (`https://api.nuget.org/v3/index.json` 접근 실패)
    - `NU1101` (`OpenCvSharp4`, 런타임 팩 등 복원 실패)

## Known Risks or Limitations
- 현재 WPF 빌드를 로컬에서 끝까지 확인하지 못했다(패키지 복원 불가 환경).
- 썸네일 생성은 워커 프로세스 호출 비용이 있어 대량 경로 변경 시 지연이 발생할 수 있다.
- `recent avatars`는 세션 파일 기반이라 외부에서 파일 이동/삭제가 빈번하면 `failed/none` 상태가 자주 나타날 수 있다.
- WinUI 호스트는 아직 미적용 상태다.

## Next Steps
- NuGet 접근 가능한 환경에서 `WpfHost` 전체 빌드/실행 스모크를 수행한다.
- 썸네일 생성 타임아웃/재시도 정책(현재 고정 20초, 수동 재시도)을 사용자 설정화한다.
- `WinUiHost`에도 동일한 recent/preview 패널 및 워커 연동을 적용한다.
