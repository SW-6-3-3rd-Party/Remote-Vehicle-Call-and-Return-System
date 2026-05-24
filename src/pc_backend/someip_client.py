import json
import socket
import threading


SOMEIP_PROTOCOL_VERSION = 0x01
SOMEIP_INTERFACE_VERSION = 0x01
SOMEIP_REQUEST = 0x00
SOMEIP_RESPONSE = 0x80
SOMEIP_E_OK = 0x00
SOMEIP_E_NOT_OK = 0x01

PC_CLIENT_ID = 0x0E00

MAIN_ECU_IP = "192.168.10.2"
MEDIA_PI_IP = "192.168.20.2"

MAIN_SOMEIP_PORT = 30492
MEDIA_SOMEIP_PORT = 30491

COLLISION_WARNING_SERVICE_ID = 0x2000
ACCIDENT_HISTORY_SERVICE_ID = 0x1001
INSTANCE_ID = 0x0001

SET_WARNING_LIGHT_METHOD_ID = 0x0001
GET_WARNING_LIGHT_METHOD_ID = 0x0002
GET_ACCIDENT_LIST_METHOD_ID = 0x0001


class SomeIpError(Exception):
    """Raised when a SOME/IP UDP request cannot be completed."""


class SomeIpClient:
    def __init__(self, client_id=PC_CLIENT_ID, timeout=2.0):
        self.client_id = client_id
        self.timeout = timeout
        self._session_id = 0
        self._lock = threading.Lock()

    def call(self, host, port, service_id, method_id, payload):
        payload_bytes = b"" if payload is None else json.dumps(payload).encode("utf-8")
        session_id = self._next_session_id()
        request = self._build_request(
            service_id=service_id,
            method_id=method_id,
            session_id=session_id,
            payload=payload_bytes,
        )

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(self.timeout)
            sock.sendto(request, (host, port))

            try:
                response, _ = sock.recvfrom(4096)
            except socket.timeout as exc:
                raise SomeIpError(
                    f"SOME/IP response timeout: {host}:{port} "
                    f"svc=0x{service_id:04X} method=0x{method_id:04X}"
                ) from exc

        return self._parse_response(response, service_id, method_id, session_id)

    def _next_session_id(self):
        with self._lock:
            self._session_id = (self._session_id + 1) & 0xFFFF
            if self._session_id == 0:
                self._session_id = 1
            return self._session_id

    def _build_request(self, service_id, method_id, session_id, payload):
        length = 8 + len(payload)
        header = bytearray(16)
        header[0:2] = service_id.to_bytes(2, "big")
        header[2:4] = method_id.to_bytes(2, "big")
        header[4:8] = length.to_bytes(4, "big")
        header[8:10] = self.client_id.to_bytes(2, "big")
        header[10:12] = session_id.to_bytes(2, "big")
        header[12] = SOMEIP_PROTOCOL_VERSION
        header[13] = SOMEIP_INTERFACE_VERSION
        header[14] = SOMEIP_REQUEST
        header[15] = SOMEIP_E_OK
        return bytes(header) + payload

    def _parse_response(self, response, service_id, method_id, session_id):
        if len(response) < 16:
            raise SomeIpError(f"SOME/IP response too short: {len(response)}B")

        rx_service_id = int.from_bytes(response[0:2], "big")
        rx_method_id = int.from_bytes(response[2:4], "big")
        rx_length = int.from_bytes(response[4:8], "big")
        rx_client_id = int.from_bytes(response[8:10], "big")
        rx_session_id = int.from_bytes(response[10:12], "big")
        protocol_version = response[12]
        interface_version = response[13]
        message_type = response[14]
        return_code = response[15]
        payload = response[16:]

        if rx_service_id != service_id or rx_method_id != method_id:
            raise SomeIpError(
                "SOME/IP response id mismatch: "
                f"svc=0x{rx_service_id:04X} method=0x{rx_method_id:04X}"
            )

        if rx_client_id != self.client_id or rx_session_id != session_id:
            raise SomeIpError(
                "SOME/IP response session mismatch: "
                f"client=0x{rx_client_id:04X} session=0x{rx_session_id:04X}"
            )

        if protocol_version != SOMEIP_PROTOCOL_VERSION:
            raise SomeIpError(f"SOME/IP protocol version mismatch: {protocol_version}")

        if interface_version != SOMEIP_INTERFACE_VERSION:
            raise SomeIpError(f"SOME/IP interface version mismatch: {interface_version}")

        if message_type != SOMEIP_RESPONSE:
            raise SomeIpError(f"SOME/IP unexpected message type: 0x{message_type:02X}")

        if return_code != SOMEIP_E_OK:
            raise SomeIpError(f"SOME/IP return code error: 0x{return_code:02X}")

        expected_payload_len = max(0, rx_length - 8)
        if expected_payload_len != len(payload):
            raise SomeIpError(
                "SOME/IP length mismatch: "
                f"length={rx_length} payload={len(payload)}"
            )

        if not payload:
            return {}

        try:
            return json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise SomeIpError(f"SOME/IP payload is not valid JSON: {exc}") from exc


someip_client = SomeIpClient()


def set_warning_light(enable):
    return someip_client.call(
        host=MAIN_ECU_IP,
        port=MAIN_SOMEIP_PORT,
        service_id=COLLISION_WARNING_SERVICE_ID,
        method_id=SET_WARNING_LIGHT_METHOD_ID,
        payload={"enable": 1 if enable else 0},
    )


def get_warning_light():
    return someip_client.call(
        host=MAIN_ECU_IP,
        port=MAIN_SOMEIP_PORT,
        service_id=COLLISION_WARNING_SERVICE_ID,
        method_id=GET_WARNING_LIGHT_METHOD_ID,
        payload=None,
    )


def get_accident_list():
    return someip_client.call(
        host=MEDIA_PI_IP,
        port=MEDIA_SOMEIP_PORT,
        service_id=ACCIDENT_HISTORY_SERVICE_ID,
        method_id=GET_ACCIDENT_LIST_METHOD_ID,
        payload={},
    )
