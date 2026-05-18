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
from .csi_recorder import CSIRecorder
from .usb_recorder import USBRecorder
from .mic_recorder import MicRecorder
from .event_db import EventDB
from .event_trigger import EventTrigger
from . import streamer
from . import someip_server
from .udp_streamer import UDPStreamServer

log = logging.getLogger(__name__)


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    csi     = CSIRecorder()
    usb     = USBRecorder()
    mic     = MicRecorder()
    db      = EventDB()
    trigger = EventTrigger(csi, usb, mic, db)

    log.info("Starting recorders …")

    csi_ok = False
    try:
        csi.start()
        csi_ok = True
    except RuntimeError as e:
        log.warning("CSI recorder 건너뜀: %s", e)

    usb_ok = False
    try:
        usb.start()
        usb_ok = True
    except RuntimeError as e:
        log.warning("USB recorder 건너뜀: %s", e)

    mic.start()
    trigger.setup()

    streamer.init(csi if csi_ok else None, usb if usb_ok else None, db)
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

    someip_server.start_in_thread(db)

    udp_stream = UDPStreamServer(usb if usb_ok else None)
    udp_stream.start()

    log.info(
        "RPi #2 Blackbox running\n"
        "  GPIO%d  = event trigger  (pre=%ds / post=%ds)\n"
        "  Web UI  = http://0.0.0.0:%d/\n"
        "  SOME/IP = AccidentHistoryService @ %s:%d\n"
        "  UDP     = live stream @ 0.0.0.0:%d\n"
        "  Ctrl+C  = 종료",
        config.GPIO_SWITCH_PIN,
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
        csi.stop()
        usb.stop()
        mic.stop()
        log.info("Bye.")


if __name__ == "__main__":
    main()
