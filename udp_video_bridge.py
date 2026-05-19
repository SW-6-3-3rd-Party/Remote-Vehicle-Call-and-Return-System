import socket
import struct
import threading
import time


class UDPVideoBridge:
    def __init__(self, media_ip: str, media_port: int, keepalive_interval: float = 0.2):
        self.media_ip = media_ip
        self.media_port = media_port
        self.keepalive_interval = keepalive_interval

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(1.0)

        self.latest_frame = None
        self.last_frame_time = 0.0
        self.running = False
        self.lock = threading.Lock()

    def start(self):
        if self.running:
            return

        self.running = True

        threading.Thread(target=self._keepalive_loop, daemon=True).start()
        threading.Thread(target=self._receive_loop, daemon=True).start()

        print(f"[UDPVideoBridge] start {self.media_ip}:{self.media_port}")

    def _keepalive_loop(self):
        while self.running:
            try:
                self.sock.sendto(b"keepalive", (self.media_ip, self.media_port))
            except OSError as error:
                print(f"[UDPVideoBridge] keepalive error: {error}")

            time.sleep(self.keepalive_interval)

    def _receive_loop(self):
        while self.running:
            try:
                packet, _ = self.sock.recvfrom(65535)

                if len(packet) < 4:
                    continue

                frame_size = struct.unpack(">I", packet[:4])[0]
                jpeg = packet[4:]

                if frame_size != len(jpeg):
                    continue

                with self.lock:
                    self.latest_frame = jpeg
                    self.last_frame_time = time.time()

            except socket.timeout:
                continue
            except OSError as error:
                print(f"[UDPVideoBridge] receive error: {error}")

    def get_latest_frame(self):
        with self.lock:
            return self.latest_frame, self.last_frame_time
