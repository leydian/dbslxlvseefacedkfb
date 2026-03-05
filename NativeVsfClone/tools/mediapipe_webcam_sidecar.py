#!/usr/bin/env python3
import argparse
import json
import math
import sys
import time

ARKIT_52 = [
    "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight",
    "cheekPuff", "cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight",
    "eyeLookDownLeft", "eyeLookDownRight", "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft",
    "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight", "eyeSquintLeft", "eyeSquintRight",
    "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft", "jawOpen",
    "jawRight", "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft",
    "mouthFrownRight", "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight",
    "mouthPressLeft", "mouthPressRight", "mouthPucker", "mouthRight", "mouthRollLower",
    "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper", "mouthSmileLeft", "mouthSmileRight",
    "mouthStretchLeft", "mouthStretchRight", "mouthUpperUpLeft", "mouthUpperUpRight", "noseSneerLeft",
    "noseSneerRight", "tongueOut",
]


def _safe_ratio(a, b):
    if abs(b) < 1e-6:
        return 0.0
    return a / b


def _distance(p1, p2):
    dx = p1.x - p2.x
    dy = p1.y - p2.y
    return math.sqrt((dx * dx) + (dy * dy))


def _clamp01(value):
    if value < 0.0:
        return 0.0
    if value > 1.0:
        return 1.0
    return value


def _blank_blendshape_payload():
    payload = {}
    for key in ARKIT_52:
        payload[key] = 0.0
    return payload


def main():
    parser = argparse.ArgumentParser(description="VsfClone MediaPipe webcam sidecar")
    parser.add_argument("--camera", default="0", help="camera index or path")
    parser.add_argument("--fps", type=int, default=30, help="target output fps")
    args = parser.parse_args()

    try:
        import cv2  # type: ignore
        import mediapipe as mp  # type: ignore
    except Exception as exc:  # pragma: no cover - runtime dependency check
        print(f"dependency import failed: {exc}", file=sys.stderr, flush=True)
        return 2

    cap = None
    try:
        if args.camera.isdigit():
            cap = cv2.VideoCapture(int(args.camera))
        else:
            cap = cv2.VideoCapture(args.camera)
        if cap is None or not cap.isOpened():
            print("camera open failed", file=sys.stderr, flush=True)
            return 3

        target_fps = max(5, min(args.fps, 120))
        frame_budget = 1.0 / float(target_fps)
        frame_id = 0
        last_emit = time.perf_counter()
        smoothed_capture_fps = 0.0

        face_mesh = mp.solutions.face_mesh.FaceMesh(
            static_image_mode=False,
            max_num_faces=1,
            refine_landmarks=True,
            min_detection_confidence=0.5,
            min_tracking_confidence=0.5,
        )

        while True:
            cycle_start = time.perf_counter()
            ok, frame = cap.read()
            if not ok or frame is None:
                print("camera frame read failed", file=sys.stderr, flush=True)
                time.sleep(0.02)
                continue

            capture_now = time.perf_counter()
            elapsed_capture = capture_now - last_emit
            if elapsed_capture > 1e-6:
                instant = 1.0 / elapsed_capture
                if smoothed_capture_fps <= 0.001:
                    smoothed_capture_fps = instant
                else:
                    smoothed_capture_fps = (smoothed_capture_fps * 0.7) + (instant * 0.3)
            last_emit = capture_now

            infer_start = time.perf_counter()
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            result = face_mesh.process(rgb)
            infer_ms = (time.perf_counter() - infer_start) * 1000.0

            if not result.multi_face_landmarks:
                payload = {
                    "frame_id": frame_id + 1,
                    "yaw_deg": 0.0,
                    "pitch_deg": 0.0,
                    "roll_deg": 0.0,
                    "head_pos_x": 0.0,
                    "head_pos_y": 0.0,
                    "head_pos_z": 0.0,
                    "blink_l": 0.0,
                    "blink_r": 0.0,
                    "mouth_open": 0.0,
                    "smile": 0.0,
                    "capture_fps": smoothed_capture_fps,
                    "inference_ms": infer_ms,
                    "confidence": 0.10,
                    "blendshapes": _blank_blendshape_payload(),
                }
            else:
                lm = result.multi_face_landmarks[0].landmark

                left_eye = _distance(lm[159], lm[145]) / max(1e-6, _distance(lm[33], lm[133]))
                right_eye = _distance(lm[386], lm[374]) / max(1e-6, _distance(lm[362], lm[263]))
                mouth_open = _distance(lm[13], lm[14]) / max(1e-6, _distance(lm[78], lm[308]))
                smile = (_distance(lm[61], lm[291]) - _distance(lm[78], lm[308])) * 6.0

                nose = lm[1]
                left_cheek = lm[234]
                right_cheek = lm[454]
                chin = lm[152]
                forehead = lm[10]

                yaw = (nose.x - ((left_cheek.x + right_cheek.x) * 0.5)) * 90.0
                pitch = (((forehead.y + chin.y) * 0.5) - nose.y) * 120.0
                roll = math.degrees(math.atan2(right_cheek.y - left_cheek.y, right_cheek.x - left_cheek.x))

                head_pos_x = (nose.x - 0.5) * 0.25
                head_pos_y = (0.5 - nose.y) * 0.25
                head_pos_z = -((right_cheek.x - left_cheek.x) - 0.35) * 0.45

                blink_l = _clamp01((0.30 - left_eye) * 5.0)
                blink_r = _clamp01((0.30 - right_eye) * 5.0)
                mouth_open_value = _clamp01((mouth_open - 0.02) * 8.0)
                smile_value = _clamp01(smile)

                blend = _blank_blendshape_payload()
                blend["eyeBlinkLeft"] = blink_l
                blend["eyeBlinkRight"] = blink_r
                blend["jawOpen"] = mouth_open_value
                blend["mouthSmileLeft"] = smile_value
                blend["mouthSmileRight"] = smile_value
                blend["eyeSquintLeft"] = _clamp01((0.22 - left_eye) * 4.0)
                blend["eyeSquintRight"] = _clamp01((0.22 - right_eye) * 4.0)
                blend["mouthFunnel"] = _clamp01(mouth_open_value * 0.65)
                blend["mouthPucker"] = _clamp01(mouth_open_value * 0.45)
                blend["browInnerUp"] = _clamp01((0.42 - lm[9].y) * 2.2)
                confidence = _clamp01(0.55 + (0.35 * (1.0 - abs(yaw) / 60.0)) + (0.10 * (1.0 - infer_ms / 50.0)))

                payload = {
                    "frame_id": frame_id + 1,
                    "yaw_deg": yaw,
                    "pitch_deg": pitch,
                    "roll_deg": roll,
                    "head_pos_x": head_pos_x,
                    "head_pos_y": head_pos_y,
                    "head_pos_z": head_pos_z,
                    "blink_l": blink_l,
                    "blink_r": blink_r,
                    "mouth_open": mouth_open_value,
                    "smile": smile_value,
                    "capture_fps": smoothed_capture_fps,
                    "inference_ms": infer_ms,
                    "confidence": confidence,
                    "blendshapes": blend,
                }

            frame_id += 1
            print(json.dumps(payload, ensure_ascii=True), flush=True)

            elapsed = time.perf_counter() - cycle_start
            remaining = frame_budget - elapsed
            if remaining > 0:
                time.sleep(remaining)
    except KeyboardInterrupt:
        return 0
    finally:
        if cap is not None:
            cap.release()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
