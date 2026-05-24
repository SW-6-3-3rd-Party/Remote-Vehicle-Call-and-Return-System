import argparse
import socket
import sys
import time
from pathlib import Path


if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from pc_backend.diagnostics import MAIN_DIDS, read_did
from pc_backend.doip import DoipClient, DoipError, hex_bytes


DEFAULT_HOST = "192.168.10.2"
DEFAULT_PORT = 13400
DEFAULT_TARGET_ADDRESS = 0x0001
DEFAULT_TESTER_ADDRESS = 0x0E00
DEFAULT_SESSION = 0x03


def parse_int(value):
    return int(value, 0)


def check_tcp_port(host, port, timeout):
    started = time.time()
    with socket.create_connection((host, port), timeout=timeout):
        return int((time.time() - started) * 1000)


def print_step(title):
    print()
    print(f"== {title} ==")


def main():
    parser = argparse.ArgumentParser(
        description="Direct MAIN ECU DoIP/UDS DID test from PC."
    )
    parser.add_argument("--host", default=DEFAULT_HOST, help="MAIN ECU IP address")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="DoIP TCP port")
    parser.add_argument(
        "--target",
        type=parse_int,
        default=DEFAULT_TARGET_ADDRESS,
        help="DoIP target logical address, e.g. 0x0001",
    )
    parser.add_argument(
        "--tester",
        type=parse_int,
        default=DEFAULT_TESTER_ADDRESS,
        help="DoIP tester/source logical address, e.g. 0x0E00",
    )
    parser.add_argument(
        "--session",
        type=parse_int,
        default=DEFAULT_SESSION,
        help="UDS DiagnosticSessionControl value, e.g. 0x03",
    )
    parser.add_argument("--timeout", type=float, default=5.0, help="Timeout in seconds")
    parser.add_argument(
        "--skip-session",
        action="store_true",
        help="Skip UDS DiagnosticSessionControl.",
    )
    args = parser.parse_args()

    print("MAIN ECU DoIP direct test")
    print(f"host          : {args.host}")
    print(f"port          : {args.port}")
    print(f"tester/source : 0x{args.tester:04X}")
    print(f"target        : 0x{args.target:04X}")
    print("protocol      : 0x02/0xFD")
    print(f"session       : 0x{args.session:02X}")
    print(f"timeout       : {args.timeout:.1f}s")

    print_step("TCP connect")
    try:
        elapsed = check_tcp_port(args.host, args.port, args.timeout)
    except OSError as exc:
        print(f"FAIL: TCP {args.host}:{args.port} 연결 실패: {exc}")
        print()
        print("확인할 것:")
        print("- PC가 RC_CAR_WIFI에 연결되어 있는지")
        print("- Windows에서 `ping 192.168.10.2`가 되는지")
        print("- MAIN ECU에서 DoIP TCP 13400 listen 상태인지")
        print("- TCU Pi 라우팅/iptables가 PC -> 192.168.10.2:13400 TCP를 허용하는지")
        return 1

    print(f"OK: TCP 연결 가능 ({elapsed} ms)")

    try:
        with DoipClient(
            args.host,
            args.port,
            args.target,
            timeout=args.timeout,
            tester_address=args.tester,
        ) as client:
            print_step("Routing Activation")
            activation = client.routing_activation()
            print(f"source_address : 0x{activation.source_address:04X}")
            print(f"logical_address: 0x{activation.target_address:04X}")
            print(f"response_code  : 0x{activation.response_code:02X}")
            print(f"reserved       : {hex_bytes(activation.reserved)}")

            if activation.response_code != 0x10:
                print("FAIL: Routing Activation denied 또는 비정상 응답입니다.")
                print("확인할 것:")
                print("- 차량이 정차 상태이고 기어가 P인지")
                print("- MAIN ECU가 진단 모드 진입을 허용하는 상태인지")
                print("- MAIN ECU logical_address=0x0001, allowed SA=0x0E00인지")
                return 1

            if not args.skip_session:
                print_step("UDS SessionControl")
                session = client.change_session(args.session)
                print(f"raw: {hex_bytes(session)}")
                if len(session) < 2 or session[0] != 0x50 or session[1] != args.session:
                    print(f"FAIL: Session 0x{args.session:02X} 진입 응답이 아닙니다.")
                    return 1

            print_step("ReadDataByIdentifier")
            failures = 0
            for did, label, decoder, *rest in MAIN_DIDS:
                unit = rest[0] if rest else None
                try:
                    result = read_did(client, did, label, decoder, unit)
                except Exception as exc:
                    failures += 1
                    print(f"0x{did:04X} {label}: FAIL ({exc})")
                    continue

                suffix = f" {result['unit']}" if result.get("unit") else ""
                state = "OK" if result["ok"] else "FAIL"
                print(
                    f"{result['did']} {label}: {state}, "
                    f"value={result['value']}{suffix}, raw={result['raw']}"
                )
                if not result["ok"]:
                    failures += 1

            print_step("DTC")
            dtc = client.read_dtc_by_status_mask(0xFF)
            print(f"raw: {hex_bytes(dtc)}")

            if failures:
                print()
                print(f"Done with {failures} DID failure(s).")
                return 1

            print()
            print("PASS: MAIN ECU DID test completed.")
            return 0

    except DoipError as exc:
        print(f"FAIL: {exc}")
        return 1
    except Exception as exc:
        print(f"FAIL: {type(exc).__name__}: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
