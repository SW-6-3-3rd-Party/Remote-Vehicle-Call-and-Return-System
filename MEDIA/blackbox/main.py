"""
RPi #2 Blackbox — entry point

  python3 -m blackbox.main

Ctrl+C 로 정상 종료.
브라우저에서 http://<라즈베리파이IP>:5000 접속.
"""
import logging
import threading
import time

from . import config
from .csi_recorder import FrontUSBRecorder
from .usb_recorder import USBRecorder
from .mic_recorder import MicRecorder
from .event_db import EventDB
from .event_trigger import EventTrigger
from . import streamer
from . import someip_server
from .udp_streamer import UDPStreamServer
from doip.gateway import DoIPGateway, build_config as doip_build_config

log = logging.getLogger(__name__)


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    usb1    = FrontUSBRecorder()
    usb     = USBRecorder()
    mic     = MicRecorder()
    db      = EventDB()
    trigger = EventTrigger(usb1, usb, mic, db)

    log.info("Starting recorders …")

    # 후방 카메라를 먼저 시작: 두 카메라가 같은 USB2 컨트롤러를 공유하므로
    # 전방이 먼저 스트리밍을 시작하면 후방 감지 시 대역폭 경쟁으로 실패함
    usb_ok = False
    try:
        usb.start()
        usb_ok = True
    except RuntimeError as e:
        log.warning("Rear USB recorder 건너뜀: %s", e)

    time.sleep(1.0)  # 후방 카메라가 안정화된 후 전방 시작

    usb1_ok = False
    try:
        usb1.start()
        usb1_ok = True
    except RuntimeError as e:
        log.warning("Front USB recorder 건너뜀: %s", e)

    mic_ok = False
    try:
        mic.start()
        mic_ok = True
    except RuntimeError as e:
        log.warning("Mic recorder 건너뜀: %s", e)

    trigger.setup()

    streamer.init(usb1 if usb1_ok else None, usb if usb_ok else None, db)
    flask_thread = threading.Thread(
        target=lambda: streamer.app.run(
            host=config.FLASK_HOST,
            port=config.FLASK_PORT,
            threaded=True,
            use_reloader=False,
        ),
        name="flask",
        daemon=True,
    )
    flask_thread.start()

    someip_server.start_in_thread(db, event_trigger=trigger)

    udp_stream = UDPStreamServer(usb if usb_ok else None)
    udp_stream.start()

    doip_cfg = doip_build_config(
        recordings_path=config.RECORDINGS_BASE,
        flask_port=config.FLASK_PORT,
        someip_port=config.SOMEIP_SERVICE_PORT,
    )
    doip_gw = DoIPGateway(doip_cfg)
    doip_gw.attach_recorders(
        usb1=usb1 if usb1_ok else None,
        usb=usb if usb_ok else None,
        mic=mic if mic_ok else None,
    )
    doip_gw.start_in_thread()

    log.info(
        "RPi #2 Blackbox running\n"
        "  ECU trigger = UDP 0.0.0.0:%d  (pre=%ds / post=%ds)\n"
        "  Web UI      = http://0.0.0.0:%d/\n"
        "  SOME/IP     = AccidentHistoryService @ %s:%d\n"
        "  UDP stream  = live stream @ 0.0.0.0:%d\n"
        "  DoIP        = diagnostic gateway @ 0.0.0.0:13401\n"
        "  Ctrl+C      = 종료",
        config.ECU_TRIGGER_LISTEN_PORT,
        config.PRE_EVENT_SECS,
        config.POST_EVENT_SECS,
        config.FLASK_PORT,
        config.MEDIA_INTERFACE_IP,
        config.SOMEIP_SERVICE_PORT,
        config.UDP_STREAM_PORT,
    )

    # Ctrl+C (KeyboardInterrupt) 로 종료
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        log.info("Shutting down …")
        trigger.cleanup()
        usb1.stop()
        usb.stop()
        if mic_ok:
            mic.stop()
        log.info("Bye.")


if __name__ == "__main__":
    main()
