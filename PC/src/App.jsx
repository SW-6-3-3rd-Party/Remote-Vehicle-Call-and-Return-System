import { useState } from "react";
import "./App.css";

import VehicleListPage from "./pages/VehicleListPage";
import AccidentHistoryPage from "./pages/AccidentHistoryPage";
import RemoteControlPage from "./pages/RemoteControlPage";
import DiagnosticPage from "./pages/DiagnosticPage";
import PreDriveDiagnosisPage from "./pages/PreDriveDiagnosisPage";

function App() {
  const [page, setPage] = useState("vehicleList");

  if (page === "diagnostic") {
    return <DiagnosticPage onBack={() => setPage("vehicleList")} />;
  }

  if (page === "preDriveDiagnosis") {
    return (
      <PreDriveDiagnosisPage
        onBack={() => setPage("vehicleList")}
        onStartRemoteControl={() => setPage("remoteControl")}
      />
    );
  }

  if (page === "accidentHistory") {
    return <AccidentHistoryPage onBack={() => setPage("vehicleList")} />;
  }

  if (page === "remoteControl") {
    return <RemoteControlPage onBack={() => setPage("vehicleList")} />;
  }

  return (
    <VehicleListPage
      onOpenDiagnostic={() => setPage("diagnostic")}
      onOpenAccidents={() => setPage("accidentHistory")}
      onOpenRemoteControl={() => setPage("preDriveDiagnosis")}
    />
  );
}

export default App;