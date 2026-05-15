import { useEffect, useState } from "react";
import "./App.css";
import VehicleListPage from "./pages/VehicleListPage";
import AccidentHistoryPage from "./pages/AccidentHistoryPage";

const REMOTE_START_URL = "http://192.168.201.2:5000/remote-control/start";
const REMOTE_STOP_URL = "http://192.168.201.2:5000/remote-control/stop";

function RemoteControlPlaceholder({ onBack }) {
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

  const handleBack = async () => {
    try {
      await fetch(REMOTE_STOP_URL, { method: "POST" });
    } catch (error) {
      console.error("원격 제어 OFF 실패:", error);
    }

    onBack();
  };

  return (
    <div className="page">
      <header className="page-header">
        <button className="back-button" onClick={handleBack}>
          ← 차량 리스트로
        </button>

        <div className="title-box">
          <h1>원격 조종</h1>
          <div className="title-underline" />
        </div>

        <div />
      </header>

      <main className="content">
        <section className="message-card">
          <h2>원격 조종 페이지</h2>
          <p>현재 이 브라우저가 원격 제어 상태를 ON으로 설정했습니다.</p>
        </section>
      </main>
    </div>
  );
}

function App() {
  const [page, setPage] = useState("vehicleList");

  if (page === "accidentHistory") {
    return <AccidentHistoryPage onBack={() => setPage("vehicleList")} />;
  }

  if (page === "remoteControl") {
    return <RemoteControlPlaceholder onBack={() => setPage("vehicleList")} />;
  }

  return (
    <VehicleListPage
      onOpenAccidents={() => setPage("accidentHistory")}
      onOpenRemoteControl={() => setPage("remoteControl")}
    />
  );
}

export default App;