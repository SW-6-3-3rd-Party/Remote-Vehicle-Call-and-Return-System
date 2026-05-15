import asyncio
from urllib.parse import urlparse

import requests
from flask import Flask, Response, jsonify, request, stream_with_context

from config import (
    GATEWAY_WIFI_IP,
    GATEWAY_PROXY_PORT,
    MEDIA_INTERFACE_IP,
    MEDIA_HTTP_PORT,
)

from gateway_someip_client import request_accident_list_from_media


app = Flask(__name__)

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
    response.headers["Access-Control-Allow-Methods"] = "GET, OPTIONS"
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
    )
