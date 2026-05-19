import { useState } from "react";
import "./App.css";
import VehicleListPage from "./pages/VehicleListPage";
import AccidentHistoryPage from "./pages/AccidentHistoryPage";
import RemoteControlPage from "./pages/RemoteControlPage";

function App() {
  const [page, setPage] = useState("vehicleList");

  if (page === "accidentHistory") {
    return <AccidentHistoryPage onBack={() => setPage("vehicleList")} />;
  }

  if (page === "remoteControl") {
    return <RemoteControlPage onBack={() => setPage("vehicleList")} />;
  }

  return (
    <VehicleListPage
      onOpenAccidents={() => setPage("accidentHistory")}
      onOpenRemoteControl={() => setPage("remoteControl")}
    />
  );
}

export default App;