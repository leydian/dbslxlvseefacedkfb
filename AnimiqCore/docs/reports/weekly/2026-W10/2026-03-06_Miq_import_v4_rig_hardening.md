# MIQ Import v4 Rig Hardening Update (2026-03-06)

## 1) 목적

기존 Unity MIQ Import 경로는 "기본 복원"은 가능했지만, 스키닝 본 계층을 더미 본으로 생성해 고정밀 리깅 복원 정확도가 낮았다.
이번 변경은 다음을 목표로 진행했다.

- MIQ 포맷을 v4로 확장해 리깅 계층 정보를 명시적으로 포함
- Exporter/Loader/Importer를 v4 스펙으로 정렬
- Import 중 부분 실패를 복구 가능한 오류로 격리하고 결과 리포트를 강화
- 머티리얼 알파 모드 복원 정책을 실사용 기준으로 보강

## 2) 핵심 변경 요약

### 2.1 포맷/데이터 모델

- MIQ 포맷 버전 범위를 `1|2|3|4`로 확장
- 신규 섹션 `0x0017` (Skeleton rig payload) 도입
  - `mesh_name`
  - `bone_count`
  - 반복 bone 엔트리
    - `bone_name`
    - `parent_index`
    - `local_matrix_f32_count` (현재 16 고정)
    - `local_matrix[16]`
- Runtime 데이터 모델에 추가
  - `MiqRigBonePayload`
  - `MiqSkeletonRigPayload`
  - `MiqAvatarPayload.SkeletonRigs`

### 2.2 Export 경로

- Export 버전을 `3 -> 4`로 상향
- `MiqAvatarExtractors`에서 `SkinnedMeshRenderer.bones` 기반 rig payload 생성
  - bone 이름
  - parent index
  - 로컬 TRS 행렬(4x4)
- `MiqExporter`에서 `0x0017` 섹션 직렬화
- Export 시 스키닝 데이터 검증 강화
  - skin 존재 시 skeleton pose(0x0016)와 rig(0x0017) 커버리지 확인
  - rig bone 수 < skin bone 수면 export 실패

### 2.3 Runtime Load 경로

- `MiqRuntimeLoader`가 `0x0017` 파싱 추가
- v4 스키닝 호환성 평가 규칙 추가
  - skin 있는데 rig 누락: `XAV4_RIG_MISSING`
  - rig bone 수 부족: `XAV4_RIG_BONE_COUNT_MISMATCH`
- strict validation 모드에서는 기존 정책대로 경고를 오류로 승격 가능

### 2.4 Unity Import 경로

- `MiqImportOptions` 확장
  - `FailOnRigDataMissing` (기본 true)
  - `MaterialRecoveryProfile` (`Standard|Aggressive`)
- `MiqImportReport` 확장
  - `RecoverableErrors`
  - `SkippedAssets`
  - `RigQuality` (`None|Partial|Full`)
- Import 안정성 개선
  - 머티리얼/메시 생성 단위 try-catch 격리
  - 실패 항목은 누적 리포트하고 가능한 범위 계속 진행(부분 임포트)
- 리깅 복원 개선
  - v4 rig 존재 시 parent index 기반 실제 본 계층 생성
  - skin bone index에 맞춰 bones 매핑
  - root bone 계산
  - rig 누락 시 옵션에 따라 즉시 실패 또는 폴백 본 생성
- 머티리얼 알파 모드 복원 강화
  - `OPAQUE|MASK|BLEND` 별 RenderType/queue/keyword/블렌딩 상태 세팅
  - Standard/URP 계열 공통 프로퍼티 존재 시 적용

### 2.5 메뉴/문서

- Import 완료 메시지에 `Rig quality`, `Recoverable errors` 요약 노출
- 포맷 문서(`docs/formats/miq.md`)에 v4/0x0017 명세 추가
- 패키지 README에 v4 rig 기반 복원 경로 반영

## 3) 테스트 변경

- Runtime loader 테스트 갱신
  - Unsupported version 케이스를 `v5`로 조정
  - `v4 + skin + rig 없음` 경고 검증 추가
  - `v4 + skin + rig 있음` 정상 파싱 검증 추가

## 4) 호환성/운영 영향

- 새 Export 산출물 기본 버전은 v4
- Loader는 v1~v4를 모두 허용하므로 기존 파일 읽기 호환성은 유지
- v4에서 rig 누락 스킨은 경고 또는 strict 실패로 처리 가능
- Import 품질 지표(`RigQuality`)와 복구 가능 오류 목록으로 운영 가시성 향상

## 5) 남은 작업/리스크

- Unity 에디터 실제 실행 기반 회귀 테스트는 아직 수행하지 않음
- 리깅 정확도는 본 계층/인덱스 정합성은 개선됐으나, 실아바타별 pose 편차 점검 필요
- shader별 세부 프로퍼티(특히 lilToon/Poiyomi 파생 키워드) 확장 여지 존재

## 6) 변경 파일 목록

- `NativeAnimiq/docs/formats/miq.md`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Runtime/MiqDataModel.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Editor/MiqExporter.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Editor/MiqImporterTypes.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Editor/MiqImporter.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Editor/MiqImportMenu.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`
- `NativeAnimiq/unity/Packages/com.animiq.miq/README.md`
