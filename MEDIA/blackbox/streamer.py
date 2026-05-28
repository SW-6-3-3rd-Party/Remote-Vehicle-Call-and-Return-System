"""
Flask streaming server for RPi #2

Routes
------
  GET /                              메인 페이지 (라이브 스트림 + 이벤트 목록)
  GET /stream/usb1                   USB1 전방 라이브 MJPEG 스트림
  GET /stream/usb                    USB2 후방 라이브 MJPEG 스트림
  GET /api/events                    이벤트 목록 JSON
  GET /api/events/sse                Server-Sent Events — 새 이벤트 push
  GET /api/events/<id>/status        MP4 변환 완료 여부 JSON
  GET /events/<id>                   이벤트 상세 페이지 (seekable 비디오)
  GET /events/<id>/video/usb1        USB1 전방 MP4 파일 서빙 (Range 요청 지원)
  GET /events/<id>/video/usb         USB MP4 파일 서빙 (Range 요청 지원)
"""
import queue
import time
import logging

import cv2
from flask import Flask, Response, jsonify, abort, render_template_string, send_file

from . import config
from .event_db import EventDB

log = logging.getLogger(__name__)

app = Flask(__name__)

_usb1 = None
_usb = None
_db: EventDB | None = None

_sse_queues: list[queue.Queue] = []
_sse_lock = __import__("threading").Lock()


def init(usb1_recorder, usb_recorder, db: EventDB) -> None:
    global _usb1, _usb, _db
    _usb1, _usb, _db = usb1_recorder, usb_recorder, db


def notify_new_event(event_id: str) -> None:
    with _sse_lock:
        for q in list(_sse_queues):
            try:
                q.put_nowait(event_id)
            except queue.Full:
                pass


# ---------------------------------------------------------------------------
# HTML — 메인 페이지
# ---------------------------------------------------------------------------

_INDEX_HTML = r"""<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<title>RPi #2 Blackbox</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: sans-serif; background: #111; color: #eee;
         margin: 0; padding: 16px; }
  h1   { margin: 0 0 14px; font-size: 1.3rem; }
  h2   { font-size: 0.95rem; color: #aaa; margin: 18px 0 6px; }
  .streams { display: flex; gap: 12px; flex-wrap: wrap; }
  .cam-box { display: flex; flex-direction: column; gap: 4px; }
  .cam-label { font-size: 0.78rem; color: #888; }
  .streams img { width: 480px; border: 2px solid #333; border-radius: 4px; }
  table { border-collapse: collapse; width: 100%; max-width: 820px; margin-top: 6px; }
  th, td { border: 1px solid #2a2a2a; padding: 7px 12px;
           font-size: 0.84rem; text-align: left; }
  th { background: #1e1e1e; }
  tr:hover td { background: #1a1a1a; }
  a  { color: #4af; text-decoration: none; }
  a:hover { text-decoration: underline; }
  .badge { background: #c33; color: #fff; border-radius: 3px;
           padding: 1px 7px; font-size: 0.72rem; margin-left: 8px;
           vertical-align: middle; }
  .new-row { animation: hl 2.5s ease-out; }
  @keyframes hl { from { background: #1a3a1a; } to { background: transparent; } }
  #toast { position: fixed; bottom: 24px; right: 24px; background: #2a5;
           color: #fff; padding: 10px 18px; border-radius: 6px;
           font-size: 0.9rem; display: none;
           box-shadow: 0 2px 8px rgba(0,0,0,.5); z-index: 999; }
</style>
</head>
<body>
<h1>RPi #2 Blackbox <span class="badge">LIVE</span></h1>

<h2>실시간 스트림</h2>
<div class="streams">
  <div class="cam-box">
    <span class="cam-label">USB 카메라 1 (전방)</span>
    <img src="/stream/usb1" alt="USB1 stream">
  </div>
  <div class="cam-box">
    <span class="cam-label">USB 카메라 (후방)</span>
    <img src="/stream/usb" alt="USB stream">
  </div>
</div>

<h2>저장된 이벤트</h2>
<table>
  <thead><tr><th>Event ID</th><th>발생 시각</th><th>재생</th></tr></thead>
  <tbody id="event-tbody">
    <tr><td colspan="3" style="color:#555">불러오는 중…</td></tr>
  </tbody>
</table>

<div id="toast"></div>

<script>
  let knownIds = new Set();

  function tsToStr(ts) {
    return new Date(ts * 1000).toLocaleString('ko-KR', { hour12: false });
  }

  function renderEvents(events) {
    const tbody = document.getElementById('event-tbody');
    if (!events.length) {
      tbody.innerHTML = '<tr><td colspan="3" style="color:#555">저장된 이벤트 없음</td></tr>';
      return;
    }
    const newIds = events.map(e => e.event_id).filter(id => !knownIds.has(id));
    newIds.forEach(id => knownIds.add(id));
    tbody.innerHTML = events.map(e => `
      <tr class="${newIds.includes(e.event_id) ? 'new-row' : ''}">
        <td>${e.event_id}</td>
        <td>${tsToStr(e.triggered_at)}</td>
        <td><a href="/events/${e.event_id}">▶ 보기</a></td>
      </tr>`).join('');
  }

  function loadEvents() {
    fetch('/api/events').then(r => r.json()).then(renderEvents).catch(() => {});
  }

  function showToast(msg) {
    const t = document.getElementById('toast');
    t.textContent = msg;
    t.style.display = 'block';
    setTimeout(() => { t.style.display = 'none'; }, 4000);
  }

  function connectSSE() {
    const es = new EventSource('/api/events/sse');
    es.onmessage = e => {
      if (e.data === 'ping') return;
      showToast('새 이벤트 저장됨: ' + e.data);
      loadEvents();
    };
    es.onerror = () => { es.close(); setTimeout(connectSSE, 3000); };
  }

  loadEvents();
  connectSSE();
</script>
</body>
</html>"""


# ---------------------------------------------------------------------------
# HTML — 이벤트 상세 페이지 (seekable <video> 플레이어)
# ---------------------------------------------------------------------------

_EVENT_HTML = r"""<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<title>Event {{ event_id }}</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: sans-serif; background: #111; color: #eee;
         margin: 0; padding: 16px; }
  h1   { font-size: 1.2rem; margin: 0 0 6px; }
  h2   { font-size: 0.9rem; color: #aaa; margin: 16px 0 5px; }
  .players { display: flex; gap: 16px; flex-wrap: wrap; }
  .player-box { display: flex; flex-direction: column; gap: 4px; }
  .player-label { font-size: 0.78rem; color: #888; }
  .player-badge { font-size: 0.7rem; padding: 1px 6px; border-radius: 3px;
                  margin-left: 6px; vertical-align: middle; }
  .badge-live { background: #c33; color:#fff; }
  .badge-seek { background: #2a5; color:#fff; }
  img.clip   { width: 480px; border: 2px solid #333; border-radius: 4px;
               display: block; background:#000; }
  video      { width: 480px; border: 2px solid #2a5; border-radius: 4px;
               background: #000; display: none; }
  .hint      { font-size: 0.75rem; color: #666; margin-top: 3px; }
  pre        { background: #1a1a1a; padding: 10px; border-radius: 4px;
               font-size: 0.78rem; overflow-x: auto; max-width: 820px; }
  a          { color: #4af; text-decoration: none; }
  a:hover    { text-decoration: underline; }
</style>
</head>
<body>
<p><a href="/">← 목록으로</a></p>
<h1>이벤트: {{ event_id }}</h1>
<p style="color:#777;font-size:0.85rem;">발생: {{ triggered_str }}</p>

<h2>이벤트 클립</h2>
<div class="players">

  <!-- USB1 전방 -->
  <div class="player-box">
    <span class="player-label">
      USB1 (전방)
      <span class="player-badge badge-live" id="usb1-badge">즉시재생</span>
    </span>
    <img class="clip" id="usb1-img" src="/events/{{ event_id }}/stream/usb1">
    <video id="usb1-video" controls>
      <source src="/events/{{ event_id }}/video/usb1" type="video/mp4">
    </video>
    <div class="hint" id="usb1-hint">MP4 변환 중… 완료되면 탐색 가능 버전으로 자동 전환</div>
  </div>

  <!-- USB -->
  <div class="player-box">
    <span class="player-label">
      USB (후방)
      <span class="player-badge badge-live" id="usb-badge">즉시재생</span>
    </span>
    <img class="clip" id="usb-img" src="/events/{{ event_id }}/stream/usb">
    <video id="usb-video" controls>
      <source src="/events/{{ event_id }}/video/usb" type="video/mp4">
    </video>
    <div class="hint" id="usb-hint">MP4 변환 중… 완료되면 탐색 가능 버전으로 자동 전환</div>
  </div>

</div>

<h2>메타데이터</h2>
<pre>{{ meta_json }}</pre>

<script>
  const EVENT_ID = "{{ event_id }}";
  let pollTimer = null;
  let pollCount = 0;
  const MAX_POLLS = 60;   // 최대 2분 폴링

  function switchToSeekable(cam) {
    document.getElementById(cam + '-img').style.display   = 'none';
    document.getElementById(cam + '-video').style.display = 'block';
    const badge = document.getElementById(cam + '-badge');
    badge.textContent = '탐색가능';
    badge.className   = 'player-badge badge-seek';
    document.getElementById(cam + '-hint').textContent =
      '✓ 탐색 가능 버전 (앞/뒤 이동 가능)';
  }

  function pollStatus() {
    if (pollCount++ >= MAX_POLLS) return;   // 타임아웃

    fetch('/api/events/' + EVENT_ID + '/status')
      .then(r => r.json())
      .then(data => {
        if (data.usb1_ready) switchToSeekable('usb1');
        if (data.usb_ready) switchToSeekable('usb');
        if (!data.usb1_ready || !data.usb_ready) {
          pollTimer = setTimeout(pollStatus, 2000);
        }
      })
      .catch(() => { pollTimer = setTimeout(pollStatus, 3000); });
  }

  pollStatus();
</script>
</body>
</html>"""


# ---------------------------------------------------------------------------
# Routes — 라이브 스트림
# ---------------------------------------------------------------------------

@app.route("/")
def index():
    return render_template_string(_INDEX_HTML)


@app.route("/stream/usb1")
def stream_usb1():
    if _usb1 is None:
        abort(503)
    return Response(_usb1.iter_frames(),
                    mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/stream/usb")
def stream_usb():
    if _usb is None:
        abort(503)
    return Response(_usb.iter_frames(),
                    mimetype="multipart/x-mixed-replace; boundary=frame")


# ---------------------------------------------------------------------------
# Routes — API
# ---------------------------------------------------------------------------

@app.route("/api/events")
def api_events():
    if _db is None:
        abort(503)
    return jsonify(_db.get_events(limit=50))


@app.route("/api/events/sse")
def api_events_sse():
    q: queue.Queue = queue.Queue(maxsize=10)
    with _sse_lock:
        _sse_queues.append(q)

    def stream():
        try:
            while True:
                try:
                    event_id = q.get(timeout=25)
                    yield f"data: {event_id}\n\n"
                except queue.Empty:
                    yield "data: ping\n\n"
        finally:
            with _sse_lock:
                try:
                    _sse_queues.remove(q)
                except ValueError:
                    pass

    return Response(stream(), mimetype="text/event-stream",
                    headers={"Cache-Control": "no-cache",
                             "X-Accel-Buffering": "no"})


@app.route("/api/events/<event_id>/status")
def api_event_status(event_id: str):
    """MP4 변환 완료 여부를 반환 — 이벤트 상세 페이지 JS 폴링용."""
    base = config.EVENTS_DIR / f"event_{event_id}"
    return jsonify({
        "usb1_ready": (base / "front_clip.mp4").exists(),
        "usb_ready": (base / "usb_clip.mp4").exists(),
    })


# ---------------------------------------------------------------------------
# Routes — 이벤트 상세
# ---------------------------------------------------------------------------

@app.route("/events/<event_id>")
def event_detail(event_id: str):
    if _db is None:
        abort(503)
    row = _db.get_event(event_id)
    if row is None:
        abort(404)

    triggered_str = __import__("time").strftime(
        "%Y-%m-%d %H:%M:%S",
        __import__("time").localtime(row["triggered_at"]),
    )
    meta_path = config.EVENTS_DIR / f"event_{event_id}" / "metadata.json"
    meta_json = meta_path.read_text() if meta_path.exists() else "{}"

    return render_template_string(
        _EVENT_HTML,
        event_id=event_id,
        triggered_str=triggered_str,
        meta_json=meta_json,
    )


@app.route("/events/<event_id>/stream/usb1")
def stream_event_usb1(event_id: str):
    """이벤트 클립 AVI를 MJPEG로 스트리밍 — MP4 변환 전 즉시재생용."""
    avi = config.EVENTS_DIR / f"event_{event_id}" / "front_clip.avi"
    if not avi.exists():
        abort(404)
    fps = config.USB1_FPS

    def generate():
        cap = cv2.VideoCapture(str(avi))
        interval = 1.0 / fps
        try:
            while cap.isOpened():
                t0 = __import__("time").time()
                ret, frame = cap.read()
                if not ret:
                    break
                ok, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 75])
                if ok:
                    yield (b"--frame\r\nContent-Type: image/jpeg\r\n\r\n"
                           + buf.tobytes() + b"\r\n")
                elapsed = __import__("time").time() - t0
                __import__("time").sleep(max(0.0, interval - elapsed))
        finally:
            cap.release()

    return Response(generate(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/events/<event_id>/stream/usb")
def stream_event_usb(event_id: str):
    """이벤트 클립 AVI를 MJPEG로 스트리밍 — MP4 변환 전 즉시재생용."""
    avi = config.EVENTS_DIR / f"event_{event_id}" / "usb_clip.avi"
    if not avi.exists():
        abort(404)
    fps = config.USB2_FPS

    def generate():
        cap = cv2.VideoCapture(str(avi))
        interval = 1.0 / fps
        try:
            while cap.isOpened():
                t0 = __import__("time").time()
                ret, frame = cap.read()
                if not ret:
                    break
                ok, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 75])
                if ok:
                    yield (b"--frame\r\nContent-Type: image/jpeg\r\n\r\n"
                           + buf.tobytes() + b"\r\n")
                elapsed = __import__("time").time() - t0
                __import__("time").sleep(max(0.0, interval - elapsed))
        finally:
            cap.release()

    return Response(generate(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/events/<event_id>/video/usb1")
def video_event_usb1(event_id: str):
    """MP4 우선 서빙, 없으면 AVI 폴백."""
    base = config.EVENTS_DIR / f"event_{event_id}"
    if (base / "front_clip.mp4").exists():
        return send_file(str(base / "front_clip.mp4"), mimetype="video/mp4", conditional=True)
    if (base / "front_clip.avi").exists():
        return send_file(str(base / "front_clip.avi"), mimetype="video/x-msvideo", conditional=True)
    abort(404)


@app.route("/events/<event_id>/video/usb")
def video_event_usb(event_id: str):
    """MP4 우선 서빙, 없으면 AVI 폴백."""
    base = config.EVENTS_DIR / f"event_{event_id}"
    if (base / "usb_clip.mp4").exists():
        return send_file(str(base / "usb_clip.mp4"), mimetype="video/mp4", conditional=True)
    if (base / "usb_clip.avi").exists():
        return send_file(str(base / "usb_clip.avi"), mimetype="video/x-msvideo", conditional=True)
    abort(404)
