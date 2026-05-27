import time

from .doip import DoipClient, DoipError, hex_bytes


MAIN_ECU = {
    "host": "192.168.10.2",
    "port": 13400,
    "logical_address": 0x0001,
}

MEDIA_PI = {
    "host": "192.168.20.2",
    "port": 13401,
    "logical_address": 0x0030,
}


def decode_ascii(data):
    return data.decode("ascii", errors="replace").rstrip("\x00")


def decode_u8(data):
    return data[0] if data else None


def decode_u16(data):
    if len(data) < 2:
        return None
    return int.from_bytes(data[:2], "big")


def decode_i16(data):
    if len(data) < 2:
        return None
    return int.from_bytes(data[:2], "big", signed=True)


def decode_speed_x100(data):
    value = decode_u16(data)
    if value is None:
        return None
    return f"{value / 100:.2f}"


def decode_bool_label(data, true_label, false_label):
    return true_label if decode_u8(data) == 1 else false_label


def decode_ok_lost(data):
    return "정상" if decode_u8(data) == 0 else "두절"


def decode_up_down(data):
    return decode_bool_label(data, "UP", "DOWN")


def decode_connected(data):
    return decode_bool_label(data, "연결", "미연결")


def decode_running(data):
    return decode_bool_label(data, "실행 중", "중지")


def decode_on_off(data):
    return decode_bool_label(data, "ON", "OFF")


def decode_turn_signal(data):
    return {0: "OFF", 1: "LEFT", 2: "RIGHT", 3: "HAZARD"}.get(decode_u8(data), "UNKNOWN")


ECU_TARGETS = {
    "act": {
        "name": "ACT ECU",
        "logical_address": 0x0010,
        "dids": [
            (0xF190, "ECU 식별", decode_ascii),
            (0x0100, "차량 속도", decode_speed_x100, "km/h"),
            (0x0101, "조향 각도", decode_i16, "deg"),
        ],
    },
    "body": {
        "name": "BODY ECU",
        "logical_address": 0x0020,
        "dids": [
            (0xF190, "ECU 식별", decode_ascii),
            (0x0201, "초음파 거리", decode_u16, "mm"),
            (0x0202, "방향지시등 상태", decode_turn_signal),
            (0x0203, "사고 스위치 상태", decode_on_off),
            (0x0204, "충돌방지 경고 활성", decode_on_off),
        ],
    },
}

MAIN_DIDS = [
    (0xF190, "ECU 식별", decode_ascii),
    (0x0400, "ACT ECU 통신 상태", decode_ok_lost),
    (0x0401, "Body ECU 통신 상태", decode_ok_lost),
    (0x0402, "PC 통신 상태", decode_ok_lost),
]

MEDIA_DIDS = [
    (0xF190, "노드 식별", decode_ascii),
    (0x0300, "eth0 상태", decode_up_down),
    (0x0301, "전방 카메라", decode_connected),
    (0x0302, "후방 카메라", decode_connected),
    (0x0303, "마이크", decode_connected),
    (0x0304, "Flask 서버", decode_running),
    (0x0305, "SOME/IP 서버", decode_running),
    (0x0306, "저장공간 사용률", decode_u8, "%"),
]

DTC_DESCRIPTIONS = {
    0xC10000: "엔코더 신호 없음",
    0xC10100: "조향 명령-실측 불일치",
    0xC20100: "초음파 센서 응답 없음",
    0xC30000: "전방 카메라 고장",
    0xC30100: "후방 카메라 고장",
    0xC30200: "마이크 고장",
    0xC30300: "저장공간 부족",
    0xC30400: "Flask 서버 오류",
    0xC30500: "SOME/IP 서버 오류",
    0xC30600: "eth0 통신 오류",
    0xC40000: "ACT ECU 통신 두절",
    0xC40100: "Body ECU 통신 두절",
    0xC40200: "PC 통신 두절",
}


def open_main_client(target_address, timeout=5.0):
    return DoipClient(
        MAIN_ECU["host"],
        MAIN_ECU["port"],
        target_address,
        timeout=timeout,
        activation_address=MAIN_ECU["logical_address"],
    )


def open_media_client(timeout=2.0):
    return DoipClient(
        MEDIA_PI["host"],
        MEDIA_PI["port"],
        MEDIA_PI["logical_address"],
        timeout=timeout,
    )


def read_did(client, did, label, decoder, unit=None):
    uds_response = client.read_data_by_identifier(did)

    response_did = (uds_response[1] << 8) | uds_response[2] if len(uds_response) >= 3 else None
    if len(uds_response) >= 3 and uds_response[0] == 0x62 and response_did == did:
        payload = uds_response[3:]
        value = decoder(payload)
        return {
            "did": f"0x{did:04X}",
            "label": label,
            "value": value,
            "unit": unit,
            "raw": hex_bytes(uds_response),
            "ok": True,
        }

    if len(uds_response) >= 3 and uds_response[0] == 0x62:
        return {
            "did": f"0x{did:04X}",
            "label": label,
            "value": "DID 불일치",
            "unit": unit,
            "raw": hex_bytes(uds_response),
            "ok": False,
            "nrc": None,
        }

    return negative_uds_result(did, label, uds_response, unit)


def read_dids(client, did_specs):
    return [
        read_did(client, did, label, decoder, unit)
        for did, label, decoder, *rest in did_specs
        for unit in [rest[0] if rest else None]
    ]


def enter_session(client, session_type, target_name):
    uds_response = client.change_session(session_type)
    if len(uds_response) >= 2 and uds_response[0] == 0x50 and uds_response[1] == session_type:
        return {
            "ok": True,
            "session": f"0x{session_type:02X}",
            "raw": hex_bytes(uds_response),
        }

    if len(uds_response) >= 3 and uds_response[0] == 0x7F:
        raise DoipError(
            f"{target_name} DiagnosticSessionControl 실패: "
            f"NRC 0x{uds_response[2]:02X}, raw={hex_bytes(uds_response)}"
        )

    raise DoipError(
        f"{target_name} DiagnosticSessionControl 응답 오류: {hex_bytes(uds_response)}"
    )


def negative_uds_result(did, label, uds_response, unit=None):
    nrc = uds_response[2] if len(uds_response) >= 3 and uds_response[0] == 0x7F else None
    return {
        "did": f"0x{did:04X}",
        "label": label,
        "value": "NRC" if nrc is not None else "응답 오류",
        "unit": unit,
        "raw": hex_bytes(uds_response),
        "ok": False,
        "nrc": f"0x{nrc:02X}" if nrc is not None else None,
    }


def read_dtcs(client, request_type="status_mask"):
    if request_type == "supported":
        uds_response = client.read_supported_dtcs()
    else:
        uds_response = client.read_dtc_by_status_mask(0xFF)

    if not uds_response or uds_response[0] != 0x59:
        return {
            "ok": False,
            "raw": hex_bytes(uds_response),
            "items": [],
        }

    items = []
    payload = uds_response[3:]
    for offset in range(0, len(payload), 4):
        if offset + 4 > len(payload):
            break
        code = int.from_bytes(payload[offset : offset + 3], "big")
        status = payload[offset + 3]
        if status == 0:
            continue
        items.append(
            {
                "code": f"0x{code:06X}",
                "status": f"0x{status:02X}",
                "state": "Active" if status & 0x01 else "Stored",
                "description": DTC_DESCRIPTIONS.get(code, "정의되지 않은 DTC"),
            }
        )

    return {
        "ok": True,
        "raw": hex_bytes(uds_response),
        "items": items,
    }


def clear_dtcs(client):
    uds_response = client.clear_dtc(0xFFFFFF)
    return {
        "ok": bool(uds_response and uds_response[0] == 0x54),
        "raw": hex_bytes(uds_response),
    }


def run_routine(client, routine_id):
    uds_response = client.start_routine(routine_id)

    return {
        "ok": bool(uds_response and uds_response[0] == 0x71),
        "raw": hex_bytes(uds_response),
        "routine_result": uds_response[4] if len(uds_response) >= 5 else None,
    }


def test_main_routing_activation():
    started = time.time()
    warnings = []
    with open_main_client(MAIN_ECU["logical_address"]) as client:
        activation = client.routing_activation()
        ensure_routing_active(activation, "MAIN ECU")
        session = enter_session(client, 0x03, "MAIN ECU")
        dids = read_dids(client, MAIN_DIDS)
        dtcs = read_dtcs(client, request_type="supported")
        if not dtcs["ok"]:
            warnings.append({
                "type": "dtc",
                "detail": f"MAIN ECU DTC 조회 실패: {dtcs['raw']}",
            })

    return {
        "result": "OK",
        "target": "MAIN ECU",
        "host": MAIN_ECU["host"],
        "port": MAIN_ECU["port"],
        "elapsed_ms": elapsed_ms(started),
        "routing_activation": format_routing_activation(activation),
        "session": session,
        "dids": dids,
        "dtcs": dtcs,
        "warnings": warnings,
    }


def scan_media_pi():
    started = time.time()
    with open_media_client() as client:
        activation = client.routing_activation()
        ensure_routing_active(activation, "MEDIA PI")
        session = enter_session(client, 0x01, "MEDIA PI")
        dids = read_dids(client, MEDIA_DIDS)
        dtcs = read_dtcs(client)
        if not dtcs["ok"]:
            raise DoipError(f"MEDIA PI DTC 조회 실패: {dtcs['raw']}")

    return {
        "result": "OK",
        "target": "MEDIA PI",
        "host": MEDIA_PI["host"],
        "port": MEDIA_PI["port"],
        "elapsed_ms": elapsed_ms(started),
        "routing_activation": format_routing_activation(activation),
        "session": session,
        "dids": dids,
        "dtcs": dtcs,
    }


def scan_main_routed_ecus():
    started = time.time()
    ecus = []
    activation = None

    for ecu_id, target in ECU_TARGETS.items():
        dids = []
        session = None
        try:
            with open_main_client(target["logical_address"], timeout=5.0) as client:
                target_activation = client.routing_activation()
                if activation is None:
                    activation = target_activation
                ensure_routing_active(target_activation, target["name"])
                session = enter_session(client, 0x03, target["name"])
                dids = read_dids(client, target["dids"])
                dtcs = read_dtcs(client)
            if not dtcs["ok"]:
                raise DoipError(f"{target['name']} DTC 조회 실패: {dtcs['raw']}")
            state = "Normal" if all(item["ok"] for item in dids) and not dtcs["items"] else "Warning"
            error = ""
        except DoipError as exc:
            state = "Offline"
            error = str(exc)
            dtcs = {"ok": False, "items": [], "raw": ""}

        ecus.append(
            {
                "id": ecu_id,
                "name": target["name"],
                "logical_address": f"0x{target['logical_address']:04X}",
                "state": state,
                "session": session,
                "dids": dids,
                "dtcs": dtcs,
                "error": error,
            }
        )

    return {
        "result": "OK",
        "target": "MAIN ECU",
        "host": MAIN_ECU["host"],
        "port": MAIN_ECU["port"],
        "elapsed_ms": elapsed_ms(started),
        "routing_activation": format_routing_activation(activation) if activation else None,
        "ecus": ecus,
    }


def clear_all_dtcs():
    results = []

    with open_media_client() as client:
        ensure_routing_active(client.routing_activation(), "MEDIA PI")
        enter_session(client, 0x01, "MEDIA PI")
        media_result = clear_dtcs(client)
        results.append({"target": "MEDIA PI", **media_result})

    with open_main_client(MAIN_ECU["logical_address"], timeout=5.0) as client:
        ensure_routing_active(client.routing_activation(), "MAIN ECU")
        enter_session(client, 0x03, "MAIN ECU")
        results.append({"target": "MAIN ECU", **clear_dtcs(client)})

    for target in ECU_TARGETS.values():
        with open_main_client(target["logical_address"], timeout=5.0) as client:
            ensure_routing_active(client.routing_activation(), target["name"])
            enter_session(client, 0x03, target["name"])
            results.append(
                {
                    "target": target["name"],
                    **clear_dtcs(client),
                }
            )

    return {
        "result": "OK" if all(item["ok"] for item in results) else "ERROR",
        "items": results,
    }


def run_function_test(test_id):
    routine_map = {
        "act-motor": ("ACT ECU", 0x0010, 0x0100),
        "act-servo": ("ACT ECU", 0x0010, 0x0101),
        "body-buzzer": ("BODY ECU", 0x0020, 0x0200),
        "body-led": ("BODY ECU", 0x0020, 0x0201),
        "body-ultrasonic": ("BODY ECU", 0x0020, 0x0202),
    }

    if test_id not in routine_map:
        return {"result": "ERROR", "detail": "알 수 없는 routine id입니다."}

    target_name, logical_address, routine_id = routine_map[test_id]
    print(
        "[routine] START "
        f"test_id={test_id} target={target_name} "
        f"logical_address=0x{logical_address:04X} routine_id=0x{routine_id:04X}",
        flush=True,
    )
    with open_main_client(logical_address, timeout=5.0) as client:
        print(f"[routine] ROUTING START target={target_name}", flush=True)
        activation = client.routing_activation()
        print(
            "[routine] ROUTING RESP "
            f"source=0x{activation.source_address:04X} "
            f"target=0x{activation.target_address:04X} "
            f"code=0x{activation.response_code:02X}",
            flush=True,
        )
        ensure_routing_active(activation, target_name)

        print(f"[routine] SESSION START target={target_name} session=0x03", flush=True)
        session = enter_session(client, 0x03, target_name)
        print(f"[routine] SESSION RESP raw={session['raw']}", flush=True)

        print(
            f"[routine] ROUTINE START target={target_name} routine_id=0x{routine_id:04X}",
            flush=True,
        )
        routine_result = run_routine(client, routine_id)
        print(
            "[routine] ROUTINE RESP "
            f"ok={routine_result['ok']} raw={routine_result['raw']}",
            flush=True,
        )

    return {
        "result": "OK" if routine_result["ok"] else "ERROR",
        "target": target_name,
        "routine_id": f"0x{routine_id:04X}",
        **routine_result,
    }


def run_full_scan():
    sections = {}
    errors = []

    for key, task in [
        ("media", scan_media_pi),
        ("main_routing", test_main_routing_activation),
        ("main_ecus", scan_main_routed_ecus),
    ]:
        try:
            sections[key] = task()
        except Exception as exc:
            sections[key] = {
                "result": "ERROR",
                "detail": str(exc),
            }
            errors.append({"section": key, "detail": str(exc)})

    return {
        "result": "OK" if not errors else "PARTIAL_ERROR",
        "sections": sections,
        "errors": errors,
        "updated_at": int(time.time()),
    }


def format_routing_activation(activation):
    return {
        "source_address": f"0x{activation.source_address:04X}",
        "target_address": f"0x{activation.target_address:04X}",
        "response_code": f"0x{activation.response_code:02X}",
        "success": activation.response_code == 0x10,
        "reserved": hex_bytes(activation.reserved),
    }


def ensure_routing_active(activation, target_name):
    if activation.response_code != 0x10:
        raise DoipError(
            f"{target_name} Routing Activation 거부: 0x{activation.response_code:02X}"
        )


def elapsed_ms(started):
    return int((time.time() - started) * 1000)
