"""
CSI camera recorder (picamera2) — callback 방식

capture_array() polling 대신 picamera2의 pre_callback을 사용.
프레임마다 picamera2 내부 스레드에서 직접 호출되므로 외부 스레드 경쟁 없음.
"""
import logging
import threading
import time
from pathlib import Path

import libcamera
import cv2
from picamera2 import Picamera2

from . import config
from .ring_buffer import RingBuffer

log = logging.getLogger(__name__)

_FrameItem = tuple[float, object]


class CSIRecorder:
    def __init__(self):
        maxlen = config.CSI_FPS * config.PRE_EVENT_SECS
        self.ring: RingBuffer[_FrameItem] = RingBuffer(maxlen)

        self._cam: Picamera2 | None = None

        # live stream
        self._latest_jpeg: bytes = b""
        self._frame_cond = threading.Condition()

        # continuous recording
        self._cont_writer: cv2.VideoWriter | None = None
        self._cont_lock = threading.Lock()
        self._cont_start: float = 0.0

        # event clip
        self._event_writer: cv2.VideoWriter | None = None
        self._event_lock = threading.Lock()

        self._first_frame = True

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    @staticmethod
    def _find_csi_camera_num() -> int | None:
        """USB/UVC 가 아닌 CSI 카메라의 번호를 반환. 없으면 None."""
        try:
            cameras = Picamera2.global_camera_info()
        except Exception:
            return None
        for cam in cameras:
            if "usb" not in cam.get("Id", "").lower():
                return cam.get("Num", 0)
        return None

    def start(self) -> None:
        idx = self._find_csi_camera_num()
        if idx is None:
            raise RuntimeError(
                "CSI 카메라(Pi Camera Module) 없음 — USB 카메라만 감지됨"
            )
        self._cam = Picamera2(idx)
        cam_cfg = self._cam.create_video_configuration(
            main={"size": (config.CSI_WIDTH, config.CSI_HEIGHT), "format": "RGB888"},
            colour_space=libcamera.ColorSpace.Srgb(),
        )
        self._cam.configure(cam_cfg)
        self._cam.pre_callback = self._on_frame

        try:
            self._cont_writer, self._cont_start = self._open_cont_writer()
        except Exception as e:
            log.error("CSI: continuous writer 초기화 실패 (%s) — 파일 저장 없이 계속", e)
            self._cont_writer = None
            self._cont_start = time.time()

        self._cam.start()
        log.info("CSI recorder started")

    def stop(self) -> None:
        if self._cam:
            self._cam.pre_callback = None
            self._cam.stop()
        with self._cont_lock:
            if self._cont_writer:
                self._cont_writer.release()
                self._cont_writer = None
        log.info("CSI recorder stopped")

    # ------------------------------------------------------------------
    # picamera2 callback (runs in picamera2's internal thread per frame)
    # ------------------------------------------------------------------

    def _on_frame(self, request) -> None:
        try:
            frame_rgb = request.make_array("main")
        except Exception as e:
            log.warning("CSI: make_array 실패: %s", e)
            return

        frame_bgr = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2BGR)
        now = time.time()

        if self._first_frame:
            log.info("CSI: 첫 프레임 수신  shape=%s  dtype=%s",
                     frame_bgr.shape, frame_bgr.dtype)
            self._first_frame = False

        # ring buffer
        self.ring.push((now, frame_bgr))

        # live stream JPEG 갱신
        ok, jpeg_buf = cv2.imencode(".jpg", frame_bgr, [cv2.IMWRITE_JPEG_QUALITY, 75])
        if ok:
            with self._frame_cond:
                self._latest_jpeg = jpeg_buf.tobytes()
                self._frame_cond.notify_all()

        # continuous recording
        with self._cont_lock:
            if self._cont_writer is not None:
                self._cont_writer.write(frame_bgr)
                if now - self._cont_start >= config.CONTINUOUS_SEGMENT_SECS:
                    self._cont_writer.release()
                    try:
                        self._cont_writer, self._cont_start = self._open_cont_writer()
                    except Exception as e:
                        log.error("CSI: 세그먼트 로테이션 실패 (%s)", e)
                        self._cont_writer = None

        # event clip post-event 프레임
        with self._event_lock:
            if self._event_writer is not None:
                f = self._ensure_bgr(frame_bgr)
                if f is not None:
                    self._event_writer.write(f)

    # ------------------------------------------------------------------
    # Live stream generator
    # ------------------------------------------------------------------

    def iter_frames(self):
        while True:
            with self._frame_cond:
                self._frame_cond.wait(timeout=2.0)
                jpeg = self._latest_jpeg
            if jpeg:
                yield (
                    b"--frame\r\n"
                    b"Content-Type: image/jpeg\r\n\r\n"
                    + jpeg
                    + b"\r\n"
                )

    # ------------------------------------------------------------------
    # Event clip API
    # ------------------------------------------------------------------

    def start_event_clip(self, event_dir: Path) -> None:
        path = event_dir / "csi_clip.avi"
        writer = self._make_writer(path, config.CSI_FPS,
                                   config.CSI_WIDTH, config.CSI_HEIGHT)
        if not writer.isOpened():
            log.error("CSI VideoWriter 열기 실패: %s", path)
            return

        snapshot = self.ring.snapshot()
        written = 0
        for _, frame in snapshot:
            f = self._ensure_bgr(frame)
            if f is None:
                continue
            writer.write(f)
            written += 1

        with self._event_lock:
            self._event_writer = writer

        log.info("CSI 이벤트 클립 시작  ring=%d  written=%d  path=%s",
                 len(snapshot), written, path)

    def stop_event_clip(self) -> None:
        with self._event_lock:
            if self._event_writer is not None:
                self._event_writer.release()
                self._event_writer = None
        log.info("CSI 이벤트 클립 저장 완료")

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _open_cont_writer(self) -> tuple[cv2.VideoWriter, float]:
        ts = time.strftime("%Y%m%d_%H%M%S")
        path = config.CONTINUOUS_DIR / "csi" / f"{ts}.avi"
        path.parent.mkdir(parents=True, exist_ok=True)
        writer = self._make_writer(path, config.CSI_FPS,
                                   config.CSI_WIDTH, config.CSI_HEIGHT)
        if not writer.isOpened():
            raise RuntimeError(f"VideoWriter 열기 실패: {path}")
        log.debug("CSI 연속 세그먼트: %s", path)
        return writer, time.time()

    @staticmethod
    def _ensure_bgr(frame):
        """프레임을 (H, W, 3) uint8 BGR로 정규화. 불가능하면 None 반환."""
        import numpy as np
        if frame is None:
            return None
        # (1, H, W, C) → (H, W, C)
        if frame.ndim == 4:
            frame = frame[0]
        if frame.ndim != 3:
            log.warning("CSI: 예상 밖 프레임 ndim=%d shape=%s", frame.ndim, frame.shape)
            return None
        h, w, c = frame.shape
        if c == 4:
            frame = frame[:, :, :3]
        elif c != 3:
            log.warning("CSI: 예상 밖 채널 수 c=%d shape=%s", c, frame.shape)
            return None
        if frame.dtype != np.uint8:
            frame = np.clip(frame, 0, 255).astype(np.uint8)
        return frame

    @staticmethod
    def _make_writer(path: Path, fps: int, w: int, h: int) -> cv2.VideoWriter:
        fourcc = cv2.VideoWriter_fourcc(*"MJPG")
        return cv2.VideoWriter(str(path), fourcc, fps, (w, h))
