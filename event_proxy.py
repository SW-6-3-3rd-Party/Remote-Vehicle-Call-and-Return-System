import socket

from config import (
    EVENT_PROXY_LISTEN_IP,
    EVENT_PROXY_LISTEN_PORT,
    EVENT_PROXY_MEDIA_IP,
    EVENT_PROXY_MEDIA_PORT,
    MAIN_ECU_IP,
)


def run_event_proxy():
    rx_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx_sock.bind((EVENT_PROXY_LISTEN_IP, EVENT_PROXY_LISTEN_PORT))

    tx_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(
        "[Event Proxy] listening "
        f"{EVENT_PROXY_LISTEN_IP}:{EVENT_PROXY_LISTEN_PORT} -> "
        f"{EVENT_PROXY_MEDIA_IP}:{EVENT_PROXY_MEDIA_PORT}"
    )

    while True:
        data, addr = rx_sock.recvfrom(256)

        if addr[0] != MAIN_ECU_IP:
            print(f"[Event Proxy] blocked unauthorized source: {addr[0]}")
            continue

        tx_sock.sendto(data, (EVENT_PROXY_MEDIA_IP, EVENT_PROXY_MEDIA_PORT))
        print(
            "[Event Proxy] forwarded accident event "
            f"{len(data)}B from {addr[0]} to {EVENT_PROXY_MEDIA_IP}:{EVENT_PROXY_MEDIA_PORT}"
        )


if __name__ == "__main__":
    run_event_proxy()
