import argparse
import json
import socket
import sys
import time
from pathlib import Path


if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from pc_backend.someip_client import (
    ACCIDENT_HISTORY_SERVICE_ID,
    GET_ACCIDENT_LIST_METHOD_ID,
    INSTANCE_ID,
    MEDIA_PI_IP,
    MEDIA_SOMEIP_MINOR_VERSION,
    PC_CLIENT_ID,
    PC_SOMEIP_CLIENT_PORT,
    SOMEIP_SD_MULTICAST_IP,
    SOMEIP_SD_PORT,
    VEHICLE_ID,
    SomeIpClient,
    SomeIpError,
    _detect_vehicle_interface_ip,
    _ensure_someipyd_running,
    _find_someipyd,
    _write_someipyd_config,
)


MEDIA_SOMEIP_SERVICE_PORT = 30491


def print_section(title):
    print()
    print(f"== {title} ==")


def route_probe(host):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect((host, 1))
        return sock.getsockname()[0]


def build_someip_request(payload):
    request_id = ((PC_CLIENT_ID & 0xFFFF) << 16) | 1
    header = bytearray()
    header += int(ACCIDENT_HISTORY_SERVICE_ID).to_bytes(2, "big")
    header += int(GET_ACCIDENT_LIST_METHOD_ID).to_bytes(2, "big")
    header += int(8 + len(payload)).to_bytes(4, "big")
    header += request_id.to_bytes(4, "big")
    header += bytes([0x01, 0x01, 0x00, 0x00])
    return bytes(header) + payload


def parse_someip_response(data):
    if len(data) < 16:
        raise ValueError(f"SOME/IP response too short: {data.hex(' ').upper()}")

    service_id = int.from_bytes(data[0:2], "big")
    method_id = int.from_bytes(data[2:4], "big")
    length = int.from_bytes(data[4:8], "big")
    client_id = int.from_bytes(data[8:10], "big")
    session_id = int.from_bytes(data[10:12], "big")
    protocol_version = data[12]
    interface_version = data[13]
    message_type = data[14]
    return_code = data[15]
    payload = data[16:]

    return {
        "service_id": service_id,
        "method_id": method_id,
        "length": length,
        "client_id": client_id,
        "session_id": session_id,
        "protocol_version": protocol_version,
        "interface_version": interface_version,
        "message_type": message_type,
        "return_code": return_code,
        "payload": payload,
    }


def run_direct_udp(timeout, direct_port):
    payload = json.dumps({"vehicle_id": VEHICLE_ID}).encode("utf-8")
    packet = build_someip_request(payload)

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.bind(("", direct_port))
        local_ip, local_port = sock.getsockname()
        print(f"local endpoint : {local_ip}:{local_port}")
        print(f"remote endpoint: {MEDIA_PI_IP}:{MEDIA_SOMEIP_SERVICE_PORT}")
        print(f"tx raw         : {packet.hex(' ').upper()}")
        started = time.time()
        sock.sendto(packet, (MEDIA_PI_IP, MEDIA_SOMEIP_SERVICE_PORT))
        data, addr = sock.recvfrom(65535)
        elapsed_ms = int((time.time() - started) * 1000)

    parsed = parse_someip_response(data)
    print(f"rx from        : {addr[0]}:{addr[1]} ({elapsed_ms} ms)")
    print(f"rx raw         : {data.hex(' ').upper()}")
    print(
        "header         : "
        f"sid=0x{parsed['service_id']:04X}, "
        f"mid=0x{parsed['method_id']:04X}, "
        f"type=0x{parsed['message_type']:02X}, "
        f"rc=0x{parsed['return_code']:02X}"
    )

    if parsed["payload"]:
        print("payload        :", parsed["payload"].decode("utf-8", errors="replace"))
    else:
        print("payload        : <empty>")


def run_someipy(timeout):
    client = SomeIpClient(timeout=timeout)
    response = client.call_json(
        service_id=ACCIDENT_HISTORY_SERVICE_ID,
        method_id=GET_ACCIDENT_LIST_METHOD_ID,
        payload={"vehicle_id": VEHICLE_ID},
        minor_version=MEDIA_SOMEIP_MINOR_VERSION,
    )
    print(json.dumps(response, ensure_ascii=False, indent=2))


def main():
    parser = argparse.ArgumentParser(
        description="PC -> Media Pi AccidentHistoryService SOME/IP test."
    )
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument(
        "--direct-only",
        action="store_true",
        help="Skip someipy SD discovery and test direct UDP SOME/IP only.",
    )
    parser.add_argument(
        "--someipy-only",
        action="store_true",
        help="Skip direct UDP and test someipy/SD only.",
    )
    parser.add_argument(
        "--direct-port",
        type=int,
        default=0,
        help="Local UDP port for direct test. Default 0 uses an ephemeral port.",
    )
    args = parser.parse_args()

    print("Media Pi SOME/IP accident-history test")
    print(f"service_id     : 0x{ACCIDENT_HISTORY_SERVICE_ID:04X}")
    print(f"instance_id    : 0x{INSTANCE_ID:04X}")
    print(f"method_id      : 0x{GET_ACCIDENT_LIST_METHOD_ID:04X}")
    print(f"minor_version  : 0x{MEDIA_SOMEIP_MINOR_VERSION:08X}")
    print(f"media endpoint : {MEDIA_PI_IP}:{MEDIA_SOMEIP_SERVICE_PORT}")
    print(f"client_id      : 0x{PC_CLIENT_ID:04X}")
    print(f"client port    : {PC_SOMEIP_CLIENT_PORT}")
    print(f"sd multicast   : {SOMEIP_SD_MULTICAST_IP}:{SOMEIP_SD_PORT}")

    print_section("Local Config")
    someipyd = _find_someipyd()
    detected_ip = _detect_vehicle_interface_ip()
    print(f"someipyd       : {someipyd or '<not found>'}")
    print(f"route to media : {route_probe(MEDIA_PI_IP)}")
    print(f"detected iface : {detected_ip}")
    print(f"config path    : {_write_someipyd_config(detected_ip)}")

    if not args.someipy_only:
        print_section("Direct UDP Method Call")
        try:
            run_direct_udp(args.timeout, args.direct_port)
            print("DIRECT UDP PASS")
        except Exception as exc:
            print(f"DIRECT UDP FAIL: {exc}")

    if not args.direct_only:
        print_section("someipy SD Discovery + Method Call")
        try:
            _ensure_someipyd_running()
            run_someipy(args.timeout)
            print("SOMEIPY PASS")
        except SomeIpError as exc:
            print(f"SOMEIPY FAIL: {exc}")
        except Exception as exc:
            print(f"SOMEIPY FAIL: {type(exc).__name__}: {exc}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
