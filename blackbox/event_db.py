"""
SQLite-backed event log.

Schema
------
events
  id           INTEGER PK AUTOINCREMENT
  event_id     TEXT      e.g. "20240101_120000"
  triggered_at REAL      unix timestamp of GPIO trigger
  event_dir    TEXT      absolute path to event folder
  created_at   REAL      row insertion time
"""
import sqlite3
import threading
import time
from pathlib import Path
from typing import Any

from . import config


class EventDB:
    def __init__(self):
        config.DB_PATH.parent.mkdir(parents=True, exist_ok=True)
        self._path = str(config.DB_PATH)
        self._lock = threading.Lock()
        self._init()

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self._path)
        conn.row_factory = sqlite3.Row
        return conn

    def _init(self) -> None:
        with self._lock, self._connect() as conn:
            conn.execute("""
                CREATE TABLE IF NOT EXISTS events (
                    id           INTEGER PRIMARY KEY AUTOINCREMENT,
                    event_id     TEXT    NOT NULL,
                    triggered_at REAL    NOT NULL,
                    event_dir    TEXT    NOT NULL,
                    created_at   REAL    NOT NULL
                )
            """)

    # ------------------------------------------------------------------
    # Write
    # ------------------------------------------------------------------

    def insert_event(self, event_id: str, triggered_at: float,
                     event_dir: str | Path) -> int:
        with self._lock, self._connect() as conn:
            cur = conn.execute(
                "INSERT INTO events (event_id, triggered_at, event_dir, created_at)"
                " VALUES (?, ?, ?, ?)",
                (event_id, triggered_at, str(event_dir), time.time()),
            )
            return cur.lastrowid

    # ------------------------------------------------------------------
    # Read
    # ------------------------------------------------------------------

    def get_events(self, limit: int = 50) -> list[dict[str, Any]]:
        with self._lock, self._connect() as conn:
            rows = conn.execute(
                "SELECT * FROM events ORDER BY triggered_at DESC LIMIT ?",
                (limit,),
            ).fetchall()
        return [dict(r) for r in rows]

    def get_event(self, event_id: str) -> dict[str, Any] | None:
        with self._lock, self._connect() as conn:
            row = conn.execute(
                "SELECT * FROM events WHERE event_id = ?", (event_id,)
            ).fetchone()
        return dict(row) if row else None
