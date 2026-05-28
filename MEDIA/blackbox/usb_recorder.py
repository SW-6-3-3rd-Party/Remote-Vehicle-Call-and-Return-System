"""
USB camera recorder (cv2 / V4L2)

Same architecture as FrontUSBRecorder:
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
        maxlen = config.USB2_FPS * config.PRE_EVENT_SECS
        self.ring: RingBuffer[_FrameItem] = RingBuffer(maxlen)

        self._stop_evt = threading.Event()
        self._thread: threading.Thread | None = None
        self._cap: cv2.VideoCapture | None = None
        self._device_idx: int | None = None  # set in start(), reused in reconnect

        # live stream
        self._latest_jpeg: bytes = b""
        self._latest_frame_ts: float = 0.0
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
        device_idx = config.USB2_DEVICE if config.USB2_DEVICE is not None \
                     else self._find_usb_device()
        self._device_idx = device_idx
        self._cap = cv2.VideoCapture(device_idx, cv2.CAP_V4L2)
        self._cap.set(cv2.CAP_PROP_FRAME_WIDTH,  config.USB2_WIDTH)
        self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.USB2_HEIGHT)
        self._cap.set(cv2.CAP_PROP_FPS,          config.USB2_FPS)
        if not self._cap.isOpened():
            raise RuntimeError(f"Cannot open rear USB camera at /dev/video{device_idx}")
        self._thread = threading.Thread(target=self._loop, name="usb2-loop", daemon=True)
        self._thread.start()
        log.info("Rear USB recorder started (device=/dev/video%d)", device_idx)

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

    def has_recent_frame(self, max_age: float = 2.0) -> bool:
        with self._frame_cond:
            return bool(self._latest_jpeg) and (time.time() - self._latest_frame_ts) <= max_age

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

        device_idx = self._device_idx  # always reconnect to the same physical device

        for attempt in range(1, 6):
            time.sleep(2.0)
            if self._stop_evt.is_set():
                return False
            try:
                cap = cv2.VideoCapture(device_idx, cv2.CAP_V4L2)
                cap.set(cv2.CAP_PROP_FRAME_WIDTH,  config.USB2_WIDTH)
                cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.USB2_HEIGHT)
                cap.set(cv2.CAP_PROP_FPS,          config.USB2_FPS)
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
                        self._latest_frame_ts = now
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
        writer = self._make_writer(path, config.USB2_FPS,
                                   config.USB2_WIDTH, config.USB2_HEIGHT)
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
        """USB2 포트(Linux Bus 1, /usb1/)에 연결된 UVC 카메라 장치 번호 반환 — 후방 카메라."""
        for idx in range(32):
            if not os.path.exists(f"/dev/video{idx}"):
                continue
            try:
                dev_path = os.path.realpath(f"/sys/class/video4linux/video{idx}/device")
            except OSError:
                continue
            if "/usb1/" not in dev_path:
                continue
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
            cap.set(cv2.CAP_PROP_FRAME_WIDTH,  640)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
            cap.set(cv2.CAP_PROP_FPS,          30)
            ret = False
            for _ in range(5):
                ret, _ = cap.read()
                if ret:
                    break
                time.sleep(0.3)
            cap.release()
            if ret:
                log.info("Rear camera auto-detected at /dev/video%d (USB2 port)", idx)
                return idx
        raise RuntimeError("USB2 포트에서 후방 카메라를 찾을 수 없습니다 — 후방 카메라를 USB2(검은색) 포트에 연결하세요")

    def _open_cont_writer(self) -> tuple[cv2.VideoWriter, float]:
        ts = time.strftime("%Y%m%d_%H%M%S")
        seg_dir = config.CONTINUOUS_DIR / "usb"
        path = seg_dir / f"{ts}.avi"
        seg_dir.mkdir(parents=True, exist_ok=True)
        writer = self._make_writer(path, config.USB2_FPS,
                                   config.USB2_WIDTH, config.USB2_HEIGHT)
        self._purge_old_segments(seg_dir, "*.avi")
        log.debug("USB2 continuous segment: %s", path)
        return writer, time.time()

    @staticmethod
    def _purge_old_segments(directory: Path, pattern: str) -> None:
        files = sorted(directory.glob(pattern))
        for old in files[: max(0, len(files) - config.CONTINUOUS_MAX_SEGMENTS)]:
            try:
                old.unlink()
                log.debug("Purged old segment: %s", old.name)
            except Exception as e:
                log.warning("Failed to purge %s: %s", old.name, e)

    @staticmethod
    def _make_writer(path: Path, fps: int, w: int, h: int) -> cv2.VideoWriter:
        fourcc = cv2.VideoWriter_fourcc(*"MJPG")
        return cv2.VideoWriter(str(path), fourcc, fps, (w, h))
