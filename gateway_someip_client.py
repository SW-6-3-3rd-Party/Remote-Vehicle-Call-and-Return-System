import asyncio
import json
import logging

from someipy import (
    TransportLayerProtocol,
    MessageType,
    ReturnCode,
    connect_to_someipy_daemon,
    ClientServiceInstance,
    Method,
    ServiceBuilder,
)
from someipy.someipy_logging import set_someipy_log_level

from config import (
    GATEWAY_ETH_IP,
    SOMEIP_CLIENT_PORT,
    ACCIDENT_SERVICE_ID,
    ACCIDENT_INSTANCE_ID,
    GET_RECORD_LIST_METHOD_ID,
    PAYLOAD_ENCODING,
)


SERVICE_WAIT_TIMEOUT_SEC = 5.0
METHOD_CALL_TIMEOUT_SEC = 5.0


def make_error_response(message: str = "Gateway internal error") -> dict:
    return {
        "result": "INTERNAL_ERROR",
        "error_code": 2,
        "accident_count": 0,
        "accidents": [],
        "detail": message,
    }


async def wait_until_available(client_instance, timeout_sec: float) -> bool:
    start_time = asyncio.get_event_loop().time()

    while True:
        if await client_instance.is_available():
            return True

        if asyncio.get_event_loop().time() - start_time > timeout_sec:
            return False

        await asyncio.sleep(0.2)


async def request_accident_list_from_media(vehicle_id: int = 1) -> dict:
    """
    Gateway가 Media RPi의 AccidentRecordService에
    GetRecordList SOME/IP 요청을 보내고 JSON 응답을 받아오는 함수.
    """

    set_someipy_log_level(logging.INFO)

    someipy_daemon = await connect_to_someipy_daemon()

    accident_list_method = Method(
        id=GET_RECORD_LIST_METHOD_ID,
        protocol=TransportLayerProtocol.UDP,
    )

    accident_service = (
        ServiceBuilder()
        .with_service_id(ACCIDENT_SERVICE_ID)
        .with_major_version(1)
        .with_method(accident_list_method)
        .build()
    )

    client_instance = ClientServiceInstance(
        daemon=someipy_daemon,
        service=accident_service,
        instance_id=ACCIDENT_INSTANCE_ID,
        endpoint_ip=GATEWAY_ETH_IP,
        endpoint_port=SOMEIP_CLIENT_PORT,
    )

    try:
        print("[Gateway] Waiting for Media AccidentRecordService...")

        available = await wait_until_available(
            client_instance,
            SERVICE_WAIT_TIMEOUT_SEC,
        )

        if not available:
            return make_error_response(
                "Media AccidentRecordService not available"
            )

        print("[Gateway] Media service available")

        request_data = {
            "vehicle_id": vehicle_id
        }

        request_payload = json.dumps(request_data).encode(PAYLOAD_ENCODING)

        print(f"[Gateway] Send GetRecordList to Media: {request_data}")

        method_result = await asyncio.wait_for(
            client_instance.call_method(
                GET_RECORD_LIST_METHOD_ID,
                request_payload,
            ),
            timeout=METHOD_CALL_TIMEOUT_SEC,
        )

        if method_result.message_type != MessageType.RESPONSE:
            return make_error_response(
                f"Unexpected SOME/IP message type: {method_result.message_type}"
            )

        if method_result.return_code != ReturnCode.E_OK:
            return make_error_response(
                f"SOME/IP return_code error: {method_result.return_code}"
            )

        response_text = method_result.payload.decode(PAYLOAD_ENCODING)
        response_data = json.loads(response_text)

        print("[Gateway] Media response received")
        return response_data

    except asyncio.TimeoutError:
        return make_error_response("Timeout while waiting Media SOME/IP response")

    except json.JSONDecodeError as e:
        return make_error_response(f"Invalid JSON from Media: {e}")

    except Exception as e:
        return make_error_response(f"Gateway SOME/IP client error: {e}")

    finally:
        await someipy_daemon.disconnect_from_daemon()
