import asyncio
import json
import platform
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


PC_CLIENT_ID = 0x0E00
PC_SOMEIP_CLIENT_PORT = 30500
VEHICLE_ID = 1

MAIN_ECU_IP = "192.168.10.2"
MEDIA_PI_IP = "192.168.20.2"

MAIN_BUZZER_SERVICE_ID = 0x2001
ACCIDENT_HISTORY_SERVICE_ID = 0x1001
INSTANCE_ID = 0x0001

MAIN_BUZZER_CONTROL_METHOD_ID = 0x0001
GET_ACCIDENT_LIST_METHOD_ID = 0x0001

MAIN_SOMEIP_MINOR_VERSION = 0x00000001
MEDIA_SOMEIP_MINOR_VERSION = 0x00000000

SOMEIP_SD_MULTICAST_IP = "224.224.224.245"
SOMEIP_SD_PORT = 30490
SOMEIPYD_TCP_HOST = "127.0.0.1"
SOMEIPYD_TCP_PORT = 30500

_daemon_proc = None
_last_warning_light_state = 0


class SomeIpError(Exception):
    """Raised when a SOME/IP request cannot be completed."""


def _detect_vehicle_interface_ip():
    for target in (MAIN_ECU_IP, MEDIA_PI_IP):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.connect((target, 1))
                return sock.getsockname()[0]
        except OSError:
            continue
    return "0.0.0.0"


def _find_someipyd():
    candidates = [
        shutil.which("someipyd"),
        str(Path(sys.executable).parent / "someipyd"),
        str(Path(sys.executable).parent / "someipyd.exe"),
        str(Path(sys.executable).parent / "Scripts" / "someipyd"),
        str(Path(sys.executable).parent / "Scripts" / "someipyd.exe"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).exists():
            return candidate
    return None


def _write_someipyd_config(interface_ip):
    use_tcp = platform.system() == "Windows"
    config_path = Path(tempfile.gettempdir()) / "pc_someipyd.json"
    config_path.write_text(
        json.dumps(
            {
                "interface": interface_ip,
                "sd_address": SOMEIP_SD_MULTICAST_IP,
                "sd_port": SOMEIP_SD_PORT,
                "use_tcp": use_tcp,
                "tcp_host": SOMEIPYD_TCP_HOST,
                "tcp_port": SOMEIPYD_TCP_PORT,
                "log_level": "INFO",
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    return config_path


def _ensure_someipyd_running():
    global _daemon_proc

    if _daemon_proc is not None and _daemon_proc.poll() is None:
        return

    someipyd = _find_someipyd()
    if someipyd is None:
        raise SomeIpError(
            "someipyd executable not found. Install with "
            "`python -m pip install -r src/pc_backend/requirements.txt`."
        )

    interface_ip = _detect_vehicle_interface_ip()
    config_path = _write_someipyd_config(interface_ip)
    log_path = Path(tempfile.gettempdir()) / "pc_someipyd.log"

    _daemon_proc = subprocess.Popen(
        [someipyd, "--config", str(config_path), "--log-path", str(log_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    time.sleep(0.5)
    if _daemon_proc.poll() is not None:
        stdout, stderr = _daemon_proc.communicate(timeout=1)
        details = []
        if stdout:
            details.append(f"stdout: {stdout.strip()}")
        if stderr:
            details.append(f"stderr: {stderr.strip()}")
        if log_path.exists():
            log_text = log_path.read_text(encoding="utf-8", errors="replace").strip()
            if log_text:
                details.append(f"log: {log_text[-2000:]}")
        suffix = "\n" + "\n".join(details) if details else ""
        raise SomeIpError(
            f"someipyd exited early with code {_daemon_proc.returncode}{suffix}"
        )


def _build_service(service_id, method_id, minor_version=MEDIA_SOMEIP_MINOR_VERSION):
    try:
        from someipy import Method, ServiceBuilder, TransportLayerProtocol
    except ImportError as exc:
        raise SomeIpError(
            "someipy is not installed. Install with "
            "`python -m pip install -r src/pc_backend/requirements.txt`."
        ) from exc

    method = Method(id=method_id, protocol=TransportLayerProtocol.UDP)
    return (
        ServiceBuilder()
        .with_service_id(service_id)
        .with_major_version(1)
        .with_minor_version(minor_version)
        .with_method(method)
        .build()
    )


class SomeIpClient:
    def __init__(self, client_id=PC_CLIENT_ID, timeout=5.0):
        self.client_id = client_id
        self.timeout = timeout

    def call(self, service_id, method_id, payload):
        return self.call_json(service_id, method_id, payload)

    def call_json(self, service_id, method_id, payload, minor_version=MEDIA_SOMEIP_MINOR_VERSION):
        payload_bytes = b"" if payload is None else json.dumps(payload).encode("utf-8")
        response_payload = self.call_raw(
            service_id=service_id,
            method_id=method_id,
            payload_bytes=payload_bytes,
            minor_version=minor_version,
        )

        if not response_payload:
            return {}
        return json.loads(response_payload.decode("utf-8"))

    def call_raw(
        self,
        service_id,
        method_id,
        payload_bytes=b"",
        minor_version=MEDIA_SOMEIP_MINOR_VERSION,
    ):
        if payload_bytes is None:
            payload_bytes = b""
        payload_bytes = bytes(payload_bytes)

        try:
            return asyncio.run(
                self._call_async(service_id, method_id, payload_bytes, minor_version)
            )
        except SomeIpError:
            raise
        except Exception as exc:
            raise SomeIpError(str(exc)) from exc

    async def _call_async(self, service_id, method_id, payload_bytes, minor_version):
        try:
            from someipy import (
                ClientServiceInstance,
                MessageType,
                ReturnCode,
                connect_to_someipy_daemon,
            )
        except ImportError as exc:
            raise SomeIpError(
                "someipy is not installed. Install with "
                "`python -m pip install -r src/pc_backend/requirements.txt`."
            ) from exc

        _ensure_someipyd_running()

        daemon = await connect_to_someipy_daemon(
            {
                "use_tcp": platform.system() == "Windows",
                "tcp_host": SOMEIPYD_TCP_HOST,
                "tcp_port": SOMEIPYD_TCP_PORT,
            }
        )
        try:
            service = _build_service(service_id, method_id, minor_version)
            instance = ClientServiceInstance(
                daemon=daemon,
                service=service,
                instance_id=INSTANCE_ID,
                endpoint_ip=_detect_vehicle_interface_ip(),
                endpoint_port=PC_SOMEIP_CLIENT_PORT,
                client_id=self.client_id,
            )

            await self._wait_until_available(instance, service_id)
            result = await asyncio.wait_for(
                instance.call_method(method_id, payload_bytes),
                timeout=self.timeout,
            )

            if result.message_type != MessageType.RESPONSE:
                raise SomeIpError(f"SOME/IP unexpected message type: {result.message_type}")
            if result.return_code != ReturnCode.E_OK:
                raise SomeIpError(f"SOME/IP return code error: {result.return_code}")

            return bytes(result.payload or b"")
        finally:
            await daemon.disconnect_from_daemon()

    async def _wait_until_available(self, instance, service_id):
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            if await self._is_available(instance):
                return
            await asyncio.sleep(0.1)

        raise SomeIpError(
            f"SOME/IP service 0x{service_id:04X} was not discovered via SD"
        )

    async def _is_available(self, instance):
        if hasattr(instance, "is_available"):
            return bool(await instance.is_available())
        if hasattr(instance, "service_found"):
            value = instance.service_found
            return bool(value() if callable(value) else value)
        return True


someip_client = SomeIpClient()


def set_warning_light(enable):
    global _last_warning_light_state

    state = 1 if enable else 0
    response = someip_client.call_raw(
        service_id=MAIN_BUZZER_SERVICE_ID,
        method_id=MAIN_BUZZER_CONTROL_METHOD_ID,
        payload_bytes=bytes([VEHICLE_ID & 0xFF, state]),
        minor_version=MAIN_SOMEIP_MINOR_VERSION,
    )

    if len(response) < 2:
        raise SomeIpError(
            "MAIN SOME/IP response is too short: "
            f"{response.hex(' ').upper() or '<empty>'}"
        )

    _last_warning_light_state = int(response[1])
    return {
        "result": "OK",
        "vehicle_id": int(response[0]),
        "state": _last_warning_light_state,
        "raw": response.hex(" ").upper(),
    }


def get_warning_light():
    return {
        "result": "OK",
        "state": _last_warning_light_state,
        "source": "pc_cache",
        "message": "MAIN SOME/IP service implements control method only.",
    }


def get_accident_list():
    return someip_client.call_json(
        service_id=ACCIDENT_HISTORY_SERVICE_ID,
        method_id=GET_ACCIDENT_LIST_METHOD_ID,
        payload={"vehicle_id": VEHICLE_ID},
    )
