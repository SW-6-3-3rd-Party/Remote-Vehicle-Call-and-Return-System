import json
import logging
import subprocess
import threading
import time
from datetime import datetime
from pathlib import Path

import cv2
import RPi.GPIO as GPIO

from . import config
from .event_db import EventDB

log = logging.getLogger(__name__)


def _transcode(avi: Path, mp4: Path,
               audio: Path | None = None,
               audio_offset: float = 0.0) -> None:
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


_POST_EVENT_COOLDOWN_SECS = 30  # 이벤트 종료 후 이 시간 동안 재트리거 무시

class EventTrigger:
    def __init__(self, csi_rec, usb_rec, mic_rec, db: EventDB):
        self._csi = csi_rec
        self._usb = usb_rec
        self._mic = mic_rec
        self._db = db
        self._in_event = False
        self._last_event_end: float = 0.0
        self._lock = threading.Lock()

    # ------------------------------------------------------------------
    # GPIO setup / teardown
    # ------------------------------------------------------------------

    def setup(self) -> None:
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(config.GPIO_SWITCH_PIN, GPIO.IN,
                   pull_up_down=GPIO.PUD_UP)
        GPIO.add_event_detect(
            config.GPIO_SWITCH_PIN,
            GPIO.FALLING,
            callback=self._gpio_callback,
            bouncetime=config.GPIO_BOUNCE_MS,
        )
        log.info("GPIO %d armed (BCM, falling edge, pull-up)",
                 config.GPIO_SWITCH_PIN)

    def cleanup(self) -> None:
        GPIO.cleanup()
        log.info("GPIO cleaned up")

    # ------------------------------------------------------------------
    # GPIO ISR  (runs in RPi.GPIO's internal thread)
    # ------------------------------------------------------------------

    def _gpio_callback(self, channel: int) -> None:
        import RPi.GPIO as _GPIO
        # 노이즈 필터: 50ms 뒤에도 여전히 LOW인지 확인 (sustained LOW)
        time.sleep(0.05)
        if _GPIO.input(channel) != _GPIO.LOW:
            log.info("GPIO false trigger (pin not sustained LOW) — ignored")
            return
        # 추가 확인: 또 50ms 뒤
        time.sleep(0.05)
        if _GPIO.input(channel) != _GPIO.LOW:
            log.info("GPIO false trigger (pin not sustained LOW x2) — ignored")
            return

        with self._lock:
            now = time.time()
            if self._in_event:
                log.debug("Event already in progress — ignoring trigger")
                return
            if now - self._last_event_end < _POST_EVENT_COOLDOWN_SECS:
                remaining = _POST_EVENT_COOLDOWN_SECS - (now - self._last_event_end)
                log.debug("Post-event cooldown active (%.1fs left) — ignoring trigger", remaining)
                return
            self._in_event = True

        threading.Thread(target=self._handle_event, name="event-handler",
                         daemon=True).start()

    # ------------------------------------------------------------------
    # Event handling
    # ------------------------------------------------------------------

    def _handle_event(self) -> None:
        triggered_at = time.time()
        event_id = datetime.fromtimestamp(triggered_at).strftime("%Y%m%d_%H%M%S")
        event_dir = config.EVENTS_DIR / f"event_{event_id}"
        event_dir.mkdir(parents=True, exist_ok=True)

        log.info("Event triggered: %s", event_id)

        try:
            # --- start clips (pre-event from ring buffer written immediately) ---
            if self._csi is not None:
                self._csi.start_event_clip(event_dir)
            usb_video_start: float | None = None
            if self._usb is not None:
                usb_video_start = self._usb.start_event_clip(event_dir)
            mic_audio_start: float = self._mic.start_event_clip(event_dir)

            # --- wait for post-event recording ---
            time.sleep(config.POST_EVENT_SECS)

            # --- stop clips (files closed and flushed) ---
            if self._csi is not None:
                self._csi.stop_event_clip()
            if self._usb is not None:
                self._usb.stop_event_clip()
            self._mic.stop_event_clip()

            # --- persist metadata ---
            meta = {
                "event_id": event_id,
                "triggered_at": triggered_at,
                "pre_secs": config.PRE_EVENT_SECS,
                "post_secs": config.POST_EVENT_SECS,
                "files": {
                    "csi": str(event_dir / "csi_clip.avi"),
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
            sync_offset = 0.0
            if usb_video_start is not None:
                sync_offset = usb_video_start - mic_audio_start
                log.info("비디오-오디오 싱크 오프셋: %.3fs "
                         "(video_start=%.3f, audio_start=%.3f)",
                         sync_offset, usb_video_start, mic_audio_start)

            # AVI → MP4 트랜스코딩 (백그라운드, seeking 지원)
            mic_wav = event_dir / "mic_clip.wav"
            for stem in ("csi_clip", "usb_clip"):
                avi = event_dir / f"{stem}.avi"
                mp4 = event_dir / f"{stem}.mp4"
                audio = mic_wav if stem == "usb_clip" else None
                offset = sync_offset if stem == "usb_clip" else 0.0
                threading.Thread(
                    target=_transcode, args=(avi, mp4, audio, offset), daemon=True
                ).start()

        except Exception:
            log.exception("Error during event handling for %s", event_id)

        finally:
            with self._lock:
                self._last_event_end = time.time()
                self._in_event = False
