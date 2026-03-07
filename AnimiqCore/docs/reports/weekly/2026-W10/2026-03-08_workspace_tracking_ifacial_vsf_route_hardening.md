# 2026-03-08 Workspace Tracking/iFacial + VSFAvatar Route Hardening

## Scope
- Branch: `main`
- Window: 2026-03-08 (current uncommitted workspace delta)
- Objective: iFacial/상반신 트래킹 품질 개선, VSFAvatar 라우팅 안전성 강화, VRM arm-pose 포맷 정책 정렬

## Changed Files
- `host/HostCore/HostController.MvpFeatures.cs`
- `host/HostCore/HostController.cs`
- `host/HostCore/HostFeatureGates.cs`
- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/TrackingInputService.cs`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
- `src/avatar/vsfavatar_loader.cpp`
- `src/nativecore/native_core.cpp`
- `tools/vsfavatar_sidecar.cpp`

## Detailed Summary
### 1) Tracking 설정 모델/영속성 확장
- `TrackingInputSettings`/`TrackingStartOptions`에 아래 항목을 추가:
  - `IfacialBlendshapeSmoothing`
  - `IfacialBlinkJawPriorityEnabled`
  - `UpperBodyFallbackFromHeadEnabled`
  - `UpperBodyFallbackFromHeadStrength`
- 세션 모델 버전을 `11 -> 13`으로 상향하고, 구버전 마이그레이션에서 기본값/클램프를 적용.
- 트래킹 설정 로그(`TrackingConfig`)에 신규 파라미터가 포함되도록 확장.

### 2) HostController 트래킹/표정/포즈 처리 강화
- ARKit 정규화 후 VRM 프리셋 alias(`blink`, `a/i/u/e/o`, `joy/angry/sorrow`, `look*`)를 자동 보강.
- `eyeWideLeft/Right` 기반 `eyeBlinkLeft/Right` 보조 복원 추가.
- 상반신 입력이 비어있을 때 head pose + expression 기반 상체 폴백 경로 추가:
  - quaternion -> euler 변환
  - trunk pitch/yaw 산출 및 양자화
  - shoulder/upperarm pitch 생성
  - 폴백 적용 여부/사유 상태 노출(`IsHeadDrivenUpperBodyFallbackApplied`, `HeadDrivenUpperBodyFallbackReason`)
- 런타임 pose payload 빌드 시 spine/chest/upperchest/neck yaw/pitch에 head-driven 폴백 성분을 합성.

### 3) TrackingInputService iFacial 처리 품질 향상
- iFacial 모드에서 저지연 바닥값(알파/데드밴드) 강제 보정 로직 추가.
- iFacial 전용 표정 후처리(`ApplyExpressionPostProcess`) 도입:
  - blink/jaw/mouth에 비선형 감쇠 곡선 적용
  - 일반 소스는 기존 adaptive calibration 유지
- `eyeblink` 단일 채널 및 `vrcv_*`, `mouth*` alias 입력 처리 확장.
- iFacial 우선 옵션 활성 시 blink/jaw 계열을 보강 반영.
- MediaPipe Python 후보 검증을 공용 probe 함수로 통합하고, `mediapipe/cv2 import` 선검증을 추가해 sidecar `code=2` 실패를 사전 차단.

### 4) WPF 트래킹 UI/상태 노출 확장
- Tracking 설정 섹션에 신규 제어 추가:
  - 상반신 헤드 폴백 토글
  - 헤드 폴백 강도 슬라이더
  - iPhone(iFacial) 스무딩 슬라이더
  - iPhone blink/jaw 우선 토글
- UI 값 변경 이벤트를 `ConfigureTrackingInputSettings(...)`에 연결.
- 추적 상태 문자열에 신규 상태 포함:
  - `ifacial_smoothing`, `ifacial_blinkjaw_priority`
  - `upper_fallback_enabled`, `upper_fallback_strength`
  - `upper_fallback_active`, `upper_fallback_reason`
- 트래킹 동작 중/Busy 상태에서 제어 비활성화 규칙 확장.

### 5) VSFAvatar sidecar/loader 라우팅 하드닝
- Sidecar JSON schema에 authored extractability 지표를 추가:
  - `authored_extractable_mesh_count`
  - `authored_extractable_vertex_sample_total`
- Sidecar에서 serialized mesh blob 샘플을 직접 스캔해 authored 경로 가능성 점수를 계산.
- Loader에서 structured mesh 빌드 통계(stride/offset/index-window) 수집 및 경고 로그 확장.
- structured 경로 후보 탐색을 강화:
  - vertex position offset 탐색
  - contiguous triangle window 선택
  - 연결성/line-like/축 지배(outlier) 게이트 강화
- authored/structured/heuristic/safe-fallback 라우팅 코드와 reject code를 명시적으로 기록.
- skinned avatar인데 skin payload가 비어있는 경우 안전 폴백으로 강등하는 정책 추가.

### 6) NativeCore 정책/렌더 안전성 보강
- arm-pose 지원 포맷을 MIQ 전용에서 MIQ/VRM으로 확장(게이트 사유 문구 포함).
- VSF heuristic avatar 렌더에서 축 비율 스파이크 mesh 스킵(`VSF_HEURISTIC_AXIS_SPIKE_SKIPPED`) 추가.
- 공격적 bounds outlier draw skip은 MIQ 정책 경로에만 적용하도록 분리.

## Impact
- iFacial 입력에서 눈/입 모션 반응성과 품질이 개선되고, 상반신 입력 공백 시 head-driven 폴백으로 시각적 정지 현상이 완화됨.
- VSFAvatar 로더가 authored/structured 경로를 더 명확한 품질 규칙으로 선택하고, 불완전 payload에서는 안전 폴백으로 수렴.
- VRM arm-pose 적용 정책이 Host/Native 간 일치.

## Validation Notes
- 본 문서는 `git diff` 기준 코드 변경사항을 정리한 문서 갱신이며, 추가 빌드/실행 검증 로그는 이번 커밋에 포함하지 않음.
