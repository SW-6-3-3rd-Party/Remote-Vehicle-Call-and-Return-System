import asyncio
import time
import socket
import threading
from urllib.parse import urlparse

import requests
from flask import Flask, Response, jsonify, request, stream_with_context

from config import (
    GATEWAY_WIFI_IP,
    GATEWAY_PROXY_PORT,
    MEDIA_INTERFACE_IP,
    MEDIA_HTTP_PORT,
    GATEWAY_MAIN_ETH_IP,
    MAIN_ECU_IP,
    MAIN_CONTROL_PORT,
    MAIN_SPEED_PORT,
)

from gateway_someip_client import (
    request_accident_list_from_media,
    request_buzzer_state_to_main,
)

from udp_video_bridge import UDPVideoBridge

app = Flask(__name__)

MEDIA_IP = "192.168.20.2"

front_bridge = UDPVideoBridge(MEDIA_IP, 5000)
rear_bridge = front_bridge

front_bridge.start()

vehicle_state = {
    "vehicle_id": 1,
    "driving_state": 0,
    "remote_control_active": False,
}

control_state_lock = threading.Lock()

control_state = {
    "sequence_number": 0,
    "accel": 0,
    "brake": 0,
    "steer": 0,
    "gear": 0,
    "turn_signal": 0,
    "horn": 0,
    "ignition": 0,
    "head_light": 0,
}

latest_speed_lock = threading.Lock()
latest_speed_kmh = None
last_speed_received_at = 0.0


@app.after_request
def add_cors_headers(response):
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type"
    return response


def generate_mjpeg_stream(bridge: UDPVideoBridge):
    last_sent_frame = None

    while True:
        frame, last_frame_time = bridge.get_latest_frame()

        if frame is None:
            time.sleep(0.05)
            continue

        if frame is last_sent_frame:
            time.sleep(0.03)
            continue

        last_sent_frame = frame

        yield (
            b"--frame\r\n"
            b"Content-Type: image/jpeg\r\n\r\n"
            + frame
            + b"\r\n"
        )


@app.get("/live/front")
def live_front():
    return Response(
        generate_mjpeg_stream(front_bridge),
        mimetype="multipart/x-mixed-replace; boundary=frame",
    )


@app.get("/live/rear")
def live_rear():
    return Response(
        generate_mjpeg_stream(rear_bridge),
        mimetype="multipart/x-mixed-replace; boundary=frame",
    )


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
    """
    MAIN으로 보낼 UDP 13바이트 binary packet 생성.

    Byte 0    sequence_number
    Byte 1    accel
    Byte 2    brake
    Byte 3    steer
    Byte 4    gear
    Byte 5    turn_signal
    Byte 6    horn
    Byte 7    ignition
    Byte 8    head_light
    Byte 9-12 hmac = 0x00000000
    """
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
            0x00,
            0x00,
            0x00,
            0x00,
        ])

        control_state["sequence_number"] = (seq + 1) & 0xFF

    return packet


def udp_control_sender_loop():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(
        f"[Gateway] UDP control sender started: "
        f"{MAIN_ECU_IP}:{MAIN_CONTROL_PORT}"
    )

    while True:
        try:
            packet = build_control_packet()
            sock.sendto(packet, (MAIN_ECU_IP, MAIN_CONTROL_PORT))
        except OSError as error:
            print(f"[Gateway] UDP control send error: {error}")

        time.sleep(0.02)


def udp_speed_receiver_loop():
    global latest_speed_kmh, last_speed_received_at

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((GATEWAY_MAIN_ETH_IP, MAIN_SPEED_PORT))

    print(
        f"[Gateway] UDP speed receiver started: "
        f"{GATEWAY_MAIN_ETH_IP}:{MAIN_SPEED_PORT}"
    )

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
            print(f"[Gateway] UDP speed receive error: {error}")


def make_gateway_video_url(video_path: str | None) -> str | None:
    if not video_path:
        return None

    if not video_path.startswith("/"):
        video_path = "/" + video_path

    return f"http://{GATEWAY_WIFI_IP}:{GATEWAY_PROXY_PORT}/proxy{video_path}"


def extract_path_from_video_url(video_url: str | None) -> str | None:
    if not video_url:
        return None

    parsed = urlparse(video_url)
    return parsed.path


def rewrite_accident_video_fields(response_data: dict) -> dict:
    if response_data.get("result") != "OK":
        return response_data

    for accident in response_data.get("accidents", []):
        front_path = accident.pop("front_video_path", None)
        rear_path = accident.pop("rear_video_path", None)
        video_path = accident.pop("video_path", None)

        old_video_url = accident.get("video_url")
        if video_path is None and old_video_url:
            video_path = extract_path_from_video_url(old_video_url)

        if front_path:
            accident["front_video_url"] = make_gateway_video_url(front_path)
        elif video_path:
            accident["front_video_url"] = make_gateway_video_url(video_path)
        else:
            accident["front_video_url"] = None

        if rear_path:
            accident["rear_video_url"] = make_gateway_video_url(rear_path)
        elif video_path:
            accident["rear_video_url"] = make_gateway_video_url(video_path)
        else:
            accident["rear_video_url"] = None

        accident["video_url"] = accident.get("front_video_url")

    return response_data


@app.get("/health")
def health_check():
    return jsonify({
        "status": "OK",
        "role": "gateway-proxy",
        "gateway_ip": GATEWAY_WIFI_IP,
        "media_ip": MEDIA_INTERFACE_IP,
    })


@app.get("/vehicle/status")
def get_vehicle_status():
    remote_control_active = vehicle_state["remote_control_active"]

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

    return jsonify({
        "result": "OK",
        "vehicle_id": vehicle_state["vehicle_id"],
        "driving_state": driving_state,
        "driving_state_text": "주행 중" if driving_state == 1 else "정지 중",
        "remote_control_active": remote_control_active,
        "remote_control_text": "ON" if remote_control_active else "OFF",
        "speed": display_speed,
        "speed_connected": speed_connected,
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
                control_state["steer"] = 0
                vehicle_state["driving_state"] = 1

            elif value == "BACKWARD":
                control_state["accel"] = 1
                control_state["gear"] = 1
                control_state["steer"] = 0
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


@app.post("/control/buzzer")
def control_buzzer():
    request_data = request.get_json(silent=True)

    if not request_data:
        return jsonify({
            "result": "INVALID_REQUEST",
            "error_code": 1,
            "message": "request body is empty",
        }), 400

    vehicle_id = request_data.get("vehicle_id", vehicle_state["vehicle_id"])
    buzzer_state = request_data.get("buzzer_state")

    try:
        vehicle_id = int(vehicle_id)
        buzzer_state = int(buzzer_state)
    except (TypeError, ValueError):
        return jsonify({
            "result": "INVALID_REQUEST",
            "error_code": 1,
            "vehicle_id": vehicle_id,
            "buzzer_state": buzzer_state,
            "message": "vehicle_id and buzzer_state must be integer",
        }), 400

    if buzzer_state not in [0, 1]:
        return jsonify({
            "result": "INVALID_REQUEST",
            "error_code": 1,
            "vehicle_id": vehicle_id,
            "buzzer_state": buzzer_state,
            "message": "buzzer_state must be 0 or 1",
        }), 400

    response_data = asyncio.run(
        request_buzzer_state_to_main(
            vehicle_id=vehicle_id,
            buzzer_state=buzzer_state,
        )
    )

    status_code = 200

    if response_data.get("result") == "INVALID_REQUEST":
        status_code = 400
    elif response_data.get("result") == "INTERNAL_ERROR":
        status_code = 502

    return jsonify(response_data), status_code


@app.get("/accidents")
def get_accidents():
    try:
        vehicle_id = int(request.args.get("vehicle_id", "1"))
    except ValueError:
        vehicle_id = 1

    response_data = asyncio.run(
        request_accident_list_from_media(vehicle_id)
    )

    response_data = rewrite_accident_video_fields(response_data)

    return jsonify(response_data)


@app.route("/proxy/<path:subpath>", methods=["GET"])
def proxy_media_file(subpath):
    target_url = f"http://{MEDIA_INTERFACE_IP}:{MEDIA_HTTP_PORT}/{subpath}"

    try:
        media_response = requests.get(
            target_url,
            stream=True,
            timeout=5.0,
        )

        content_type = media_response.headers.get(
            "Content-Type",
            "application/octet-stream",
        )

        return Response(
            stream_with_context(media_response.iter_content(chunk_size=8192)),
            status=media_response.status_code,
            content_type=content_type,
        )

    except requests.RequestException as error:
        return jsonify({
            "result": "INTERNAL_ERROR",
            "error_code": 2,
            "message": "Failed to proxy media file",
            "detail": str(error),
            "target_url": target_url,
        }), 502


if __name__ == "__main__":
    print("[Gateway Proxy] Start HTTP proxy server")
    print(
        f"[Gateway Proxy] API URL   : "
        f"http://{GATEWAY_WIFI_IP}:{GATEWAY_PROXY_PORT}/accidents?vehicle_id=1"
    )
    print(
        f"[Gateway Proxy] Proxy URL : "
        f"http://{GATEWAY_WIFI_IP}:{GATEWAY_PROXY_PORT}/proxy/..."
    )
    print(
        f"[Gateway Proxy] Media HTTP: "
        f"http://{MEDIA_INTERFACE_IP}:{MEDIA_HTTP_PORT}"
    )

#    threading.Thread(target=udp_control_sender_loop, daemon=True).start()
#    threading.Thread(target=udp_speed_receiver_loop, daemon=True).start()

    app.run(
        host="0.0.0.0",
        port=GATEWAY_PROXY_PORT,
        debug=True,
        threaded=True,
        use_reloader=False,
    )
