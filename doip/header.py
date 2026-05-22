import struct
from dataclasses import dataclass
from .constants import (
    DOIP_PROTOCOL_VERSION, DOIP_PROTOCOL_VERSION_INV,
    DOIP_HEADER_LEN, PayloadType, GenericNackCode
)

# ISO 13400-2 Section 9.2 — Generic DoIP Header
# ┌─────────────────────────────────────────────────────────┐
# │ Proto Ver (1) │ ~Proto Ver (1) │ Payload Type (2)       │
# ├─────────────────────────────────────────────────────────┤
# │              Payload Length (4)                         │
# └─────────────────────────────────────────────────────────┘

_HEADER_STRUCT = struct.Struct(">BBHI")  # version, inv_version, type, length


@dataclass
class DoIPHeader:
    protocol_version: int
    payload_type: int
    payload_length: int

    def is_valid(self) -> tuple[bool, int | None]:
        """Returns (valid, nack_code). nack_code is None when valid."""
        if self.protocol_version != DOIP_PROTOCOL_VERSION:
            return False, GenericNackCode.INCORRECT_PATTERN_FORMAT
        return True, None

    def encode(self) -> bytes:
        return _HEADER_STRUCT.pack(
            self.protocol_version,
            DOIP_PROTOCOL_VERSION_INV,
            self.payload_type,
            self.payload_length,
        )

    @classmethod
    def decode(cls, data: bytes) -> "DoIPHeader":
        if len(data) < DOIP_HEADER_LEN:
            raise ValueError(f"Header too short: {len(data)} < {DOIP_HEADER_LEN}")
        proto_ver, inv_ver, ptype, plen = _HEADER_STRUCT.unpack_from(data)
        hdr = cls(protocol_version=proto_ver, payload_type=ptype, payload_length=plen)
        # ISO 13400-2 §9.2: inv_ver must equal ~proto_ver
        if inv_ver != (~proto_ver & 0xFF):
            raise ValueError("Inverse protocol version mismatch")
        return hdr


def build_frame(payload_type: int, payload: bytes) -> bytes:
    """Assemble a complete DoIP frame (header + payload)."""
    hdr = DoIPHeader(
        protocol_version=DOIP_PROTOCOL_VERSION,
        payload_type=payload_type,
        payload_length=len(payload),
    )
    return hdr.encode() + payload


def parse_frame(data: bytes) -> tuple[DoIPHeader, bytes]:
    """Split raw bytes into (header, payload). Validates header integrity."""
    hdr = DoIPHeader.decode(data)
    payload = data[DOIP_HEADER_LEN: DOIP_HEADER_LEN + hdr.payload_length]
    return hdr, payload
