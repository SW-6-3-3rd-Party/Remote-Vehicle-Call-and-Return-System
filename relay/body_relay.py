#!/usr/bin/env python3
"""
Body ECU → RPi #2 이벤트 중계 (RPi #1에서 실행)

흐름:
  Body ECU (VLAN 10) --5byte UDP--> RPi #1 (이 스크립트) --5byte UDP--> RPi #2 (VLAN 20)

패킷 포맷 (5 bytes):
  Byte 0     : event_type  (0x01 = ACCIDENT)
  Byte 1~4   : timestamp   (uint32 big-endian, ms 단위)

실행 예:
  python3 body_relay.py
  python3 body_relay.py --listen-port 5200 --rpi2-ip 192.168.20.2 --rpi2-port 5011
"""

import argparse
import logging
import socket
import struct

log = logging.getLogger(__name__)

LISTEN_PORT = 5200         # Body ECU → RPi #1 수신 포트
RPI2_IP     = "192.168.20.2"
RPI2_PORT   = 5011         # RPi #2 수신 포트 (config.ECU_TRIGGER_LISTEN_PORT)


def relay(listen_port: int, rpi2_ip: str, rpi2_port: int) -> None:
    sock_in  = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_in.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock_in.bind(("0.0.0.0", listen_port))
    log.info("Relay started: 0.0.0.0:%d → %s:%d", listen_port, rpi2_ip, rpi2_port)

    while True:
        data, addr = sock_in.recvfrom(16)
        if len(data) < 5:
            log.warning("Too short packet from %s (%d bytes) — ignored", addr, len(data))
            continue

        event_type   = data[0]
        timestamp_ms = struct.unpack(">I", data[1:5])[0]
        log.info("Received from %s: event_type=0x%02X  ts=%d ms",
                 addr, event_type, timestamp_ms)

        sock_out.sendto(data[:5], (rpi2_ip, rpi2_port))
        log.info("Forwarded to %s:%d", rpi2_ip, rpi2_port)


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )
    parser = argparse.ArgumentParser(description="Body ECU → RPi #2 relay")
    parser.add_argument("--listen-port", type=int, default=LISTEN_PORT,
                        help=f"Body ECU로부터 수신할 포트 (기본값: {LISTEN_PORT})")
    parser.add_argument("--rpi2-ip",   default=RPI2_IP,
                        help=f"RPi #2 IP (기본값: {RPI2_IP})")
    parser.add_argument("--rpi2-port", type=int, default=RPI2_PORT,
                        help=f"RPi #2 수신 포트 (기본값: {RPI2_PORT})")
    args = parser.parse_args()
    relay(args.listen_port, args.rpi2_ip, args.rpi2_port)
