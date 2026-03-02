# build/reports Policy

이 폴더는 실행/검증 과정에서 생성되는 리포트를 임시 보관합니다.

## 유지 기준

- 각 리포트군별 `latest` 1개 유지
- 의미 있는 milestone 스냅샷 유지

## 아카이브 기준

- 중간 단계 산출물/구버전 리포트는 `docs/archive/build-reports/`로 이동합니다.
- 장기 보관/비교 기준 문서는 `docs/reports/`에 별도 Markdown 리포트로 정리합니다.

## 현재 유지 파일(예시)

- `vsfavatar_probe_latest_after_scoring.txt` (latest)
- `vsfavatar_probe_latest_decode_tuning.txt` (milestone)
- `vsfavatar_probe_latest_block0_hypothesis.txt` (milestone)
- `vsfavatar_probe_sidecar.txt` (milestone)
- `vsfavatar_probe_fixed.txt` (baseline snapshot)
