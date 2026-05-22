"""
System Health Monitor for RPi #2

5초마다 각 컴포넌트를 점검하여 DTC를 set/clear한다.

점검 방법 (사양서 기준):
  eth0      — /sys/class/net/eth0/operstate == "up"
  전방 카메라 — /dev/video0 존재 및 스트림 확인 (USB1)
  후방 카메라 — /dev/video2 존재 여부
  마이크     — /proc/asound/card0 존재 여부
  Flask      — TCP 8080 포트 listen 여부
  SOME/IP    — UDP 30491 포트 listen 여부
  저장공간   — df 사용률 > 90% → DTC
"""
import logging
import shutil
import socket
import threading
from pathlib import Path

import cv2

from .dtc_store import DTCCode, DTCStore

log = logging.getLogger(__name__)

STORAGE_WARN_PCT = 90.0


class SystemStatus:
    def __init__(self):
        self.eth0:   bool  = True
        self.usb1:   bool  = False
        self.usb:    bool  = False
        self.mic:    bool  = False
        self.flask:  bool  = False
        self.someip: bool  = False
        self.storage_used_pct: float = 0.0
        self._lock = threading.Lock()

    def update(self, **kwargs) -> None:
        with self._lock:
            for k, v in kwargs.items():
                setattr(self, k, v)

    def snapshot(self) -> dict:
        with self._lock:
            return {
                "eth0":             self.eth0,
                "usb1":             self.usb1,
                "usb":              self.usb,
                "mic":              self.mic,
                "flask":            self.flask,
                "someip":           self.someip,
                "storage_used_pct": self.storage_used_pct,
            }


class HealthMonitor:
    def __init__(
        self,
        dtc_store: DTCStore,
        recordings_path: Path,
        flask_port: int = 8080,
        someip_port: int = 30491,
        check_interval: float = 10.0,
    ):
        self._dtc         = dtc_store
        self._recordings  = recordings_path
        self._flask_port  = flask_port
        self._someip_port = someip_port
        self._interval    = check_interval
        self._stop_evt    = threading.Event()
        self._thread: threading.Thread | None = None
        self.status = SystemStatus()

    def attach_recorders(self, usb=None, mic=None) -> None:
        pass   # 사양서 기준으로 장치 파일 경로만 확인하므로 recorder 불필요

    def start(self) -> None:
        self._thread = threading.Thread(
            target=self._loop, name="health-monitor", daemon=True
        )
        self._thread.start()
        log.info("HealthMonitor started  interval=%.0fs", self._interval)

    def stop(self) -> None:
        self._stop_evt.set()
        if self._thread:
            self._thread.join(timeout=5)

    def _loop(self) -> None:
        while not self._stop_evt.is_set():
            try:
                self._run_checks()
            except Exception as e:
                log.error("Health check error: %s", e)
            self._stop_evt.wait(timeout=self._interval)

    # ------------------------------------------------------------------

    def _run_checks(self) -> None:
        self._check_eth0()
        self._check_usb1_camera()
        self._check_usb_camera()
        self._check_mic()
        self._check_flask()
        self._check_someip()
        self._check_storage()

    def _check_eth0(self) -> None:
        try:
            state = Path("/sys/class/net/eth0/operstate").read_text().strip()
            ok = (state == "up")
        except Exception:
            ok = False
        self.status.update(eth0=ok)
        self._set_or_clear(ok, DTCCode.ETH0_ERROR)

    def _check_usb1_camera(self) -> None:
        ok = self._test_camera_stream(0) if Path("/dev/video0").exists() else False
        self.status.update(usb1=ok)
        self._set_or_clear(ok, DTCCode.USB1_CAMERA_FAIL)

    def _check_usb_camera(self) -> None:
        exists = Path("/dev/video2").exists()
        can_stream = self._test_camera_stream(2) if exists else False
        ok = exists and can_stream
        self.status.update(usb=ok)
        self._set_or_clear(ok, DTCCode.USB_CAMERA_FAIL)

    def _check_mic(self) -> None:
        # USB 마이크: usbid + pcm0c(캡처)가 모두 있는 카드를 동적 탐색
        ok = self._find_usb_capture_card() is not None
        self.status.update(mic=ok)
        self._set_or_clear(ok, DTCCode.MIC_FAIL)

    @staticmethod
    def _find_usb_capture_card() -> Path | None:
        for card in sorted(Path("/proc/asound").glob("card*")):
            if (card / "usbid").exists() and (card / "pcm0c").exists():
                return card
        return None

    def _check_flask(self) -> None:
        ok = self._tcp_open(self._flask_port)
        self.status.update(flask=ok)
        self._set_or_clear(ok, DTCCode.FLASK_ERROR)

    def _check_someip(self) -> None:
        ok = self._udp_bound(self._someip_port)
        self.status.update(someip=ok)
        self._set_or_clear(ok, DTCCode.SOMEIP_ERROR)

    def _check_storage(self) -> None:
        try:
            u = shutil.disk_usage(str(self._recordings))
            used_pct = u.used / u.total * 100
        except Exception:
            used_pct = 0.0
        self.status.update(storage_used_pct=used_pct)
        self._set_or_clear(used_pct <= STORAGE_WARN_PCT, DTCCode.STORAGE_LOW)

    # ------------------------------------------------------------------

    def _set_or_clear(self, ok: bool, code: int) -> None:
        if ok:
            self._dtc.clear_active(code)
        else:
            self._dtc.set_active(code)

    @staticmethod
    def _test_camera_stream(device_idx: int) -> bool:
        cap = cv2.VideoCapture(device_idx, cv2.CAP_V4L2)
        try:
            if not cap.isOpened():
                return False
            ret, _ = cap.read()
            return ret
        except Exception:
            return False
        finally:
            cap.release()

    @staticmethod
    def _tcp_open(port: int) -> bool:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(0.5)
                return s.connect_ex(("127.0.0.1", port)) == 0
        except Exception:
            return False

    @staticmethod
    def _udp_bound(port: int) -> bool:
        try:
            hex_port = f"{port:04X}"
            with open("/proc/net/udp") as f:
                return any(f":{hex_port}" in line for line in f)
        except Exception:
            return False
