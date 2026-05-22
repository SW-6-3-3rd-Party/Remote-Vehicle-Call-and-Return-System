"""
Local UDS Server — RPi #2 자체 진단 (LA=0x0030)

지원 서비스:
  0x10  DiagnosticSessionControl   0x01(Default)만 지원
  0x22  ReadDataByIdentifier       0xF190, 0x0300~0x0306
  0x19  ReadDTCInformation         subFunc 0x02(ByStatusMask)
  0x14  ClearDiagnosticInformation 0xFFFFFF(전체 삭제)

DID:
  0xF190  노드 식별 이름     4B   "MED1"
  0x0300  eth0 상태          1B   0=DOWN / 1=UP
  0x0301  전방 카메라 상태   1B   0=미연결 / 1=연결  (/dev/video0)
  0x0302  후방 카메라 상태   1B   0=미연결 / 1=연결  (/dev/video2)
  0x0303  마이크 상태        1B   0=미연결 / 1=연결
  0x0304  Flask 서버 상태    1B   0=중지 / 1=실행중  (TCP 8080)
  0x0305  SOME/IP 서버 상태  1B   0=중지 / 1=실행중  (UDP 30491)
  0x0306  저장공간 사용률    1B   0~100 (%)
"""
import logging
import struct

from .dtc_store import DTCStatusBit, DTC_DESCRIPTIONS, DTCStore
from .health_monitor import HealthMonitor

log = logging.getLogger(__name__)

NODE_ID = b"MED1"


class NRC:
    SERVICE_NOT_SUPPORTED     = 0x11
    SUBFUNCTION_NOT_SUPPORTED = 0x12
    INCORRECT_MSG_LENGTH      = 0x13
    REQUEST_OUT_OF_RANGE      = 0x31


def _nrc(sid: int, code: int) -> bytes:
    return bytes([0x7F, sid, code])


class LocalUDSServer:
    def __init__(self, dtc_store: DTCStore, health: HealthMonitor):
        self._dtc    = dtc_store
        self._health = health

    def handle(self, uds_data: bytes) -> bytes:
        if not uds_data:
            return _nrc(0x00, NRC.INCORRECT_MSG_LENGTH)
        sid = uds_data[0]
        if sid == 0x10:
            return self._dsc(uds_data)
        if sid == 0x22:
            return self._rdbi(uds_data)
        if sid == 0x19:
            return self._rdtc(uds_data)
        if sid == 0x14:
            return self._clear_dtc(uds_data)
        return _nrc(sid, NRC.SERVICE_NOT_SUPPORTED)

    # ------------------------------------------------------------------
    # 0x10 DiagnosticSessionControl
    # ------------------------------------------------------------------

    def _dsc(self, data: bytes) -> bytes:
        if len(data) < 2:
            return _nrc(0x10, NRC.INCORRECT_MSG_LENGTH)
        sub = data[1]
        if sub != 0x01:   # Default Session만 지원
            return _nrc(0x10, NRC.SUBFUNCTION_NOT_SUPPORTED)
        # [0x50][subFunc][P2=25ms(2B)][P2*=500ms×25(2B)]
        return bytes([0x50, 0x01, 0x00, 0x19, 0x01, 0xF4])

    # ------------------------------------------------------------------
    # 0x22 ReadDataByIdentifier
    # ------------------------------------------------------------------

    def _rdbi(self, data: bytes) -> bytes:
        if len(data) < 3:
            return _nrc(0x22, NRC.INCORRECT_MSG_LENGTH)
        did = struct.unpack_from(">H", data, 1)[0]
        s   = self._health.status.snapshot()

        if did == 0xF190:
            val = NODE_ID

        elif did == 0x0300:
            val = bytes([1 if s["eth0"] else 0])

        elif did == 0x0301:
            val = bytes([1 if s["usb1"] else 0])

        elif did == 0x0302:
            val = bytes([1 if s["usb"] else 0])

        elif did == 0x0303:
            val = bytes([1 if s["mic"] else 0])

        elif did == 0x0304:
            val = bytes([1 if s["flask"] else 0])

        elif did == 0x0305:
            val = bytes([1 if s["someip"] else 0])

        elif did == 0x0306:
            val = bytes([min(int(s["storage_used_pct"]), 100)])

        else:
            return _nrc(0x22, NRC.REQUEST_OUT_OF_RANGE)

        return bytes([0x62]) + struct.pack(">H", did) + val

    # ------------------------------------------------------------------
    # 0x19 ReadDTCInformation  (subFunc 0x02 — reportDTCByStatusMask)
    # ------------------------------------------------------------------

    def _rdtc(self, data: bytes) -> bytes:
        if len(data) < 2:
            return _nrc(0x19, NRC.INCORRECT_MSG_LENGTH)
        sub = data[1]

        if sub == 0x02:
            if len(data) < 3:
                return _nrc(0x19, NRC.INCORRECT_MSG_LENGTH)
            mask = data[2]
            dtcs = self._dtc.get_all_dtcs(status_mask=mask)
            # [0x59][subFunc][statusAvailMask=0xFF] + N×[DTC(3B)+status(1B)]
            resp = bytes([0x59, 0x02, 0xFF])
            for d in dtcs:
                resp += d["dtc_code"].to_bytes(3, "big") + bytes([d["status_byte"]])
            return resp

        return _nrc(0x19, NRC.SUBFUNCTION_NOT_SUPPORTED)

    # ------------------------------------------------------------------
    # 0x14 ClearDiagnosticInformation  (0xFFFFFF — 전체 삭제)
    # ------------------------------------------------------------------

    def _clear_dtc(self, data: bytes) -> bytes:
        if len(data) < 4:
            return _nrc(0x14, NRC.INCORRECT_MSG_LENGTH)
        group = int.from_bytes(data[1:4], "big")
        if group != 0xFFFFFF:
            return _nrc(0x14, NRC.REQUEST_OUT_OF_RANGE)
        self._dtc.clear_all()
        log.info("All DTCs cleared via UDS 0x14")
        return bytes([0x54])
