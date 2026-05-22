# DoIP Protocol Constants

DOIP_PROTOCOL_VERSION = 0x02
DOIP_PROTOCOL_VERSION_INV = 0xFD
DOIP_HEADER_LEN = 8

DOIP_UDP_PORT = 13401
DOIP_TCP_PORT = 13401
DOIP_UDP_DISCOVERY_PORT = 13401

# ---------------------------------------------------------------------------
# Payload Types (ISO 13400-2 Table 18)
# ---------------------------------------------------------------------------
class PayloadType:
    GENERIC_NACK                          = 0x0000
    VEHICLE_ID_REQUEST                    = 0x0001
    VEHICLE_ID_REQUEST_WITH_EID           = 0x0002
    VEHICLE_ID_REQUEST_WITH_VIN           = 0x0003
    VEHICLE_ANNOUNCEMENT                  = 0x0004  # also Identification Response
    ROUTING_ACTIVATION_REQUEST            = 0x0005
    ROUTING_ACTIVATION_RESPONSE           = 0x0006
    ALIVE_CHECK_REQUEST                   = 0x0007
    ALIVE_CHECK_RESPONSE                  = 0x0008
    ENTITY_STATUS_REQUEST                 = 0x4001
    ENTITY_STATUS_RESPONSE                = 0x4002
    POWER_MODE_INFO_REQUEST               = 0x4003
    POWER_MODE_INFO_RESPONSE              = 0x4004
    DIAGNOSTIC_MESSAGE                    = 0x8001
    DIAGNOSTIC_MESSAGE_POSITIVE_ACK       = 0x8002
    DIAGNOSTIC_MESSAGE_NEGATIVE_ACK       = 0x8003

# ---------------------------------------------------------------------------
# Generic Header NACK Codes (ISO 13400-2 Table 19)
# ---------------------------------------------------------------------------
class GenericNackCode:
    INCORRECT_PATTERN_FORMAT              = 0x00
    UNKNOWN_PAYLOAD_TYPE                  = 0x01
    MESSAGE_TOO_LARGE                     = 0x02
    OUT_OF_MEMORY                         = 0x03
    INVALID_PAYLOAD_LENGTH                = 0x04

# ---------------------------------------------------------------------------
# Routing Activation Response Codes (ISO 13400-2 Table 27)
# ---------------------------------------------------------------------------
class RoutingActivationResponseCode:
    DENIED_UNKNOWN_SA                     = 0x00
    DENIED_ALL_SOCKETS_REGISTERED         = 0x01
    DENIED_SA_MISMATCH                    = 0x02
    DENIED_SA_ALREADY_ACTIVE              = 0x03
    DENIED_MISSING_AUTH                   = 0x04
    DENIED_REJECTED_CONFIRMATION          = 0x05
    DENIED_UNSUPPORTED_TYPE               = 0x06
    SUCCESS                               = 0x10
    SUCCESS_CONFIRMATION_REQUIRED         = 0x11

# ---------------------------------------------------------------------------
# Routing Activation Types (ISO 13400-2 Table 26)
# ---------------------------------------------------------------------------
class RoutingActivationType:
    DEFAULT                               = 0x00
    WWH_OBD                               = 0x01
    CENTRAL_SECURITY                      = 0xE0

# ---------------------------------------------------------------------------
# Diagnostic Message NACK Codes (ISO 13400-2 Table 38)
# ---------------------------------------------------------------------------
class DiagnosticNackCode:
    INVALID_SA                            = 0x02
    UNKNOWN_TA                            = 0x03
    MESSAGE_TOO_LARGE                     = 0x04
    OUT_OF_MEMORY                         = 0x05
    TARGET_UNREACHABLE                    = 0x06
    UNKNOWN_NETWORK                       = 0x07
    TP_ERROR                              = 0x08

# ---------------------------------------------------------------------------
# Diagnostic Power Mode (ISO 13400-2 Table 33)
# ---------------------------------------------------------------------------
class PowerMode:
    NOT_READY                             = 0x00
    READY                                 = 0x01
    NOT_SUPPORTED                         = 0x02

# ---------------------------------------------------------------------------
# Node Type for Entity Status Response (ISO 13400-2 Table 31)
# ---------------------------------------------------------------------------
class NodeType:
    DOIP_GATEWAY                          = 0x00
    DOIP_NODE                             = 0x01

# ---------------------------------------------------------------------------
# Further Action Required (Vehicle Announcement, ISO 13400-2 Table 24)
# ---------------------------------------------------------------------------
class FurtherAction:
    NO_FURTHER_ACTION                     = 0x00
    RESERVED_BY_ISO                       = 0x01  # 0x01-0x0F reserved
    ROUTING_ACTIVATION_REQUIRED           = 0x10  # 0x10-0xFF OEM specific

# ---------------------------------------------------------------------------
# Logical Addresses — project-specific configuration
# ---------------------------------------------------------------------------
class LogicalAddress:
    GATEWAY_RPi2 = 0x0030   # RPi #2 Media Device Node
    TESTER       = 0x0E00   # PC diagnostic tool (tester)
    BROADCAST    = 0xFFFF


# ---------------------------------------------------------------------------
# Data Identifiers — RPi #2 (UDS 0x22)
# ---------------------------------------------------------------------------
class DataIdentifier:
    NODE_ID        = 0xF190   # 4B:  "MED1" (고정값)
    ETH0_STATUS    = 0x0300   # 1B:  0=DOWN / 1=UP
    USB1_STATUS    = 0x0301   # 1B:  0=미연결 / 1=연결  (/dev/video0, 전방 USB)
    USB_STATUS     = 0x0302   # 1B:  0=미연결 / 1=연결  (/dev/video2, 후방)
    MIC_STATUS     = 0x0303   # 1B:  0=미연결 / 1=연결  (/proc/asound/card0)
    FLASK_STATUS   = 0x0304   # 1B:  0=중지 / 1=실행중  (TCP 8080)
    SOMEIP_STATUS  = 0x0305   # 1B:  0=중지 / 1=실행중  (UDP 30491)
    STORAGE        = 0x0306   # 1B:  사용률 % (0~100)
