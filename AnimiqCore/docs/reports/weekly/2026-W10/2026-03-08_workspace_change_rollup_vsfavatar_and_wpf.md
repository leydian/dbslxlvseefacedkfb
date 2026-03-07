# 2026-03-08 Workspace Change Rollup (VSFAvatar + WPF + Branding)

## Scope
- Window: 2026-03-08 session
- Branch: `main`
- Objective: VSFAvatar 로더 안정화/복구, WPF 운영 UI 확장, 브랜딩 자산 반영, 배포 경로 정리

## Changed File Inventory
### HostCore / Feature Gates
- `AnimiqCore/host/HostCore/HostController.MvpFeatures.cs`
- `AnimiqCore/host/HostCore/HostFeatureGates.cs`
- `AnimiqCore/host/HostCore/PlatformFeatures.cs`

### WPF Host UI
- `AnimiqCore/host/WpfHost/MainWindow.xaml`
- `AnimiqCore/host/WpfHost/MainWindow.xaml.cs`
- `AnimiqCore/host/WpfHost/CommandPalette.xaml` (new)
- `AnimiqCore/host/WpfHost/CommandPalette.xaml.cs` (new)

### VSFAvatar / Native Rendering
- `AnimiqCore/include/animiq/vsf/serialized_file_reader.h`
- `AnimiqCore/include/animiq/vsf/unityfs_reader.h`
- `AnimiqCore/src/vsf/serialized_file_reader.cpp`
- `AnimiqCore/src/vsf/unityfs_reader.cpp`
- `AnimiqCore/src/avatar/vsfavatar_loader.cpp`
- `AnimiqCore/tools/vsfavatar_sidecar.cpp`
- `AnimiqCore/src/nativecore/native_core.cpp`

### VRM Runtime (co-edited in same workspace)
- `AnimiqCore/src/avatar/vrm_loader.cpp`

### Branding Assets
- `AnimiqCore/host/Branding/generated/` (new)
- `AnimiqCore/host/Branding/tmp_edge/` (new)

## Detailed Summary
### 1) VSFAvatar load/recovery hardening
- Sidecar 실행 timeout 상향 및 cmdline 안정화 반영.
- Sidecar/loader contract 진단 항목 확장 (`serialized_parse_path`, major type, skinned renderer count 등).
- object table parse 성공 시 payload 단계로 진입하는 복구 루트 강화.

### 2) Placeholder/stub rendering safety policy
- placeholder/object-stub payload 처리와 warning code 계약 정리.
- preview/output 경계에서 실패 시 사용자에게 불안정 geometry 대신 안전 fallback 제공.
- 구조화/휴리스틱 mesh 경로 품질 미달 시 stub로 강제 fallback.

### 3) Structured mesh extraction prototype
- Serialized mesh blob 추출 API 도입:
  - `SerializedFileReader::ExtractMeshObjectBlobs(...)`
- UnityFS probe에 best serialized bytes 전달 필드 추가:
  - `UnityFsProbe::serialized_file_bytes`
- 로더에서 index/vertex 후보 매칭 기반 구조화 payload 시도 경로 추가.
- 오탐 방지를 위한 품질 게이트(잘못된 인덱스/장거리 엣지/라인형 점군) 추가.
- 현재 상태: `NewOnYou.vsfavatar` 기준 실메쉬 경로는 실험적이며, 기본 동작은 안정성을 위해 stub 우선.

### 4) Native camera/autofit behavior tuning
- VSFAvatar placeholder/stub 조건에서 과확대 방지용 autofit 보정 분기 반영.
- focus/fit 스케일 디버깅 정보 확장.

### 5) WPF UI and ops improvements (same workspace batch)
- MainWindow UI/코드비하인드 확장.
- Command Palette 신규 창 추가.
- 운영 관련 기능 게이트/플랫폼 기능 코드 조정.

### 6) Branding integration
- generated + tmp_edge 하위 신규 브랜딩 산출물 반영.

## Verification and Deployment Notes
- `nativecore` 빌드 및 배포 스크립트 반복 검증.
- 일부 구간에서 파일 잠금으로 `build_hotfix` fallback 경로 사용.
- 최근 상태에서 WPF publish는 XAML 이벤트 핸들러 누락 오류로 실패 가능 (`Window_Drop`, `Window_DragOver`, `OpenCommandPalette_Click` 등).
- 운영상 우회 배포: `nativecore.dll` 단독 교체 배포 수행.

## Current Known Issues
- VSFAvatar 정식 topology 복원(`m_SubMeshes` 기반)은 아직 미완료.
- Structured mesh 경로는 안전 게이트 강화 상태이며 기본 안정 출력은 stub.
- WPF publish는 핸들러 누락 문제 해결 전까지 실패 가능.

## Next Actions
1. `m_SubMeshes` descriptor 파싱으로 index range/topology 정식 복원.
2. vertex channel layout(uv/normal/skin) 해석 고도화.
3. WPF `MainWindow.xaml` 핸들러 누락 정리 후 full publish 경로 복구.
