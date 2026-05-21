const express = require("express");
const dgram = require("dgram");
const cors = require("cors");

const app = express();

app.use(cors());
app.use(express.json());

const MAIN_ECU_IP = "192.168.10.2";
const MAIN_CONTROL_PORT = 5000;
const PC_SPEED_PORT = 5001;
const HTTP_PORT = 5100;

const udpControlSocket = dgram.createSocket("udp4");
const udpSpeedSocket = dgram.createSocket("udp4");

const vehicleState = {
  vehicle_id: 1,
  driving_state: 0,
  remote_control_active: false,
};

const controlState = {
  sequence_number: 0,
  accel: 0,
  brake: 0,
  steer: 0,
  gear: 0, // 0=P, 1=R, 2=N, 3=D
  turn_signal: 0, // 0=OFF, 1=LEFT, 2=RIGHT, 3=HAZARD
  horn: 0,
  ignition: 0,
  head_light: 0,
};

let latestSpeedKmh = null;
let lastSpeedReceivedAt = 0;

function resetControlState() {
  controlState.accel = 0;
  controlState.brake = 0;
  controlState.steer = 0;
  controlState.gear = 0;
  controlState.turn_signal = 0;
  controlState.horn = 0;
  controlState.ignition = 0;
  controlState.head_light = 0;
}

function buildControlPacket() {
  const packet = Buffer.alloc(13);

  packet[0] = controlState.sequence_number & 0xff;
  packet[1] = controlState.accel & 0xff;
  packet[2] = controlState.brake & 0xff;
  packet[3] = controlState.steer & 0xff;
  packet[4] = controlState.gear & 0xff;
  packet[5] = controlState.turn_signal & 0xff;
  packet[6] = controlState.horn & 0xff;
  packet[7] = controlState.ignition & 0xff;
  packet[8] = controlState.head_light & 0xff;

  // Byte 9~12: HMAC 임시 0
  packet[9] = 0x00;
  packet[10] = 0x00;
  packet[11] = 0x00;
  packet[12] = 0x00;

  controlState.sequence_number = (controlState.sequence_number + 1) & 0xff;

  return packet;
}

function startUdpControlSender() {
  setInterval(() => {
    const packet = buildControlPacket();

    udpControlSocket.send(
      packet,
      0,
      packet.length,
      MAIN_CONTROL_PORT,
      MAIN_ECU_IP,
      (error) => {
        if (error) {
          console.error("[PC Backend] UDP control send error:", error.message);
        }
      }
    );
  }, 20);

  console.log(
    `[PC Backend] UDP control sender started: ${MAIN_ECU_IP}:${MAIN_CONTROL_PORT}`
  );
}

udpSpeedSocket.on("message", (message, remote) => {
  if (message.length < 3) return;

  const speedRaw = message[1] | (message[2] << 8);
  latestSpeedKmh = speedRaw / 100.0;
  lastSpeedReceivedAt = Date.now();

  console.log(
    `[PC Backend] speed from ${remote.address}:${remote.port} = ${latestSpeedKmh.toFixed(
      2
    )} km/h`
  );
});

udpSpeedSocket.bind(PC_SPEED_PORT, "0.0.0.0", () => {
  console.log(`[PC Backend] UDP speed receiver started: 0.0.0.0:${PC_SPEED_PORT}`);
});

app.get("/health", (req, res) => {
  res.json({
    result: "OK",
    role: "pc-control-backend",
    main_ip: MAIN_ECU_IP,
    main_control_port: MAIN_CONTROL_PORT,
    speed_port: PC_SPEED_PORT,
  });
});

app.get("/vehicle/status", (req, res) => {
  const now = Date.now();
  const speedAgeMs = lastSpeedReceivedAt > 0 ? now - lastSpeedReceivedAt : null;
  const speedConnected = speedAgeMs !== null && speedAgeMs < 500;

  const speed = speedConnected ? latestSpeedKmh : null;
  const drivingState = speed !== null && speed > 0 ? 1 : 0;

  vehicleState.driving_state = drivingState;

  res.json({
    result: "OK",
    vehicle_id: vehicleState.vehicle_id,
    driving_state: drivingState,
    driving_state_text: drivingState === 1 ? "주행 중" : "정지 중",
    remote_control_active: vehicleState.remote_control_active,
    remote_control_text: vehicleState.remote_control_active ? "ON" : "OFF",
    speed: speed !== null ? Number(speed.toFixed(2)) : null,
    speed_connected: speedConnected,
  });
});

app.post("/remote-control/start", (req, res) => {
  vehicleState.remote_control_active = true;
  vehicleState.driving_state = 0;
  resetControlState();

  res.json({
    result: "OK",
    remote_control_active: true,
    remote_control_text: "ON",
  });
});

app.post("/remote-control/stop", (req, res) => {
  vehicleState.remote_control_active = false;
  vehicleState.driving_state = 0;
  resetControlState();

  res.json({
    result: "OK",
    remote_control_active: false,
    remote_control_text: "OFF",
  });
});

app.post("/control", (req, res) => {
  const commandData = req.body;

  if (!commandData) {
    return res.status(400).json({
      result: "BAD_REQUEST",
      error_code: 1,
      message: "control command body is empty",
    });
  }

  const commandType = commandData.type;
  const value = commandData.value;

  if (!commandType) {
    return res.status(400).json({
      result: "BAD_REQUEST",
      error_code: 1,
      message: "type field is required",
    });
  }

  if (commandType === "drive") {
    const validDriveValues = [
      "FORWARD",
      "BACKWARD",
      "LEFT",
      "RIGHT",
      "FORWARD_LEFT",
      "FORWARD_RIGHT",
      "BACKWARD_LEFT",
      "BACKWARD_RIGHT",
      "STOP",
    ];

    if (!validDriveValues.includes(value)) {
      return res.status(400).json({
        result: "BAD_REQUEST",
        error_code: 1,
        message: `invalid drive value: ${value}`,
      });
    }

    controlState.accel = 0;
    controlState.steer = 0;

    if (value === "FORWARD") {
      controlState.accel = 1;
      controlState.gear = 3;
      controlState.steer = 0;
      vehicleState.driving_state = 1;
    } else if (value === "BACKWARD") {
      controlState.accel = 1;
      controlState.gear = 1;
      controlState.steer = 0;
      vehicleState.driving_state = 1;
    } else if (value === "LEFT") {
      controlState.steer = 1;
    } else if (value === "RIGHT") {
      controlState.steer = 2;
    } else if (value === "FORWARD_LEFT") {
      controlState.accel = 1;
      controlState.gear = 3;
      controlState.steer = 1;
      vehicleState.driving_state = 1;
    } else if (value === "FORWARD_RIGHT") {
      controlState.accel = 1;
      controlState.gear = 3;
      controlState.steer = 2;
      vehicleState.driving_state = 1;
    } else if (value === "BACKWARD_LEFT") {
      controlState.accel = 1;
      controlState.gear = 1;
      controlState.steer = 1;
      vehicleState.driving_state = 1;
    } else if (value === "BACKWARD_RIGHT") {
      controlState.accel = 1;
      controlState.gear = 1;
      controlState.steer = 2;
      vehicleState.driving_state = 1;
    } else if (value === "STOP") {
      controlState.accel = 0;
      controlState.steer = 0;
      vehicleState.driving_state = 0;
    }
  } else if (commandType === "brake") {
    controlState.brake = value === "ON" ? 1 : 0;

    if (value === "ON") {
      vehicleState.driving_state = 0;
    }
  } else if (commandType === "gear") {
    const gearMap = {
      P: 0,
      R: 1,
      N: 2,
      D: 3,
    };

    if (!(value in gearMap)) {
      return res.status(400).json({
        result: "BAD_REQUEST",
        error_code: 1,
        message: `invalid gear value: ${value}`,
      });
    }

    controlState.gear = gearMap[value];
  } else if (commandType === "indicator_left") {
    if (value === "ON") {
      controlState.turn_signal = 1;
    } else if (controlState.turn_signal === 1) {
      controlState.turn_signal = 0;
    }
  } else if (commandType === "indicator_right") {
    if (value === "ON") {
      controlState.turn_signal = 2;
    } else if (controlState.turn_signal === 2) {
      controlState.turn_signal = 0;
    }
  } else if (commandType === "hazard") {
    controlState.turn_signal = value === "ON" ? 3 : 0;
  } else if (commandType === "horn") {
    controlState.horn = value === "ON" ? 1 : 0;
  } else if (commandType === "ignition") {
    controlState.ignition = value === "ON" ? 1 : 0;
  } else if (commandType === "head_light") {
    controlState.head_light = value === "ON" ? 1 : 0;
  } else {
    return res.status(400).json({
      result: "BAD_REQUEST",
      error_code: 1,
      message: `unknown control type: ${commandType}`,
    });
  }

  res.json({
    result: "OK",
    command: commandData,
  });
});

app.listen(HTTP_PORT, "127.0.0.1", () => {
  console.log(`[PC Backend] HTTP server running on http://127.0.0.1:${HTTP_PORT}`);
  startUdpControlSender();
});