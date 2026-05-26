import { useEffect, useRef, useState } from "react";

const PC_CONTROL_BASE_URL = "http://127.0.0.1:5000";
const MEDIA_PI_BASE_URL = "http://192.168.20.2:8080";

// 원격 조종 제어 / 상태 조회는 PC backend 사용
const REMOTE_START_URL = `${PC_CONTROL_BASE_URL}/remote-control/start`;
const REMOTE_STOP_URL = `${PC_CONTROL_BASE_URL}/remote-control/stop`;
const CONTROL_URL = `${PC_CONTROL_BASE_URL}/control`;
const VEHICLE_STATUS_URL = `${PC_CONTROL_BASE_URL}/vehicle/status`;

const WARNING_LIGHT_URL = `${PC_CONTROL_BASE_URL}/body/warning-light`;
const LIVE_FRONT_URL = `${MEDIA_PI_BASE_URL}/stream/usb1`;
const LIVE_REAR_URL = `${MEDIA_PI_BASE_URL}/stream/usb`;

const initialPressedKeys = {
  ArrowUp: false,
  ArrowDown: false,
  ArrowLeft: false,
  ArrowRight: false,
};

function RemoteControlPage({ onBack }) {
  const [speed, setSpeed] = useState(null);
  const [lastSpeedReceivedAt, setLastSpeedReceivedAt] = useState(0);

  const [gear, setGear] = useState("P");
  const [brakeOn, setBrakeOn] = useState(false);
  const [leftSignalOn, setLeftSignalOn] = useState(false);
  const [rightSignalOn, setRightSignalOn] = useState(false);
  const [hazardOn, setHazardOn] = useState(false);
  const [ignitionOn, setIgnitionOn] = useState(false);
  const [warningLightOn, setWarningLightOn] = useState(false);
  const [warningLightPending, setWarningLightPending] = useState(false);
  const [hornOn, setHornOn] = useState(false);
  const [latestEventName, setLatestEventName] = useState("");

  const [pressedKeys, setPressedKeys] = useState(initialPressedKeys);

  const pressedKeysRef = useRef(initialPressedKeys);
  const lastDriveCommandRef = useRef("STOP");
  const brakeOnRef = useRef(false);

  const communicationConnected =
    speed !== null && Date.now() - lastSpeedReceivedAt < 3000;

  const sendControlCommand = async (type, value) => {
    try {
      await fetch(CONTROL_URL, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          type,
          value,
        }),
      });
    } catch (error) {
      console.error("제어 명령 전송 실패:", type, value, error);
    }
  };

  const getDriveCommand = (keys) => {
    if (keys.ArrowUp && keys.ArrowLeft) return "FORWARD_LEFT";
    if (keys.ArrowUp && keys.ArrowRight) return "FORWARD_RIGHT";
    if (keys.ArrowDown && keys.ArrowLeft) return "BACKWARD_LEFT";
    if (keys.ArrowDown && keys.ArrowRight) return "BACKWARD_RIGHT";
    if (keys.ArrowUp) return "FORWARD";
    if (keys.ArrowDown) return "BACKWARD";
    if (keys.ArrowLeft) return "LEFT";
    if (keys.ArrowRight) return "RIGHT";
    return "STOP";
  };

  const sendDriveCommandIfChanged = (keys) => {
    const nextCommand = getDriveCommand(keys);

    if (lastDriveCommandRef.current === nextCommand) {
      return;
    }

    lastDriveCommandRef.current = nextCommand;
    sendControlCommand("drive", nextCommand);
  };

  const handleBrakeOn = () => {
    if (brakeOnRef.current) return;

    brakeOnRef.current = true;
    setBrakeOn(true);
    sendControlCommand("brake", "ON");
  };

  const handleBrakeOff = () => {
    if (!brakeOnRef.current) return;

    brakeOnRef.current = false;
    setBrakeOn(false);
    sendControlCommand("brake", "OFF");
  };

  useEffect(() => {
    fetch(REMOTE_START_URL, { method: "POST" }).catch((error) => {
      console.error("원격 제어 ON 실패:", error);
    });

    return () => {
      fetch(REMOTE_STOP_URL, { method: "POST" }).catch((error) => {
        console.error("원격 제어 OFF 실패:", error);
      });
    };
  }, []);

  useEffect(() => {
    const statusIntervalId = setInterval(async () => {
      try {
        const response = await fetch(VEHICLE_STATUS_URL);
        const data = await response.json();

        if (data.result === "OK") {
          if (data.speed_connected && typeof data.speed === "number") {
            setSpeed(data.speed);
            setLastSpeedReceivedAt(Date.now());
          } else if (!data.speed_connected) {
            setSpeed(null);
          }

          if (data.latest_event?.event_name) {
            setLatestEventName(data.latest_event.event_name);
          }
        }
      } catch (error) {
        console.error("차량 상태 수신 실패:", error);
      }
    }, 1000);

    return () => clearInterval(statusIntervalId);
  }, []);

  useEffect(() => {
    const targetKeys = [
  "ArrowUp",
  "ArrowDown",
  "ArrowLeft",
  "ArrowRight",
  " ",
  "p",
  "P",
  "r",
  "R",
  "n",
  "N",
  "d",
  "D",
];

    const handleKeyDown = (event) => {
      if (!targetKeys.includes(event.key)) return;

      event.preventDefault();

      if (event.key === " ") {
        handleBrakeOn();
        return;
      }

      const gearKey = event.key.toUpperCase();

if (["P", "R", "N", "D"].includes(gearKey)) {
  handleGearClick(gearKey);
  return;
}

      if (pressedKeysRef.current[event.key]) {
        return;
      }

      const nextPressedKeys = {
        ...pressedKeysRef.current,
        [event.key]: true,
      };

      pressedKeysRef.current = nextPressedKeys;
      setPressedKeys(nextPressedKeys);
      sendDriveCommandIfChanged(nextPressedKeys);
    };

    const handleKeyUp = (event) => {
      if (!targetKeys.includes(event.key)) return;

      event.preventDefault();

      if (event.key === " ") {
        handleBrakeOff();
        return;
      }

      const nextPressedKeys = {
        ...pressedKeysRef.current,
        [event.key]: false,
      };

      pressedKeysRef.current = nextPressedKeys;
      setPressedKeys(nextPressedKeys);
      sendDriveCommandIfChanged(nextPressedKeys);
    };

    window.addEventListener("keydown", handleKeyDown);
    window.addEventListener("keyup", handleKeyUp);

    return () => {
      window.removeEventListener("keydown", handleKeyDown);
      window.removeEventListener("keyup", handleKeyUp);

      pressedKeysRef.current = initialPressedKeys;
      setPressedKeys(initialPressedKeys);
      lastDriveCommandRef.current = "STOP";

      brakeOnRef.current = false;
      setBrakeOn(false);

      sendControlCommand("drive", "STOP");
      sendControlCommand("brake", "OFF");
    };
  }, []);

const handleBack = () => {
  onBack();

  sendControlCommand("drive", "STOP");
  sendControlCommand("brake", "OFF");

  fetch(REMOTE_STOP_URL, { method: "POST" }).catch((error) => {
    console.error("원격 제어 OFF 실패:", error);
  });
};

  const getKeyboardCommandText = () => {
    if (pressedKeys.ArrowUp && pressedKeys.ArrowLeft) return "전진 + 좌회전";
    if (pressedKeys.ArrowUp && pressedKeys.ArrowRight) return "전진 + 우회전";
    if (pressedKeys.ArrowDown && pressedKeys.ArrowLeft) return "후진 + 좌회전";
    if (pressedKeys.ArrowDown && pressedKeys.ArrowRight) return "후진 + 우회전";
    if (pressedKeys.ArrowUp) return "전진";
    if (pressedKeys.ArrowDown) return "후진";
    if (pressedKeys.ArrowLeft) return "좌회전";
    if (pressedKeys.ArrowRight) return "우회전";
    return "입력 대기";
  };

  const handleLeftSignalClick = () => {
    const next = !leftSignalOn;

    setLeftSignalOn(next);

    if (next) {
      setRightSignalOn(false);
      sendControlCommand("indicator_left", "ON");
      sendControlCommand("indicator_right", "OFF");
    } else {
      sendControlCommand("indicator_left", "OFF");
    }
  };

  const handleRightSignalClick = () => {
    const next = !rightSignalOn;

    setRightSignalOn(next);

    if (next) {
      setLeftSignalOn(false);
      sendControlCommand("indicator_right", "ON");
      sendControlCommand("indicator_left", "OFF");
    } else {
      sendControlCommand("indicator_right", "OFF");
    }
  };

  const handleHazardClick = () => {
    const next = !hazardOn;
    setHazardOn(next);
    sendControlCommand("hazard", next ? "ON" : "OFF");
  };

  const handleHornOn = () => {
    setHornOn(true);
    sendControlCommand("horn", "ON");
  };

  const handleHornOff = () => {
    setHornOn(false);
    sendControlCommand("horn", "OFF");
  };

  const handleWarningLightClick = async () => {
    if (warningLightPending) return;

    const previous = warningLightOn;
    const next = !warningLightOn;
    const nextState = next ? 1 : 0;

    setWarningLightOn(next);
    setWarningLightPending(true);

    try {
      const response = await fetch(WARNING_LIGHT_URL, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          enable: nextState,
        }),
      });

      const data = await response.json();

      const resultText = String(data.result || "").toUpperCase();

      if (resultText !== "OK") {
        setWarningLightOn(previous);
        alert(data.message || data.detail || "제어 명령 오류");
        return;
      }

      setWarningLightOn(data.state === 1);
    } catch (error) {
      setWarningLightOn(previous);
      console.error("충돌방지 경고등 제어 실패:", error);
      alert("PC backend 서버에 연결할 수 없습니다.");
    } finally {
      setWarningLightPending(false);
    }
  };

  const handleIgnitionClick = () => {
    const next = !ignitionOn;
    setIgnitionOn(next);
    sendControlCommand("ignition", next ? "ON" : "OFF");
  };

  const handleGearClick = (nextGear) => {
    setGear(nextGear);
    sendControlCommand("gear", nextGear);
  };

  return (
    <div className="remote-page">
      <header className="remote-top-bar">
        <button className="remote-back-button" onClick={handleBack}>
          ← 차량 리스트로
        </button>

        <div className="remote-title-box">
          <h1>PC 원격 조종 화면</h1>
          <p>Remote Vehicle Call & Return System</p>
        </div>

        <div className="remote-status-group">
          <span
            className={`connection-pill ${
              communicationConnected ? "connected" : "disconnected"
            }`}
          >
            통신 상태: {communicationConnected ? "연결됨" : "끊김"}
          </span>

          <span className="battery-pill">
            이벤트: {latestEventName || "없음"}
            <span className="battery-icon">
              <span />
            </span>
          </span>
        </div>
      </header>

      <main className="remote-layout">
        <section className="camera-main-panel">
          <div className="camera-label">전방 카메라</div>

          <div className="camera-stream-box">
            <img
              className="live-camera-stream"
              src={LIVE_FRONT_URL}
              alt="전방 실시간 영상"
            />
          </div>
        </section>

        <aside className="right-control-panel">
          <section className="camera-sub-panel">
            <div className="camera-label">후방 카메라</div>

            <div className="camera-stream-box">
              <img
                className="live-camera-stream"
                src={LIVE_REAR_URL}
                alt="후방 실시간 영상"
              />
            </div>
          </section>

          <section className="signal-panel">
            <h3>방향지시등</h3>

            <div className="signal-buttons">
              <button
                className={`signal-button ${leftSignalOn ? "active" : ""}`}
                onClick={handleLeftSignalClick}
              >
                ←<span>좌</span>
              </button>

              <button
                className={`signal-button ${rightSignalOn ? "active" : ""}`}
                onClick={handleRightSignalClick}
              >
                →<span>우</span>
              </button>
            </div>
          </section>

          <section className="hazard-panel">
            <h3>비상등</h3>

            <button
              className={`hazard-button ${hazardOn ? "active" : ""}`}
              onClick={handleHazardClick}
            >
              △
            </button>
          </section>

          <section className="small-action-grid horn-only-grid">
            <div className="horn-card">
              <h3>경적</h3>

              <button
                className={`horn-button ${hornOn ? "active" : ""}`}
                onMouseDown={handleHornOn}
                onMouseUp={handleHornOff}
                onMouseLeave={handleHornOff}
                onTouchStart={handleHornOn}
                onTouchEnd={handleHornOff}
              >
                📣
              </button>

              <p>{hornOn ? "켜짐" : "꺼짐"}</p>
            </div>
          </section>

          <section className="buzzer-card side-buzzer-card">
            <h3>충돌방지 경고등</h3>

            <button
              className={`buzzer-button ${warningLightOn ? "active" : ""}`}
              type="button"
              onClick={handleWarningLightClick}
              disabled={warningLightPending}
            >
              !
            </button>
          </section>
        </aside>

        <section className="bottom-control-panel compact">
          <div className="ignition-card bottom-ignition-card">
            <h3>시동</h3>

            <button
              className={`power-button ${ignitionOn ? "active" : ""}`}
              onClick={handleIgnitionClick}
            >
              ⏻
            </button>

            <p>{ignitionOn ? "켜짐" : "꺼짐"}</p>
          </div>

          <div className="keyboard-card">
            <h3>키보드 조종 입력</h3>

            <div className="keyboard-command">
              현재 입력: <strong>{getKeyboardCommandText()}</strong>
            </div>

            <div className="arrow-key-grid">
              <div />

              <div className={`arrow-key ${pressedKeys.ArrowUp ? "active" : ""}`}>
                ↑
                <span>전진</span>
              </div>

              <div />

              <div
                className={`arrow-key ${pressedKeys.ArrowLeft ? "active" : ""}`}
              >
                ←
                <span>좌회전</span>
              </div>

              <div
                className={`arrow-key ${pressedKeys.ArrowDown ? "active" : ""}`}
              >
                ↓
                <span>후진</span>
              </div>

              <div
                className={`arrow-key ${pressedKeys.ArrowRight ? "active" : ""}`}
              >
                →
                <span>우회전</span>
              </div>
            </div>
          </div>

          <div className="speed-card">
            <h3>속도</h3>

            <div className="speed-value">
              {speed === null ? "--" : speed}
              <span>km/h</span>
            </div>

            <div className="speed-scale">
              <span>0</span>
              <div>
                <i style={{ width: `${Math.min(speed || 0, 100)}%` }} />
              </div>
              <span>100</span>
            </div>
          </div>

          <div className="gear-card">
            <h3>기어</h3>

            <div className="gear-buttons">
              {["P", "R", "N", "D"].map((item) => (
                <button
                  key={item}
                  className={gear === item ? "active" : ""}
                  onClick={() => handleGearClick(item)}
                >
                  {item}
                </button>
              ))}
            </div>
          </div>

          <div className="brake-card bottom-brake-card">
            <h3>브레이크</h3>

            <button
              className={`brake-button ${brakeOn ? "active" : ""}`}
              type="button"
            >
              !
            </button>

            <p>{brakeOn ? "켜짐" : "꺼짐"}</p>
          </div>
        </section>
      </main>

      <footer className="remote-footer">
        <span>조종 입력: 키보드 방향키</span>
        <span>브레이크: Space 누르는 동안 ON</span>
        <span>현재 시간: {new Date().toLocaleTimeString()}</span>
      </footer>
    </div>
  );
}

export default RemoteControlPage;
