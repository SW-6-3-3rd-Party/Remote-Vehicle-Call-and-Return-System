import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import "./PreDriveDiagnosisPage.css";

const getDefaultApiBaseUrl = () => {
  const hostname = window.location.hostname || "127.0.0.1";
  return `${window.location.protocol}//${hostname}:5000`;
};

const API_BASE_URL =
  import.meta.env.VITE_GATEWAY_BASE_URL || getDefaultApiBaseUrl();

const FUNCTION_TESTS = [
  { id: "act-motor", ecu: "ACT ECU", label: "모터 테스트", routine: "0x0100" },
  { id: "act-servo", ecu: "ACT ECU", label: "서보 테스트", routine: "0x0101" },
  { id: "body-buzzer", ecu: "BODY ECU", label: "부저 테스트", routine: "0x0200" },
  { id: "body-led", ecu: "BODY ECU", label: "LED 테스트", routine: "0x0201" },
  { id: "body-ultrasonic", ecu: "BODY ECU", label: "초음파 테스트", routine: "0x0202" },
];

const INITIAL_ECUS = [
  {
    id: "main",
    name: "MAIN ECU",
    address: "Central Gateway",
    route: "DoIP 192.168.10.2:13400",
    state: "Standby",
    summary: "Routing Activation 대기 중",
    dids: [
      { label: "Routing Activation", value: "대기" },
      { label: "MAIN 자체 진단", value: "대기" },
    ],
    dtcs: [],
  },
  {
    id: "act",
    name: "ACT ECU",
    address: "Drive Unit",
    route: "MAIN 경유 / CAN Bus 1",
    state: "Standby",
    summary: "Drive / Steering / Brake",
    dids: [],
    dtcs: [],
  },
  {
    id: "body",
    name: "BODY ECU",
    address: "Body Unit",
    route: "MAIN 경유 / CAN Bus 2",
    state: "Standby",
    summary: "Lighting / Ultrasonic / Collision",
    dids: [],
    dtcs: [],
  },
  {
    id: "media",
    name: "MEDIA PI",
    address: "Media Unit",
    route: "DoIP 192.168.20.2:13401",
    state: "Standby",
    summary: "Camera / Audio / EDR",
    dids: [],
    dtcs: [],
  },
];

function getStateClass(state) {
  if (state === "Ready") return "ready";
  if (state === "Normal") return "normal";
  if (state === "Warning") return "warning";
  if (state === "Offline") return "offline";
  if (state === "Running") return "running";
  return "standby";
}

function getDidTone(item) {
  if (item.tone) return item.tone;
  if (item.ok === false) return "bad";

  const value = String(item.value ?? "");
  if (
    ["미연결", "DOWN", "중지", "두절", "응답 오류", "DID 불일치"].includes(
      value
    )
  ) {
    return "bad";
  }

  if (value === "NRC") return "warn";
  return "good";
}

function formatValue(value) {
  if (value === null || value === undefined || value === "") return "--";
  return String(value);
}

function normalizeDids(dids = []) {
  return dids.map((item) => ({
    label: item.label,
    value: formatValue(item.value),
    unit: item.unit,
    ok: item.ok,
    tone: getDidTone(item),
  }));
}

function normalizeDtcs(dtcs) {
  return dtcs?.items || [];
}

function hasActiveDtc(dtcs = []) {
  return dtcs.some((dtc) => dtc.state === "Active" || dtc.status === "Active");
}

function buildMainCard(mainRoutingSection) {
  if (!mainRoutingSection || mainRoutingSection.result === "ERROR") {
    return {
      ...INITIAL_ECUS[0],
      state: "Offline",
      summary: mainRoutingSection?.detail || "MAIN ECU 응답 없음",
      dids: [
        { label: "Routing Activation", value: "실패", tone: "bad" },
        { label: "MAIN 자체 진단", value: "실패", tone: "bad" },
      ],
      dtcs: [],
    };
  }

  const dtcs = normalizeDtcs(mainRoutingSection.dtcs);
  const routingSuccess = mainRoutingSection.routing_activation?.success;

  return {
    ...INITIAL_ECUS[0],
    state: routingSuccess && !hasActiveDtc(dtcs) ? "Ready" : "Warning",
    summary: routingSuccess
      ? "Routing Activation 완료"
      : "Routing Activation 확인 필요",
    dids: [
      {
        label: "Routing Activation",
        value: routingSuccess ? "Active" : "Fail",
        tone: routingSuccess ? "good" : "bad",
      },
      ...normalizeDids(mainRoutingSection.dids),
    ],
    dtcs,
  };
}

function buildMediaCard(mediaSection) {
  if (!mediaSection || mediaSection.result === "ERROR") {
    return {
      ...INITIAL_ECUS[3],
      state: "Offline",
      summary: mediaSection?.detail || "MEDIA PI 응답 없음",
      dids: [],
      dtcs: [],
    };
  }

  const dtcs = normalizeDtcs(mediaSection.dtcs);
  const hasBadDid = (mediaSection.dids || []).some(
    (item) => getDidTone(item) === "bad"
  );

  return {
    ...INITIAL_ECUS[3],
    state: hasBadDid || hasActiveDtc(dtcs) ? "Warning" : "Normal",
    summary: `DoIP 응답 완료 / ${mediaSection.elapsed_ms ?? "-"} ms`,
    dids: normalizeDids(mediaSection.dids),
    dtcs,
  };
}

function buildRoutedEcuCard(ecu, fallback) {
  if (!ecu) {
    return {
      ...fallback,
      state: "Offline",
      summary: `${fallback.name} 응답 없음`,
      dids: [],
      dtcs: [],
    };
  }

  const dtcs = normalizeDtcs(ecu.dtcs);
  const hasBadDid = (ecu.dids || []).some((item) => getDidTone(item) === "bad");

  return {
    ...fallback,
    state:
      ecu.state || (hasBadDid || hasActiveDtc(dtcs) ? "Warning" : "Normal"),
    summary: ecu.error || fallback.summary,
    dids: normalizeDids(ecu.dids),
    dtcs,
  };
}

function buildEcusFromScan(data) {
  const sections = data?.sections || {};
  const routedEcus = sections.main_ecus?.ecus || [];

  const act = routedEcus.find((ecu) => ecu.id === "act");
  const body = routedEcus.find((ecu) => ecu.id === "body");

  return [
    buildMainCard(sections.main_routing),
    buildRoutedEcuCard(act, INITIAL_ECUS[1]),
    buildRoutedEcuCard(body, INITIAL_ECUS[2]),
    buildMediaCard(sections.media),
  ];
}

function buildInitialTestResults() {
  return FUNCTION_TESTS.map((test) => ({
    ...test,
    status: "대기",
    tone: "idle",
    detail: "자동 실행 대기 중",
    raw: "",
  }));
}

function EcuCard({ ecu }) {
  return (
    <article className="prediag-ecu-card">
      <div className="prediag-ecu-head">
        <div>
          <p>{ecu.address}</p>
          <h2>{ecu.name}</h2>
        </div>

        <span className={`prediag-state ${getStateClass(ecu.state)}`}>
          {ecu.state}
        </span>
      </div>

      <p className="prediag-ecu-summary">{ecu.summary}</p>
      <div className="prediag-route">{ecu.route}</div>

      <div className="prediag-did-list">
        {ecu.dids.length === 0 ? (
          <div className="prediag-empty">아직 DID 조회 결과가 없습니다.</div>
        ) : (
          ecu.dids.slice(0, 5).map((item) => (
            <div className="prediag-did-row" key={`${ecu.id}-${item.label}`}>
              <span>{item.label}</span>
              <strong className={`prediag-value-${item.tone || "good"}`}>
                {item.value}
                {item.unit && <em>{item.unit}</em>}
              </strong>
            </div>
          ))
        )}
      </div>
    </article>
  );
}

function TestRow({ test }) {
  return (
    <div className={`prediag-test-row ${test.tone}`}>
      <div>
        <strong>{test.label}</strong>
        <span>
          {test.ecu} / {test.routine}
        </span>
      </div>
      <em>{test.status}</em>
    </div>
  );
}

function PreDriveDiagnosisPage({ onBack, onStartRemoteControl }) {
  const hasStartedRef = useRef(false);

  const [running, setRunning] = useState(false);
  const [completed, setCompleted] = useState(false);
  const [lastUpdatedAt, setLastUpdatedAt] = useState(null);
  const [ecus, setEcus] = useState(INITIAL_ECUS);
  const [testResults, setTestResults] = useState(buildInitialTestResults);

  const allDtcs = useMemo(
    () =>
      ecus.flatMap((ecu) =>
        (ecu.dtcs || []).map((dtc) => ({
          ...dtc,
          ecu: ecu.name,
        }))
      ),
    [ecus]
  );

  const activeDtcCount = allDtcs.filter(
    (dtc) => dtc.state === "Active" || dtc.status === "Active"
  ).length;

  const failedTestCount = testResults.filter(
    (test) => test.tone === "fail"
  ).length;

  const successTestCount = testResults.filter(
    (test) => test.tone === "success"
  ).length;

  const updateTest = useCallback((id, patch) => {
    setTestResults((current) =>
      current.map((test) => (test.id === id ? { ...test, ...patch } : test))
    );
  }, []);

  const runSingleFunctionTest = useCallback(
    async (test) => {
      updateTest(test.id, {
        status: "실행 중",
        tone: "running",
        detail: "RoutineControl 실행 중",
        raw: "",
      });

      const response = await fetch(`${API_BASE_URL}/diagnostics/routine`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          test_id: test.id,
        }),
      });

      const data = await response.json();

      if (!response.ok || data.result !== "OK") {
        throw new Error(data.detail || data.raw || "RoutineControl 실패");
      }

      const resultOk = data.routine_result !== 0x01;

      updateTest(test.id, {
        status: resultOk ? "성공" : "실패",
        tone: resultOk ? "success" : "fail",
        detail: `${data.target} RoutineControl ${data.routine_id}`,
        raw: data.raw,
      });
    },
    [updateTest]
  );

  const runPreDriveDiagnosis = useCallback(async () => {
    try {
      setRunning(true);
      setCompleted(false);
      setTestResults(buildInitialTestResults());

      const response = await fetch(`${API_BASE_URL}/diagnostics/run`);
      const data = await response.json();

      if (!response.ok || data.result === "ERROR") {
        throw new Error(data.detail || `HTTP ${response.status}`);
      }

      const nextEcus = buildEcusFromScan(data);
      setEcus(nextEcus);
      setLastUpdatedAt(new Date());

      for (const test of FUNCTION_TESTS) {
        try {
          await runSingleFunctionTest(test);
        } catch (error) {
          updateTest(test.id, {
            status: "실패",
            tone: "fail",
            detail: error.message,
            raw: "",
          });
        }
      }

      setCompleted(true);
      setLastUpdatedAt(new Date());
    } catch (error) {
      setCompleted(true);
      console.error("사전 진단 실패:", error);
    } finally {
      setRunning(false);
    }
  }, [runSingleFunctionTest, updateTest]);

  useEffect(() => {
    if (hasStartedRef.current) return;
    hasStartedRef.current = true;

    runPreDriveDiagnosis();
  }, [runPreDriveDiagnosis]);

  const diagnosisSummary = running
    ? "RUNNING"
    : completed
      ? failedTestCount > 0 || activeDtcCount > 0
        ? "CHECK"
        : "READY"
      : "STANDBY";

  return (
    <div className="prediag-page">
      <header className="prediag-header">
        <button className="prediag-back-button" type="button" onClick={onBack}>
          차량 리스트
        </button>

        <div className="prediag-title">
          <p>PRE-DRIVE DIAGNOSTIC</p>
          <h1>원격 조종 사전 진단</h1>
          <span>
            원격 조종 진입 전 MAIN / ACT / BODY / MEDIA 진단과 기능 테스트를
            자동으로 수행합니다.
          </span>
        </div>

<div className="prediag-header-right-group">
  <div className="prediag-status-box">
    <strong className={`prediag-mode ${running ? "running" : ""}`}>
      {diagnosisSummary}
    </strong>

    <small>
      마지막 갱신{" "}
      {lastUpdatedAt
        ? lastUpdatedAt.toLocaleTimeString("ko-KR", { hour12: false })
        : "아직 없음"}
    </small>

    <button type="button" onClick={runPreDriveDiagnosis} disabled={running}>
      재진단
    </button>
  </div>

  <div className="prediag-confirm-box-top">
    <p className="prediag-confirm-title">
      원격 조종으로 넘어가시겠습니까?
    </p>

    <div className="prediag-header-decision-buttons">
      <button
        className="prediag-header-start-button"
        type="button"
        onClick={onStartRemoteControl}
        disabled={running || !completed}
      >
        확인
      </button>

      <button
        className="prediag-header-cancel-button"
        type="button"
        onClick={onBack}
        disabled={running}
      >
        취소
      </button>
    </div>
  </div>
</div>
      </header>

      <main className="prediag-content">
        <section className="prediag-overview">
          <article>
            <span>진단 대상</span>
            <strong>{ecus.length}</strong>
            <p>MAIN, ACT, BODY, MEDIA</p>
          </article>

          <article>
            <span>Routing</span>
            <strong>{ecus[0]?.state === "Ready" ? "Active" : "Check"}</strong>
            <p>MAIN ECU Routing Activation</p>
          </article>

          <article>
            <span>Active DTC</span>
            <strong>{activeDtcCount}</strong>
            <p>{activeDtcCount ? "확인 필요 항목 있음" : "활성 고장 없음"}</p>
          </article>

          <article>
            <span>기능 테스트</span>
            <strong>
              {successTestCount}/{FUNCTION_TESTS.length}
            </strong>
            <p>
              {running
                ? "자동 실행 중"
                : failedTestCount
                  ? "실패 항목 있음"
                  : "실행 완료"}
            </p>
          </article>
        </section>

        <section className="prediag-main-grid">
          <div className="prediag-ecu-grid">
            {ecus.map((ecu) => (
              <EcuCard key={ecu.id} ecu={ecu} />
            ))}
          </div>

          <aside className="prediag-side-panel">
            <section className="prediag-test-panel">
              <div className="prediag-panel-head">
                <p>FUNCTION TEST</p>
                <h2>자동 기능 테스트</h2>
              </div>

              <div className="prediag-test-list">
                {testResults.map((test) => (
                  <TestRow key={test.id} test={test} />
                ))}
              </div>
            </section>
          </aside>
        </section>
      </main>
    </div>
  );
}

export default PreDriveDiagnosisPage;