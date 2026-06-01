import argparse
import time

import can


CAN_ID_CMD = 0x100
CAN_ID_STATUS = 0x200

GEAR_P = 0
GEAR_D = 3

STEER_CENTER = 0

MODE_STANDBY = 0
MODE_REMOTE = 1

SAFETY_NORMAL = 0
SAFETY_FORCE_STOP = 1


def open_bus(args):
    return can.Bus(
        interface=args.interface,
        channel=args.channel,
        fd=True,
        f_clock_mhz=80,
        nom_brp=2,
        nom_tseg1=63,
        nom_tseg2=16,
        nom_sjw=16,
        data_brp=2,
        data_tseg1=15,
        data_tseg2=4,
        data_sjw=4,
    )


def send_act_command(bus, accel, steering, brake, gear, mode, safety):
    msg = can.Message(
        arbitration_id=CAN_ID_CMD,
        data=[
            accel & 0xFF,
            steering & 0xFF,
            brake & 0xFF,
            gear & 0xFF,
            mode & 0xFF,
            safety & 0xFF,
        ],
        is_extended_id=False,
        is_fd=True,
        bitrate_switch=True,
        check=True,
    )
    bus.send(msg)


def print_speed(msg, scale):
    data = bytes(msg.data)

    if msg.arbitration_id != CAN_ID_STATUS or len(data) < 2:
        return

    speed_raw = data[0] | (data[1] << 8)
    speed_kmh = speed_raw / scale

    print(f"raw={speed_raw:5d}  speed={speed_kmh:.4f} km/h")


def send_safe_stop(bus):
    for _ in range(20):
        send_act_command(
            bus,
            accel=0,
            steering=STEER_CENTER,
            brake=1,
            gear=GEAR_P,
            mode=MODE_STANDBY,
            safety=SAFETY_FORCE_STOP,
        )
        time.sleep(0.02)


def main():
    parser = argparse.ArgumentParser(description="ACT forward speed feedback test")
    parser.add_argument("--interface", default="pcan")
    parser.add_argument("--channel", default="PCAN_USBBUS1")
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--scale", type=float, default=10000.0)
    args = parser.parse_args()

    bus = open_bus(args)

    try:
        bus.set_filters([{"can_id": CAN_ID_STATUS, "can_mask": 0x7FF, "extended": False}])
    except Exception:
        pass

    print(f"FORWARD {args.duration:.1f}s")
    print("Receiving ACT speed feedback from CAN ID 0x200")

    try:
        end_time = time.monotonic() + args.duration
        next_tx_time = 0.0

        while time.monotonic() < end_time:
            now = time.monotonic()

            if now >= next_tx_time:
                send_act_command(
                    bus,
                    accel=1,
                    steering=STEER_CENTER,
                    brake=0,
                    gear=GEAR_D,
                    mode=MODE_REMOTE,
                    safety=SAFETY_NORMAL,
                )
                next_tx_time = now + 0.02

            msg = bus.recv(timeout=0.001)

            if msg is not None:
                print_speed(msg, args.scale)

            time.sleep(0.001)

    finally:
        print("SAFE STOP")
        send_safe_stop(bus)
        bus.shutdown()


if __name__ == "__main__":
    main()
