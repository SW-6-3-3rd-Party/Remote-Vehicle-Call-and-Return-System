from pathlib import Path

# ---------- Event clip timing ----------
PRE_EVENT_SECS  = 5
POST_EVENT_SECS = 10

# ---------- USB camera 1 — Front / 전방 (cv2) ----------
USB1_DEVICE = None   # None = auto-detect: 1번째 USB 카메라
USB1_WIDTH  = 640
USB1_HEIGHT = 480
USB1_FPS    = 30

# ---------- USB camera 2 — Rear / 후방 (cv2) ----------
USB2_DEVICE = None   # None = auto-detect: 2번째 USB 카메라
USB2_WIDTH  = 320 #640
USB2_HEIGHT = 180 #480
USB2_FPS    = 30

# ---------- USB microphone (pyaudio) ----------
AUDIO_RATE     = 48000
AUDIO_CHANNELS = 1
AUDIO_CHUNK    = 1024
# Keywords used to auto-detect USB mic by device name
AUDIO_MIC_KEYWORDS = ("USB", "AB13X")

# ---------- Storage ----------
CONTINUOUS_SEGMENT_SECS = 60   # rotate continuous recording every N seconds
CONTINUOUS_MAX_SEGMENTS = 10   # continuous 폴더당 보관할 최대 파일 수 (초과분 자동 삭제)

# 절대경로로 고정 — cwd에 무관하게 항상 같은 위치
_PROJECT_ROOT   = Path(__file__).parent.parent   # remote_vehicle_project/
RECORDINGS_BASE = _PROJECT_ROOT / "recordings"
CONTINUOUS_DIR  = RECORDINGS_BASE / "continuous"
EVENTS_DIR      = RECORDINGS_BASE / "events"
DB_PATH         = RECORDINGS_BASE / "events.db"

# ---------- Flask streaming server ----------
FLASK_HOST = "0.0.0.0"
FLASK_PORT = 8080

# ---------- UDP live streaming ----------
UDP_STREAM_PORT    = 5000   # RPi가 수신 대기하는 포트 (PC → RPi 요청/keepalive)
UDP_STREAM_TIMEOUT = 5.0    # 이 초 동안 keepalive 없으면 스트리밍 자동 중단

# ---------- ECU UDP Trigger ----------
ECU_TRIGGER_LISTEN_IP   = "0.0.0.0"
ECU_TRIGGER_LISTEN_PORT = 5201   # RPi #1 → RPi #2 충돌 이벤트 수신 포트

# ---------- SOME/IP ----------
MEDIA_INTERFACE_IP        = "192.168.20.2"   # someipyd interface IP
SOMEIP_SERVICE_PORT       = 30491
ACCIDENT_SERVICE_ID       = 0x1001
ACCIDENT_INSTANCE_ID      = 0x0001
GET_RECORD_LIST_METHOD_ID = 0x0001
PAYLOAD_ENCODING          = "utf-8"
VEHICLE_ID                = 1
