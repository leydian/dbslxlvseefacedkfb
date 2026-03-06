# MIQ Import Rig Accuracy Hardening Follow-up (2026-03-06)

## 개요

이번 라운드는 기존 v4 rig(0x0017) 기반 임포트/익스포트 경로 위에서, 실제 사용 시 문제가 되던 리깅 정확도와 진단 가시성을 강화하는 데 집중했다.
핵심 목표는 다음 3가지였다.

- 안전 기본정책 유지: rig 누락/불량을 기본적으로 엄격 처리
- 리깅 품질 향상: 본 매핑/루트본 선택/행렬 해석의 안정성 강화
- 운영 가시성 강화: 문제가 발생했을 때 원인을 코드/메시로 추적 가능하게 개선

## 변경 상세

### 1) Import 옵션/리포트 확장

파일: `unity/Packages/com.animiq.miq/Editor/MiqImporterTypes.cs`

- 신규 enum: `MiqRigRecoveryPolicy`
  - `Strict` (기본)
  - `Fallback`
- 신규 진단 타입: `MiqRigDiagnostic`
  - `Code`, `MeshName`, `BoneName`, `Message`
- `MiqImportOptions` 확장
  - `RigRecoveryPolicy` 추가 (기본 `Strict`)
- `MiqImportReport` 확장
  - `RigDiagnostics` 컬렉션 추가

의미:
- 문자열 경고만으로는 추적이 어려웠던 리깅 문제를 구조화된 진단으로 수집 가능.
- 정책 플래그가 명시되어 배포 환경에서 strict/fallback 동작 제어가 쉬워짐.

### 2) Import 리깅 복원 로직 고도화

파일: `unity/Packages/com.animiq.miq/Editor/MiqImporter.cs`

#### 2-1. strict 정책 적용 강화
- 기존: `FailOnRigDataMissing`만 실패 조건
- 변경: `FailOnRigDataMissing == true` 또는 `RigRecoveryPolicy == Strict`이면 rig 누락 시 즉시 실패

#### 2-2. 본 매핑 진단 강화
- `MapSkinBones(...)` 시 skin bone index가 rig bones를 못 찾으면
  - `XAV4_RIG_BONE_UNRESOLVED` 진단 추가
  - warning / warning code 동시 누적

#### 2-3. 루트 본 선택 규칙 개선
- `ResolveRootBone(...)` 우선순위:
  1. parent index가 -1인 bone
  2. skin에서 참조 빈도가 가장 높은 bone index
  3. 첫 번째 bone fallback

효과:
- root 식별 실패 시 기존보다 실제 skin 영향도가 높은 bone을 선택하여 변형 품질 개선.

#### 2-4. parent index 방어 로직
- `CreateSkeletonBonesFromRig(...)`에서
  - self-parent, 범위 초과 parent index 감지
  - `XAV4_RIG_PARENT_INDEX_INVALID` 진단 기록
  - 안전 parent(fallback)로 연결

#### 2-5. non-orthonormal matrix 진단
- 행렬 기반 TRS 적용 전 정규직교성(orthonormal basis) 점검
- 비정상 시 `XAV4_RIG_MATRIX_NON_ORTHONORMAL` 진단 기록 후 정규화 폴백 경로 적용

#### 2-6. 공통 진단 helper 도입
- `AddRigDiagnostic(...)` 추가
  - `RigDiagnostics` 객체 추가
  - 경고 문자열/코드 동시 등록

### 3) Export 쪽 rig 그래프 검증 강화

파일: `unity/Packages/com.animiq.miq/Editor/MiqExporter.cs`

`ValidateRigGraph(...)` 신규 추가 및 export 전 검증 수행:

- bone name 공백 금지
- bone name 중복 금지
- parent index 유효 범위 검증
- self-parent 금지
- parent cycle 탐지

효과:
- 잘못된 rig가 아카이브되는 것을 export 단계에서 조기 차단.

### 4) Runtime Loader rig 스키마 검증 강화

파일: `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`

`TryParseSkeletonRig(...)` 이후 `ValidateRigGraph(...)` 실행:

- empty bone name
- duplicate bone name
- invalid parent index / self-parent
- parent cycle

문제 발견 시 코드:
- `XAV4_RIG_SCHEMA_INVALID`

strict 모드에서는 기존 정책에 따라 경고가 fail로 승격됨.

### 5) Runtime 테스트 확장

파일: `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`

신규 테스트:
- `TryLoad_V4RigDuplicateBoneName_WarnsSchemaInvalid`
- `TryLoad_V4RigCycle_Strict_Fails`

테스트 빌더 보강:
- rig duplicate/cycle 생성 파라미터 추가

의미:
- 새 rig 스키마 검증 규칙이 회귀 없이 유지되도록 자동 보호.

### 6) 메뉴/README 업데이트

파일:
- `unity/Packages/com.animiq.miq/Editor/MiqImportMenu.cs`
- `unity/Packages/com.animiq.miq/README.md`

변경:
- Import 완료 요약에 `Rig diagnostics` 개수 표시
- README에 rig 기본 정책 명시
  - `FailOnRigDataMissing = true`
  - `RigRecoveryPolicy = Strict`

## 동작/품질 영향

- 기본 동작은 더 안전해졌고(엄격), 문제 파일은 조기에 실패하거나 정확한 진단으로 파악 가능.
- 리깅 복원 시 root/bone 매핑 품질이 개선되어 변형 오차 가능성을 줄임.
- 비정상 행렬/부모관계/중복본 같은 구조적 문제는 exporter/loader 양쪽에서 방어됨.

## 남은 과제

- Unity EditMode에서 importer 통합 테스트(실아바타 샘플 기반) 추가 필요
- shader별 재질 복원 정밀도(lilToon/Poiyomi 세부 프로퍼티)는 후속 고도화 여지 있음
- large avatar에서 import 성능 프로파일링(메시/텍스처 개수 많은 케이스) 권장

## 이번 커밋 포함 파일

- `NativeAnimiq/docs/reports/miq_import_rig_accuracy_hardening_followup_2026-03-06.md`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Editor/MiqExporter.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Editor/MiqImportMenu.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Editor/MiqImporter.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Editor/MiqImporterTypes.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/README.md`
