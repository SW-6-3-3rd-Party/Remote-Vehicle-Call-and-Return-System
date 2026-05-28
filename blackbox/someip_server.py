"""
SOME/IP server — AccidentHistoryService
  Service ID : config.ACCIDENT_SERVICE_ID  /  Instance ID : 0x0001
  Method     : GetAccidentList (0x0001)

blackbox.main 에서 별도 스레드로 실행된다.
"""
import asyncio
import json
import logging
import os
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Tuple

from someipy import (
    TransportLayerProtocol,
    MethodResult,
    ReturnCode,
    MessageType,
    connect_to_someipy_daemon,
    ServerServiceInstance,
    ServiceBuilder,
    Method,
)
from someipy.someipy_logging import set_someipy_log_level

from . import config
from .event_db import EventDB

log = logging.getLogger(__name__)

_DAEMON_SOCKET = Path("/tmp/someipyd.sock")
_daemon_proc: subprocess.Popen | None = None


def _can_connect_daemon(timeout: float = 0.2) -> bool:
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(timeout)
            s.connect(str(_DAEMON_SOCKET))
            return True
    except OSError:
        return False


def _ensure_someipyd_running() -> None:
    global _daemon_proc

    if _can_connect_daemon():
        return

    if _DAEMON_SOCKET.exists():
        try:
            _DAEMON_SOCKET.unlink()
        except OSError:
            pass

    config_path = Path(__file__).resolve().parent.parent / "someipyd.json"
    log_path = config.RECORDINGS_BASE / "someipyd.log"
    config.RECORDINGS_BASE.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(Path(sys.executable).parent / "someipyd"),
        "--config", str(config_path),
        "--log-path", str(log_path),
    ]
    env = os.environ.copy()
    env["SOMEIP_MULTICAST_TTL"] = str(config.SOMEIP_IP_MULTICAST_TTL)
    _daemon_proc = subprocess.Popen(cmd, env=env)

    deadline = time.time() + 3.0
    while time.time() < deadline:
        if _can_connect_daemon():
            log.info("someipyd daemon started  config=%s", config_path)
            return
        if _daemon_proc.poll() is not None:
            raise RuntimeError(f"someipyd exited early with code {_daemon_proc.returncode}")
        time.sleep(0.1)

    raise RuntimeError("someipyd daemon did not become ready")


async def _run(db: EventDB, event_trigger=None) -> None:
    set_someipy_log_level(logging.WARNING)

    async def get_record_list_handler(
        input_data: bytes,
        addr: Tuple[str, int],
    ) -> MethodResult:
        log.info("GetRecordList from %s:%d", addr[0], addr[1])
        try:
            vehicle_id = None
            if input_data:
                vehicle_id = json.loads(
                    input_data.decode(config.PAYLOAD_ENCODING)
                ).get("vehicle_id")

            if vehicle_id is None:
                resp = {"result": "INTERNAL_ERROR", "error_code": 2,
                        "recording_started": False,
                        "accident_count": 0, "accidents": []}
            elif vehicle_id != config.VEHICLE_ID:
                resp = {"result": "EMPTY", "error_code": 1,
                        "recording_started": False,
                        "accident_count": 0, "accidents": []}
            else:
                recording_started = False
                events = db.get_events(limit=50)
                if not events:
                    resp = {"result": "EMPTY", "error_code": 1,
                            "recording_started": recording_started,
                            "accident_count": 0, "accidents": []}
                else:
                    accidents = [
                        {
                            "accident_id":   ev["id"],
                            "accident_time": time.strftime(
                                "%Y-%m-%d %H:%M:%S",
                                time.localtime(ev["triggered_at"])
                            ),
                            "driving_state": 1,
                            "video_url": (
                                f"http://{config.MEDIA_INTERFACE_IP}"
                                f":{config.FLASK_PORT}"
                                f"/events/{ev['event_id']}/video/usb"
                            ),
                        }
                        for ev in events
                    ]
                    resp = {"result": "OK", "error_code": 0,
                            "recording_started": recording_started,
                            "accident_count": len(accidents),
                            "accidents": accidents}

        except Exception:
            log.exception("GetRecordList 처리 오류")
            resp = {"result": "INTERNAL_ERROR", "error_code": 2,
                    "recording_started": False,
                    "accident_count": 0, "accidents": []}

        result = MethodResult()
        result.message_type = MessageType.RESPONSE
        result.return_code  = ReturnCode.E_OK
        result.payload = json.dumps(resp, ensure_ascii=False).encode(
            config.PAYLOAD_ENCODING
        )
        log.info("Response: result=%s  count=%s",
                 resp["result"], resp["accident_count"])
        return result

    _ensure_someipyd_running()
    daemon = await connect_to_someipy_daemon()

    method = Method(
        id=config.GET_RECORD_LIST_METHOD_ID,
        protocol=TransportLayerProtocol.UDP,
        method_handler=get_record_list_handler,
    )
    service = (
        ServiceBuilder()
        .with_service_id(config.ACCIDENT_SERVICE_ID)
        .with_major_version(1)
        .with_minor_version(0)
        .with_method(method)
        .build()
    )
    instance = ServerServiceInstance(
        daemon=daemon,
        service=service,
        instance_id=config.ACCIDENT_INSTANCE_ID,
        endpoint_ip=config.MEDIA_INTERFACE_IP,
        endpoint_port=config.SOMEIP_SERVICE_PORT,
        ttl=config.SOMEIP_SD_OFFER_TTL,
        cyclic_offer_delay_ms=config.SOMEIP_SD_OFFER_INTERVAL_MS,
    )

    log.info(
        "SOME/IP AccidentHistoryService 시작  ServiceID=0x%04X  %s:%d",
        config.ACCIDENT_SERVICE_ID,
        config.MEDIA_INTERFACE_IP,
        config.SOMEIP_SERVICE_PORT,
    )
    await instance.start_offer()

    try:
        await asyncio.Future()
    except asyncio.CancelledError:
        await instance.stop_offer()
    finally:
        await daemon.disconnect_from_daemon()


def start_in_thread(db: EventDB, event_trigger=None) -> None:
    """별도 스레드에서 asyncio 루프를 돌려 SOME/IP 서버를 시작한다."""
    import threading

    def _thread():
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            loop.run_until_complete(_run(db, event_trigger=event_trigger))
        except Exception as e:
            log.error("SOME/IP 서버 오류: %s", e)
        finally:
            loop.close()

    t = threading.Thread(target=_thread, name="someip-server", daemon=True)
    t.start()
    log.info("SOME/IP 서버 스레드 시작")
