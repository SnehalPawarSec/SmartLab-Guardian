"""
SmartLab Guardian - Laptop camera human detection
=================================================
Keep the laptop in the lab area. This script watches the laptop camera.
Person seen    -> Firebase commands/cameraPresent = true  -> ESP32 turns LIGHT/LED + FAN ON (AUTO mode)
No person      -> cameraPresent = false -> ESP32 turns them OFF after the empty-lab timer

The laptop and ESP32 never talk directly. Both use the same Firebase database over Wi-Fi,
so the dashboard sees the camera status too (sensors/camera).

Keys in the video window:  A = AUTO (camera controls)   O = force OFF   N = force ON   Q = quit

Run:   pip install -r requirements.txt
       python camera_presence.py              (with preview window, press Q to quit)
       python camera_presence.py --no-window  (background)
       python camera_presence.py --camera 1   (use a USB webcam instead of the built-in one)

Detection: YuNet deep-learning face detector (OpenCV DNN). Works on faces looking roughly toward
the camera, up to ~5 m away. Each box shows its confidence score.
"""
import argparse
import os
import time

import cv2
import requests

FIREBASE_URL = "https://smartlab-guardian-1f880-default-rtdb.asia-southeast1.firebasedatabase.app"
SEND_EVERY_SEC = 2.0      # heartbeat to Firebase (ESP32 treats the camera as offline after 15 s)
HOLD_SEC = 3.0            # keep "present" this long after the last detection (stops flicker)
CONFIRM_FRAMES = 3        # a person must be seen in this many frames in a row (ignores one-frame false alarms)
YUNET_MODEL = "face_detection_yunet_2023mar.onnx"   # OpenCV Zoo face model (232 KB, shipped in this folder)
MIN_SCORE = 0.85          # face confidence 0-1. Raise to 0.9 if something that is not a person still gets a box
MIN_FACE_PX = 24          # ignore tiny "faces" (in the 640-px-wide working image)


class Detector:
    """YuNet deep-learning face detector (OpenCV DNN, model file next to this script).
    Far fewer false alarms than the old Haar/HOG detectors. Falls back to Haar if the model is missing."""

    def __init__(self):
        model = os.path.join(os.path.dirname(os.path.abspath(__file__)), YUNET_MODEL)
        self.yunet = None
        if os.path.exists(model) and hasattr(cv2, "FaceDetectorYN"):
            self.yunet = cv2.FaceDetectorYN.create(model, "", (320, 320), MIN_SCORE, 0.3, 50)
            print(f"[detector] YuNet deep-learning face detector (min score {MIN_SCORE})")
        else:
            self.face = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
            print(f"[detector] {YUNET_MODEL} not found -> basic Haar face detector (less accurate)")

    def people(self, frame):
        """Returns a list of (x, y, w, h, score) boxes, one per face."""
        k = frame.shape[1] / 640
        small = cv2.resize(frame, (640, int(frame.shape[0] / k)))
        if self.yunet is not None:
            self.yunet.setInputSize((small.shape[1], small.shape[0]))
            _, faces = self.yunet.detect(small)
            if faces is None:
                return []
            return [(int(f[0] * k), int(f[1] * k), int(f[2] * k), int(f[3] * k), float(f[14]))
                    for f in faces if f[2] >= MIN_FACE_PX and f[3] >= MIN_FACE_PX]
        gray = cv2.equalizeHist(cv2.cvtColor(small, cv2.COLOR_BGR2GRAY))
        return [(int(x * k), int(y * k), int(w * k), int(h * k), 1.0)
                for (x, y, w, h) in self.face.detectMultiScale(gray, 1.1, 8, minSize=(50, 50))]


def send(session, present, count, online=True):
    body = {
        "commands/cameraPresent": present,
        "commands/cameraPeople": count,
        "commands/cameraLastSeen": {".sv": "timestamp"},
        "sensors/camera": ("PERSON" if present else "EMPTY") if online else "OFFLINE",
        "sensors/cameraPeople": count,
    }
    try:
        r = session.patch(f"{FIREBASE_URL}/smartlab.json", json=body, timeout=5)
        return r.ok
    except requests.RequestException:
        return False


def set_mode(session, mode, on=False):
    """Keyboard control from the camera window: A = AUTO, O = force OFF, N = force ON."""
    body = {"mode": "AUTO"} if mode == "AUTO" else {"mode": "MANUAL", "light": on, "fan": on}
    try:
        session.patch(f"{FIREBASE_URL}/smartlab/commands.json", json=body, timeout=5)
        print(time.strftime("%H:%M:%S"), "MODE ->", "AUTO (camera controls)" if mode == "AUTO" else f"MANUAL {'ON' if on else 'OFF'}")
    except requests.RequestException:
        print("  [Firebase send FAILED - check internet]")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--camera", type=int, default=0)
    ap.add_argument("--no-window", action="store_true")
    args = ap.parse_args()

    cam = cv2.VideoCapture(args.camera, cv2.CAP_DSHOW)
    if not cam.isOpened():
        cam = cv2.VideoCapture(args.camera)
    if not cam.isOpened():
        raise SystemExit(f"Camera {args.camera} not found / busy (close Zoom, Teams, Camera app)")

    det = Detector()
    s = requests.Session()
    last_seen = 0.0
    last_send = 0.0
    last_state = None
    count = 0
    hits = 0
    mode_label = "AUTO"
    set_mode(s, "AUTO")          # start in AUTO so the camera controls the light + fan
    print("Running. In the video window: A = AUTO, O = force OFF, N = force ON, Q = quit.")

    try:
        while True:
            ok, frame = cam.read()
            if not ok:
                time.sleep(0.2)
                continue
            boxes = det.people(frame)
            now = time.time()
            hits = hits + 1 if boxes else 0
            if hits >= CONFIRM_FRAMES:
                last_seen = now
                count = len(boxes)
            present = now - last_seen < HOLD_SEC
            if not present:
                count = 0

            if present != last_state or now - last_send >= SEND_EVERY_SEC:
                sent = send(s, present, count)
                if present != last_state:
                    print(time.strftime("%H:%M:%S"), "PERSON" if present else "EMPTY",
                          f"({count})", "" if sent else "  [Firebase send FAILED - check internet]")
                last_state, last_send = present, now

            if not args.no_window:
                for (x, y, w, h, score) in boxes:
                    cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 0), 2)
                    cv2.putText(frame, f"person {score:.2f}", (x, max(15, y - 6)),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 2)
                if mode_label == "AUTO":
                    label = f"PERSON x{count} -> LIGHT ON" if present else "EMPTY -> LIGHT OFF (after timer)"
                else:
                    label = f"{'PERSON' if present else 'EMPTY'} (camera ignored: {mode_label})"
                cv2.putText(frame, label, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8,
                            (0, 255, 0) if present else (0, 0, 255), 2)
                cv2.putText(frame, f"Mode: {mode_label}   keys: A=Auto  O=Off  N=On  Q=Quit", (10, frame.shape[0] - 12),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2)
                cv2.imshow("SmartLab Guardian - Camera", frame)
                key = cv2.waitKey(1) & 0xFF
                if key in (ord("q"), ord("Q")):
                    break
                if key in (ord("a"), ord("A")):
                    set_mode(s, "AUTO"); mode_label = "AUTO"
                elif key in (ord("o"), ord("O")):
                    set_mode(s, "MANUAL", False); mode_label = "MANUAL OFF"
                elif key in (ord("n"), ord("N")):
                    set_mode(s, "MANUAL", True); mode_label = "MANUAL ON"
    except KeyboardInterrupt:
        pass
    finally:
        send(s, False, 0, online=False)   # tell ESP32 + dashboard the camera stopped
        cam.release()
        cv2.destroyAllWindows()
        print("Stopped.")


if __name__ == "__main__":
    main()
