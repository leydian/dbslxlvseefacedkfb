# VRM -> MIQ 품질/런타임 검증 고도화 (실행 상세)

## 요약
- `vrm_to_miq`에 품질 우선 검증을 강화하고, 변환 산출물을 런타임 경로로 재로딩해 진단 JSON에 `runtimeValidation`/`qualityGate`를 추가했다.
- 품질 게이트 스크립트에 `GateQ`를 신설해, 변환 성공 여부뿐 아니라 런타임 준비 상태(`runtimeReady`)와 P0 품질 조건을 동시에 검사하도록 확장했다.
- 결과적으로 `GateA/B/C/Q`는 통과하며, 성능 KPI(`GateD/E`)는 현재 샘플 기준 미달 상태로 리포트된다.

## 구현 변경 상세

### 1) 변환기 품질 검증 강화 (`tools/vrm_to_miq.cpp`)
- Rig 검증 추가/강화
  - `XAV4_RIG_PARENT_INDEX_INVALID`: parent index 범위 오류
  - `XAV4_RIG_PARENT_SELF_LOOP`: self-parent 루프
  - `XAV4_RIG_MATRIX_NONFINITE`: 행렬값 NaN/Inf
  - `XAV4_RIG_REQUIRED_HUMANOID_MISSING`: humanoid bone 태그가 있는 rig에 한해 필수 humanoid ID 누락 검사
- BlendShape 검증 추가
  - `MIQ_BLENDSHAPE_DELTA_VERTEX_INVALID`: vertex delta 바이트 정합 오류(12의 배수 아님)
  - `MIQ_BLENDSHAPE_DELTA_NORMAL_MISMATCH`: normal delta 크기 불일치
  - `MIQ_BLENDSHAPE_DELTA_TANGENT_MISMATCH`: tangent delta 크기 불일치
- Strict 정책 강화
  - P0 성격 코드들을 hard-reject 세트로 분리해 allowlist가 있어도 거부하도록 구성

### 2) 런타임 재검증 + 진단 계약 확장 (`tools/vrm_to_miq.cpp`)
- 변환 직후 출력 MIQ를 `AvatarLoaderFacade`로 다시 로딩하는 `RuntimeValidation` 단계 추가
- 진단 JSON(`--diag-json`)에 아래 필드 추가
  - `runtimeValidation`: `attempted/loadOk/compat/parserStage/primaryErrorCode/warningCodes/missingFeatures/runtimeReady`
  - `qualityGate`: `p0IssueCodes/p0RuntimeWarningCodes/pass`
- 품질 요약 집계 시 중복 코드 제거(dedup) 처리

### 3) 품질 게이트 확장 (`tools/vrm_to_miq_quality_gate.ps1`)
- `GateQ (quality)` 추가
  - 필수 진단 계약 존재 여부 확인: `runtimeValidation`, `qualityGate`
  - `runtimeValidation.runtimeReady == true` 및 `qualityGate.pass == true`를 품질 통과 조건으로 사용
- 리포트 row에 `quality_ok` 추가
- strict smoke allowlist 기본값 보강
  - `VRM_NODE_TRANSFORM_APPLIED`
  - `XAV4_RIG_REQUIRED_HUMANOID_MISSING`

## 검증 실행
- 빌드
  - `cmake --build build --config Release --target vrm_to_miq avatar_tool`
- 게이트
  - `powershell -ExecutionPolicy Bypass -File .\tools\vrm_to_miq_quality_gate.ps1 -SampleDir ..\sample -MaxSamples 1`
- 결과 요약(`build/reports/vrm_to_miq_quality_gate_summary.txt`)
  - PASS: `GateA`, `GateB`, `GateC`, `GateQ`
  - FAIL: `GateD`, `GateE` (성능 KPI 미달)
  - Overall: `PASS` (`EnforceKpi=False` 기준)

## 영향 및 후속 권고
- 현재 변경은 품질/정합 중심으로, 성능 KPI는 경고성 리포팅 상태로 유지된다.
- `EnforceKpi=True` 운영 전에는 `runtime_optimized` 프로파일의 write/buffer 개선이 추가로 필요하다.
