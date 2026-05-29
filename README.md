<div align="center">

# ReV-CaRS

### Remote Vehicle Call & Return System

카셰어링 차량의 고정 대여·반납 위치 문제를 해결하기 위해  
관제 PC에서 차량을 원격 호출·이동·회수할 수 있도록 구현한  
**RC카 기반 차량용 통신 제어 PoC 프로젝트**

<p>
  <img src="https://img.shields.io/badge/Infineon-TC375-005B95?style=for-the-badge" alt="TC375" />
  <img src="https://img.shields.io/badge/Raspberry%20Pi-4-C51A4A?style=for-the-badge" alt="Raspberry Pi 4" />
  <img src="https://img.shields.io/badge/PC-React%20%2B%20Flask-2563EB?style=for-the-badge" alt="React Flask" />
</p>
<p>
  <img src="https://img.shields.io/badge/CAN%20FD-ECU%20Control-15803D?style=for-the-badge" alt="CAN FD" />
  <img src="https://img.shields.io/badge/Ethernet-UDP%20%2F%20TCP-0F766E?style=for-the-badge" alt="Ethernet" />
  <img src="https://img.shields.io/badge/UDS%20%2F%20DoIP-Diagnostics-7C3AED?style=for-the-badge" alt="UDS DoIP" />
  <img src="https://img.shields.io/badge/SOME%2FIP-Accident%20Service-B45309?style=for-the-badge" alt="SOME/IP" />
</p>
</div>

---

## Contributors

<div align="center">
<table>
  <tr>
    <td align="center">
      <a href="https://github.com/Gon0304">
        <img src="https://github.com/Gon0304.png" width="100px;" alt="김태곤"/>
        <br />
        <sub><b>김태곤</b></sub>
      </a>
    </td>
    <td align="center">
      <a href="https://github.com/kookjd7759">
        <img src="https://github.com/kookjd7759.png" width="100px;" alt="국동균"/>
        <br />
        <sub><b>국동균</b></sub>
      </a>
    </td>
    <td align="center">
      <a href="https://github.com/LSA31">
        <img src="https://github.com/LSA31.png" width="100px;" alt="이승아"/>
        <br />
        <sub><b>이승아</b></sub>
      </a>
    </td>
    <td align="center">
      <a href="https://github.com/cenway">
        <img src="https://github.com/cenway.png" width="100px;" alt="윤한준"/>
        <br />
        <sub><b>윤한준</b></sub>
      </a>
    </td>
    <td align="center">
      <a href="https://github.com/chohabin">
        <img src="https://github.com/chohabin.png" width="100px;" alt="조하빈"/>
        <br />
        <sub><b>조하빈</b></sub>
      </a>
    </td>
  </tr>
</table>
</div>

## 산출물 (Deliverables)

<div align="center">

<p>
  <img src="https://img.shields.io/badge/Deliverables-6%20HTML-1F4E79?style=for-the-badge" alt="Deliverables 6 HTML" />
  <img src="https://img.shields.io/badge/Layout-Spreadsheet%20Style-2E7D32?style=for-the-badge" alt="Spreadsheet Style" />
  <img src="https://img.shields.io/badge/Docs-Requirements%20%2F%20Interface%20%2F%20PinMap-0F766E?style=for-the-badge" alt="Docs" />
</p>

</div>

| Sheet | 문서 | 유형 | 바로가기 |
| --- | --- | --- | --- |
| S-01 | 시스템 개념 정의서 | Concept | [Open](<docs/1. 시스템 개념 정의서.html>) |
| S-02 | 유저 요구사항 | Requirements | [Open](<docs/2. 유저 요구사항.html>) |
| S-03 | 기능 요구사항 | Requirements | [Open](<docs/3. 기능 요구사항.html>) |
| S-04 | 시스템 요구사항 | Requirements | [Open](<docs/4. 시스템 요구사항.html>) |
| S-05 | 인터페이스 | Interface | [Open](<docs/5. 인터페이스.html>) |
| S-06 | Pin Map | HW Mapping | [Open](<docs/6. PinMap.html>) |

## 1. 프로젝트 소개

> ReV-CaRS는 **관제 PC의 원격 명령을 Ethernet과 CAN 기반 차량 내부 네트워크로 전달하여, RC카의 구동·조향·제동·등화·진단·사고 이력 조회까지 통합 제어하는 시스템**입니다.  
> 사용자가 차량이 있는 곳까지 이동해야 하는 기존 카셰어링 구조의 불편을 줄이고, 차량 호출과 반납 회수를 원격 운전 방식으로 수행할 수 있는 제어 구조를 검증하기 위해 개발했습니다.

<table>
  <tr>
    <td>
      특히 다음과 같은 상황을 대상으로 합니다.
      <br /><br />
      - 사용자가 원하는 위치로 차량을 호출해야 하는 상황<br />
      - 이용 종료 후 차량을 원격으로 회수·주차해야 하는 상황<br />
      - 원격 제어 명령이 ECU 네트워크를 거쳐 안전하게 전달되어야 하는 상황<br />
      - 차량 상태, 진단 결과, 사고 영상 이력을 관제 화면에서 확인해야 하는 상황<br />
      - 통신 단절, 비정상 명령, 충돌 이벤트 발생 시 안전 상태로 전환해야 하는 상황
    </td>
  </tr>
</table>

## 2. 프로젝트 목표

- 카셰어링 차량의 호출·반납 문제를 원격 제어 기반 서비스 흐름으로 구현
- 관제 PC 입력을 차량 제어 명령으로 변환하고 Ethernet으로 MAIN ECU에 전달
- MAIN ECU에서 제어 명령을 CAN 프레임으로 변환해 ACT/BODY ECU로 라우팅
- RC카의 구동, 조향, 제동, 기어, 등화, 경적 기능을 원격으로 제어
- 차량 속도, 주행 상태, 이벤트, 진단 결과를 관제 화면에 실시간 표시
- UDS/DoIP 기반 진단으로 MAIN, ACT, BODY, MEDIA 장치 상태 확인
- SOME/IP 기반 사고 영상 목록 조회와 블랙박스 이벤트 기록 기능 구현
- 통신 단절, 비인가 명령, 충돌 상황에서 안전 정지와 오류 기록 수행
- Wireshark와 PCAN-View로 Ethernet 패킷과 CAN 프레임 검증

## 3. 주요 기능

| 구분 | 내용 |
| --- | --- |
| **3-1. 관제 PC 원격 제어** | **차량 리스트 화면**: 차량 상태와 원격 제어 가능 여부 표시<br/>**원격 조종 화면**: 키보드 입력 기반 전진, 후진, 좌우 조향, 브레이크, 기어 변경 제어<br/>**상태 피드백**: 속도, 주행 여부, 최신 이벤트, 통신 상태 표시<br/>**전후방 영상 연동**: MEDIA Pi의 카메라 스트림을 관제 화면에서 확인 |
| **3-2. Ethernet-CAN 제어 게이트웨이** | **UDP 제어 수신**: PC에서 송신한 제어 패킷을 MAIN ECU가 수신<br/>**PDU/Frame Routing**: 수신 데이터를 ACT 제어명령, BODY 제어명령, 상태 피드백으로 분리<br/>**CAN 프레임 변환**: 구동/조향/제동 명령은 ACT ECU, 등화/경적/경고 명령은 BODY ECU로 전달 |
| **3-3. ACT ECU 구동 제어** | **CAN FD 명령 수신**: MAIN → ACT 제어 프레임 처리<br/>**모터 제어**: D/R 기어와 가속 명령에 따라 전진·후진 수행<br/>**제동 제어**: 브레이크 및 safety override 명령 시 모터 정지<br/>**서보 조향**: 좌/우 조향 입력에 따라 서보 모터 위치 제어<br/>**Timeout Fail-Safe**: 제어 프레임 미수신 시 안전 상태 진입 |
| **3-4. BODY ECU 차체 제어** | **등화 제어**: 전조등, 브레이크등, 좌/우 방향지시등, 비상등 출력<br/>**경적 및 경고음**: 부저를 이용한 경적·충돌 경고 출력<br/>**초음파 장애물 감지**: 거리 단계에 따라 경고 패턴 변경<br/>**충돌 이벤트 감지**: 버튼 입력 기반 충돌 이벤트 생성<br/>**램프 진단**: 피드백 핀으로 출력 이상 여부 확인 |
| **3-5. MEDIA Pi 블랙박스** | **전후방 카메라 녹화**: USB 카메라 기반 연속 녹화 및 이벤트 저장<br/>**마이크 녹음**: 사고 전후 음성 데이터 저장<br/>**Flask 스트리밍 서버**: 실시간 영상 및 이벤트 영상 제공<br/>**SOME/IP 사고 이력 서비스**: PC의 사고 목록 요청에 영상 메타데이터 응답<br/>**DoIP 자체 진단**: 카메라, 마이크, 저장공간, 서버 상태를 DID/DTC로 제공 |
| **3-6. 진단 및 안전 제어** | **UDS 진단**: DiagnosticSessionControl, ReadDataByIdentifier, ReadDTCInformation, ClearDTC 지원<br/>**Routing Activation**: PC에서 MAIN/MEDIA 진단 세션 진입<br/>**기능 테스트 루틴**: ACT 모터·서보, BODY 부저·LED·초음파 테스트<br/>**Fail-Safe**: 통신 단절, 진단 실패, 비정상 명령, 충돌 이벤트 발생 시 제어 제한 |

## 프로젝트 시연

### 시스템 아키텍처

> 관제 PC, MAIN Gateway, ACT ECU, BODY ECU, MEDIA Pi가 Ethernet/CAN/SOME-IP/DoIP로 연결되는 전체 구조를 나타냅니다.

<p align="center">
  <img src="./docs/resources/cellImage_2015213638_0.jpg" width="90%" alt="시스템 아키텍처" />
</p>

### 핵심 시연 흐름

| 단계 | 시나리오 |
| --- | --- |
| 1 | 관제 PC에서 차량을 선택하고 원격 제어 화면으로 진입 |
| 2 | UDS/DoIP 진단으로 MAIN, ACT, BODY, MEDIA 상태 확인 |
| 3 | 키보드 입력으로 RC카 전진, 후진, 조향, 브레이크, 기어 변경 수행 |
| 4 | MAIN ECU가 Ethernet 제어 패킷을 CAN 제어 프레임으로 변환 |
| 5 | ACT ECU와 BODY ECU가 각각 구동부와 차체 전장 장치를 제어 |
| 6 | MEDIA Pi가 전후방 영상과 사고 이력 데이터를 제공 |
| 7 | 충돌, 통신 단절, 비인가 명령 상황에서 Fail-Safe 동작 확인 |

## 4. 시스템 구성

본 시스템은 5개의 주요 파트로 구성됩니다.

- **PC Control Center** - React 기반 관제 UI, Flask 제어 백엔드, 진단/사고 이력 조회 클라이언트
- **MAIN / Gateway ECU (TC375)** - Ethernet 수신, DoIP 처리, SOME/IP 처리, PDU 라우팅, CAN 제어 송신
- **ACT ECU (TC375)** - 구동 모터, 브레이크, 서보 조향, 속도/상태 피드백, ACT 진단
- **BODY ECU (TC375)** - 전조등, 방향지시등, 브레이크등, 경적, 초음파 경고, 충돌 이벤트, BODY 진단
- **MEDIA Pi (Raspberry Pi 4)** - 전후방 카메라, 마이크, 블랙박스 저장, Flask 스트리밍, SOME/IP, DoIP 진단

### 네트워크 아키텍처

| 구간 | 통신 방식 | 주요 데이터 |
| --- | --- | --- |
| PC ↔ MAIN ECU | Ethernet UDP/TCP | 원격 제어 명령, 차량 상태 피드백, DoIP 진단 |
| MAIN ECU ↔ ACT ECU | CAN FD | 구동/조향/제동 명령, ACT 상태, ACT UDS 응답 |
| MAIN ECU ↔ BODY ECU | CAN FD | 등화/경적/경고 명령, BODY 상태, 충돌 이벤트, BODY UDS 응답 |
| PC ↔ MEDIA Pi | Ethernet / SOME-IP / DoIP | 사고 영상 목록, 미디어 장치 진단, 영상 스트림 |
| MEDIA Pi 내부 | Flask / SQLite / Recorder | 영상 저장, 이벤트 DB, 실시간 스트리밍 |

## 5. 개발 포인트

- **차량용 통신 구조 구현**: Ethernet, UDP, TCP, CAN FD, SOME/IP, DoIP를 하나의 시나리오 안에서 연동
- **Gateway 기반 라우팅**: PC 제어 명령을 ACT/BODY CAN 프레임으로 분배하는 PDU/Frame 기반 라우팅 구현
- **분산 ECU 제어**: MAIN, ACT, BODY, MEDIA가 역할을 나누어 제어·진단·미디어 기능 수행
- **원격 관제 UI**: 차량 리스트, 진단, 원격 조종, 사고 이력을 하나의 React 화면에서 제공
- **진단 기능 통합**: UDS/DoIP 기반 DID, DTC, Routine Control 흐름 구현
- **블랙박스 서비스**: 충돌 이벤트와 전후방 영상 저장 정보를 SOME/IP 서비스로 조회
- **Fail-Safe 설계**: timeout, 비정상 명령, 충돌 이벤트, 통신 두절에 대한 안전 상태 전환 고려
- **검증 가능성 확보**: Wireshark와 PCAN-View에서 패킷과 CAN 프레임을 확인할 수 있는 구조 설계

## 6. 테스트 및 검증

| 검증 항목 | 검증 방법 |
| --- | --- |
| PC 원격 제어 | React UI에서 키 입력 후 Flask 백엔드 제어 패킷 생성 확인 |
| Ethernet 제어 패킷 | Wireshark로 PC → MAIN UDP 제어 데이터 확인 |
| CAN 제어 프레임 | PCAN-View에서 ACT/BODY CAN ID와 Data Field 확인 |
| ACT 구동 제어 | 전진, 후진, 조향, 브레이크, 기어별 모터 동작 확인 |
| BODY 등화 제어 | 전조등, 방향지시등, 비상등, 브레이크등, 경적 출력 확인 |
| UDS/DoIP 진단 | 진단 화면에서 Routing Activation, DID, DTC, Routine 결과 확인 |
| SOME/IP 사고 이력 | PC 사고 이력 화면에서 MEDIA Pi 영상 목록 응답 확인 |
| Fail-Safe | 통신 단절, 비인가 명령, 충돌 이벤트 발생 시 안전 상태 전환 확인 |

## 7. 실행 방법

### PC 관제 프로그램

```bash
cd PC
python -m pip install -r src/pc_backend/requirements.txt
npm install
python start_pc.py
```

실행 후 접속 주소:

- React 관제 UI: `http://127.0.0.1:5173`
- PC Backend API: `http://127.0.0.1:5000`

### MEDIA Pi 블랙박스

```bash
cd MEDIA
python -m blackbox.main
```

주요 서비스:

- Flask 영상/이벤트 서버: `http://<MEDIA_PI_IP>:8080`
- SOME/IP AccidentHistoryService: UDP `30491`
- DoIP 진단 게이트웨이: TCP/UDP `13401`

## 8. 담당 파트

| 파트 | 담당 범위 |
| --- | --- |
| **PC** | 관제 UI, 원격 제어 화면, 진단 화면, 사고 이력 화면, Flask 제어 백엔드 |
| **MAIN / Gateway** | Ethernet 수신, LwIP, SoAd, Service Discovery, DoIP, SOME/IP, COM/PduR 라우팅 |
| **ACT** | CAN FD 수신, 구동 모터 제어, 서보 조향, 브레이크, 속도/상태 송신, ACT UDS |
| **BODY** | 등화, 부저, 초음파 경고, 충돌 이벤트, 램프 진단, BODY UDS |
| **MEDIA** | 전후방 카메라, 마이크, 이벤트 녹화, SQLite 로그, Flask 스트리밍, SOME/IP, DoIP |
| **Docs** | 요구사항, 시스템 요구사항, 인터페이스, Pin Map, 아키텍처 산출물 정리 |

## 9. 기술 스택

| Hardware | Embedded / Network | PC / Media | Tools |
| --- | --- | --- | --- |
| Infineon **TC375**<br/>Raspberry Pi 4<br/>RC Car<br/>DC Motor, Servo Motor<br/>LED, Buzzer, Ultrasonic Sensor<br/>USB Camera, Microphone | **C**<br/>CAN / CAN FD<br/>Ethernet / UDP / TCP<br/>LwIP<br/>UDS / DoIP<br/>SOME/IP-like Service<br/>PDU / Frame Routing | **Python 3**<br/>Flask<br/>React / Vite<br/>SQLite<br/>someipy<br/>doipclient / udsoncan | AURIX Development Studio<br/>VS Code<br/>GitHub<br/>Wireshark<br/>PCAN-View |

## 10. 프로젝트 의의

ReV-CaRS는 단순 RC카 조종이 아니라,  
**관제 입력 → Ethernet 제어 패킷 → MAIN Gateway → CAN 제어 프레임 → ACT/BODY 동작 → 상태·진단·사고 이력 피드백**까지 이어지는 차량용 통신 제어 흐름을 구현한 프로젝트입니다.

특히 원격 호출·회수 서비스 시나리오 안에서 ECU 제어, 진단, 미디어, 사고 이력, Fail-Safe를 하나로 연결해 보았다는 점에서 의미가 있습니다.

## 11. 아쉬웠던 점 및 개선 방향

- 원격 제어 패킷 인증 및 HMAC 검증 로직 고도화 필요
- 통신 지연 및 패킷 손실 상황에 대한 정량 테스트 보완 필요
- 영상 스트리밍 품질과 카메라 장치 예외 처리 개선 필요
- 진단 결과와 DTC 로그의 화면 필터링 기능 고도화 필요
- CAN/CAN FD 성능 비교 결과를 표와 캡처 자료로 추가 정리 필요
- 실제 차량 환경을 고려한 제동 우선순위와 안전 정책 보완 필요

## 12. 저장소 구조

| 디렉터리 | 설명 |
| --- | --- |
| **PC/** | React 관제 화면과 Flask 기반 PC 제어 백엔드가 포함되어 있습니다. 차량 리스트, 진단, 원격 조종, 사고 이력 화면과 SOME/IP/DoIP 클라이언트 코드가 포함됩니다. |
| **MAIN/** | TC375 기반 MAIN/Gateway ECU 프로젝트입니다. Ethernet 수신, LwIP, UDP/TCP, DoIP, SOME/IP, COM, PduR, CAN 라우팅 및 상태 피드백 로직이 포함됩니다. |
| **ACT/** | TC375 기반 ACT ECU 프로젝트입니다. MAIN에서 받은 CAN FD 제어명령을 바탕으로 구동 모터, 브레이크, 서보 조향을 제어하고 상태 및 진단 응답을 송신합니다. |
| **BODY/** | TC375 기반 BODY ECU 프로젝트입니다. 전조등, 방향지시등, 브레이크등, 부저, 초음파 경고, 충돌 이벤트, 램프 진단 기능을 담당합니다. |
| **MEDIA/** | Raspberry Pi 기반 미디어/블랙박스 파트입니다. 전후방 카메라, 마이크, 이벤트 녹화, Flask 스트리밍, SOME/IP 사고 이력 서비스, DoIP 자체 진단 게이트웨이가 포함됩니다. |
| **GATEWAY/** | Gateway 관련 정리 공간입니다. |
| **docs/** | 시스템 개념 정의서, 유저 요구사항, 기능 요구사항, 시스템 요구사항, 인터페이스, Pin Map HTML 산출물이 포함됩니다. |
