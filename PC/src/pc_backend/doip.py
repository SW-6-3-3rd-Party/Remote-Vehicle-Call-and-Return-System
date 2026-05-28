from dataclasses import dataclass


TESTER_LOGICAL_ADDRESS = 0x0E00


class DoipError(Exception):
    """Raised when DoIP/UDS communication cannot be completed."""


@dataclass
class RoutingActivationResult:
    source_address: int
    target_address: int
    response_code: int
    reserved: bytes


class DoipClient:
    """Thin project wrapper around python-doipclient + udsoncan.

    The rest of the PC backend uses this wrapper so route/DID definitions stay
    independent from the transport library details.
    """

    def __init__(
        self,
        host,
        port,
        target_address,
        timeout=2.0,
        tester_address=TESTER_LOGICAL_ADDRESS,
        activation_address=None,
    ):
        self.host = host
        self.port = port
        self.target_address = target_address
        self.activation_address = activation_address if activation_address is not None else target_address
        self.timeout = timeout
        self.tester_address = tester_address
        self.doip = None
        self.connector = None
        self.uds = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def connect(self):
        try:
            from doipclient import DoIPClient as LibraryDoIPClient
            from doipclient.connectors import DoIPClientUDSConnector
            from doipclient.messages import RoutingActivationRequest
            from udsoncan.client import Client as UdsClient
        except ImportError as exc:
            raise DoipError(
                "DoIP/UDS 라이브러리가 설치되어 있지 않습니다. "
                "`pip install doipclient udsoncan` 후 다시 실행하세요."
            ) from exc

        self._routing_activation_request = RoutingActivationRequest
        self.doip = LibraryDoIPClient(
            self.host,
            self.activation_address,
            tcp_port=self.port,
            activation_type=None,
            protocol_version=0x02,
            client_logical_address=self.tester_address,
        )
        self.connector = DoIPClientUDSConnector(self.doip, close_connection=False)
        self.uds = UdsClient(
            self.connector,
            request_timeout=self.timeout,
            config={
                "exception_on_negative_response": False,
                "exception_on_invalid_response": False,
                "exception_on_unexpected_response": False,
                "request_timeout": self.timeout,
                "p2_timeout": min(self.timeout, 1.0),
                "p2_star_timeout": max(self.timeout, 5.0),
                "data_identifiers": {},
                "tolerate_zero_padding": True,
                "ignore_all_zero_dtc": True,
                "standard_version": 2013,
            },
        )
        self.uds.open()

    def close(self):
        if self.uds is not None:
            self.uds.close()
            self.uds = None
        if self.doip is not None:
            self.doip.close()
            self.doip = None
        self.connector = None

    def routing_activation(self, activation_type=None):
        if self.doip is None:
            raise DoipError("DoIP client가 연결되어 있지 않습니다.")

        if activation_type is None:
            activation_type = getattr(
                self._routing_activation_request.ActivationType,
                "Default",
                0x00,
            )

        self._set_library_target_address(self.activation_address)
        try:
            response = self.doip.request_activation(activation_type)
        finally:
            self._set_library_target_address(self.target_address)

        return RoutingActivationResult(
            source_address=self._coerce_int(response.client_logical_address),
            target_address=self._coerce_int(response.logical_address),
            response_code=self._coerce_int(response.response_code),
            reserved=self._coerce_reserved(response.reserved),
        )

    def change_session(self, session_type):
        response = self._require_uds().change_session(session_type)
        return self._payload(response)

    def read_data_by_identifier(self, did):
        response = self._require_uds().test_data_identifier([did])
        return self._payload(response)

    def read_dtc_by_status_mask(self, status_mask=0xFF):
        response = self._require_uds().get_dtc_by_status_mask(status_mask)
        return self._payload(response)

    def read_supported_dtcs(self):
        try:
            from udsoncan import Request
            from udsoncan.services import ReadDTCInformation
        except ImportError as exc:
            raise DoipError(
                "UDS 라이브러리가 설치되어 있지 않습니다. "
                "`pip install udsoncan` 후 다시 실행하세요."
            ) from exc

        request = Request(ReadDTCInformation, subfunction=0x0A)
        response = self._require_uds().send_request(request)
        return self._payload(response)

    def clear_dtc(self, group=0xFFFFFF):
        response = self._require_uds().clear_dtc(group)
        return self._payload(response)

    def start_routine(self, routine_id):
        response = self._require_uds().start_routine(routine_id)
        return self._payload(response)

    def _require_uds(self):
        if self.uds is None:
            raise DoipError("UDS client가 연결되어 있지 않습니다.")
        return self.uds

    def _set_library_target_address(self, address):
        if self.doip is not None and hasattr(self.doip, "_ecu_logical_address"):
            self.doip._ecu_logical_address = address

    def _payload(self, response):
        if response is None:
            raise DoipError("UDS 응답이 없습니다.")
        if response.original_payload is not None:
            return bytes(response.original_payload)
        return bytes(response.get_payload())

    @staticmethod
    def _coerce_int(value):
        return int(getattr(value, "value", value))

    @staticmethod
    def _coerce_reserved(value):
        if isinstance(value, bytes):
            return value
        if isinstance(value, bytearray):
            return bytes(value)
        return DoipClient._coerce_int(value).to_bytes(4, "big")


def hex_bytes(data):
    return data.hex(" ").upper()
