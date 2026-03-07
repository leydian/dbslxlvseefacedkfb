# Animiq User Guide (v1.0 RC)

Welcome to Animiq, the interactive avatar host! This guide helps you get started with loading avatars and webcam-based tracking.

## 1. Quick Start

### Loading an Avatar
1. Launch `WpfHost.exe` from the `dist/wpf` folder.
2. Click the **"Import Avatar"** button in the UI.
3. Select a `.vrm`, `.miq`, or `.vsfavatar` file.
4. The avatar should appear in the render window.

### Starting Webcam Tracking
1. Go to the **Tracking** tab in the host.
2. Select **"MediaPipe Webcam"** as the tracking source.
3. Click **"Start Tracking"**.
4. (Optional) Adjust the **Inference FPS** target to balance performance and latency.

## 2. Advanced Features

### Explicit Format Overrides
If an avatar fails to load due to an incorrect extension, you can force a specific format:
- Set environment variable: `$env:VSF_PARSER_MODE = "sidecar"`
- Use `animiq_cli.exe <path> --format=vrm` for diagnostics.

### Sidecar Performance
For large `.vsfavatar` models, the system automatically extends the loading timeout based on file size (1MB -> +500ms).

## 3. Troubleshooting

### Avatar is "Orbiting" or Displaced
This usually happens due to coordinate system conflicts. Ensure the model's node transforms are applied correctly in your modeling software (e.g., Blender) before exporting to VRM.

### WinUI Build Fails
If you are developing and WinUI fails to build, refer to [WINUI_BUILD_SETUP.md](./WINUI_BUILD_SETUP.md) to install the required Windows SDK.

---
© 2026 Animiq Core Team
