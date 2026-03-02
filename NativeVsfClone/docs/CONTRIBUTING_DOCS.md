# Documentation Conventions

문서 작성/정리 시 아래 규칙을 기본으로 사용합니다.

## 1. 언어 및 표현

- 기본 언어는 한국어로 작성합니다.
- 기술 키워드/코드 식별자는 영어 원문을 유지합니다.
- 날짜는 `YYYY-MM-DD` 형식을 사용합니다.

## 2. 문서 분류

- `README.md`: 사용/빌드/실행 중심 입문 문서
- `CHANGELOG.md`: 변경 요약 기록
- `docs/formats/`: 포맷 스펙
- `docs/reports/`: 구현/검증 리포트
- `docs/archive/`: 과거 문서 및 생성 리포트 보관

## 3. CHANGELOG 운영 규칙

- 한 엔트리는 다음 3개 섹션으로 유지합니다:
  - `Summary`
  - `Changed`
  - `Verified`
- 상세 실험 로그는 `docs/reports/*.md`로 작성하고, CHANGELOG에는 요약+링크만 남깁니다.

## 4. 생성 리포트(`build/reports`) 보존 규칙

- 각 리포트군당 최신 파일 1개와 의미 있는 milestone 스냅샷만 유지합니다.
- 나머지 파일은 `docs/archive/build-reports/`로 이동합니다.
- `build/reports`는 재생성 가능한 산출물 폴더이며 장기 보관 위치가 아닙니다.

## 5. 리포트 문서 템플릿

`docs/reports/*.md`는 아래 구조를 권장합니다.

1. Scope
2. Implemented Changes
3. Verification Summary
4. Known Limitations
5. Next Steps
