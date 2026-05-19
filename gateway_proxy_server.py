import asyncio
import time
import socket
from urllib.parse import urlparse

import requests
from flask import Flask, Response, jsonify, request, stream_with_context

from config import (
    GATEWAY_WIFI_IP,
    GATEWAY_PROXY_PORT,
    MEDIA_INTERFACE_IP,
    MEDIA_HTTP_PORT,
)

from gateway_someip_client import (
    request_accident_list_from_media,
    request_buzzer_state_to_main,
)

from udp_video_bridge import UDPVideoBridge


app = Flask(__name__)

MEDIA_IP = "192.168.20.2"

MAIN_ECU_IP = "192.168.10.2"
MAIN_CONTROL_PORT = 5000

front_bridge = UDPVideoBridge(MEDIA_IP, 5000)
rear_bridge = front_bridge

front_bridge.start()

def generate_mjpeg_stream(bridge: UDPVideoBridge):
    last_sent_frame = None

    while True:
        frame, last_frame_time = bridge.get_latest_frame()

        if frame is None:
            time.sleep(0.05)
            continue

        # 같은 프레임을 너무 빠르게 반복 전송하지 않도록 최소 대기
        if frame is last_sent_frame:
            time.sleep(0.03)
            continue

        last_sent_frame = frame

        yield (
            b"--frame\r\n"
            b"Content-Type: image/jpeg\r\n\r\n" +
            frame +
            b"\r\n"
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

vehicle_state = {
    "vehicle_id": 1,
    "driving_state": 0,              # 0: 정지 중, 1: 주행 중
    "remote_control_active": False,  # False: OFF, True: ON
}

@app.after_request
def add_cors_headers(response):
    """
    React 화면에서 Gateway API를 호출할 수 있도록 CORS 허용.
    개발/시연용 설정.
    """
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type"
    return response


def make_gateway_video_url(video_path: str | None) -> str | None:
    """
    Media 내부 video_path를 PC가 접근 가능한 Gateway proxy URL로 변환.

    예:
    /events/1/video/csi
    → http://192.168.201.2:5000/proxy/events/1/video/csi
    """

    if not video_path:
        return None

    if not video_path.startswith("/"):
        video_path = "/" + video_path

    return (
        f"http://{GATEWAY_WIFI_IP}:{GATEWAY_PROXY_PORT}"
        f"/proxy{video_path}"
    )


def extract_path_from_video_url(video_url: str | None) -> str | None:
    """
    혹시 Media가 video_path 대신 video_url을 주는 경우를 대비.

    예:
    http://192.168.20.2:8088/events/1/video/csi
    → /events/1/video/csi
    """

    if not video_url:
        return None

    parsed = urlparse(video_url)
    return parsed.path


def rewrite_accident_video_fields(response_data: dict) -> dict:
    """
    Media 응답을 PC 화면용 응답으로 변환한다.

    권장 Media 응답:
    - front_video_path
    - rear_video_path

    기존 호환:
    - video_path 하나만 오는 경우
    - video_url 하나만 오는 경우

    Gateway 응답:
    - front_video_url
    - rear_video_url
    - video_url 기존 호환용
    """

    if response_data.get("result") != "OK":
        return response_data

    for accident in response_data.get("accidents", []):
        # 권장 구조: 전방/후방 경로를 각각 받음
        front_path = accident.pop("front_video_path", None)
        rear_path = accident.pop("rear_video_path", None)

        # 기존 구조 호환: 영상 경로 1개만 받음
        video_path = accident.pop("video_path", None)

        # 기존 구조 호환: video_url 하나만 받음
        old_video_url = accident.get("video_url")
        if video_path is None and old_video_url:
            video_path = extract_path_from_video_url(old_video_url)

        # front_video_path가 있으면 전방 URL로 사용
        if front_path:
            accident["front_video_url"] = make_gateway_video_url(front_path)
        elif video_path:
            accident["front_video_url"] = make_gateway_video_url(video_path)
        else:
            accident["front_video_url"] = None

        # rear_video_path가 있으면 후방 URL로 사용
        if rear_path:
            accident["rear_video_url"] = make_gateway_video_url(rear_path)
        elif video_path:
            accident["rear_video_url"] = make_gateway_video_url(video_path)
        else:
            accident["rear_video_url"] = None

        # 기존 React/테스트 코드 호환용
        # video_url 하나만 참조하는 코드가 있어도 전방 영상으로 동작하게 둠
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
    driving_state = vehicle_state["driving_state"]
    remote_control_active = vehicle_state["remote_control_active"]

    return jsonify({
        "result": "OK",
        "vehicle_id": vehicle_state["vehicle_id"],
        "driving_state": driving_state,
        "driving_state_text": "주행 중" if driving_state == 1 else "정지 중",
        "remote_control_active": remote_control_active,
        "remote_control_text": "ON" if remote_control_active else "OFF",
    })


@app.post("/remote-control/start")
def start_remote_control():
    vehicle_state["remote_control_active"] = True

    return jsonify({
        "result": "OK",
        "remote_control_active": True,
        "remote_control_text": "ON",
    })

def send_control_to_main(command_data: dict) -> bool:
    """
    Gateway -> MAIN 제어 명령 전송 함수.

    지금은 UDP JSON으로 보내는 형태.
    MAIN 쪽 통신 규약이 SOME/IP로 확정되어 있으면,
    이 함수 내부만 SOME/IP method call로 바꾸면 됨.
    """

    try:
        payload = {
            "vehicle_id": vehicle_state["vehicle_id"],
            "timestamp": time.time(),
            **command_data,
        }

        message = str(payload).encode("utf-8")

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto(message, (MAIN_ECU_IP, MAIN_CONTROL_PORT))
        sock.close()

        print(f"[Control -> MAIN] {payload}")

        return True

    except OSError as e:
        print(f"[Control -> MAIN] failed: {e}")
        return False

@app.post("/control/buzzer")
def control_buzzer():
    """
    React 후진 경고음 버튼에서 들어오는 요청 처리.

    PC/React -> Gateway:
    POST /control/buzzer

    Gateway -> MAIN:
    SOME/IP BuzzerControlService.SetBuzzerState
    """

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

@app.post("/control")
def control_command():
    """
    React 원격 조종 화면에서 들어오는 제어 명령 처리.

    React -> Gateway:
    POST /control

    Gateway -> MAIN:
    send_control_to_main()
    """

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

    # 차량 상태도 Gateway에 일부 반영
    if command_type == "drive":
        if value in ["FORWARD", "BACKWARD", "LEFT", "RIGHT", "FORWARD_LEFT", "FORWARD_RIGHT", "BACKWARD_LEFT", "BACKWARD_RIGHT"]:
            vehicle_state["driving_state"] = 1
        elif value == "STOP":
            vehicle_state["driving_state"] = 0

    ok = send_control_to_main(command_data)

    if not ok:
        return jsonify({
            "result": "INTERNAL_ERROR",
            "error_code": 2,
            "message": "failed to send control command to MAIN",
            "command": command_data,
        }), 502

    return jsonify({
        "result": "OK",
        "command": command_data,
    })


@app.post("/remote-control/stop")
def stop_remote_control():
    vehicle_state["remote_control_active"] = False

    return jsonify({
        "result": "OK",
        "remote_control_active": False,
        "remote_control_text": "OFF",
    })

@app.get("/accidents")
def get_accidents():
    """
    PC/React가 호출하는 사고 이력 조회 API.

    PC → Gateway:
    GET http://192.168.201.2:5000/accidents?vehicle_id=1

    Gateway → Media:
    SOME/IP GetRecordList

    Gateway → PC:
    JSON response with front_video_url / rear_video_url
    """

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
    """
    PC가 Gateway URL로 영상에 접근하면,
    Gateway가 Media RPi의 실제 영상 URL로 요청을 전달한다.

    PC:
    http://192.168.201.2:5000/proxy/events/1/video/csi

    Gateway 내부 요청:
    http://192.168.20.2:8088/events/1/video/csi
    """

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

    except requests.RequestException as e:
        return jsonify({
            "result": "INTERNAL_ERROR",
            "error_code": 2,
            "message": "Failed to proxy media file",
            "detail": str(e),
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

    app.run(
        host="0.0.0.0",
        port=GATEWAY_PROXY_PORT,
        debug=True,
        threaded=True,
        use_reloader=False,
    )
