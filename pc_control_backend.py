import socket
import threading
import time
from collections import deque

from flask import Flask, jsonify, request

from src.pc_backend.diagnostics import (
    clear_all_dtcs,
    run_full_scan,
    run_function_test,
    scan_main_routed_ecus,
    scan_media_pi,
    test_main_routing_activation,
)
from src.pc_backend.someip_client import (
    SomeIpError,
    get_accident_list,
    get_warning_light,
    set_warning_light,
)

app = Flask(__name__)

MAIN_ECU_IP = "192.168.10.2"
MAIN_CONTROL_PORT = 5000
PC_SPEED_PORT = 5001
PC_EVENT_PORT = 5002
HTTP_PORT = 5000

EVENT_TYPES = {
    1: "COLLISION",
    2: "COMM_WARN",
    3: "COMM_LOST",
    4: "COMM_RESTORED",
    5: "DIAG_MODE",
    6: "DRIVE_MODE",
    7: "CAN1_LOST",
    8: "CAN1_RESTORED",
    9: "CAN2_LOST",
    10: "CAN2_RESTORED",
}

control_state_lock = threading.Lock()

control_state = {
    "sequence_number": 0,
    "accel": 0,
    "brake": 0,
    "steer": 0,
    "gear": 0,          # 0=P, 1=R, 2=N, 3=D
    "turn_signal": 0,   # 0=OFF, 1=LEFT, 2=RIGHT, 3=HAZARD
    "horn": 0,
    "ignition": 0,
    "head_light": 0,
}

vehicle_state = {
    "vehicle_id": 1,
    "remote_control_active": False,
    "driving_state": 0,
}

latest_speed_lock = threading.Lock()
latest_speed_kmh = None
last_speed_received_at = 0.0

event_lock = threading.Lock()
event_history = deque(maxlen=50)
latest_event = None


@app.after_request
def add_cors_headers(response):
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type"
    return response


def reset_control_state():
    with control_state_lock:
        control_state["accel"] = 0
        control_state["brake"] = 0
        control_state["steer"] = 0
        control_state["gear"] = 0
        control_state["turn_signal"] = 0
        control_state["horn"] = 0
        control_state["ignition"] = 0
        control_state["head_light"] = 0


def build_control_packet() -> bytes:
    with control_state_lock:
        seq = control_state["sequence_number"] & 0xFF

        packet = bytes([
            seq,
            control_state["accel"] & 0xFF,
            control_state["brake"] & 0xFF,
            control_state["steer"] & 0xFF,
            control_state["gear"] & 0xFF,
            control_state["turn_signal"] & 0xFF,
            control_state["horn"] & 0xFF,
            control_state["ignition"] & 0xFF,
            control_state["head_light"] & 0xFF,
            0x00, 0x00, 0x00, 0x00,  # HMAC 임시 0
        ])

        control_state["sequence_number"] = (seq + 1) & 0xFF

    return packet


def udp_control_sender_loop():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(f"[PC Backend] UDP control sender started: {MAIN_ECU_IP}:{MAIN_CONTROL_PORT}")

    while True:
        try:
            packet = build_control_packet()
            sock.sendto(packet, (MAIN_ECU_IP, MAIN_CONTROL_PORT))
        except OSError as error:
            print(f"[PC Backend] UDP control send error: {error}")

        time.sleep(0.02)


def udp_speed_receiver_loop():
    global latest_speed_kmh, last_speed_received_at

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", PC_SPEED_PORT))

    print(f"[PC Backend] UDP speed receiver started: 0.0.0.0:{PC_SPEED_PORT}")

    while True:
        try:
            data, addr = sock.recvfrom(64)

            if len(data) < 3:
                continue

            speed_raw = data[1] | (data[2] << 8)
            speed_kmh = speed_raw / 100.0

            with latest_speed_lock:
                latest_speed_kmh = speed_kmh
                last_speed_received_at = time.time()

        except OSError as error:
            print(f"[PC Backend] UDP speed receive error: {error}")


def parse_event_packet(data: bytes, addr):
    if len(data) < 6:
        return None

    event_type = data[0]
    event_data = data[1]
    timestamp_ms = int.from_bytes(data[2:6], "big")

    return {
        "event_type": event_type,
        "event_name": EVENT_TYPES.get(event_type, "UNKNOWN"),
        "event_data": event_data,
        "timestamp_ms": timestamp_ms,
        "source_ip": addr[0],
        "source_port": addr[1],
        "received_at": time.time(),
        "raw": data[:6].hex(" ").upper(),
    }


def udp_event_receiver_loop():
    global latest_event

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", PC_EVENT_PORT))

    print(f"[PC Backend] UDP event receiver started: 0.0.0.0:{PC_EVENT_PORT}")

    while True:
        try:
            data, addr = sock.recvfrom(64)
            if addr[0] != MAIN_ECU_IP:
                print(f"[PC Backend] blocked event from unauthorized source: {addr[0]}")
                continue

            event = parse_event_packet(data, addr)
            if event is None:
                continue

            with event_lock:
                latest_event = event
                event_history.appendleft(event)

            print(
                "[PC Backend] event "
                f"{event['event_name']} from {addr[0]}:{addr[1]} "
                f"timestamp={event['timestamp_ms']}"
            )

        except OSError as error:
            print(f"[PC Backend] UDP event receive error: {error}")


def json_call(func, *args, **kwargs):
    try:
        return jsonify(func(*args, **kwargs))
    except Exception as error:
        return jsonify({
            "result": "ERROR",
            "detail": str(error),
        }), 502


@app.get("/health")
def health_check():
    return jsonify({
        "result": "OK",
        "role": "pc-control-backend",
        "main_ip": MAIN_ECU_IP,
        "main_control_port": MAIN_CONTROL_PORT,
        "speed_port": PC_SPEED_PORT,
        "event_port": PC_EVENT_PORT,
    })


@app.get("/vehicle/status")
def get_vehicle_status():
    now = time.time()

    with latest_speed_lock:
        speed = latest_speed_kmh
        speed_age = (
            now - last_speed_received_at
            if last_speed_received_at > 0
            else None
        )

    speed_connected = speed_age is not None and speed_age < 0.5

    if speed_connected and speed is not None:
        driving_state = 1 if speed > 0 else 0
        display_speed = round(speed, 2)
    else:
        driving_state = 0
        display_speed = None

    vehicle_state["driving_state"] = driving_state

    with event_lock:
        event = latest_event

    return jsonify({
        "result": "OK",
        "vehicle_id": vehicle_state["vehicle_id"],
        "driving_state": driving_state,
        "driving_state_text": "주행 중" if driving_state == 1 else "정지 중",
        "remote_control_active": vehicle_state["remote_control_active"],
        "remote_control_text": "ON" if vehicle_state["remote_control_active"] else "OFF",
        "speed": display_speed,
        "speed_connected": speed_connected,
        "latest_event": event,
    })


@app.get("/events")
def get_events():
    with event_lock:
        events = list(event_history)

    return jsonify({
        "result": "OK",
        "events": events,
    })


@app.get("/events/latest")
def get_latest_event():
    with event_lock:
        event = latest_event

    return jsonify({
        "result": "OK",
        "event": event,
    })


@app.post("/remote-control/start")
def start_remote_control():
    vehicle_state["remote_control_active"] = True
    vehicle_state["driving_state"] = 0
    reset_control_state()

    return jsonify({
        "result": "OK",
        "remote_control_active": True,
        "remote_control_text": "ON",
    })


@app.post("/remote-control/stop")
def stop_remote_control():
    vehicle_state["remote_control_active"] = False
    vehicle_state["driving_state"] = 0
    reset_control_state()

    return jsonify({
        "result": "OK",
        "remote_control_active": False,
        "remote_control_text": "OFF",
    })


@app.post("/control")
def control_command():
    command_data = request.get_json(silent=True)

    if not command_data:
        return jsonify({
            "result": "BAD_REQUEST",
            "error_code": 1,
            "message": "control command body is empty",
        }), 400

    command_type = command_data.get("type")
    value = command_data.get("value")

    if command_type is None:
        return jsonify({
            "result": "BAD_REQUEST",
            "error_code": 1,
            "message": "type field is required",
        }), 400

    valid_drive_values = [
        "FORWARD",
        "BACKWARD",
        "LEFT",
        "RIGHT",
        "FORWARD_LEFT",
        "FORWARD_RIGHT",
        "BACKWARD_LEFT",
        "BACKWARD_RIGHT",
        "STOP",
    ]

    with control_state_lock:
        if command_type == "drive":
            if value not in valid_drive_values:
                return jsonify({
                    "result": "BAD_REQUEST",
                    "error_code": 1,
                    "message": f"invalid drive value: {value}",
                }), 400

            control_state["accel"] = 0
            control_state["steer"] = 0

            if value == "FORWARD":
                control_state["accel"] = 1
                control_state["gear"] = 3
                vehicle_state["driving_state"] = 1

            elif value == "BACKWARD":
                control_state["accel"] = 1
                control_state["gear"] = 1
                vehicle_state["driving_state"] = 1

            elif value == "LEFT":
                control_state["steer"] = 1

            elif value == "RIGHT":
                control_state["steer"] = 2

            elif value == "FORWARD_LEFT":
                control_state["accel"] = 1
                control_state["gear"] = 3
                control_state["steer"] = 1
                vehicle_state["driving_state"] = 1

            elif value == "FORWARD_RIGHT":
                control_state["accel"] = 1
                control_state["gear"] = 3
                control_state["steer"] = 2
                vehicle_state["driving_state"] = 1

            elif value == "BACKWARD_LEFT":
                control_state["accel"] = 1
                control_state["gear"] = 1
                control_state["steer"] = 1
                vehicle_state["driving_state"] = 1

            elif value == "BACKWARD_RIGHT":
                control_state["accel"] = 1
                control_state["gear"] = 1
                control_state["steer"] = 2
                vehicle_state["driving_state"] = 1

            elif value == "STOP":
                control_state["accel"] = 0
                control_state["steer"] = 0
                vehicle_state["driving_state"] = 0

        elif command_type == "brake":
            control_state["brake"] = 1 if value == "ON" else 0
            if value == "ON":
                vehicle_state["driving_state"] = 0

        elif command_type == "gear":
            gear_map = {
                "P": 0,
                "R": 1,
                "N": 2,
                "D": 3,
            }

            if value not in gear_map:
                return jsonify({
                    "result": "BAD_REQUEST",
                    "error_code": 1,
                    "message": f"invalid gear value: {value}",
                }), 400

            control_state["gear"] = gear_map[value]

        elif command_type == "indicator_left":
            if value == "ON":
                control_state["turn_signal"] = 1
            elif control_state["turn_signal"] == 1:
                control_state["turn_signal"] = 0

        elif command_type == "indicator_right":
            if value == "ON":
                control_state["turn_signal"] = 2
            elif control_state["turn_signal"] == 2:
                control_state["turn_signal"] = 0

        elif command_type == "hazard":
            control_state["turn_signal"] = 3 if value == "ON" else 0

        elif command_type == "horn":
            control_state["horn"] = 1 if value == "ON" else 0

        elif command_type == "ignition":
            control_state["ignition"] = 1 if value == "ON" else 0

        elif command_type == "head_light":
            control_state["head_light"] = 1 if value == "ON" else 0

        else:
            return jsonify({
                "result": "BAD_REQUEST",
                "error_code": 1,
                "message": f"unknown control type: {command_type}",
            }), 400

    return jsonify({
        "result": "OK",
        "command": command_data,
    })


@app.post("/body/warning-light")
def set_body_warning_light():
    request_data = request.get_json(silent=True)

    if not request_data or "enable" not in request_data:
        return jsonify({
            "result": "BAD_REQUEST",
            "error_code": 1,
            "message": "enable field is required",
        }), 400

    try:
        enable = int(request_data.get("enable"))
    except (TypeError, ValueError):
        return jsonify({
            "result": "BAD_REQUEST",
            "error_code": 1,
            "message": "enable must be 0 or 1",
        }), 400

    if enable not in (0, 1):
        return jsonify({
            "result": "BAD_REQUEST",
            "error_code": 1,
            "message": "enable must be 0 or 1",
        }), 400

    try:
        response_data = set_warning_light(enable)
    except SomeIpError as error:
        return jsonify({
            "result": "INTERNAL_ERROR",
            "error_code": 2,
            "message": "Failed to call CollisionWarningLightService",
            "detail": str(error),
        }), 502

    return jsonify(response_data)


@app.get("/body/warning-light")
def get_body_warning_light():
    try:
        response_data = get_warning_light()
    except SomeIpError as error:
        return jsonify({
            "result": "INTERNAL_ERROR",
            "error_code": 2,
            "message": "Failed to call CollisionWarningLightService",
            "detail": str(error),
        }), 502

    return jsonify(response_data)


@app.get("/accidents")
def get_accidents():
    try:
        response_data = get_accident_list()
    except SomeIpError as error:
        return jsonify({
            "result": "INTERNAL_ERROR",
            "error_code": 2,
            "accident_count": 0,
            "accidents": [],
            "message": "Failed to call AccidentHistoryService",
            "detail": str(error),
        }), 502

    return jsonify(response_data)


@app.get("/diagnostics/media")
def diagnostics_media():
    return json_call(scan_media_pi)


@app.get("/diagnostics/main/routing-activation")
def diagnostics_main_routing_activation():
    return json_call(test_main_routing_activation)


@app.get("/diagnostics/main/ecus")
def diagnostics_main_ecus():
    return json_call(scan_main_routed_ecus)


@app.get("/diagnostics/run")
def diagnostics_run():
    return json_call(run_full_scan)


@app.post("/diagnostics/dtc/clear")
def diagnostics_clear_dtc():
    return json_call(clear_all_dtcs)


@app.post("/diagnostics/routine")
def diagnostics_routine():
    request_data = request.get_json(silent=True) or {}
    return json_call(run_function_test, request_data.get("test_id", ""))


if __name__ == "__main__":
    threading.Thread(target=udp_control_sender_loop, daemon=True).start()
    threading.Thread(target=udp_speed_receiver_loop, daemon=True).start()
    threading.Thread(target=udp_event_receiver_loop, daemon=True).start()

    app.run(
        host="127.0.0.1",
        port=HTTP_PORT,
        debug=True,
        threaded=True,
        use_reloader=False,
    )
