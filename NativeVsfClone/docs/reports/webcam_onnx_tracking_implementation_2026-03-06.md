# Webcam ONNX Tracking Implementation Update (2026-03-06)

## Summary

This update completes the `Webcam (ONNX)` tracking runtime path in HostCore by replacing the placeholder loop with real webcam capture + ONNX inference and surfacing runtime diagnostics to WPF/WinUI status views.

Primary outcomes:

1. `TrackingInputService.WebcamLoopAsync` now performs real frame capture and model inference.
2. Webcam start now validates model path and fixed ONNX schema before entering active state.
3. ARKit 52 blendshape output is mapped into HostCore expression cache and forwarded to native runtime through existing tick flow.
4. Tracking diagnostics now include `capture_fps`, `inference_ms`, schema validity, and last webcam error code.

## Detailed Changes

### 1) HostCore dependencies and tracking diagnostics contract

Updated:

- `host/HostCore/HostCore.csproj`
- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/HostController.cs`

Key changes:

- Added package references to HostCore:
  - `Microsoft.ML.OnnxRuntime (1.20.1)`
  - `OpenCvSharp4 (4.10.0.20240616)`
  - `OpenCvSharp4.runtime.win (4.10.0.20240616)`
- Extended `TrackingDiagnostics` with:
  - `CaptureFps`
  - `InferenceMsAvg`
  - `ModelSchemaOk`
  - `LastErrorCode`
- Updated HostController initial diagnostics seed to match the expanded contract.

### 2) Webcam runtime implementation in TrackingInputService

Updated:

- `host/HostCore/TrackingInputService.cs`

Key changes:

- Added webcam runtime resources:
  - `VideoCapture` instance
  - ONNX `InferenceSession`
  - validated webcam schema cache
- Replaced placeholder webcam loop with real processing:
  - capture frame
  - resize to `256x256`
  - BGR->RGB conversion
  - tensor conversion to `[1,3,256,256]`
  - ONNX inference
  - result mapping to tracking frame + expression cache
  - capped-loop pacing using `InferenceFpsCap`
- Added startup-time webcam runtime initialization/validation:
  - non-empty model path requirement
  - model-file existence check
  - webcam open check
  - fixed ONNX schema check
- Added explicit source-state transitions:
  - `webcam-onnx:starting`
  - `webcam-onnx:initialized`
  - `webcam-onnx:receiving`
  - `webcam-onnx:error`
  - `webcam-onnx:camera-open-failed/model-missing/model-not-found/schema-mismatch/init-failed`
- Added smoothing metrics:
  - rolling capture FPS
  - rolling inference milliseconds

### 3) Fixed ONNX schema contract

Implemented in `TrackingInputService.WebcamOnnxSchema`:

- required input:
  - name: `input`
  - shape: `[1,3,256,256]` (batch dimension allows `-1`)
- required outputs:
  - `blendshape` with shape `[1,52]`
  - `head_pose` with shape `[1,3]`
- optional output:
  - `head_pos` with shape `[1,3]`

Mismatch behavior:

- `StartTracking` fails with `InvalidArgument`.
- diagnostics status/message includes schema mismatch reason.

### 4) ARKit 52 blendshape mapping behavior

Implemented in `TrackingInputService`:

- Introduced canonical 52-key ARKit blendshape order.
- Each frame:
  - updates normalized expression cache keys
  - updates MVP tracking channels:
    - `eyeBlinkLeft` -> `BlinkL`
    - `eyeBlinkRight` -> `BlinkR`
    - `jawOpen` -> `MouthOpen`
  - keeps full expression snapshot for `nc_set_expression_weights(...)` path.

### 5) WPF/WinUI diagnostics text update

Updated:

- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml.cs`

Status/runtime tracking text now includes:

- `capture_fps`
- `infer_ms`
- `schema_ok`
- `err` (last error code)

## Verification

Executed:

```powershell
dotnet build host\HostCore\HostCore.csproj -c Release
dotnet build host\WpfHost\WpfHost.csproj -c Release --no-restore
dotnet build host\WinUiHost\WinUiHost.csproj -c Release --no-restore
```

Result:

- `HostCore`: PASS (`0 warnings`, `0 errors`)
- `WpfHost`: PASS (`0 warnings`, `0 errors`)
- `WinUiHost`: FAIL due existing baseline XAML compiler failure (`XamlCompiler.exe` exit code 1)
  - additional warning observed: ONNX runtime assembly version conflict (`WinAppSDK ML` vs HostCore package version)

## Scope Notes

- This update focuses on HostCore webcam ONNX runtime integration and diagnostics visibility.
- It does not include:
  - WinUI XAML compiler blocker remediation
  - ONNX runtime version conflict resolution in WinUI dependency graph
  - automated HostCore unit test project bootstrap in this commit
