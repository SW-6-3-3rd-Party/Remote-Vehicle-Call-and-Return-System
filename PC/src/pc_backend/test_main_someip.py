import argparse
import sys
from pathlib import Path


if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from pc_backend.someip_client import (
    INSTANCE_ID,
    MAIN_BUZZER_CONTROL_METHOD_ID,
    MAIN_BUZZER_SERVICE_ID,
    MAIN_SOMEIP_MINOR_VERSION,
    SomeIpError,
    set_warning_light,
)


def parse_state(value):
    state = int(value, 0)
    if state not in (0, 1):
        raise argparse.ArgumentTypeError("state must be 0 or 1")
    return state


def main():
    parser = argparse.ArgumentParser(
        description="Direct PC -> MAIN SOME/IP test using someipy."
    )
    parser.add_argument(
        "--state",
        type=parse_state,
        default=1,
        help="Warning/buzzer state to send to MAIN, 0 or 1.",
    )
    args = parser.parse_args()

    print("MAIN SOME/IP direct test")
    print(f"service_id    : 0x{MAIN_BUZZER_SERVICE_ID:04X}")
    print(f"instance_id   : 0x{INSTANCE_ID:04X}")
    print(f"method_id     : 0x{MAIN_BUZZER_CONTROL_METHOD_ID:04X}")
    print(f"minor_version : 0x{MAIN_SOMEIP_MINOR_VERSION:08X}")
    print(f"payload       : vehicle_id=1, state={args.state}")
    print()

    try:
        result = set_warning_light(args.state)
    except SomeIpError as exc:
        print(f"FAIL: {exc}")
        return 1

    print("PASS: MAIN SOME/IP response received")
    print(f"result        : {result['result']}")
    print(f"vehicle_id    : {result['vehicle_id']}")
    print(f"state         : {result['state']}")
    print(f"raw           : {result['raw']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
