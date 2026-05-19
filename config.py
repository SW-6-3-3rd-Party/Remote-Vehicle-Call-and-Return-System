# =========================
# Gateway Network Config
# =========================

# PC가 바라보는 Gateway 주소
GATEWAY_WIFI_IP = "192.168.201.1"

# Media RPi와 연결되는 Gateway eth0 주소
GATEWAY_ETH_IP = "192.168.20.1"

# Media RPi eth0 주소
MEDIA_INTERFACE_IP = "192.168.20.2"

# =========================
# SOME/IP Config
# =========================

SOMEIP_SERVICE_PORT = 30491
SOMEIP_CLIENT_PORT = 30492

ACCIDENT_SERVICE_ID = 0x1000
ACCIDENT_INSTANCE_ID = 0x0001

GET_RECORD_LIST_METHOD_ID = 0x0001
GET_RECORD_DETAIL_METHOD_ID = 0x0002
DELETE_RECORD_METHOD_ID = 0x0003

PAYLOAD_ENCODING = "utf-8"

# =========================
# HTTP / Proxy Config
# =========================

MEDIA_HTTP_PORT = 8088
GATEWAY_PROXY_PORT = 5000

# =========================
# MAIN / Buzzer SOME/IP Config
# =========================

# 문서 기준: Gateway eth0.10 -> MAIN ECU
GATEWAY_MAIN_ETH_IP = "192.168.10.1"
MAIN_ECU_IP = "192.168.10.2"

BUZZER_SOMEIP_CLIENT_PORT = 30492

BUZZER_SERVICE_ID = 0x2001
BUZZER_INSTANCE_ID = 0x0001
SET_BUZZER_STATE_METHOD_ID = 0x0001
