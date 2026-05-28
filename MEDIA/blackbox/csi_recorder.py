"""
USB camera 1 recorder — Front / 전방 (cv2 / V4L2)
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


class FrontUSBRecorder:
    def __init__(self):
        maxlen = config.USB1_FPS * config.PRE_EVENT_SECS
        self.ring: RingBuffer[_FrameItem] = RingBuffer(maxlen)

        self._stop_evt = threading.Event()
        self._thread: threading.Thread | None = None
        self._cap: cv2.VideoCapture | None = None
        self._device_idx: int | None = None  # set in start(), reused in reconnect

        self._latest_jpeg: bytes = b""
        self._latest_frame_ts: float = 0.0
        self._frame_cond = threading.Condition()

        self._cont_writer: cv2.VideoWriter | None = None
        self._cont_start: float = 0.0

        self._event_writer: cv2.VideoWriter | None = None
        self._event_lock = threading.Lock()

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self) -> None:
        device_idx = config.USB1_DEVICE if config.USB1_DEVICE is not None \
                     else self._find_usb_device()
        self._device_idx = device_idx
        self._cap = cv2.VideoCapture(device_idx, cv2.CAP_V4L2)
        self._cap.set(cv2.CAP_PROP_FRAME_WIDTH,  config.USB1_WIDTH)
        self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.USB1_HEIGHT)
        self._cap.set(cv2.CAP_PROP_FPS,          config.USB1_FPS)
        if not self._cap.isOpened():
            raise RuntimeError(f"Cannot open front USB camera at /dev/video{device_idx}")
        self._thread = threading.Thread(target=self._loop, name="usb1-loop", daemon=True)
        self._thread.start()
        log.info("Front USB recorder started (device=/dev/video%d)", device_idx)

    def stop(self) -> None:
        self._stop_evt.set()
        if self._thread:
            self._thread.join(timeout=5)
        if self._cap:
            self._cap.release()
        if self._cont_writer:
            self._cont_writer.release()
        log.info("Front USB recorder stopped")

    # ------------------------------------------------------------------
    # Live stream API
    # ------------------------------------------------------------------

    def wait_frame(self, timeout: float = 2.0) -> bytes:
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
        log.warning("Front USB camera disconnected — attempting reconnect …")
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
                cap.set(cv2.CAP_PROP_FRAME_WIDTH,  config.USB1_WIDTH)
                cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.USB1_HEIGHT)
                cap.set(cv2.CAP_PROP_FPS,          config.USB1_FPS)
                ret, _ = cap.read()
                if cap.isOpened() and ret:
                    self._cap = cap
                    log.info("Front USB camera reconnected (attempt %d)", attempt)
                    return True
                cap.release()
            except Exception as e:
                log.warning("Front USB reconnect attempt %d failed: %s", attempt, e)

        log.error("Front USB camera reconnect failed after 5 attempts")
        return False

    def _loop(self) -> None:
        try:
            self._cont_writer, self._cont_start = self._open_cont_writer()
        except Exception as e:
            log.error("USB1: continuous writer 초기화 실패 (%s) — 파일 저장 없이 계속", e)
            self._cont_writer = None
            self._cont_start = time.time()

        consecutive_failures = 0

        while not self._stop_evt.is_set():
            try:
                ret, frame_bgr = self._cap.read()
                if not ret:
                    consecutive_failures += 1
                    if consecutive_failures >= 30:
                        consecutive_failures = 0
                        if not self._reconnect():
                            break
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
                            log.error("USB1: 세그먼트 로테이션 실패 (%s)", e)
                            self._cont_writer = None

                with self._event_lock:
                    if self._event_writer is not None:
                        self._event_writer.write(frame_bgr)

            except Exception as e:
                log.warning("USB1 캡처 오류: %s", e)

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
        path = event_dir / "front_clip.avi"
        writer = self._make_writer(path, config.USB1_FPS,
                                   config.USB1_WIDTH, config.USB1_HEIGHT)
        if not writer.isOpened():
            log.error("USB1 VideoWriter 열기 실패: %s", path)
            return None

        snapshot = self.ring.snapshot()
        cutoff_ts = time.time() - config.PRE_EVENT_SECS
        filtered = [(ts, frame) for ts, frame in snapshot if ts >= cutoff_ts]
        video_start_ts = filtered[0][0] if filtered else None
        for _, frame in filtered:
            writer.write(frame)

        with self._event_lock:
            self._event_writer = writer

        log.info("USB1 이벤트 클립 시작  pre=%d프레임  path=%s  start_ts=%.3f",
                 len(filtered), path, video_start_ts or 0)
        return video_start_ts

    def stop_event_clip(self) -> None:
        with self._event_lock:
            if self._event_writer is not None:
                self._event_writer.release()
                self._event_writer = None
        log.info("USB1 이벤트 클립 저장 완료")

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _find_usb_device() -> int:
        """전방 카메라 장치 번호 반환.

        USB3 포트(/usb2/) 우선 탐색; 없으면 USB2 포트(/usb1/)에서
        두 번째로 감지된 물리 장치(후방 카메라 제외)를 전방으로 사용.
        """
        usb2_devs: dict[str, int] = {}  # phys_port_path -> min_video_idx
        usb1_devs: dict[str, int] = {}

        for idx in range(32):
            if not os.path.exists(f"/dev/video{idx}"):
                continue
            try:
                dev_path = os.path.realpath(f"/sys/class/video4linux/video{idx}/device")
            except OSError:
                continue
            try:
                driver_link = os.readlink(
                    f"/sys/class/video4linux/video{idx}/device/driver"
                )
                if "uvcvideo" not in driver_link:
                    continue
            except OSError:
                continue
            phys = str(Path(dev_path).parent)  # 물리 USB 포트 경로 (e.g. .../1-1.3)
            if "/usb2/" in dev_path:
                usb2_devs[phys] = min(usb2_devs.get(phys, idx), idx)
            elif "/usb1/" in dev_path:
                usb1_devs[phys] = min(usb1_devs.get(phys, idx), idx)

        def _try_open(video_idx: int, label: str) -> bool:
            cap = cv2.VideoCapture(video_idx, cv2.CAP_V4L2)
            if not cap.isOpened():
                cap.release()
                return False
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
                log.info("Front camera auto-detected at /dev/video%d (%s)", video_idx, label)
            return ret

        # USB3 포트 우선
        for video_idx in sorted(usb2_devs.values()):
            if _try_open(video_idx, "USB3 port"):
                return video_idx

        # USB2 두 번째 물리 장치 (인덱스 낮은 것이 후방, 두 번째가 전방)
        usb1_sorted = sorted(usb1_devs.values())
        if len(usb1_sorted) >= 2:
            video_idx = usb1_sorted[1]
            if _try_open(video_idx, "USB2 port, 2nd device"):
                return video_idx

        raise RuntimeError(
            "전방 카메라를 찾을 수 없습니다 — "
            "USB3(파란색) 포트 또는 USB2 두 번째 포트에 연결하세요"
        )

    def _open_cont_writer(self) -> tuple[cv2.VideoWriter, float]:
        ts = time.strftime("%Y%m%d_%H%M%S")
        seg_dir = config.CONTINUOUS_DIR / "front"
        path = seg_dir / f"{ts}.avi"
        seg_dir.mkdir(parents=True, exist_ok=True)
        writer = self._make_writer(path, config.USB1_FPS,
                                   config.USB1_WIDTH, config.USB1_HEIGHT)
        if not writer.isOpened():
            raise RuntimeError(f"VideoWriter 열기 실패: {path}")
        self._purge_old_segments(seg_dir, "*.avi")
        log.debug("USB1 연속 세그먼트: %s", path)
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


# 하위 호환 별칭
CSIRecorder = FrontUSBRecorder
