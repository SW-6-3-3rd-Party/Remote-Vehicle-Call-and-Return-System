# =========================
# Gateway Network Config
# =========================

# PC가 바라보는 Gateway 주소
GATEWAY_WIFI_IP = "192.168.1.1"

# Media RPi와 연결되는 Gateway eth0 주소
GATEWAY_ETH_IP = "192.168.20.1"

# Media RPi eth0 주소
MEDIA_INTERFACE_IP = "192.168.20.2"

# 문서 기준: Gateway eth0.10 -> MAIN ECU
GATEWAY_MAIN_ETH_IP = "192.168.10.1"
MAIN_ECU_IP = "192.168.10.2"

# =========================
# Accident Event Proxy Config
# =========================

EVENT_PROXY_LISTEN_IP = GATEWAY_MAIN_ETH_IP
EVENT_PROXY_LISTEN_PORT = 5010
EVENT_PROXY_MEDIA_IP = MEDIA_INTERFACE_IP
EVENT_PROXY_MEDIA_PORT = 5011

# =========================
# MAIN / UDP Control Config
# =========================

MAIN_CONTROL_PORT=5000
MAIN_SPEED_PORT=5001
