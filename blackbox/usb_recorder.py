"""
USB camera recorder (cv2 / V4L2)

Same architecture as CSIRecorder:
  ring buffer + continuous writer + event writer + live stream via iter_frames()
"""
import logging
import os
import threading
import time
from pathlib import Path

import cv2

from . import config
from .ring_buffer import RingBuffer

log = logging.getLogger(__name__)

_FrameItem = tuple[float, object]


class USBRecorder:
    def __init__(self):
        maxlen = config.USB_FPS * config.PRE_EVENT_SECS
        self.ring: RingBuffer[_FrameItem] = RingBuffer(maxlen)

        self._stop_evt = threading.Event()
        self._thread: threading.Thread | None = None
        self._cap: cv2.VideoCapture | None = None

        # live stream
        self._latest_jpeg: bytes = b""
        self._frame_cond = threading.Condition()

        # continuous recording
        self._cont_writer: cv2.VideoWriter | None = None
        self._cont_start: float = 0.0

        # event clip
        self._event_writer: cv2.VideoWriter | None = None
        self._event_lock = threading.Lock()

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self) -> None:
        device_idx = config.USB_DEVICE if config.USB_DEVICE is not None \
                     else self._find_usb_device()
        self._cap = cv2.VideoCapture(device_idx, cv2.CAP_V4L2)
        self._cap.set(cv2.CAP_PROP_FRAME_WIDTH, config.USB_WIDTH)
        self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.USB_HEIGHT)
        self._cap.set(cv2.CAP_PROP_FPS, config.USB_FPS)
        if not self._cap.isOpened():
            raise RuntimeError(f"Cannot open USB camera at /dev/video{device_idx}")
        self._thread = threading.Thread(target=self._loop, name="usb-loop", daemon=True)
        self._thread.start()
        log.info("USB recorder started (device=/dev/video%d)", device_idx)

    def stop(self) -> None:
        self._stop_evt.set()
        if self._thread:
            self._thread.join(timeout=5)
        if self._cap:
            self._cap.release()
        if self._cont_writer:
            self._cont_writer.release()
        log.info("USB recorder stopped")

    # ------------------------------------------------------------------
    # Live stream API
    # ------------------------------------------------------------------

    def wait_frame(self, timeout: float = 2.0) -> bytes:
        """다음 JPEG 프레임이 올 때까지 대기 후 반환. timeout 초 내 프레임 없으면 b'' 반환."""
        with self._frame_cond:
            self._frame_cond.wait(timeout=timeout)
            return self._latest_jpeg

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
    # Main capture loop
    # ------------------------------------------------------------------

    def _reconnect(self) -> bool:
        """카메라 재연결 시도. 성공하면 True 반환."""
        log.warning("USB camera disconnected — attempting reconnect …")
        if self._cap:
            self._cap.release()
            self._cap = None

        for attempt in range(1, 6):
            time.sleep(2.0)
            if self._stop_evt.is_set():
                return False
            try:
                device_idx = config.USB_DEVICE if config.USB_DEVICE is not None \
                             else self._find_usb_device()
                cap = cv2.VideoCapture(device_idx, cv2.CAP_V4L2)
                cap.set(cv2.CAP_PROP_FRAME_WIDTH, config.USB_WIDTH)
                cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.USB_HEIGHT)
                cap.set(cv2.CAP_PROP_FPS, config.USB_FPS)
                ret, _ = cap.read()
                if cap.isOpened() and ret:
                    self._cap = cap
                    log.info("USB camera reconnected (attempt %d)", attempt)
                    return True
                cap.release()
            except Exception as e:
                log.warning("USB reconnect attempt %d failed: %s", attempt, e)

        log.error("USB camera reconnect failed after 5 attempts")
        return False

    def _loop(self) -> None:
        try:
            self._cont_writer, self._cont_start = self._open_cont_writer()
        except Exception as e:
            log.error("USB: continuous writer 초기화 실패 (%s) — 파일 저장 없이 계속", e)
            self._cont_writer = None
            self._cont_start = time.time()

        consecutive_failures = 0

        while not self._stop_evt.is_set():
            try:
                ret, frame_bgr = self._cap.read()
                if not ret:
                    consecutive_failures += 1
                    if consecutive_failures >= 30:  # ~0.3초 연속 실패 → 재연결
                        consecutive_failures = 0
                        if not self._reconnect():
                            break  # 재연결 실패 시 루프 종료
                    else:
                        time.sleep(0.01)
                    continue
                consecutive_failures = 0
                now = time.time()

                self.ring.push((now, frame_bgr))

                ok, jpeg_buf = cv2.imencode(
                    ".jpg", frame_bgr, [cv2.IMWRITE_JPEG_QUALITY, 75]
                )
                if ok:
                    with self._frame_cond:
                        self._latest_jpeg = jpeg_buf.tobytes()
                        self._frame_cond.notify_all()

                if self._cont_writer is not None:
                    self._cont_writer.write(frame_bgr)
                    if now - self._cont_start >= config.CONTINUOUS_SEGMENT_SECS:
                        self._cont_writer.release()
                        try:
                            self._cont_writer, self._cont_start = self._open_cont_writer()
                        except Exception as e:
                            log.error("USB: 세그먼트 로테이션 실패 (%s)", e)
                            self._cont_writer = None

                with self._event_lock:
                    if self._event_writer is not None:
                        self._event_writer.write(frame_bgr)

            except Exception as e:
                log.warning("USB 캡처 오류: %s", e)

        if self._cont_writer is not None:
            self._cont_writer.release()

    # ------------------------------------------------------------------
    # Event clip API
    # ------------------------------------------------------------------

    def start_event_clip(self, event_dir: Path) -> float | None:
        """
        Returns the actual timestamp of the oldest pre-event frame,
        used to calculate audio-video sync offset.
        """
        path = event_dir / "usb_clip.avi"
        writer = self._make_writer(path, config.USB_FPS,
                                   config.USB_WIDTH, config.USB_HEIGHT)
        if not writer.isOpened():
            log.error("USB VideoWriter 열기 실패: %s", path)
            return None

        # ① lock 없이 pre-event 쓰기 → 캡처 루프 차단 없음 (스터터 제거)
        snapshot = self.ring.snapshot()
        # 타임스탬프 기준으로 PRE_EVENT_SECS 이내 프레임만 사용
        cutoff_ts = time.time() - config.PRE_EVENT_SECS
        filtered = [(ts, frame) for ts, frame in snapshot if ts >= cutoff_ts]
        video_start_ts = filtered[0][0] if filtered else None
        for _, frame in filtered:
            writer.write(frame)

        # ② writer 활성화만 lock으로 보호
        with self._event_lock:
            self._event_writer = writer

        log.info("USB 이벤트 클립 시작  pre=%d프레임  path=%s  start_ts=%.3f",
                 len(filtered), path, video_start_ts or 0)
        return video_start_ts

    def stop_event_clip(self) -> None:
        with self._event_lock:
            if self._event_writer is not None:
                self._event_writer.release()
                self._event_writer = None
        log.info("USB event clip saved")

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _find_usb_device() -> int:
        for idx in range(10):
            if not os.path.exists(f"/dev/video{idx}"):
                continue
            # uvcvideo 드라이버(USB 카메라)만 허용 — unicam(CSI) 등 내부 장치 제외
            try:
                driver_link = os.readlink(
                    f"/sys/class/video4linux/video{idx}/device/driver"
                )
                if "uvcvideo" not in driver_link:
                    continue
            except OSError:
                continue
            cap = cv2.VideoCapture(idx, cv2.CAP_V4L2)
            if not cap.isOpened():
                cap.release()
                continue
            ret, _ = cap.read()
            cap.release()
            if ret:
                log.info("USB camera auto-detected at /dev/video%d", idx)
                return idx
        raise RuntimeError("No accessible USB camera found on /dev/video0-9")

    def _open_cont_writer(self) -> tuple[cv2.VideoWriter, float]:
        ts = time.strftime("%Y%m%d_%H%M%S")
        path = config.CONTINUOUS_DIR / "usb" / f"{ts}.avi"
        path.parent.mkdir(parents=True, exist_ok=True)
        writer = self._make_writer(path, config.USB_FPS,
                                   config.USB_WIDTH, config.USB_HEIGHT)
        log.debug("USB continuous segment: %s", path)
        return writer, time.time()

    @staticmethod
    def _make_writer(path: Path, fps: int, w: int, h: int) -> cv2.VideoWriter:
        fourcc = cv2.VideoWriter_fourcc(*"MJPG")
        return cv2.VideoWriter(str(path), fourcc, fps, (w, h))
