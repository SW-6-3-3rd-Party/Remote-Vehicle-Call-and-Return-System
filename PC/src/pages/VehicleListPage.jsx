import { useEffect, useState } from "react";

const STATUS_API_URL = "http://127.0.0.1:5000/vehicle/status";

function VehicleListPage({
  onOpenAccidents,
  onOpenRemoteControl,
  onOpenDiagnostic,
}) {
  const [vehicleStatus, setVehicleStatus] = useState({
    driving_state: 0,
    driving_state_text: "정지 중",
    remote_control_active: false,
    remote_control_text: "OFF",
  });

  const fetchVehicleStatus = async () => {
    try {
      const response = await fetch(STATUS_API_URL);
      const data = await response.json();

      if (data.result === "OK") {
        setVehicleStatus({
          driving_state: data.driving_state,
          driving_state_text: data.driving_state_text,
          remote_control_active: data.remote_control_active,
          remote_control_text: data.remote_control_text,
        });
      }
    } catch (error) {
      console.error("차량 상태 조회 실패:", error);
    }
  };

  useEffect(() => {
    fetchVehicleStatus();

    const intervalId = setInterval(() => {
      fetchVehicleStatus();
    }, 1000);

    return () => clearInterval(intervalId);
  }, []);

  return (
    <div className="vehicle-page">
      <header className="vehicle-header">
        <div>
          <p className="system-label">Remote Vehicle Call & Return System</p>
          <h1>차량 리스트</h1>
        </div>
      </header>

      <main className="vehicle-content">
        <article className="vehicle-list-card">
          <div className="vehicle-main-info">
            <div className="vehicle-icon">3</div>

            <div>
              <h2>3rd-Party</h2>
            </div>
          </div>

          <div className="vehicle-summary">
            <div>
              <span>차량 상태</span>
              <strong>{vehicleStatus.driving_state_text}</strong>
            </div>

            <div>
              <span>원격 제어</span>
              <strong>{vehicleStatus.remote_control_text}</strong>
            </div>
          </div>

          <div className="vehicle-actions">
            <button className="secondary-button" onClick={onOpenDiagnostic}>
              진단
            </button>

            <button className="primary-outline-button" onClick={onOpenAccidents}>
              사고 이력
            </button>

            <button className="primary-button" onClick={onOpenRemoteControl}>
              원격 조종
            </button>
          </div>
        </article>
      </main>
    </div>
  );
}

export default VehicleListPage;
