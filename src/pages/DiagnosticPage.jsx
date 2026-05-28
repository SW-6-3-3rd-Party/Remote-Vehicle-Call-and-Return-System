import { useCallback, useEffect, useState } from "react";
import "./DiagnosticPage.css";

const getDefaultApiBaseUrl = () => {
  const hostname = window.location.hostname || "127.0.0.1";
  return `${window.location.protocol}//${hostname}:5000`;
};

const API_BASE_URL = import.meta.env.VITE_GATEWAY_BASE_URL || getDefaultApiBaseUrl();

const FUNCTION_TESTS = [
  {
    ecu: "ACT ECU",
    items: [
      { id: "act-motor", label: "모터 테스트", routine: "0x0100" },
      { id: "act-servo", label: "서보 테스트", routine: "0x0101" },
    ],
  },
  {
    ecu: "BODY ECU",
    items: [
      { id: "body-buzzer", label: "부저 테스트", routine: "0x0200" },
      { id: "body-led", label: "LED 테스트", routine: "0x0201" },
      { id: "body-ultrasonic", label: "초음파 테스트", routine: "0x0202" },
    ],
  },
];

function getStateLabel(state) {
  if (state === "Warning") return "warning";
  if (state === "Ready") return "ready";
  if (state === "Offline") return "offline";
  return "normal";
}

function getDidTone(item) {
  if (item.tone) return item.tone;
  if (item.ok === false) return "bad";
  if (["미연결", "DOWN", "중지", "두절", "응답 오류", "DID 불일치"].includes(item.value)) return "bad";
  if (item.value === "NRC") return "warn";
  return "good";
}

function formatValue(value) {
  if (value === null || value === undefined || value === "") return "--";
  return String(value);
}

function buildInitialEcus() {
  return [
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
}

function makeDids(items = []) {
  return items.map((item) => ({
    label: item.label,
    value: formatValue(item.value),
    unit: item.unit,
    tone: getDidTone(item),
  }));
}

function buildMainCard(section) {
  if (!section || section.result === "ERROR") {
    return {
      id: "main",
      name: "MAIN ECU",
      address: "Central Gateway",
      route: "DoIP 192.168.10.2:13400",
      state: "Offline",
      summary: section?.detail || "MAIN ECU Routing Activation 실패",
      dids: [{ label: "Routing Activation", value: "실패", tone: "bad" }],
      dtcs: [],
    };
  }

  const activation = section.routing_activation;
  const success = Boolean(activation?.success);
  const dids = [
    {
      label: "Routing Activation",
      value: success ? "성공" : "거부",
      tone: success ? "good" : "bad",
    },
    {
      label: "진단 세션",
      value: section.session?.session || "--",
      tone: section.session?.ok ? "good" : "bad",
    },
    ...(section.dids ? makeDids(section.dids) : []),
  ];
  const dtcs = section.dtcs?.items || [];
  const hasBadDid = dids.some((item) => getDidTone(item) === "bad");

  return {
    id: "main",
    name: "MAIN ECU",
    address: "Central Gateway",
    route: `DoIP ${section.host}:${section.port}`,
    state: success && !hasBadDid && dtcs.length === 0 ? "Ready" : "Warning",
    summary: "CAN 기반 ECU 진단 중계 + 자체 통신 DTC",
    dids,
    dtcs,
  };
}

function buildRoutedEcuCard(ecu) {
  const sessionDid = ecu.session
    ? [{
        label: "진단 세션",
        value: ecu.session.session,
        tone: ecu.session.ok ? "good" : "bad",
      }]
    : [];

  return {
    id: ecu.id,
    name: ecu.name,
    address: ecu.logical_address || ecu.address || "--",
    route: ecu.route || (ecu.id === "act" ? "MAIN 경유 / CAN Bus 1" : "MAIN 경유 / CAN Bus 2"),
    state: ecu.state || "Warning",
    summary: ecu.error || (ecu.id === "act" ? "Drive / Steering / Brake" : "Lighting / Ultrasonic / Collision"),
    dids: [...sessionDid, ...makeDids(ecu.dids)],
    dtcs: ecu.dtcs?.items || [],
  };
}

function buildRoutedErrorCard(template, section) {
  return {
    ...template,
    state: "Offline",
    summary: section?.detail || "MAIN ECU 경유 진단 실패",
    dids: [{ label: "MAIN 경유 진단", value: "실패", tone: "bad" }],
    dtcs: [],
  };
}

function buildMediaCard(section) {
  if (!section || section.result === "ERROR") {
    return {
      id: "media",
      name: "MEDIA PI",
      address: "Media Unit",
      route: "DoIP 192.168.20.2:13401",
      state: "Offline",
      summary: section?.detail || "Media Pi DoIP 연결 실패",
      dids: [{ label: "DoIP 연결", value: "실패", tone: "bad" }],
      dtcs: [],
    };
  }

  const dtcs = section.dtcs?.items || [];
  const hasBadDid = (section.dids || []).some((item) => getDidTone(item) === "bad");
  const sessionDid = section.session
    ? [{
        label: "진단 세션",
        value: section.session.session,
        tone: section.session.ok ? "good" : "bad",
      }]
    : [];

  return {
    id: "media",
    name: "MEDIA PI",
    address: "Media Unit",
    route: `DoIP ${section.host}:${section.port}`,
    state: dtcs.length > 0 || hasBadDid ? "Warning" : "Normal",
    summary: "Camera / Audio / EDR",
    dids: [...sessionDid, ...makeDids(section.dids)],
    dtcs,
  };
}

function buildEcus(scanData) {
  const sections = scanData?.sections || {};
  const initial = buildInitialEcus();
  const routed = sections.main_ecus?.ecus || [];
  const routedFailed = sections.main_ecus?.result === "ERROR";

  return [
    buildMainCard(sections.main_routing),
    routedFailed
      ? buildRoutedErrorCard(initial[1], sections.main_ecus)
      : buildRoutedEcuCard(routed.find((ecu) => ecu.id === "act") || initial[1]),
    routedFailed
      ? buildRoutedErrorCard(initial[2], sections.main_ecus)
      : buildRoutedEcuCard(routed.find((ecu) => ecu.id === "body") || initial[2]),
    buildMediaCard(sections.media),
  ];
}

function mergeEcuById(current, next) {
  return current.map((ecu) => (ecu.id === next.id ? next : ecu));
}

function buildDtcRows(ecus) {
  return ecus.flatMap((ecu) =>
    (ecu.dtcs || []).map((dtc) => ({
      ecu: ecu.name,
      code: dtc.code,
      status: dtc.state || dtc.status || "Stored",
      firstSeenAt: "--",
      duration: "--",
      description: dtc.description || "정의되지 않은 DTC",
    }))
  );
}

function EcuDiagnosticCard({ ecu }) {
  return (
    <article className="diag-ecu-card">
      <div className="diag-ecu-head">
        <div>
          <p>{ecu.address}</p>
          <h2>{ecu.name}</h2>
        </div>
        <span className={`diag-state ${getStateLabel(ecu.state)}`}>
          {ecu.state}
        </span>
      </div>

      <p className="diag-ecu-summary">{ecu.summary}</p>
      <div className="diag-route">{ecu.route}</div>

      <div className="diag-did-list">
        {ecu.dids.length === 0 ? (
          <div className="diag-empty-dtc">아직 DID 조회 결과가 없습니다.</div>
        ) : (
          ecu.dids.map((item) => (
            <div className="diag-did-row" key={`${ecu.id}-${item.label}`}>
              <span className="diag-did-label">{item.label}</span>
              <strong className={item.tone ? `diag-value-${item.tone}` : ""}>
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

function DiagnosticPage({ onBack }) {
  const [diagMode, setDiagMode] = useState(true);
  const [loading, setLoading] = useState(false);
  const [lastUpdatedAt, setLastUpdatedAt] = useState(null);
  const [ecus, setEcus] = useState(buildInitialEcus);
  const [dtcs, setDtcs] = useState([]);
  const [selectedTestResult, setSelectedTestResult] = useState(null);
  const [logs, setLogs] = useState([
    "진단 페이지 진입: Media Pi DoIP부터 조회할 준비가 되었습니다.",
  ]);

  const activeDtcCount = dtcs.filter((dtc) => dtc.status === "Active").length;

  const pushLog = useCallback((message) => {
    setLogs((current) =>
      [
        `${new Date().toLocaleTimeString("ko-KR", { hour12: false })} ${message}`,
        ...current,
      ].slice(0, 10)
    );
  }, []);

  const runDiagnostics = useCallback(async () => {
    try {
      setLoading(true);
      pushLog(`DoIP 진단을 시작합니다. API=${API_BASE_URL}`);
      pushLog("Media Pi 직접 DoIP 조회를 먼저 수행합니다.");

      try {
        const mediaResponse = await fetch(`${API_BASE_URL}/diagnostics/media`);
        const mediaData = await mediaResponse.json();

        if (!mediaResponse.ok || mediaData.result === "ERROR") {
          throw new Error(mediaData.detail || `Media HTTP ${mediaResponse.status}`);
        }

        const mediaCard = buildMediaCard(mediaData);
        setEcus((current) => mergeEcuById(current, mediaCard));
        setDtcs((current) => [
          ...current.filter((dtc) => dtc.ecu !== "MEDIA PI"),
          ...buildDtcRows([mediaCard]),
        ]);
        setLastUpdatedAt(new Date());
        pushLog("Media Pi DID 조회가 완료되었습니다.");
      } catch (error) {
        pushLog(`Media Pi DID 조회 실패: ${error.message}`);
      }

      pushLog("MAIN/ACT/BODY 포함 전체 진단을 이어서 수행합니다.");

      const response = await fetch(`${API_BASE_URL}/diagnostics/run`);
      const data = await response.json();

      if (!response.ok || data.result === "ERROR") {
        throw new Error(data.detail || `HTTP ${response.status}`);
      }

      const nextEcus = buildEcus(data);
      setEcus(nextEcus);
      setDtcs(buildDtcRows(nextEcus));
      setLastUpdatedAt(new Date());

      if (data.result === "PARTIAL_ERROR") {
        pushLog(`일부 진단 실패: ${data.errors.map((item) => item.section).join(", ")}`);
      } else {
        pushLog("전체 진단 조회가 완료되었습니다.");
      }
    } catch (error) {
      pushLog(`진단 실패: ${error.message}`);
    } finally {
      setLoading(false);
    }
  }, [pushLog]);

  const handleBack = () => {
    setDiagMode(false);
    onBack();
  };

  const handleClearDtc = async () => {
    try {
      setLoading(true);
      const response = await fetch(`${API_BASE_URL}/diagnostics/dtc/clear`, {
        method: "POST",
      });
      const data = await response.json();

      if (!response.ok || !["OK", "PARTIAL"].includes(data.result)) {
        throw new Error(data.detail || "DTC 삭제 실패");
      }

      const clearedTargets = new Set(
        (data.items || []).filter((item) => item.ok).map((item) => item.target)
      );
      setEcus((current) =>
        current.map((ecu) => {
          const hasBadDid = (ecu.dids || []).some((item) => item.tone === "bad");
          const nextState = ecu.id === "main" && !hasBadDid ? "Ready" : hasBadDid ? "Warning" : "Normal";
          if (!clearedTargets.has(ecu.name)) {
            return ecu;
          }
          return {
            ...ecu,
            state: nextState,
            dtcs: [],
          };
        })
      );
      setDtcs((current) => current.filter((dtc) => !clearedTargets.has(dtc.ecu)));
      if (data.result === "PARTIAL") {
        const failedTargets = (data.items || [])
          .filter((item) => !item.ok)
          .map((item) => item.target)
          .join(", ");
        pushLog(`일부 DTC 삭제 완료. 실패: ${failedTargets}`);
      } else {
        pushLog("전체 DTC 삭제 요청을 전송했습니다.");
      }
    } catch (error) {
      pushLog(`DTC 삭제 실패: ${error.message}`);
    } finally {
      setLoading(false);
    }
  };

  const handleRoutine = async (item) => {
    try {
      setLoading(true);
      setSelectedTestResult(null);

      const response = await fetch(`${API_BASE_URL}/diagnostics/routine`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ test_id: item.id }),
      });
      const data = await response.json();

      if (!response.ok || data.result !== "OK") {
        throw new Error(data.detail || data.raw || "RoutineControl 실패");
      }

      const resultOk = data.result === "OK" && data.routine_result !== 0x01;
      setSelectedTestResult({
        label: item.label,
        ecu: item.ecu,
        result: resultOk ? "성공" : "실패",
        tone: resultOk ? "success" : "fail",
        summary: `${data.target} RoutineControl ${data.routine_id} 응답을 수신했습니다.`,
        detail: `UDS Raw: ${data.raw}`,
      });
      pushLog(`${item.label} 실행 완료: ${data.raw}`);
    } catch (error) {
      setSelectedTestResult({
        label: item.label,
        ecu: item.ecu,
        result: "실패",
        tone: "fail",
        summary: error.message,
        detail: "MAIN ECU 진단 모드와 Routing Activation 상태를 확인하세요.",
      });
      pushLog(`${item.label} 실행 실패: ${error.message}`);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    const timeoutId = setTimeout(runDiagnostics, 0);

    return () => clearTimeout(timeoutId);
  }, [runDiagnostics]);

  return (
    <div className="diagnostic-page">
      <header className="diag-header">
        <button className="back-button" type="button" onClick={handleBack}>
          차량 리스트
        </button>

        <div className="diag-title">
          <p className="page-kicker">DoIP / UDS Diagnostic Console</p>
          <h1>진단 대시보드</h1>
          <p>
            Media Pi 직접 DoIP와 MAIN 게이트웨이 Routing Activation, ACT/BODY
            MAIN 자체 통신 DTC와 ACT/BODY/Media UDS DID, DTC, RoutineControl을 수행합니다.
          </p>
        </div>

        <div className="diag-mode-box">
          <span className={`diag-mode ${diagMode ? "active" : ""}`}>
            {loading ? "RUNNING" : diagMode ? "DIAG MODE" : "STANDBY"}
          </span>
          <small>
            마지막 갱신{" "}
            {lastUpdatedAt
              ? lastUpdatedAt.toLocaleTimeString("ko-KR", { hour12: false })
              : "아직 없음"}
          </small>
          <button
            className="diag-run-button"
            type="button"
            onClick={runDiagnostics}
            disabled={loading}
          >
            재진단
          </button>
        </div>
      </header>

      <main className="diag-content">
        <section className="diag-overview">
          <article>
            <span>진단 대상</span>
            <strong>{ecus.length}</strong>
            <p>MAIN, ACT, BODY, MEDIA</p>
          </article>
          <article>
            <span>Routing</span>
            <strong>{ecus[0]?.state === "Ready" ? "Active" : "Standby"}</strong>
            <p>MAIN ECU Routing Activation 상태</p>
          </article>
          <article>
            <span>Active DTC</span>
            <strong>{activeDtcCount}</strong>
            <p>{activeDtcCount ? "확인 필요 항목 있음" : "현재 활성 고장 없음"}</p>
          </article>
          <article>
            <span>기능 테스트</span>
            <strong>{diagMode && !loading ? "Enabled" : "Locked"}</strong>
            <p>RoutineControl 0x31 실행 가능</p>
          </article>
        </section>

        <section className="diag-ecu-grid">
          {ecus.map((ecu) => (
            <EcuDiagnosticCard key={ecu.id} ecu={ecu} />
          ))}
        </section>

        <section className="diag-lower-grid">
          <article className="diag-test-panel">
            <div className="diag-section-head">
              <p className="page-kicker">Function Test</p>
              <h2>기능 테스트</h2>
            </div>

            <div className="diag-test-layout">
              <div className="diag-test-list">
                {FUNCTION_TESTS.map((group) => (
                  <div className="diag-test-group" key={group.ecu}>
                    <h3>{group.ecu}</h3>
                    <div>
                      {group.items.map((item) => (
                        <button
                          type="button"
                          key={item.id}
                          disabled={!diagMode || loading}
                          onClick={() => handleRoutine({ ...item, ecu: group.ecu })}
                        >
                          <span>{item.label}</span>
                          <small>{item.routine}</small>
                        </button>
                      ))}
                    </div>
                  </div>
                ))}
              </div>

              <div className="diag-test-result">
                {selectedTestResult ? (
                  <>
                    <span className={`diag-test-result-pill ${selectedTestResult.tone}`}>
                      {selectedTestResult.result}
                    </span>
                    <h3>{selectedTestResult.label}</h3>
                    <p>{selectedTestResult.summary}</p>
                    <strong>{selectedTestResult.detail}</strong>
                  </>
                ) : (
                  <>
                    <span className="diag-test-result-pill idle">대기</span>
                    <h3>테스트 결과</h3>
                    <p>왼쪽에서 기능 테스트를 실행하면 UDS 응답 원문과 결과가 표시됩니다.</p>
                    <strong>진단 모드에서만 실행 가능</strong>
                  </>
                )}
              </div>
            </div>
          </article>

          <article className="diag-dtc-panel">
            <div className="diag-section-head with-action">
              <div>
                <p className="page-kicker">DTC Monitor</p>
                <h2>고장 코드</h2>
              </div>
              <button
                className="diag-clear-dtc-button"
                type="button"
                onClick={handleClearDtc}
                disabled={loading}
              >
                DTC 전체 삭제
              </button>
            </div>

            <div className="diag-dtc-table">
              <div className="diag-dtc-table-head">
                <span>ECU</span>
                <span>Code</span>
                <span>Status</span>
                <span>최초 발생</span>
                <span>지속 시간</span>
                <span>설명</span>
              </div>

              {dtcs.length === 0 ? (
                <div className="diag-empty-dtc">현재 저장된 DTC가 없습니다.</div>
              ) : (
                dtcs.map((dtc) => (
                  <div className="diag-dtc-table-row" key={`${dtc.ecu}-${dtc.code}`}>
                    <span>{dtc.ecu}</span>
                    <strong>{dtc.code}</strong>
                    <em>{dtc.status}</em>
                    <span>{dtc.firstSeenAt}</span>
                    <span>{dtc.duration}</span>
                    <span>{dtc.description}</span>
                  </div>
                ))
              )}
            </div>
          </article>

          <article className="diag-log-panel">
            <div className="diag-section-head">
              <p className="page-kicker">Session Log</p>
              <h2>진단 로그</h2>
            </div>

            <div className="diag-log-list">
              {logs.map((log, index) => (
                <p key={`${log}-${index}`}>{log}</p>
              ))}
            </div>
          </article>
        </section>
      </main>
    </div>
  );
}

export default DiagnosticPage;
