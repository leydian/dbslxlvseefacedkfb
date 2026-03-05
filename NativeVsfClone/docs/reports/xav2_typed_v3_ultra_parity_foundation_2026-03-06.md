# XAV2 typed-v3 + UltraParity foundation implementation report (2026-03-06)

## Summary

이번 변경은 `.xav2`의 "최상급 품질/완전 셰이더 재현" 단계로 가기 위한 기반 공사에 해당한다.
핵심은 다음 4축이다.

1. typed material 스키마를 `typed-v2`에서 `typed-v3`로 확장 (역호환 유지)
2. native/runtime/Unity exporter/loader 간 스키마 정렬
3. runtime 성능/렌더 진단 지표 확장 (`gpu/cpu/material/pass`)
4. 품질 게이트/프로파일(`ultra-parity`) 기초 계약 추가

이번 패치는 고충실도 셰이더 재현 "완성"이 아니라, 다음 단계(실제 lilToon 고충실도 렌더 구현)를 안전하게 진행하기 위한 계약/계측/호환 기반 완성에 초점을 둔다.

## Key changes

### 1) XAV2 typed-v3 schema foundation (native + Unity)

- native payload model 확장:
  - `include/vsfclone/avatar/avatar_package.h`
  - `MaterialRenderPayload`에 `typed_schema_version` 추가
- native loader 파싱 확장:
  - `src/avatar/xav2_loader.cpp`
  - `0x0015` section에서 `typed-v3` optional schema field를 읽고, `typed-v2` fallback 유지
  - manifest `materialParamEncoding` 힌트(`typed-v3`) 기반 파싱 우선순위 적용
  - material 결합 시 `material_param_encoding`, `typed_schema_version`을 payload로 전달
- Unity data/runtime/export alignment:
  - `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`
  - `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`
  - `unity/Packages/com.vsfclone.xav2/Editor/Xav2Exporter.cs`
  - `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`
  - extractor 기본 방출을 `typed-v3`로 전환, exporter는 `typed-v3` schema marker 기록
  - runtime loader는 `typed-v3`/`typed-v2` 양쪽 파싱 지원
- Unity tests:
  - `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`
  - `typed-v3` 기본 파싱 검증 + `typed-v2` legacy shape 역호환 테스트 추가

### 2) Native render/runtime diagnostics contract expansion

- public C API 확장:
  - `include/vsfclone/nativecore/api.h`
  - `NcRenderQualityProfile` 추가 (`DEFAULT`, `BALANCED`, `ULTRA_PARITY`)
  - `NcRenderQualityOptions`에 `quality_profile` 추가
  - `NcRuntimeStats`에 확장 지표 추가:
    - `gpu_frame_ms`
    - `cpu_frame_ms`
    - `material_resolve_ms`
    - `pass_count`
- native runtime 계측:
  - `src/nativecore/native_core.cpp`
  - material resolve 구간 계측 추가
  - draw pass 수(opaque/mask/blend) 집계 추가
  - runtime stats 채움 및 initialize 시 초기화
- typed material alpha 해석 확장:
  - `src/nativecore/native_core.cpp`
  - `typed-v3`도 `typed-v2`와 동일한 alpha feature flag 해석 경로 사용

### 3) HostCore profile/telemetry plumbing

- interop 확장:
  - `host/HostCore/NativeCoreInterop.cs`
  - 새 enum/필드 매핑 + `BuildUltraParityPreset()` 추가
- controller profile mapping:
  - `host/HostCore/HostController.cs`
  - `LastProfileName` 기준으로 native `quality_profile` 전달
- MVP profile extension:
  - `host/HostCore/HostController.MvpFeatures.cs`
  - `ApplyRenderProfile("ultra-parity")` 추가
- diagnostics + metrics CSV 확장:
  - `host/HostCore/DiagnosticsModel.cs`
  - `host/HostCore/PlatformFeatures.cs`
  - `host/HostCore/HostController.MvpFeatures.cs`
  - frame metric/CSV header에 gpu/cpu/material/pass 지표 반영

### 4) Quality gate/tooling and format docs sync

- render perf gate profile 추가:
  - `tools/render_perf_gate.ps1`
  - `ultra-parity` profile 기본 임계치 추가
    - `P95 <= 16.7`
    - `P99 <= 20.0`
    - `DropRatio <= 0.01`
- XAV2 render regression gate 강화:
  - `tools/xav2_render_regression_gate.ps1`
  - snapshot parity 필수화 옵션 `-RequireSnapshotParity` 추가
  - `XAV2_SHADER_PARITY_*` warning code를 critical warning 판정 대상으로 포함
- format 문서 갱신:
  - `docs/formats/xav2.md`
  - `0x0015`를 `v2/v3`로 명시, `typed_schema_version` optional field 문서화

## Verification

실행한 검증:

```powershell
cmake --build NativeVsfClone\build --config Release --target nativecore avatar_tool
NativeVsfClone\build\Release\avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.xav2"
dotnet build NativeVsfClone\host\HostCore\HostCore.csproj -c Release
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release
dotnet build NativeVsfClone\host\WinUiHost\WinUiHost.csproj -c Release
powershell -ExecutionPolicy Bypass -File NativeVsfClone\tools\xav2_render_regression_gate.ps1 `
  -SampleDir D:\dbslxlvseefacedkfb `
  -AvatarToolPath D:\dbslxlvseefacedkfb\NativeVsfClone\build\Release\avatar_tool.exe `
  -FailOnRenderWarnings
```

결과:

- `nativecore/avatar_tool` build: PASS
- sample `.xav2` parse: PASS
  - `Format=XAV2`
  - `Compat=full`
  - `ParserStage=runtime-ready`
  - `PrimaryError=NONE`
- `HostCore` build: PASS
- `WpfHost` build: PASS (NuGet restore network 허용 후)
- `WinUiHost` build:
  - NuGet restore는 통과했으나, 환경의 WinUI XAML compile 단계에서 `MSB3073 (XamlCompiler.exe)` 실패
- XAV2 regression gate:
  - 현재 로컬 샘플 1개 환경에서 `MinSampleCount` 기준 미달로 overall FAIL
  - 파서/에러/critical warning 라인은 PASS

## Scope notes

- 본 변경은 품질 목표 달성을 위한 "기반 공사"다.
- full lilToon high-fidelity parity(정교한 조명/림/노멀/멀티패스 재현)는 후속 렌더링 구현 단계에서 완료해야 한다.
