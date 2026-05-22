"""
DoIP 진단 테스트 클라이언트 — RPi #2 (LA=0x0030)

RPi #2 자체에서 실행:  python -m doip.test_client
PC에서 실행:           python -m doip.test_client --host 192.168.20.2

옵션:
  --host  대상 IP (기본: 127.0.0.1)
  --clear DTC 전체 삭제 후 종료
"""
import argparse
import socket
import struct
import sys
import time

# ── DoIP 상수 ─────────────────────────────────────────────────────────────
DOIP_VERSION   = 0x02
HEADER_LEN     = 8
TCP_PORT       = 13401

ROUTING_ACT_REQ = 0x0005
ROUTING_ACT_RES = 0x0006
DIAG_MSG        = 0x8001
DIAG_ACK        = 0x8002

SA = 0x0E00   # PC 진단툴  (LogicalAddress.TESTER)
TA = 0x0030   # RPi #2

# ── DID / DTC 이름표 ───────────────────────────────────────────────────────
DID_NAMES = {
    0xF190: "노드 식별 이름",
    0x0300: "eth0 상태",
    0x0301: "전방 카메라",
    0x0302: "후방 카메라",
    0x0303: "마이크",
    0x0304: "Flask 서버",
    0x0305: "SOME/IP 서버",
    0x0306: "저장공간 사용률",
}

DTC_NAMES = {
    0xC30000: "전방 카메라 고장",
    0xC30100: "후방 카메라 고장",
    0xC30200: "마이크 고장",
    0xC30300: "저장공간 부족",
    0xC30400: "Flask 서버 오류",
    0xC30500: "SOME/IP 서버 오류",
    0xC30600: "eth0 통신 오류",
}


# ── DoIP 프레임 빌더 ───────────────────────────────────────────────────────

def _frame(ptype: int, payload: bytes) -> bytes:
    return struct.pack(
        ">BBHI", DOIP_VERSION, (~DOIP_VERSION) & 0xFF, ptype, len(payload)
    ) + payload

def _routing_act_req() -> bytes:
    return _frame(ROUTING_ACT_REQ,
                  struct.pack(">HB", SA, 0x00) + b"\x00\x00\x00\x00")

def _diag(uds: bytes) -> bytes:
    return _frame(DIAG_MSG, struct.pack(">HH", SA, TA) + uds)


# ── 수신 헬퍼 ─────────────────────────────────────────────────────────────

def _recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Connection closed")
        buf += chunk
    return buf

def _recv_frame(sock: socket.socket) -> tuple[int, bytes]:
    hdr = _recv_exact(sock, HEADER_LEN)
    _, _, ptype, plen = struct.unpack(">BBHI", hdr)
    payload = _recv_exact(sock, plen)
    return ptype, payload

def _recv_diag_response(sock: socket.socket) -> bytes:
    """ACK 건너뛰고 UDS 응답 payload 반환."""
    while True:
        ptype, payload = _recv_frame(sock)
        if ptype == DIAG_MSG and len(payload) >= 4:
            return payload[4:]   # SA(2) + TA(2) 스킵 → UDS data
        if ptype == DIAG_ACK:
            continue
        raise RuntimeError(f"Unexpected payload type: 0x{ptype:04X}")


# ── UDS 응답 파서 ──────────────────────────────────────────────────────────

def _parse_rdbi(did: int, resp: bytes) -> str:
    if not resp or resp[0] == 0x7F:
        nrc = resp[2] if len(resp) >= 3 else 0
        return f"NRC=0x{nrc:02X}"
    if resp[0] != 0x62 or len(resp) < 3:
        return f"잘못된 응답: {resp.hex()}"
    val = resp[3:]

    if did == 0xF190:
        return val.decode("ascii", errors="replace")
    if did == 0x0306:
        return f"{val[0]}%"
    status_map = {0: "DOWN / 미연결 / 중지", 1: "UP / 연결 / 실행 중"}
    return status_map.get(val[0], f"0x{val[0]:02X}") if val else "-"

def _parse_dtc(resp: bytes) -> list[tuple[int, int]]:
    if not resp or resp[0] != 0x59:
        return []
    # [0x59][subFunc][statusAvail] + N×[DTC(3B)+status(1B)]
    dtcs = []
    i = 3
    while i + 3 < len(resp):
        code = (resp[i] << 16) | (resp[i+1] << 8) | resp[i+2]
        status = resp[i+3]
        dtcs.append((code, status))
        i += 4
    return dtcs

def _parse_dtc_history(resp: bytes) -> list[dict]:
    """DID 0x0307 응답 파싱 — DTC당 15바이트 레코드."""
    if not resp or resp[0] != 0x62 or len(resp) < 3:
        return []
    data = resp[3:]   # 62 03 07 이후
    records = []
    i = 0
    while i + 15 <= len(data):
        code     = (data[i] << 16) | (data[i+1] << 8) | data[i+2]
        status   = data[i+3]
        f_start  = struct.unpack_from(">I", data, i+4)[0]
        recovered = struct.unpack_from(">I", data, i+8)[0]
        duration = struct.unpack_from(">H", data, i+12)[0]
        count    = data[i+14]
        records.append({
            "code": code, "status": status,
            "fault_start": f_start, "recovered": recovered,
            "duration": duration, "count": count,
        })
        i += 15
    return records

def _fmt_ts(ts: int) -> str:
    if ts == 0:
        return "-"
    return time.strftime("%H:%M:%S", time.localtime(ts))


# ── 메인 ──────────────────────────────────────────────────────────────────

def run(host: str, clear_dtc: bool) -> None:
    print(f"\n{'='*55}")
    print(f"  DoIP 진단 클라이언트  →  {host}:{TCP_PORT}")
    print(f"  대상: RPi #2  LA=0x0030")
    print(f"{'='*55}")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(5.0)
        try:
            s.connect((host, TCP_PORT))
        except (ConnectionRefusedError, TimeoutError) as e:
            print(f"\n[ERROR] 연결 실패: {e}")
            print("        → python -m doip.gateway 가 실행 중인지 확인하세요.")
            sys.exit(1)

        # ── Routing Activation ───────────────────────────────────────
        s.sendall(_routing_act_req())
        ptype, payload = _recv_frame(s)
        if ptype != ROUTING_ACT_RES or payload[4] != 0x10:
            print(f"[ERROR] Routing Activation 실패: {payload.hex()}")
            sys.exit(1)
        print(f"\n[OK] Routing Activation 완료  SA=0x{SA:04X} → LA=0x{TA:04X}\n")

        if clear_dtc:
            # ── DTC 전체 삭제 ────────────────────────────────────────
            s.sendall(_diag(bytes([0x14, 0xFF, 0xFF, 0xFF])))
            resp = _recv_diag_response(s)
            if resp and resp[0] == 0x54:
                print("[OK] DTC 전체 삭제 완료 (0x54)")
            else:
                print(f"[FAIL] DTC 삭제 실패: {resp.hex()}")
            return

        # ── 세션 제어 (0x10) ─────────────────────────────────────────
        s.sendall(_diag(bytes([0x10, 0x01])))
        resp = _recv_diag_response(s)
        ok = resp and resp[0] == 0x50
        print(f"  [0x10] DiagnosticSessionControl  →  {'OK (Default)' if ok else 'FAIL'}")

        # ── DID 읽기 (0x22) ──────────────────────────────────────────
        print(f"\n{'─'*55}")
        print(f"  DID 읽기 (0x22 ReadDataByIdentifier)")
        print(f"{'─'*55}")

        dids = [0xF190, 0x0300, 0x0301, 0x0302, 0x0303, 0x0304, 0x0305, 0x0306]
        for did in dids:
            uds = bytes([0x22]) + struct.pack(">H", did)
            s.sendall(_diag(uds))
            resp = _recv_diag_response(s)
            val_str = _parse_rdbi(did, resp)
            name = DID_NAMES.get(did, f"DID 0x{did:04X}")
            icon = "⚠" if "DOWN" in val_str or "중지" in val_str or "미연결" in val_str else "✓"
            print(f"  {icon}  0x{did:04X}  {name:<16}  {val_str}")
            time.sleep(0.05)

        # ── DTC 조회 (0x19) ──────────────────────────────────────────
        print(f"\n{'─'*55}")
        print(f"  DTC 상태 (0x19 subFunc=0x02, mask=0xFF)")
        print(f"{'─'*55}")

        s.sendall(_diag(bytes([0x19, 0x02, 0xFF])))
        resp = _recv_diag_response(s)
        dtcs = _parse_dtc(resp)

        if not dtcs:
            print("  ✓  저장된 DTC 없음")
        else:
            for code, status in dtcs:
                name = DTC_NAMES.get(code, "Unknown")
                active = "● ACTIVE" if status & 0x01 else "○ stored"
                print(f"  {active}  0x{code:06X}  {name}")

        # ── Active DTC만 별도 조회 ────────────────────────────────────
        s.sendall(_diag(bytes([0x19, 0x02, 0x01])))
        resp = _recv_diag_response(s)
        active_dtcs = _parse_dtc(resp)
        stored_count = len([d for d in dtcs if not (d[1] & 0x01)])
        print(f"\n  현재 활성 DTC: {len(active_dtcs)}개")
        print(f"  저장된 DTC 이력: {stored_count}개")

    print(f"\n{'='*55}\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host",  default="127.0.0.1", help="대상 IP")
    parser.add_argument("--clear", action="store_true",  help="DTC 전체 삭제")
    args = parser.parse_args()
    run(args.host, args.clear)
