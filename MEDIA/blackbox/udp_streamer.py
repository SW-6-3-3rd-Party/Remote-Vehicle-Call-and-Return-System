"""
UDP Real-time Video Stream Server

프로토콜
--------
PC → RPi (UDP_STREAM_PORT):  아무 패킷이나 보내면 스트리밍 시작/keepalive
RPi → PC (발신자 주소):      [4B big-endian uint32 크기] + [JPEG 데이터]

UDP_STREAM_TIMEOUT 초 동안 PC에서 패킷이 없으면 자동 중단.
"""
import logging
import socket
import struct
import threading
import time

from . import config
from .usb_recorder import USBRecorder

log = logging.getLogger(__name__)

_MAX_UDP_PAYLOAD = 65000  # UDP 안전 상한 (65507 - 헤더 여유)


class UDPStreamServer:
    def __init__(self, usb: USBRecorder | None) -> None:
        self._usb = usb
        self._sock: socket.socket | None = None
        self._client_addr: tuple[str, int] | None = None
        self._last_ping: float = 0.0
        self._lock = threading.Lock()

    def start(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind(("0.0.0.0", config.UDP_STREAM_PORT))

        threading.Thread(target=self._recv_loop, name="udp-recv", daemon=True).start()
        threading.Thread(target=self._send_loop, name="udp-send", daemon=True).start()

        log.info("UDP stream server  @ 0.0.0.0:%d", config.UDP_STREAM_PORT)

    # ------------------------------------------------------------------

    def _recv_loop(self) -> None:
        """PC에서 오는 요청/keepalive 수신."""
        while True:
            try:
                _, addr = self._sock.recvfrom(64)
                with self._lock:
                    if self._client_addr != addr:
                        log.info("UDP stream: client %s", addr)
                    self._client_addr = addr
                    self._last_ping = time.time()
            except Exception as e:
                log.error("UDP recv: %s", e)

    def _send_loop(self) -> None:
        """최신 JPEG 프레임을 PC로 전송."""
        if self._usb is None:
            log.warning("USB recorder 없음 — UDP 스트리밍 비활성")
            return

        while True:
            jpeg = self._usb.wait_frame(timeout=1.0)
            if not jpeg:
                continue

            with self._lock:
                addr = self._client_addr
                timed_out = (time.time() - self._last_ping) > config.UDP_STREAM_TIMEOUT

            if addr is None or timed_out:
                time.sleep(0.05)
                continue

            if len(jpeg) > _MAX_UDP_PAYLOAD:
                log.debug("frame too large (%d B), skip", len(jpeg))
                continue

            try:
                self._sock.sendto(struct.pack(">I", len(jpeg)) + jpeg, addr)
            except OSError as e:
                log.warning("UDP send: %s", e)
