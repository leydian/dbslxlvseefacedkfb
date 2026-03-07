# 2026-03-08 VSFAvatar Authored Route, Safe Fallback, and Runtime Stabilization Bundle

## 배경
- `.vsfavatar` 로딩은 성공했지만 화면에서 점/축 노이즈 또는 깨진 면이 관찰되었고, 경우에 따라 모델이 사실상 보이지 않는 상태가 반복되었습니다.
- 진단상 `RenderRc: Ok`, `DrawCalls > 0` 임에도 시각적으로 실패하는 케이스가 있어, 로더 라우팅/메쉬 품질/프리뷰 카메라 기준을 함께 손봐야 했습니다.

## 이번 번들의 핵심 목표
- `authored -> structured -> heuristic -> safe fallback` 순서의 일관된 라우팅 정책 확립
- 낮은 품질 메쉬의 AutoFit 오염(축 스파이크) 방지
- VSFAvatar UX에서 오진 유발 진단(`EXPRESSION_COUNT_ZERO`) 완화
- VSFAvatar 프리뷰 기본 정면(yaw=0) 고정

## 변경 사항 상세

### 1) Sidecar 계약 확장 (`tools/vsfavatar_sidecar.cpp`)
- 스키마/버전
  - `schema_version: 5 -> 6`
  - `extractor_version: inhouse-sidecar-v5 -> inhouse-sidecar-v6`
- 새 출력 필드 추가
  - `payload_quality_score` (정수)
  - `skin_binding_coverage` (실수)
  - `payload_route_reason_code` (문자열)
  - `topology_flags` (문자열 배열)
- 새 라우트 모드 추가
  - `render_payload_mode: authored_mesh_v1`
  - 조건: `probe_stage=complete`, `object_table_parsed=true`, `mesh_object_count>0`, `skinned_mesh_renderer_count>0`
- 기존 stub/placeholder 유지하되 route 코드/품질 정보 동반 노출

### 2) Loader 라우터/스키마 처리 확장 (`src/avatar/vsfavatar_loader.cpp`)
- sidecar schema 검증 확장
  - v6 허용
  - v6 필수 필드 검사(`payload_quality_score`, `skin_binding_coverage`, `payload_route_reason_code`, `topology_flags`)
- JSON 실수 파서 `GetJsonF64` 추가
- 라우팅 로직 확장
  - `render_payload_mode == authored_mesh_v1` 지원
  - sidecar 품질/커버리지와 추출 결과를 결합해 경로 결정
  - 최종 선택 경고 체계:
    - `VSF_AUTHORED_MESH_PAYLOAD`
    - `VSF_SERIALIZED_STRUCTURED_MESH_PAYLOAD`
    - `VSF_SERIALIZED_HEURISTIC_MESH_PAYLOAD`
    - `VSF_SAFE_FALLBACK_APPLIED` (+ 호환용 `VSF_OBJECT_STUB_RENDER_PAYLOAD`)
- 관찰성 강화
  - `W_ROUTE`, `W_TOPOLOGY_FLAGS`, `W_PAYLOAD_QUALITY` 진단 라인 추가

### 3) Heuristic 메쉬 보정/노이즈 완화 (`src/avatar/vsfavatar_loader.cpp`)
- heuristic 포인트 기반 payload 생성 시
  - 재센터링(center shift)
  - 스케일 정규화(normalization scale)
  - 포인트 스플랫 크기 보정
- 목적: 좌표계 편차로 인한 화면 이탈/과도한 스파이크 영향 완화

### 4) Runtime bounds/draw outlier 필터 확장 (`src/nativecore/native_core.cpp`)
- VSF heuristic 경로 식별(`VSF_SERIALIZED_HEURISTIC_MESH_PAYLOAD` warning code 기반)
- AutoFit bounds 계산에서 축/선형 스파이크 메쉬 제외 로직 적용
- draw 단계에서도 동일 excluded 집합을 스킵(`skip_bounds_outlier_draws`)하여 시각 노이즈 감소

### 5) VSFAvatar 프리뷰 yaw 정책 고정
- Native 기본 yaw 정책 변경 (`src/nativecore/native_core.cpp`)
  - `AvatarSourceType::VsfAvatar` 기본 preview yaw를 `180 -> 0`
- Host load 시 flip 재적용 방지 (`host/HostCore/HostController.cs`)
  - `VsfAvatar`는 stored flip이 있어도 `resolved=false`, `mode=vsfavatar-default-front`로 고정

### 6) Feature Gate 메시지 정책 조정 (`host/HostCore/HostFeatureGates.cs`)
- VSFAvatar에서 `ExpressionCount==0`일 때
  - 공통 원인 `payload_policy_gate/EXPRESSION_COUNT_ZERO`로 승격하지 않음
  - 별도 사유 `EXPRESSION_OPTIONAL_VSFAVATAR` 사용
- 사용자 액션 힌트 텍스트 추가
  - VSFAvatar는 표정 페이로드가 없어도 렌더 가능함을 명시

### 7) VRM 보정 반영 (`src/avatar/vrm_loader.cpp`)
- reflection transform(det<0) 감지 시 triangle winding swap 보정
  - back-face culling으로 인한 inside-out 렌더 문제 완화
- 비스키닝 메쉬 normal bake 조건 명확화

## 검증
- `tools/publish_hosts.ps1` 반복 실행으로 native/WPF 재배포 및 smoke 통과
- `cmake --build build --config Release --target vsfavatar_sidecar` 재빌드 후 sidecar 출력 검증
  - `schema_version=6`
  - `render_payload_mode=authored_mesh_v1`
  - 새 진단 필드 출력 확인
- `avatar_tool`로 `NewOnYou.vsfavatar` 로딩 진단 확인
  - `W_ROUTE`, `W_TOPOLOGY_FLAGS`, `W_PAYLOAD_QUALITY` 반영 확인

## 현재 상태 요약
- 라우팅/계약/진단 프레임워크는 authored-first + safe fallback 구조로 전환 완료
- VSFAvatar yaw/flip 정책은 정면 기준으로 안정화
- heuristic 스파이크에 대한 bounds/draw 필터가 런타임에 적용됨
- 실제 샘플 품질 최적화는 추가 튜닝 여지가 있으나, 실패 시 안전 폴백과 원인 가시성은 확보됨
