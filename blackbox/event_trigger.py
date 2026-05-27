import json
import logging
import socket
import struct
import subprocess
import threading
import time
from datetime import datetime
from pathlib import Path

import cv2

from . import config
from .event_db import EventDB

log = logging.getLogger(__name__)

_ECU_TRIGGER_FRAME_LEN = 5
_ECU_EVENT_TYPE_ACCIDENT = 0x01


def _transcode(avi: Path, mp4: Path,
               audio: Path | None = None,
               audio_offset: float = 0.0,
               target_duration: float | None = None) -> None:
    """
    AVI(MJPG) → MP4(H.264) 변환.  -movflags +faststart 로 웹 seeking 지원.
    audio가 주어지면 WAV를 AAC로 함께 합친다.
    audio_offset: 양수 → 비디오 기준 오디오 시작이 빠름 → 오디오 지연
                  음수 → 오디오 기준 비디오 시작이 빠름 → 오디오 앞부분 잘라냄

    코덱 우선순위:
      1. h264_v4l2m2m  — RPi 하드웨어 인코더 (빠름)
      2. libx264        — 소프트웨어 인코더 (호환성 높음)
    """
    if not avi.exists():
        log.error("트랜스코딩 원본 없음: %s", avi)
        return

    video_filter: list[str] = []
    if target_duration is not None and target_duration > 0:
        video_timeline = _probe_video_timeline(avi)
        if video_timeline is not None:
            media_duration, pts_span = video_timeline
            duration_ratio = target_duration / pts_span
            if abs(duration_ratio - 1.0) > 0.01:
                video_filter = ["-filter:v", f"setpts={duration_ratio:.6f}*PTS"]
                log.info("비디오 길이 보정: %s %.3fs → %.3fs (ratio=%.6f)",
                         avi.name, media_duration, target_duration, duration_ratio)

    audio_inputs: list[str] = []
    audio_codec: list[str] = ["-an"]
    if audio and audio.exists():
        if audio_offset > 0.005:
            # 오디오가 비디오보다 일찍 시작 → 오디오를 offset만큼 지연
            audio_inputs = ["-itsoffset", f"{audio_offset:.3f}", "-i", str(audio)]
        elif audio_offset < -0.005:
            # 비디오가 오디오보다 일찍 시작 → 오디오 앞부분 잘라냄
            audio_inputs = ["-ss", f"{-audio_offset:.3f}", "-i", str(audio)]
        else:
            audio_inputs = ["-i", str(audio)]
        audio_codec = ["-c:a", "aac", "-b:a", "128k"]
        log.info("오디오 싱크 오프셋 적용: %.3fs", audio_offset)

    codecs = [
        ("h264_v4l2m2m", ["-pix_fmt", "yuv420p"]),
        ("libx264",       ["-preset", "fast", "-crf", "23", "-pix_fmt", "yuv420p"]),
    ]
    for codec, extra in codecs:
        mp4.unlink(missing_ok=True)
        cmd = [
            "ffmpeg", "-y", "-i", str(avi), *audio_inputs,
            *video_filter,
            "-c:v", codec, *extra,
            *audio_codec,
            "-movflags", "+faststart",
            str(mp4),
        ]
        try:
            subprocess.run(cmd, check=True, capture_output=True, timeout=120)
            log.info("트랜스코딩 완료 (%s): %s", codec, mp4.name)
            return
        except FileNotFoundError:
            log.error("ffmpeg 없음 — sudo apt install ffmpeg")
            return
        except subprocess.TimeoutExpired:
            log.error("트랜스코딩 타임아웃: %s", avi.name)
            mp4.unlink(missing_ok=True)
            return
        except subprocess.CalledProcessError as e:
            log.warning("코덱 %s 실패, 다음 시도: %s",
                        codec, e.stderr.decode()[-100:].strip())

    # ffmpeg 모두 실패 → OpenCV 폴백 (오디오 없이 영상만, MJPG AVI 픽셀 포맷 문제 우회)
    log.warning("ffmpeg 실패, OpenCV 변환 시도 (오디오 미포함): %s", avi.name)
    mp4.unlink(missing_ok=True)
    try:
        cap = cv2.VideoCapture(str(avi))
        if not cap.isOpened():
            raise RuntimeError("VideoCapture 열기 실패")
        fps = cap.get(cv2.CAP_PROP_FPS) or 30
        w   = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        h   = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        out = cv2.VideoWriter(
            str(mp4), cv2.VideoWriter_fourcc(*"mp4v"), fps, (w, h)
        )
        count = 0
        while True:
            ret, frame = cap.read()
            if not ret:
                break
            out.write(frame)
            count += 1
        cap.release()
        out.release()
        if count == 0:
            mp4.unlink(missing_ok=True)
            log.error("OpenCV 변환 실패 (0 frames): %s", avi.name)
        else:
            log.info("OpenCV 변환 완료 (%d frames): %s", count, mp4.name)
    except Exception as cv_err:
        log.error("OpenCV 변환 예외: %s", cv_err)
        mp4.unlink(missing_ok=True)


def _probe_video_timeline(path: Path) -> tuple[float, float] | None:
    cap = cv2.VideoCapture(str(path))
    try:
        if not cap.isOpened():
            return None
        fps = cap.get(cv2.CAP_PROP_FPS)
        frames = cap.get(cv2.CAP_PROP_FRAME_COUNT)
        if fps <= 0 or frames <= 0:
            return None
        media_duration = frames / fps
        pts_span = max((frames - 1) / fps, 1 / fps)
        return media_duration, pts_span
    finally:
        cap.release()


_POST_EVENT_COOLDOWN_SECS = 30  # 이벤트 종료 후 이 시간 동안 재트리거 무시

class EventTrigger:
    def __init__(self, usb1_rec, usb_rec, mic_rec, db: EventDB):
        self._usb1 = usb1_rec
        self._usb = usb_rec
        self._mic = mic_rec
        self._db = db
        self._in_event = False
        self._last_event_end: float = 0.0
        self._lock = threading.Lock()
        self._stop_event = threading.Event()

    # ------------------------------------------------------------------
    # UDP trigger setup / teardown
    # ------------------------------------------------------------------

    def setup(self) -> None:
        self._stop_event.clear()
        threading.Thread(target=self._udp_listener, name="udp-trigger",
                         daemon=True).start()
        log.info("UDP trigger listener started on %s:%d",
                 config.ECU_TRIGGER_LISTEN_IP, config.ECU_TRIGGER_LISTEN_PORT)

    def cleanup(self) -> None:
        self._stop_event.set()
        log.info("UDP trigger listener stopped")

    # ------------------------------------------------------------------
    # UDP listener  (runs in dedicated thread)
    # ------------------------------------------------------------------

    def _udp_listener(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(1.0)
        sock.bind((config.ECU_TRIGGER_LISTEN_IP, config.ECU_TRIGGER_LISTEN_PORT))
        log.info("UDP trigger socket bound to %s:%d",
                 config.ECU_TRIGGER_LISTEN_IP, config.ECU_TRIGGER_LISTEN_PORT)
        while not self._stop_event.is_set():
            try:
                data, addr = sock.recvfrom(16)
            except socket.timeout:
                continue
            except OSError:
                break
            if len(data) != _ECU_TRIGGER_FRAME_LEN:
                log.warning(
                    "Invalid ECU trigger frame from %s (%d bytes, expected %d) — ignored",
                    addr, len(data), _ECU_TRIGGER_FRAME_LEN,
                )
                continue
            event_type   = data[0]
            timestamp_ms = struct.unpack(">I", data[1:5])[0]
            log.info("ECU trigger from %s: event_type=0x%02X ts=%d ms",
                     addr, event_type, timestamp_ms)
            if event_type != _ECU_EVENT_TYPE_ACCIDENT:
                log.debug("Unknown event_type 0x%02X — ignored", event_type)
                continue
            self._on_trigger(source=f"ECU UDP {addr[0]}:{addr[1]}",
                             ecu_event_type=event_type,
                             ecu_timestamp_ms=timestamp_ms)
        sock.close()

    def trigger(self, source: str = "manual") -> bool:
        """Trigger an event recording from a non-UDP source."""
        accepted = self._on_trigger(source=source)
        if accepted:
            log.info("Event trigger accepted from %s", source)
        else:
            log.info("Event trigger ignored from %s", source)
        return accepted

    def _on_trigger(
        self,
        source: str = "manual",
        ecu_event_type: int | None = None,
        ecu_timestamp_ms: int | None = None,
    ) -> bool:
        with self._lock:
            now = time.time()
            if self._in_event:
                log.debug("Event already in progress — ignoring trigger")
                return False
            if now - self._last_event_end < _POST_EVENT_COOLDOWN_SECS:
                remaining = _POST_EVENT_COOLDOWN_SECS - (now - self._last_event_end)
                log.debug("Post-event cooldown active (%.1fs left) — ignoring trigger", remaining)
                return False
            self._in_event = True
        threading.Thread(
            target=self._handle_event,
            kwargs={
                "source": source,
                "ecu_event_type": ecu_event_type,
                "ecu_timestamp_ms": ecu_timestamp_ms,
            },
            name="event-handler",
            daemon=True,
        ).start()
        return True

    # ------------------------------------------------------------------
    # Event handling
    # ------------------------------------------------------------------

    def _handle_event(
        self,
        source: str = "manual",
        ecu_event_type: int | None = None,
        ecu_timestamp_ms: int | None = None,
    ) -> None:
        triggered_at = time.time()
        event_id = datetime.fromtimestamp(triggered_at).strftime("%Y%m%d_%H%M%S")
        event_dir = config.EVENTS_DIR / f"event_{event_id}"
        event_dir.mkdir(parents=True, exist_ok=True)

        log.info("Event triggered: %s  source=%s", event_id, source)

        try:
            # --- start clips (pre-event from ring buffer written immediately) ---
            usb1_video_start: float | None = None
            if self._usb1 is not None:
                usb1_video_start = self._usb1.start_event_clip(event_dir)
            usb_video_start: float | None = None
            if self._usb is not None:
                usb_video_start = self._usb.start_event_clip(event_dir)
            mic_audio_start: float | None = None
            if self._mic is not None:
                mic_audio_start = self._mic.start_event_clip(event_dir)

            # --- wait for post-event recording ---
            time.sleep(config.POST_EVENT_SECS)

            # --- stop clips (files closed and flushed) ---
            event_end_ts = time.time()
            if self._usb1 is not None:
                self._usb1.stop_event_clip()
            if self._usb is not None:
                self._usb.stop_event_clip()
            if self._mic is not None:
                self._mic.stop_event_clip()

            # --- persist metadata ---
            meta = {
                "event_id": event_id,
                "triggered_at": triggered_at,
                "trigger_source": source,
                "ecu_event_type": ecu_event_type,
                "ecu_timestamp_ms": ecu_timestamp_ms,
                "pre_secs": config.PRE_EVENT_SECS,
                "post_secs": config.POST_EVENT_SECS,
                "files": {
                    "front": str(event_dir / "front_clip.avi"),
                    "usb": str(event_dir / "usb_clip.avi"),
                    "mic": str(event_dir / "mic_clip.wav"),
                },
            }
            with open(event_dir / "metadata.json", "w") as f:
                json.dump(meta, f, indent=2)

            self._db.insert_event(event_id, triggered_at, event_dir)
            log.info("Event %s saved to %s", event_id, event_dir)

            # SSE로 브라우저에 실시간 알림 → 이벤트 테이블 자동 갱신
            from . import streamer
            streamer.notify_new_event(event_id)

            # 비디오-오디오 싱크 오프셋 계산
            sync_offsets = {
                "front_clip": 0.0,
                "usb_clip": 0.0,
            }
            target_durations = {
                "front_clip": float(config.PRE_EVENT_SECS + config.POST_EVENT_SECS),
                "usb_clip": float(config.PRE_EVENT_SECS + config.POST_EVENT_SECS),
            }
            if usb1_video_start is not None and mic_audio_start is not None:
                sync_offsets["front_clip"] = usb1_video_start - mic_audio_start
                target_durations["front_clip"] = event_end_ts - usb1_video_start
                log.info("전방 비디오-오디오 싱크 오프셋: %.3fs "
                         "(video_start=%.3f, audio_start=%.3f, duration=%.3f)",
                         sync_offsets["front_clip"], usb1_video_start,
                         mic_audio_start, target_durations["front_clip"])
            if usb_video_start is not None and mic_audio_start is not None:
                sync_offsets["usb_clip"] = usb_video_start - mic_audio_start
                target_durations["usb_clip"] = event_end_ts - usb_video_start
                log.info("후방 비디오-오디오 싱크 오프셋: %.3fs "
                         "(video_start=%.3f, audio_start=%.3f, duration=%.3f)",
                         sync_offsets["usb_clip"], usb_video_start,
                         mic_audio_start, target_durations["usb_clip"])

            # AVI → MP4 트랜스코딩 (백그라운드, seeking 지원)
            mic_wav = event_dir / "mic_clip.wav"
            has_audio = self._mic is not None and mic_wav.exists()
            for stem in ("front_clip", "usb_clip"):
                avi = event_dir / f"{stem}.avi"
                mp4 = event_dir / f"{stem}.mp4"
                audio = mic_wav if has_audio else None
                offset = sync_offsets[stem]
                duration = target_durations[stem]
                threading.Thread(
                    target=_transcode, args=(avi, mp4, audio, offset, duration), daemon=True
                ).start()

        except Exception:
            log.exception("Error during event handling for %s", event_id)

        finally:
            with self._lock:
                self._last_event_end = time.time()
                self._in_event = False
