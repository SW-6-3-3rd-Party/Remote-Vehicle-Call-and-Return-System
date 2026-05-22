"""
ISO 13400-2 message builders and parsers.
Each build_* function returns raw payload bytes (without DoIP header).
Use header.build_frame(PayloadType.X, build_X(...)) to get a full frame.
"""
import struct
from .constants import (
    PayloadType, RoutingActivationResponseCode, DiagnosticNackCode,
    GenericNackCode, NodeType, PowerMode, LogicalAddress
)
from .header import build_frame

# ---------------------------------------------------------------------------
# Generic NACK  (ISO 13400-2 §9.4 / Table 19)
# ---------------------------------------------------------------------------

def build_generic_nack(nack_code: int) -> bytes:
    return build_frame(PayloadType.GENERIC_NACK, bytes([nack_code]))


# ---------------------------------------------------------------------------
# Vehicle Announcement / Identification Response  (ISO 13400-2 §9.5 / Table 23)
# Payload: VIN(17) + LogicAddr(2) + EID(6) + GID(6) + FurtherAction(1)
# ---------------------------------------------------------------------------

def build_vehicle_announcement(
    vin: str,
    logical_address: int,
    eid: bytes,
    gid: bytes,
    further_action: int = 0x00,
) -> bytes:
    assert len(vin) == 17, "VIN must be 17 characters"
    assert len(eid) == 6, "EID must be 6 bytes"
    assert len(gid) == 6, "GID must be 6 bytes"
    payload = (
        vin.encode("ascii")
        + struct.pack(">H", logical_address)
        + eid
        + gid
        + bytes([further_action])
    )
    return build_frame(PayloadType.VEHICLE_ANNOUNCEMENT, payload)


# ---------------------------------------------------------------------------
# Routing Activation Request  (ISO 13400-2 §9.7 / Table 25)
# Payload: SA(2) + ActivationType(1) + Reserved(4) [+ OEM(4) optional]
# ---------------------------------------------------------------------------

def parse_routing_activation_request(payload: bytes) -> dict:
    if len(payload) < 7:
        raise ValueError("Routing Activation Request too short")
    sa = struct.unpack_from(">H", payload, 0)[0]
    activation_type = payload[2]
    reserved = payload[3:7]
    oem = payload[7:11] if len(payload) >= 11 else None
    return {"source_address": sa, "activation_type": activation_type,
            "reserved": reserved, "oem": oem}


# ---------------------------------------------------------------------------
# Routing Activation Response  (ISO 13400-2 §9.8 / Table 27)
# Payload: ClientSA(2) + ServerLA(2) + ResponseCode(1) + Reserved(4) [+ OEM(4)]
# ---------------------------------------------------------------------------

def build_routing_activation_response(
    client_sa: int,
    server_la: int,
    response_code: int,
    oem: bytes | None = None,
) -> bytes:
    payload = struct.pack(">HHB", client_sa, server_la, response_code) + b"\x00" * 4
    if oem:
        payload += oem[:4]
    return build_frame(PayloadType.ROUTING_ACTIVATION_RESPONSE, payload)


# ---------------------------------------------------------------------------
# Alive Check Request / Response  (ISO 13400-2 §9.9 / Table 28-29)
# ---------------------------------------------------------------------------

def build_alive_check_request() -> bytes:
    return build_frame(PayloadType.ALIVE_CHECK_REQUEST, b"")


def build_alive_check_response(source_address: int) -> bytes:
    return build_frame(PayloadType.ALIVE_CHECK_RESPONSE,
                       struct.pack(">H", source_address))


# ---------------------------------------------------------------------------
# Entity Status Request / Response  (ISO 13400-2 §9.10 / Table 31-32)
# Response: NodeType(1) + MaxOpenSockets(1) + CurrentOpenSockets(1) + MaxDataSize(4)
# ---------------------------------------------------------------------------

def build_entity_status_response(
    node_type: int,
    max_sockets: int,
    current_sockets: int,
    max_data_size: int,
) -> bytes:
    payload = struct.pack(">BBBI", node_type, max_sockets,
                          current_sockets, max_data_size)
    return build_frame(PayloadType.ENTITY_STATUS_RESPONSE, payload)


# ---------------------------------------------------------------------------
# Power Mode Information Response  (ISO 13400-2 §9.11 / Table 33-34)
# ---------------------------------------------------------------------------

def build_power_mode_response(power_mode: int) -> bytes:
    return build_frame(PayloadType.POWER_MODE_INFO_RESPONSE, bytes([power_mode]))


# ---------------------------------------------------------------------------
# Diagnostic Message  (ISO 13400-2 §9.13 / Table 37)
# Payload: SA(2) + TA(2) + UserData(variable)
# ---------------------------------------------------------------------------

def build_diagnostic_message(sa: int, ta: int, user_data: bytes) -> bytes:
    payload = struct.pack(">HH", sa, ta) + user_data
    return build_frame(PayloadType.DIAGNOSTIC_MESSAGE, payload)


def parse_diagnostic_message(payload: bytes) -> dict:
    if len(payload) < 4:
        raise ValueError("Diagnostic message payload too short")
    sa, ta = struct.unpack_from(">HH", payload, 0)
    user_data = payload[4:]
    return {"source_address": sa, "target_address": ta, "user_data": user_data}


# ---------------------------------------------------------------------------
# Diagnostic Message Positive ACK  (ISO 13400-2 §9.14 / Table 39)
# Payload: SA(2) + TA(2) + ACK_Code(1) [+ PreviousMessage optional]
# ---------------------------------------------------------------------------

def build_diagnostic_positive_ack(
    sa: int, ta: int, ack_code: int = 0x00, prev_msg: bytes = b""
) -> bytes:
    payload = struct.pack(">HHB", sa, ta, ack_code) + prev_msg
    return build_frame(PayloadType.DIAGNOSTIC_MESSAGE_POSITIVE_ACK, payload)


# ---------------------------------------------------------------------------
# Diagnostic Message Negative ACK  (ISO 13400-2 §9.15 / Table 41)
# Payload: SA(2) + TA(2) + NACK_Code(1) [+ PreviousMessage optional]
# ---------------------------------------------------------------------------

def build_diagnostic_negative_ack(
    sa: int, ta: int, nack_code: int, prev_msg: bytes = b""
) -> bytes:
    payload = struct.pack(">HHB", sa, ta, nack_code) + prev_msg
    return build_frame(PayloadType.DIAGNOSTIC_MESSAGE_NEGATIVE_ACK, payload)
