"""
SQLite-backed DTC Storage — ISO 14229-1 based

DTC Codes (3-byte):
  0xC30000  전방 카메라 고장   /dev/video0 사라짐
  0xC30100  후방 카메라 고장   /dev/video2 사라짐
  0xC30200  마이크 고장        /proc/asound/card0 사라짐
  0xC30300  저장공간 부족      사용률 > 90%
  0xC30400  Flask 서버 오류    TCP 8080 listen 안 됨
  0xC30500  SOME/IP 서버 오류  UDP 30491 listen 안 됨
  0xC30600  eth0 통신 오류     eth0 인터페이스 DOWN

Status Byte (ISO 14229-1 Annex D):
  bit 0 (0x01)  testFailed   — 현재 활성 고장
  bit 3 (0x08)  confirmedDTC — 저장된 이력

고장 이력 컬럼 (상태 전이 기준으로만 기록):
  fault_start_time  — 현재 에피소드 시작 시각 (ACTIVE 진입 시 기록, 복구 시 NULL)
  recovered_time    — 가장 최근 복구 시각 (ACTIVE→STORED 전이 시 기록)
  duration_sec      — 가장 최근 완료된 에피소드의 지속 시간(초)
"""
import sqlite3
import threading
import time
from pathlib import Path


class DTCCode:
    USB1_CAMERA_FAIL = 0xC30000
    USB_CAMERA_FAIL = 0xC30100
    MIC_FAIL        = 0xC30200
    STORAGE_LOW     = 0xC30300
    FLASK_ERROR     = 0xC30400
    SOMEIP_ERROR    = 0xC30500
    ETH0_ERROR      = 0xC30600


DTC_DESCRIPTIONS: dict[int, str] = {
    DTCCode.USB1_CAMERA_FAIL: "Front USB camera failure (1st USB camera missing)",
    DTCCode.USB_CAMERA_FAIL: "Rear camera failure (/dev/video2 missing)",
    DTCCode.MIC_FAIL:        "Microphone failure (/proc/asound/card0 missing)",
    DTCCode.STORAGE_LOW:     "Storage space critically low (>90%)",
    DTCCode.FLASK_ERROR:     "Flask HTTP server not responding (TCP 8080)",
    DTCCode.SOMEIP_ERROR:    "SOME/IP server not responding (UDP 30491)",
    DTCCode.ETH0_ERROR:      "eth0 interface DOWN",
}


class DTCStatusBit:
    TEST_FAILED = 0x01
    CONFIRMED   = 0x08


class DTCStore:
    def __init__(self, db_path: Path):
        db_path.parent.mkdir(parents=True, exist_ok=True)
        self._path = str(db_path)
        self._lock = threading.Lock()
        self._init_db()

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self._path)
        conn.row_factory = sqlite3.Row
        return conn

    def _init_db(self) -> None:
        with self._lock, self._connect() as conn:
            conn.execute("""
                CREATE TABLE IF NOT EXISTS dtcs (
                    dtc_code         INTEGER PRIMARY KEY,
                    status_byte      INTEGER NOT NULL DEFAULT 0,
                    first_occurrence REAL    NOT NULL,
                    last_occurrence  REAL    NOT NULL,
                    occurrence_count INTEGER NOT NULL DEFAULT 0,
                    fault_start_time REAL,
                    recovered_time   REAL,
                    duration_sec     REAL
                )
            """)
            # 기존 DB 마이그레이션 (컬럼이 없으면 추가)
            for col, defn in [
                ("fault_start_time", "REAL"),
                ("recovered_time",   "REAL"),
                ("duration_sec",     "REAL"),
            ]:
                try:
                    conn.execute(f"ALTER TABLE dtcs ADD COLUMN {col} {defn}")
                except Exception:
                    pass  # 이미 존재하는 컬럼

    def set_active(self, code: int) -> None:
        now = time.time()
        new_status = DTCStatusBit.TEST_FAILED | DTCStatusBit.CONFIRMED
        with self._lock, self._connect() as conn:
            row = conn.execute(
                "SELECT status_byte FROM dtcs WHERE dtc_code = ?", (code,)
            ).fetchone()
            if row is None:
                # 최초 발생 — fault_start_time 기록
                conn.execute(
                    "INSERT INTO dtcs "
                    "(dtc_code, status_byte, first_occurrence, last_occurrence, "
                    " occurrence_count, fault_start_time, recovered_time, duration_sec) "
                    "VALUES (?, ?, ?, ?, 1, ?, NULL, NULL)",
                    (code, new_status, now, now, now),
                )
            elif not (row["status_byte"] & DTCStatusBit.TEST_FAILED):
                # STORED → ACTIVE 재발 — 새 에피소드 시작, 이전 복구 이력 초기화
                conn.execute(
                    "UPDATE dtcs SET status_byte = status_byte | ?, "
                    "last_occurrence = ?, occurrence_count = occurrence_count + 1, "
                    "fault_start_time = ?, recovered_time = NULL, duration_sec = NULL "
                    "WHERE dtc_code = ?",
                    (new_status, now, now, code),
                )
            else:
                # ACTIVE 지속 — last_occurrence만 갱신 (상태 전이 없음)
                conn.execute(
                    "UPDATE dtcs SET last_occurrence = ? WHERE dtc_code = ?",
                    (now, code),
                )

    def clear_active(self, code: int) -> None:
        now = time.time()
        with self._lock, self._connect() as conn:
            row = conn.execute(
                "SELECT status_byte, fault_start_time FROM dtcs WHERE dtc_code = ?",
                (code,),
            ).fetchone()
            if row and (row["status_byte"] & DTCStatusBit.TEST_FAILED):
                # ACTIVE → STORED 전이 — 복구 시각과 지속 시간 기록
                start = row["fault_start_time"] or now
                duration = now - start
                conn.execute(
                    "UPDATE dtcs SET status_byte = status_byte & ~?, "
                    "recovered_time = ?, duration_sec = ?, fault_start_time = NULL "
                    "WHERE dtc_code = ?",
                    (DTCStatusBit.TEST_FAILED, now, duration, code),
                )

    def clear_all(self) -> None:
        with self._lock, self._connect() as conn:
            conn.execute("DELETE FROM dtcs")

    def get_active_dtcs(self) -> list[dict]:
        return self._query(DTCStatusBit.TEST_FAILED)

    def get_all_dtcs(self, status_mask: int = 0xFF) -> list[dict]:
        return self._query(status_mask)

    def _query(self, mask: int) -> list[dict]:
        with self._lock, self._connect() as conn:
            rows = conn.execute(
                "SELECT * FROM dtcs WHERE status_byte & ? != 0 "
                "ORDER BY last_occurrence DESC",
                (mask,),
            ).fetchall()
        return [dict(r) for r in rows]
