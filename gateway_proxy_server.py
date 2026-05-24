from event_proxy import run_event_proxy


if __name__ == "__main__":
    print(
        "[Gateway] HTTP/SOME-IP proxy is retired. "
        "TCU Pi now runs only the accident Event Proxy in this process."
    )
    run_event_proxy()
