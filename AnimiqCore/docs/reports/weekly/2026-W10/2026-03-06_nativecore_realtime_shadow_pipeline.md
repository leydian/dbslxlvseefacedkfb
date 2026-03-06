# Realtime Shadow Pipeline Implementation (VSF-style)

## Scope

이 문서는 VSF 전용 실시간 그림자 세팅 참고 이미지를 기준으로, NativeAnimiq 런타임에 실시간 그림자 패스를 추가하고 Host/WPF/WinUI/Unity Exporter까지 동기화한 변경을 정리한다.  
범위는 기능 구현과 로컬 빌드 검증까지이며, WinUI XAML 컴파일 환경 이슈의 완전 해소는 범위에서 제외한다.

## Implemented Changes

1. Native API 확장 (`include/animiq/nativecore/api.h`)
- `NcLightingOptions` 구조체 추가.
- `nc_set_lighting_options`, `nc_get_lighting_options` C API 추가.

2. Native 렌더러 실시간 그림자 파이프라인 추가 (`src/nativecore/native_core.cpp`)
- 기본/정규화 로직 추가:
  - `MakeDefaultLightingOptions()`
  - `SanitizeLightingOptions(...)`
- 그림자 리소스 추가 및 수명주기 관리:
  - `shadow_texture`, `shadow_dsv`, `shadow_srv`, `shadow_sampler`, `shadow_resolution`
  - `EnsureShadowResources(...)`, `ResetRendererResources(...)` 정리 경로 확장
- 셰이더 상수/리소스 슬롯 확장:
  - `world_matrix`, `light_view_proj`, `lighting_params`, `shadow_params`
  - `Texture2D tex6` + `SamplerComparisonState samp1`
- 픽셀 셰이더에 깊이 비교 기반 샘플링(PCF) 추가, 고정 조명 대신 런타임 조명 방향 사용.
- 렌더 루프에 shadow-map prepass 추가:
  - 라이트 뷰/투영 계산
  - 전용 shadow DSV 렌더
  - 본 패스에서 shadow SRV/sampler 바인딩
- 패스 토큰 해석 확장:
  - `forwardadd`, `forward_add`를 shadow 관련 토큰으로 인식.

3. HostCore 상태/Interop 연동
- `host/HostCore/NativeCoreInterop.cs`
  - `NcLightingOptions`(flattened) 추가
  - lighting set/get DllImport 추가
  - `BuildVsfRealtimeShadowPreset()` 추가
- `host/HostCore/HostUiState.cs`
  - `RenderUiState`에 조명/그림자 옵션 필드 추가:
    - pitch/yaw/roll, intensity, range, spot angle
    - shadow strength/bias, ambient intensity, shadow enabled
- `host/HostCore/RenderPresetStore.cs`
  - 프리셋 모델에 동일 필드 반영
  - 기본 프리셋 `"VSF Realtime Shadow"` 추가
- `host/HostCore/HostController.cs`
  - `_lightingOptions` 상태 추가
  - Render 옵션 적용 시 quality + lighting 동시 set/get roundtrip 반영
  - 프리셋/상태 정규화 및 생성 경로에 lighting 옵션 포함

4. UI 제어 패널 확장 (WPF/WinUI)
- `host/WpfHost/MainWindow.xaml`, `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`, `host/WinUiHost/MainWindow.xaml.cs`
- 렌더 고급 탭에 실시간 그림자 제어 추가:
  - Light Pitch / Yaw
  - Intensity, Range, Spot Angle
  - Shadow Strength, Shadow Bias
  - Ambient Intensity
  - Shadow Enabled
- 상태 동기화/변경 이벤트/Busy 게이팅까지 연결.

5. Tracking 수신 안정화 보강
- `host/HostCore/TrackingInputService.cs`
- UDP 소켓에 `ReceiveTimeout`을 명시 적용하고 `ReceiveAsync` 대기 대신 timeout-aware receive 흐름으로 전환.
- timeout 발생 시 진단 상태를 `receive-timeout`, `receive-rebind`, `receive-rebind-failed`로 분리해 운영자 원인 파악성을 개선.
- 패킷 수신 복구 시 상태를 `ifacial-active:receive-recovered`로 표기해 복구 이벤트 추적 가능하도록 보강.

6. WPF 리소스 동적 테마 참조 일관화
- `host/WpfHost/App.xaml`
- 다수 리소스 참조를 `StaticResource` 중심에서 `DynamicResource` 중심으로 정렬해 런타임 테마 스위치/리소스 갱신 일관성을 개선.
- UI 스타일 로직 변화 없이 리소스 참조 계약을 안정화하는 리팩터링 성격의 변경.

7. Unity Exporter 패스 플래그 개선
- `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`
- material pass를 하드코딩 `"base"` 대신 실제 셰이더 패스에서 추출:
  - `depth` (`DepthOnly`, `DepthForwardOnly`)
  - `shadowcaster` (`ShadowCaster`)
  - `forwardadd` (`ForwardAdd`)
- Native pass 토큰 로직과 연결되어 shadow prepass 대상 식별 정확도 향상.

## Verification Summary

- `cmake --build D:\dbslxlvseefacedkfb\build --config Release --target nativecore`: PASS
- `dotnet build host/HostCore/HostCore.csproj -c Debug --no-restore`: PASS
- `dotnet build host/WpfHost/WpfHost.csproj -c Debug --no-restore`: PASS
- `dotnet build host/WinUiHost/WinUiHost.csproj -c Debug --no-restore`: FAIL
  - `MSB3073` / `XamlCompiler.exe` 종료 코드 1 (환경 의존 WinUI 컴파일러 이슈)
  - `/p:UseXamlCompilerExecutable=false` 시 `WMC9999` (현재 플랫폼에서 미지원) 재현

## Known Risks or Limitations

- WinUI 경로는 코드 반영은 되었지만 현재 환경의 XAML toolchain 이슈로 최종 빌드 합격 증거가 부족하다.
- 그림자 품질(해상도/PCF 샘플 수)은 안정성 우선 기본값으로 설정되어 있어 고사양 프리셋 튜닝 여지가 남아 있다.
- 입력 에셋의 pass 플래그 품질이 낮은 경우(비표준 셰이더) shadow 대상 식별 정확도 저하 가능성이 있다.

## Next Steps

1. WinUI 빌드 환경(Windows SDK + XAML compiler chain) 고정 후 동일 소스 재검증.
2. 대표 아바타 샘플(표준/릴툰/포이요미)로 그림자 품질/성능 매트릭 수집.
3. `VSF Realtime Shadow` 프리셋을 기준으로 강도/바이어스 권장값을 문서화하고 UI 툴팁에 반영.
