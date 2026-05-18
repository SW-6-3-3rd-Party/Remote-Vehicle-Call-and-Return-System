"""
USB microphone recorder (pyaudio)

Ring buffer stores raw PCM chunks (bytes).
Continuous recording writes to rotating WAV files.
Event clip: pre-event chunks from ring + POST_EVENT_SECS of live chunks.
"""
import logging
import threading
import time
import wave
from pathlib import Path

import pyaudio

from . import config
from .ring_buffer import RingBuffer

log = logging.getLogger(__name__)

# chunks needed to cover PRE_EVENT_SECS seconds
_PRE_CHUNKS = int(
    config.AUDIO_RATE / config.AUDIO_CHUNK * config.PRE_EVENT_SECS
)
# chunks needed to cover CONTINUOUS_SEGMENT_SECS seconds
_SEG_CHUNKS = int(
    config.AUDIO_RATE / config.AUDIO_CHUNK * config.CONTINUOUS_SEGMENT_SECS
)


class MicRecorder:
    def __init__(self):
        self.ring: RingBuffer[bytes] = RingBuffer(_PRE_CHUNKS)

        self._stop_evt = threading.Event()
        self._thread: threading.Thread | None = None
        self._pa: pyaudio.PyAudio | None = None
        self._mic_index: int | None = None

        # event capture: set by start_event_clip(), cleared by stop_event_clip()
        self._event_chunks: list[bytes] | None = None
        self._event_lock = threading.Lock()

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self) -> None:
        self._pa = pyaudio.PyAudio()
        self._mic_index = self._find_mic()
        if self._mic_index is None:
            raise RuntimeError("USB microphone not found")
        self._thread = threading.Thread(target=self._loop, name="mic-loop", daemon=True)
        self._thread.start()
        log.info("Mic recorder started (device index=%d)", self._mic_index)

    def stop(self) -> None:
        self._stop_evt.set()
        if self._thread:
            self._thread.join(timeout=5)
        if self._pa:
            self._pa.terminate()
        log.info("Mic recorder stopped")

    # ------------------------------------------------------------------
    # Main capture loop
    # ------------------------------------------------------------------

    def _loop(self) -> None:
        stream = self._pa.open(
            format=pyaudio.paInt16,
            channels=config.AUDIO_CHANNELS,
            rate=config.AUDIO_RATE,
            input=True,
            input_device_index=self._mic_index,
            frames_per_buffer=config.AUDIO_CHUNK,
        )
        cont_writer = self._open_cont_writer()
        chunk_count = 0

        try:
            while not self._stop_evt.is_set():
                chunk = stream.read(config.AUDIO_CHUNK, exception_on_overflow=False)

                self.ring.push(chunk)
                cont_writer.writeframes(chunk)
                chunk_count += 1

                # rotate continuous WAV segment
                if chunk_count >= _SEG_CHUNKS:
                    cont_writer.close()
                    cont_writer = self._open_cont_writer()
                    chunk_count = 0

                # accumulate event chunks if event is active
                with self._event_lock:
                    if self._event_chunks is not None:
                        self._event_chunks.append(chunk)

        finally:
            stream.stop_stream()
            stream.close()
            cont_writer.close()

    # ------------------------------------------------------------------
    # Event clip API  (called from EventTrigger thread)
    # ------------------------------------------------------------------

    def start_event_clip(self, event_dir: Path) -> float:
        """
        Snapshot pre-event audio from ring buffer and start collecting
        post-event chunks. The actual WAV file is written in stop_event_clip().
        Returns the calculated timestamp of the oldest audio chunk.
        """
        snapshot = self.ring.snapshot()
        call_time = time.time()
        audio_pre_secs = len(snapshot) * config.AUDIO_CHUNK / config.AUDIO_RATE
        audio_start_ts = call_time - audio_pre_secs

        initial = list(snapshot)
        with self._event_lock:
            self._event_chunks = initial
        self._event_dir = event_dir
        log.info("Mic event clip started: pre=%d chunks (%.3fs) start_ts=%.3f",
                 len(snapshot), audio_pre_secs, audio_start_ts)
        return audio_start_ts

    def stop_event_clip(self) -> None:
        with self._event_lock:
            chunks = self._event_chunks
            self._event_chunks = None

        if chunks is None:
            return

        path = self._event_dir / "mic_clip.wav"
        self._write_wav(path, chunks)
        log.info("Mic event clip saved: %s  (%d chunks)", path, len(chunks))

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _find_mic(self) -> int | None:
        for i in range(self._pa.get_device_count()):
            info = self._pa.get_device_info_by_index(i)
            if info.get("maxInputChannels", 0) > 0:
                name = info.get("name", "")
                if any(kw in name for kw in config.AUDIO_MIC_KEYWORDS):
                    log.info("Found mic: index=%d  name=%s", i, name)
                    return i
        return None

    def _open_cont_writer(self) -> wave.Wave_write:
        ts = time.strftime("%Y%m%d_%H%M%S")
        path = config.CONTINUOUS_DIR / "mic" / f"{ts}.wav"
        path.parent.mkdir(parents=True, exist_ok=True)
        wf = wave.open(str(path), "wb")
        wf.setnchannels(config.AUDIO_CHANNELS)
        wf.setsampwidth(self._pa.get_sample_size(pyaudio.paInt16))
        wf.setframerate(config.AUDIO_RATE)
        log.debug("Mic continuous segment: %s", path)
        return wf

    def _write_wav(self, path: Path, chunks: list[bytes]) -> None:
        with wave.open(str(path), "wb") as wf:
            wf.setnchannels(config.AUDIO_CHANNELS)
            wf.setsampwidth(self._pa.get_sample_size(pyaudio.paInt16))
            wf.setframerate(config.AUDIO_RATE)
            wf.writeframes(b"".join(chunks))
