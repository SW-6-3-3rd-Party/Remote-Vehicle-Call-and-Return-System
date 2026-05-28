"""
ISO 13400-2 §9.7–9.15 — TCP Diagnostic Session Handler

RPi #2는 자기 자신에 대한 진단 서버로만 동작한다 (TC375 forwarding 없음).

TA=0x0030 (GATEWAY_RPi2) 인 진단 요청만 수락하여 LocalUDSServer로 처리.

Lifecycle per connection:
  1. Routing Activation Request / Response
  2. (Alive Check loop)
  3. Diagnostic Message → LocalUDSServer → Response
"""
import asyncio
import logging
import struct
from .constants import (
    PayloadType, RoutingActivationResponseCode, DiagnosticNackCode,
    GenericNackCode, LogicalAddress, DOIP_HEADER_LEN, DOIP_TCP_PORT,
)
from .header import parse_frame, build_frame
from .messages import (
    build_routing_activation_response,
    build_alive_check_response,
    build_diagnostic_positive_ack,
    build_diagnostic_negative_ack,
    parse_routing_activation_request,
    parse_diagnostic_message,
    build_generic_nack,
)

log = logging.getLogger(__name__)

ALIVE_CHECK_INTERVAL = 30.0
ALIVE_CHECK_TIMEOUT  = 5.0
MAX_PAYLOAD_SIZE     = 0xFFFF


class DoIPTCPSession:
    """Handles one TCP connection from an external tester (PC)."""

    def __init__(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
        config: dict,
        server: "DoIPTCPServer",
        local_uds: "LocalUDSServer",
    ):
        self.reader    = reader
        self.writer    = writer
        self.cfg       = config
        self.server    = server
        self.local_uds = local_uds

        peer = writer.get_extra_info("peername")
        self.peer = f"{peer[0]}:{peer[1]}" if peer else "unknown"
        self.activated  = False
        self.client_sa: int | None = None
        self._alive_task: asyncio.Task | None = None

    # ------------------------------------------------------------------
    # Main receive loop
    # ------------------------------------------------------------------

    async def run(self):
        log.info("TCP session opened: %s", self.peer)
        try:
            self._alive_task = asyncio.create_task(self._alive_check_loop())
            while True:
                hdr_bytes = await self._read_exactly(DOIP_HEADER_LEN)
                if not hdr_bytes:
                    break
                hdr, _ = parse_frame(hdr_bytes + b"\x00" * 4)

                valid, nack = hdr.is_valid()
                if not valid:
                    await self._send(build_generic_nack(nack))
                    continue

                if hdr.payload_length > MAX_PAYLOAD_SIZE:
                    await self._send(build_generic_nack(GenericNackCode.MESSAGE_TOO_LARGE))
                    continue

                payload = await self._read_exactly(hdr.payload_length)
                if payload is None:
                    break

                await self._dispatch(hdr, payload)

        except (asyncio.IncompleteReadError, ConnectionResetError):
            log.info("TCP session closed by peer: %s", self.peer)
        except Exception as e:
            log.exception("TCP session error (%s): %s", self.peer, e)
        finally:
            self._cleanup()

    # ------------------------------------------------------------------
    # Dispatch
    # ------------------------------------------------------------------

    async def _dispatch(self, hdr, payload: bytes):
        pt = hdr.payload_type

        if pt == PayloadType.ROUTING_ACTIVATION_REQUEST:
            await self._handle_routing_activation(payload)

        elif pt == PayloadType.ALIVE_CHECK_RESPONSE:
            pass

        elif pt == PayloadType.DIAGNOSTIC_MESSAGE:
            if not self.activated:
                # Routing activation required before diagnostic messages
                if len(payload) >= 4:
                    sa = struct.unpack_from(">H", payload, 0)[0]
                    ta = struct.unpack_from(">H", payload, 2)[0]
                    await self._send(build_diagnostic_negative_ack(
                        sa, ta, DiagnosticNackCode.OUT_OF_MEMORY, payload[:5]
                    ))
                else:
                    await self._send(build_generic_nack(GenericNackCode.INVALID_PAYLOAD_LENGTH))
            else:
                await self._handle_diagnostic(payload)

        elif pt == PayloadType.ENTITY_STATUS_REQUEST:
            from .messages import build_entity_status_response
            from .constants import NodeType
            frame = build_entity_status_response(
                node_type=NodeType.DOIP_GATEWAY,
                max_sockets=self.cfg.get("max_tcp_sessions", 8),
                current_sockets=len(self.server.sessions),
                max_data_size=MAX_PAYLOAD_SIZE,
            )
            await self._send(frame)

        else:
            log.debug("Unexpected TCP payload type 0x%04X from %s", pt, self.peer)
            await self._send(build_generic_nack(GenericNackCode.UNKNOWN_PAYLOAD_TYPE))

    # ------------------------------------------------------------------
    # Routing Activation
    # ------------------------------------------------------------------

    async def _handle_routing_activation(self, payload: bytes):
        try:
            req = parse_routing_activation_request(payload)
        except ValueError:
            await self._send(build_generic_nack(GenericNackCode.INVALID_PAYLOAD_LENGTH))
            return

        sa        = req["source_address"]
        server_la = self.cfg["logical_address"]

        allowed_sa = self.cfg.get("allowed_source_addresses")
        if allowed_sa and sa not in allowed_sa:
            await self._send(build_routing_activation_response(
                sa, server_la, RoutingActivationResponseCode.DENIED_UNKNOWN_SA
            ))
            log.warning("Routing activation denied (SA=0x%04X) from %s", sa, self.peer)
            return

        for sess in self.server.sessions.values():
            if sess is not self and sess.client_sa == sa and sess.activated:
                await self._send(build_routing_activation_response(
                    sa, server_la, RoutingActivationResponseCode.DENIED_SA_ALREADY_ACTIVE
                ))
                return

        self.client_sa = sa
        self.activated = True
        await self._send(build_routing_activation_response(
            sa, server_la, RoutingActivationResponseCode.SUCCESS
        ))
        log.info("Routing activated: SA=0x%04X  peer=%s", sa, self.peer)

    # ------------------------------------------------------------------
    # Diagnostic Message — handled locally (RPi #2 self-diagnostics only)
    # ------------------------------------------------------------------

    async def _handle_diagnostic(self, payload: bytes):
        try:
            msg = parse_diagnostic_message(payload)
        except ValueError:
            await self._send(build_generic_nack(GenericNackCode.INVALID_PAYLOAD_LENGTH))
            return

        sa   = msg["source_address"]
        ta   = msg["target_address"]
        data = msg["user_data"]

        if sa != self.client_sa:
            await self._send(build_diagnostic_negative_ack(
                sa, ta, DiagnosticNackCode.INVALID_SA, payload[:5]
            ))
            return

        # RPi #2만 진단 대상 — 다른 TA는 거부
        if ta != LogicalAddress.GATEWAY_RPi2:
            await self._send(build_diagnostic_negative_ack(
                ta, sa, DiagnosticNackCode.UNKNOWN_TA, payload[:5]
            ))
            log.warning("Unknown TA=0x%04X from %s — only 0x%04X supported",
                        ta, self.peer, LogicalAddress.GATEWAY_RPi2)
            return

        await self._send(build_diagnostic_positive_ack(ta, sa, 0x00, payload[:5]))
        log.debug("UDS SID=0x%02X  SA=0x%04X→TA=0x%04X  from %s",
                  data[0] if data else 0, sa, ta, self.peer)

        response_uds = self.local_uds.handle(data)

        resp_frame = build_frame(
            PayloadType.DIAGNOSTIC_MESSAGE,
            struct.pack(">HH", ta, sa) + response_uds,
        )
        await self._send(resp_frame)

    # ------------------------------------------------------------------
    # Alive Check
    # ------------------------------------------------------------------

    async def _alive_check_loop(self):
        await asyncio.sleep(ALIVE_CHECK_INTERVAL)
        while True:
            await self._send(build_frame(PayloadType.ALIVE_CHECK_REQUEST, b""))
            await asyncio.sleep(ALIVE_CHECK_TIMEOUT)
            await asyncio.sleep(ALIVE_CHECK_INTERVAL)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    async def _read_exactly(self, n: int) -> bytes | None:
        if n == 0:
            return b""
        try:
            return await self.reader.readexactly(n)
        except asyncio.IncompleteReadError:
            return None

    async def _send(self, frame: bytes):
        try:
            self.writer.write(frame)
            await self.writer.drain()
        except (ConnectionResetError, BrokenPipeError):
            pass

    def _cleanup(self):
        if self._alive_task:
            self._alive_task.cancel()
        self.server.sessions.pop(id(self), None)
        try:
            self.writer.close()
        except Exception:
            pass
        log.info("TCP session cleaned up: %s", self.peer)


# ---------------------------------------------------------------------------
# TCP Server
# ---------------------------------------------------------------------------

class DoIPTCPServer:
    def __init__(self, config: dict, local_uds: "LocalUDSServer"):
        self.cfg       = config
        self.local_uds = local_uds
        self.sessions: dict[int, DoIPTCPSession] = {}
        self._server: asyncio.Server | None = None

    async def start(self):
        self._server = await asyncio.start_server(
            self._on_client, host="0.0.0.0", port=DOIP_TCP_PORT,
        )
        log.info("DoIP TCP server listening on port %d", DOIP_TCP_PORT)

    async def _on_client(self, reader: asyncio.StreamReader,
                         writer: asyncio.StreamWriter):
        if len(self.sessions) >= self.cfg.get("max_tcp_sessions", 8):
            log.warning("Max TCP sessions reached, rejecting connection")
            writer.close()
            return
        session = DoIPTCPSession(reader, writer, self.cfg, self, self.local_uds)
        self.sessions[id(session)] = session
        await session.run()

    async def stop(self):
        if self._server:
            self._server.close()
            await self._server.wait_closed()
