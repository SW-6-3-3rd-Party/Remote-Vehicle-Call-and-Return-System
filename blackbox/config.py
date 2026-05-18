from pathlib import Path

# ---------- Event clip timing ----------
PRE_EVENT_SECS  = 5
POST_EVENT_SECS = 10

# ---------- CSI camera (picamera2) ----------
CSI_WIDTH  = 640
CSI_HEIGHT = 480
CSI_FPS    = 30

# ---------- USB camera (cv2) ----------
# None = auto-detect by scanning /dev/video0–9 after picamera2 is up
USB_DEVICE = None
USB_WIDTH  = 640
USB_HEIGHT = 480
USB_FPS    = 30

# ---------- USB microphone (pyaudio) ----------
AUDIO_RATE     = 48000
AUDIO_CHANNELS = 1
AUDIO_CHUNK    = 1024
# Keywords used to auto-detect USB mic by device name
AUDIO_MIC_KEYWORDS = ("USB", "AB13X")

# ---------- GPIO ----------
GPIO_SWITCH_PIN = 17    # BCM numbering, active-LOW with internal pull-up
GPIO_BOUNCE_MS  = 300

# ---------- Storage ----------
CONTINUOUS_SEGMENT_SECS = 60   # rotate continuous recording every N seconds

# 절대경로로 고정 — cwd에 무관하게 항상 같은 위치
_PROJECT_ROOT   = Path(__file__).parent.parent   # remote_vehicle_project/
RECORDINGS_BASE = _PROJECT_ROOT / "recordings"
CONTINUOUS_DIR  = RECORDINGS_BASE / "continuous"
EVENTS_DIR      = RECORDINGS_BASE / "events"
DB_PATH         = RECORDINGS_BASE / "events.db"

# ---------- Flask streaming server ----------
FLASK_HOST = "0.0.0.0"
FLASK_PORT = 8088

# ---------- UDP live streaming ----------
UDP_STREAM_PORT    = 5000   # RPi가 수신 대기하는 포트 (PC → RPi 요청/keepalive)
UDP_STREAM_TIMEOUT = 5.0    # 이 초 동안 keepalive 없으면 스트리밍 자동 중단

# ---------- SOME/IP ----------
MEDIA_INTERFACE_IP        = "192.168.20.2"   # someipyd interface IP
SOMEIP_SERVICE_PORT       = 30491
ACCIDENT_SERVICE_ID       = 0x1000
ACCIDENT_INSTANCE_ID      = 0x0001
GET_RECORD_LIST_METHOD_ID = 0x0001
PAYLOAD_ENCODING          = "utf-8"
VEHICLE_ID                = 1
