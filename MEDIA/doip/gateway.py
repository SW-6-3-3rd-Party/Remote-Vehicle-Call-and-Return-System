"""
DoIP Gateway for Raspberry Pi #2 — self-diagnostics only

진단 대상: RPi #2 자기 자신 (LA=0x0030)
진단 클라이언트: PC (RPi#1 경유)

구성:
  UDP 13401  — vehicle discovery (DoIPUDPHandler)
  TCP 13401  — diagnostic session (DoIPTCPServer → LocalUDSServer)
  HealthMonitor — 백그라운드 스레드로 컴포넌트 상태 점검
  DTCStore      — SQLite DTC 이력 관리

Usage (standalone):
    python -m doip.gateway

Usage (blackbox.main):
    gw = DoIPGateway(build_config(...))
    gw.attach_recorders(usb1=front_rec, usb=rear_rec, mic=mic_rec)
    gw.start_in_thread()
"""
import asyncio
import contextlib
import logging
import socket
import threading
import uuid
from pathlib import Path

from .constants import LogicalAddress, DOIP_UDP_PORT, DOIP_TCP_PORT
from .dtc_store import DTCStore
from .health_monitor import HealthMonitor
from .uds_server import LocalUDSServer
from .udp_handler import DoIPUDPHandler
from .tcp_handler import DoIPTCPServer

log = logging.getLogger(__name__)


def _get_mac_bytes(iface: str = "eth0") -> bytes:
    try:
        with open(f"/sys/class/net/{iface}/address") as f:
            return bytes(int(b, 16) for b in f.read().strip().split(":"))
    except Exception:
        return uuid.getnode().to_bytes(6, "big")


def build_config(
    vin: str = "TESTVIN0000000001", 
    iface: str = "eth0",
    recordings_path: Path | None = None,
    flask_port: int = 8080,
    someip_port: int = 30491,
    health_interval: float = 5.0,
) -> dict:
    eid = _get_mac_bytes(iface)
    return {
        "vin":              vin,
        "logical_address":        LogicalAddress.GATEWAY_RPi2,
        "allowed_source_addresses": [LogicalAddress.TESTER],
        "eid":              eid,
        "gid":              eid,
        "max_tcp_sessions": 8,
        "max_data_size":    0xFFFF,
        "recordings_path":  recordings_path or Path("/tmp"),
        "flask_port":       flask_port,
        "someip_port":      someip_port,
        "health_interval":  health_interval,
    }


class DoIPGateway:
    def __init__(self, config: dict):
        self.cfg = config

        dtc_db = config["recordings_path"] / "dtc_store.db"
        self.dtc_store = DTCStore(dtc_db)
        self.health    = HealthMonitor(
            dtc_store       = self.dtc_store,
            recordings_path = config["recordings_path"],
            flask_port      = config["flask_port"],
            someip_port     = config["someip_port"],
            check_interval  = config["health_interval"],
        )
        self.local_uds  = LocalUDSServer(self.dtc_store, self.health)
        self.tcp_server = DoIPTCPServer(config, self.local_uds)
        self.udp_handler = DoIPUDPHandler(config, tcp_server=self.tcp_server)
        self._udp_transport = None
        self._announce_task: asyncio.Task | None = None
        self._health_started = False

    def attach_recorders(self, usb1=None, usb=None, mic=None) -> None:
        self.health.attach_recorders(usb1=usb1, usb=usb, mic=mic)

    # ------------------------------------------------------------------

    async def start(self) -> None:
        try:
            await self.tcp_server.start()
            await self._start_udp()

            self.health.start()
            self._health_started = True

            self._announce_task = asyncio.create_task(self.udp_handler.announce_loop())

        except Exception:
            await self.stop()
            raise

        log.info(
            "DoIP Gateway running  LA=0x%04X  VIN=%s  TCP/UDP %d",
            self.cfg["logical_address"], self.cfg["vin"], DOIP_TCP_PORT,
        )

    async def _start_udp(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.bind(("0.0.0.0", DOIP_UDP_PORT))

            loop = asyncio.get_running_loop()
            self._udp_transport, _ = await loop.create_datagram_endpoint(
                lambda: self.udp_handler, sock=sock
            )
        except Exception:
            sock.close()
            raise

    async def stop(self) -> None:
        if self._announce_task:
            self._announce_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._announce_task
            self._announce_task = None

        if self._udp_transport:
            self._udp_transport.close()
            self._udp_transport = None
            self.udp_handler.transport = None

        await self.tcp_server.stop()

        if self._health_started:
            self.health.stop()
            self._health_started = False

        log.info("DoIP Gateway stopped")

    # ------------------------------------------------------------------
    # Thread entry point
    # ------------------------------------------------------------------

    def start_in_thread(self) -> None:
        def _run():
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            try:
                loop.run_until_complete(self._run_forever())
            except Exception as e:
                log.error("DoIP gateway error: %s", e)
            finally:
                loop.close()

        threading.Thread(target=_run, name="doip-gateway", daemon=True).start()
        log.info("DoIP gateway thread started")

    async def _run_forever(self) -> None:
        await self.start()
        try:
            await asyncio.Event().wait()
        except asyncio.CancelledError:
            pass
        finally:
            await self.stop()


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

async def _main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    cfg = build_config(
        vin="TESTVIN0000000001",
        recordings_path=Path(__file__).parent.parent / "recordings",
    )
    gw = DoIPGateway(cfg)
    await gw._run_forever()


if __name__ == "__main__":
    asyncio.run(_main())
