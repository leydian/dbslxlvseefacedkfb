# Webcam Tracking Setup (MediaPipe)

This guide helps you set up and troubleshoot webcam-based tracking using the MediaPipe sidecar.

## Prerequisites

- Windows 10/11
- A functional webcam
- Python 3.10 or 3.11 (Recommended)

## Installation Steps

1. **Install Python**: Download and install Python 3.11 from [python.org](https://www.python.org/). Ensure "Add Python to PATH" is checked.
2. **Setup Virtual Environment**:
   Run the following command in the project root:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\tools\setup_tracking_python_venv.ps1
   ```
3. **Configure Python Path**:
   Set the `ANIMIQ_MEDIAPIPE_PYTHON` environment variable to point to your virtual environment's Python executable:
   ```powershell
   [Environment]::SetEnvironmentVariable("ANIMIQ_MEDIAPIPE_PYTHON", "$PWD\.venv\Scripts\python.exe", "User")
   ```

## Troubleshooting

### Error: `TRACKING_MEDIAPIPE_START_FAILED`
- **Cause**: The host cannot find or execute the Python interpreter.
- **Fix**: Verify `ANIMIQ_MEDIAPIPE_PYTHON` points to a valid `python.exe`. Run `tools/mediapipe_sidecar_sanity.ps1` to validate.

### Error: `TRACKING_MEDIAPIPE_NO_FRAME`
- **Cause**: Python started, but the camera is not emitting frames or is blocked by another app.
- **Fix**: Ensure your webcam is not in use by other applications (Zoom, Discord, etc.). Try running `tools/mediapipe_webcam_sidecar.py` manually to see if it shows errors.

### Error: `code=2` on Exit
- **Cause**: Missing dependencies in the virtual environment.
- **Fix**: Re-run the setup script and ensure `pip install` succeeds for `mediapipe` and `opencv-python`.
