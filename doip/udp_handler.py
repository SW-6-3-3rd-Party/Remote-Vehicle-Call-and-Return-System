"""
ISO 13400-2 §9.5 — UDP Vehicle Discovery Handler

Listens on UDP 13400 for:
  - Vehicle Identification Request (broadcast)
  - Vehicle Identification Request with EID
  - Vehicle Identification Request with VIN
  - DoIP Entity Status Request
  - Diagnostic Power Mode Information Request

Responds with Vehicle Announcement or the appropriate response frame.
"""
import asyncio
import logging
import socket
from .constants import PayloadType, NodeType, PowerMode, DOIP_UDP_PORT, DOIP_HEADER_LEN
from .header import parse_frame, DoIPHeader
from .messages import (
    build_vehicle_announcement,
    build_entity_status_response,
    build_power_mode_response,
    build_generic_nack,
)
from .constants import GenericNackCode

log = logging.getLogger(__name__)


class DoIPUDPHandler(asyncio.DatagramProtocol):
    def __init__(self, config: dict, tcp_server: "DoIPTCPServer | None" = None):
        self.cfg = config
        self.tcp_server = tcp_server  # reference to query open socket count
        self.transport: asyncio.DatagramTransport | None = None

    # ------------------------------------------------------------------
    # asyncio DatagramProtocol interface
    # ------------------------------------------------------------------

    def connection_made(self, transport: asyncio.DatagramTransport):
        self.transport = transport
        log.info("DoIP UDP handler ready on port %d", DOIP_UDP_PORT)

    def datagram_received(self, data: bytes, addr: tuple[str, int]):
        try:
            hdr, payload = parse_frame(data)
        except ValueError as e:
            log.warning("Malformed UDP frame from %s: %s", addr, e)
            self._send(build_generic_nack(GenericNackCode.INCORRECT_PATTERN_FORMAT), addr)
            return

        valid, nack = hdr.is_valid()
        if not valid:
            self._send(build_generic_nack(nack), addr)
            return

        self._dispatch(hdr, payload, addr)

    def error_received(self, exc: Exception):
        log.error("UDP error: %s", exc)

    # ------------------------------------------------------------------
    # Internal dispatch
    # ------------------------------------------------------------------

    def _dispatch(self, hdr: DoIPHeader, payload: bytes, addr: tuple[str, int]):
        pt = hdr.payload_type

        if pt in (PayloadType.VEHICLE_ID_REQUEST,
                  PayloadType.VEHICLE_ID_REQUEST_WITH_EID,
                  PayloadType.VEHICLE_ID_REQUEST_WITH_VIN):
            self._handle_vehicle_id_request(pt, payload, addr)

        elif pt == PayloadType.ENTITY_STATUS_REQUEST:
            self._handle_entity_status(addr)

        elif pt == PayloadType.POWER_MODE_INFO_REQUEST:
            self._handle_power_mode(addr)

        else:
            log.debug("Unexpected UDP payload type 0x%04X from %s", pt, addr)
            self._send(build_generic_nack(GenericNackCode.UNKNOWN_PAYLOAD_TYPE), addr)

    def _handle_vehicle_id_request(self, ptype: int, payload: bytes,
                                   addr: tuple[str, int]):
        # EID/VIN-specific filtering (optional — respond to all here)
        if ptype == PayloadType.VEHICLE_ID_REQUEST_WITH_EID:
            if len(payload) < 6:
                self._send(build_generic_nack(GenericNackCode.INVALID_PAYLOAD_LENGTH), addr)
                return
            requested_eid = payload[:6]
            if requested_eid != self.cfg["eid"]:
                return  # not for us
        elif ptype == PayloadType.VEHICLE_ID_REQUEST_WITH_VIN:
            if len(payload) < 17:
                self._send(build_generic_nack(GenericNackCode.INVALID_PAYLOAD_LENGTH), addr)
                return
            requested_vin = payload[:17].decode("ascii", errors="replace")
            if requested_vin != self.cfg["vin"]:
                return  # not for us

        frame = build_vehicle_announcement(
            vin=self.cfg["vin"],
            logical_address=self.cfg["logical_address"],
            eid=self.cfg["eid"],
            gid=self.cfg["gid"],
            further_action=0x00,
        )
        log.info("Vehicle Identification Response → %s", addr)
        self._send(frame, addr)

    def _handle_entity_status(self, addr: tuple[str, int]):
        current = len(self.tcp_server.sessions) if self.tcp_server else 0
        frame = build_entity_status_response(
            node_type=NodeType.DOIP_GATEWAY,
            max_sockets=self.cfg.get("max_tcp_sessions", 8),
            current_sockets=current,
            max_data_size=self.cfg.get("max_data_size", 0xFFFF),
        )
        self._send(frame, addr)

    def _handle_power_mode(self, addr: tuple[str, int]):
        frame = build_power_mode_response(PowerMode.READY)
        self._send(frame, addr)

    def _send(self, frame: bytes, addr: tuple[str, int]):
        if self.transport:
            self.transport.sendto(frame, addr)


    # ------------------------------------------------------------------
    # Periodic Vehicle Announcement (ISO 13400-2 §9.5.2)
    # ------------------------------------------------------------------

    async def announce_loop(self, broadcast_addr: str = "255.255.255.255",
                            interval: float = 0.5, count: int = 3):
        """Send unsolicited Vehicle Announcements on startup (ISO 13400-2 §9.5.2)."""
        frame = build_vehicle_announcement(
            vin=self.cfg["vin"],
            logical_address=self.cfg["logical_address"],
            eid=self.cfg["eid"],
            gid=self.cfg["gid"],
            further_action=0x00,
        )
        for _ in range(count):
            if self.transport:
                self.transport.sendto(frame, (broadcast_addr, DOIP_UDP_PORT))
                log.info("Vehicle Announcement broadcast sent")
            await asyncio.sleep(interval)
