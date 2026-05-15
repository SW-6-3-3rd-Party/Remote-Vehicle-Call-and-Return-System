import { useEffect, useState } from "react";

const API_URL = "http://192.168.201.2:5000/accidents?vehicle_id=1";

function getDrivingStateText(state) {
  if (state === 1) return "주행 중";
  if (state === 0) return "정지 중";
  return "확인 불가";
}

function getVideoUrls(accident) {
  return {
    front: accident.front_video_url || accident.video_url || "",
    rear: accident.rear_video_url || accident.video_url || "",
  };
}

function VideoThumbnail({ title, videoUrl, duration, onClick }) {
  const [videoDuration, setVideoDuration] = useState(duration || "--:--");

  const handleLoadedMetadata = (event) => {
    const seconds = event.currentTarget.duration;

    if (!Number.isFinite(seconds)) {
      setVideoDuration("--:--");
      return;
    }

    const minute = Math.floor(seconds / 60);
    const second = Math.floor(seconds % 60);

    setVideoDuration(
      `${String(minute).padStart(2, "0")}:${String(second).padStart(2, "0")}`
    );
  };

  return (
    <button className="video-thumb" onClick={onClick}>
      {videoUrl && (
        <video
          className="thumb-video"
          src={videoUrl}
          muted
          preload="metadata"
          playsInline
          onLoadedMetadata={handleLoadedMetadata}
        />
      )}

      <div className="thumb-overlay" />

      <div className="thumb-top-label">{title}</div>

      <div className="play-circle">
        <span>▶</span>
      </div>

      <div className="duration-badge">
        <span>▣</span>
        <span>{videoDuration}</span>
      </div>

      {!videoUrl && <div className="no-video">영상 없음</div>}
    </button>
  );
}

function AccidentCard({ accident, onSelectVideo }) {
  const { front, rear } = getVideoUrls(accident);
  const drivingText = getDrivingStateText(accident.driving_state);

  return (
    <article className="accident-card">
      <div className="video-area">
        <VideoThumbnail
          title="전방 카메라"
          videoUrl={front}
          duration={accident.front_duration}
          onClick={() => onSelectVideo(front, "전방 카메라")}
        />

        <VideoThumbnail
          title="후방 카메라"
          videoUrl={rear}
          duration={accident.rear_duration}
          onClick={() => onSelectVideo(rear, "후방 카메라")}
        />
      </div>

      <div className="info-area">
        <div className="info-row">
          <div className="icon-circle">◷</div>
          <span className="info-title">사고 당시 시간</span>
          <span className="info-value">{accident.accident_time}</span>
        </div>

        <div className="divider" />

        <div className="info-row">
          <div className="icon-circle car">▰</div>
          <span className="info-title">사고 당시 주행 여부</span>
          <span className="info-value state">{drivingText}</span>
        </div>
      </div>

      <div className="button-area">
        <button
          className="outline-button"
          onClick={() => onSelectVideo(front, "전방 카메라")}
        >
          ▶ 전방 보기
        </button>

        <button
          className="outline-button"
          onClick={() => onSelectVideo(rear, "후방 카메라")}
        >
          ▶ 후방 보기
        </button>
      </div>
    </article>
  );
}

function VideoModal({ videoUrl, videoTitle, onClose }) {
  if (!videoUrl) return null;

  return (
    <div className="video-modal-backdrop" onClick={onClose}>
      <section className="video-modal" onClick={(event) => event.stopPropagation()}>
        <div className="video-modal-header">
          <h2>{videoTitle}</h2>

          <button className="close-button" onClick={onClose}>
            닫기
          </button>
        </div>

        <video
          className="modal-video-player"
          src={videoUrl}
          controls
          autoPlay
          playsInline
        >
          브라우저에서 영상을 재생할 수 없습니다.
        </video>
      </section>
    </div>
  );
}

function AccidentHistoryPage({ onBack }) {
  const [loading, setLoading] = useState(false);
  const [result, setResult] = useState("");
  const [accidents, setAccidents] = useState([]);
  const [errorMessage, setErrorMessage] = useState("");
  const [selectedVideoUrl, setSelectedVideoUrl] = useState("");
  const [selectedVideoTitle, setSelectedVideoTitle] = useState("");

  const fetchAccidents = async () => {
    try {
      setLoading(true);
      setErrorMessage("");
      setSelectedVideoUrl("");
      setSelectedVideoTitle("");

      const response = await fetch(API_URL);

      if (!response.ok) {
        throw new Error(`HTTP Error: ${response.status}`);
      }

      const data = await response.json();

      setResult(data.result || "");

      if (data.result === "OK") {
        setAccidents(data.accidents || []);
      } else if (data.result === "EMPTY") {
        setAccidents([]);
      } else {
        setAccidents([]);
        setErrorMessage(data.detail || "조회 중 오류가 발생했습니다.");
      }
    } catch (error) {
      console.error(error);
      setResult("INTERNAL_ERROR");
      setAccidents([]);
      setErrorMessage("Gateway 서버에 연결할 수 없습니다.");
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchAccidents();
  }, []);

  const handleSelectVideo = (videoUrl, title) => {
    if (!videoUrl) {
      alert("영상 URL이 없습니다.");
      return;
    }

    setSelectedVideoUrl(videoUrl);
    setSelectedVideoTitle(title);
  };

  const handleCloseVideo = () => {
    setSelectedVideoUrl("");
    setSelectedVideoTitle("");
  };

  return (
    <div className="page">
      <header className="page-header">
        <button className="back-button" onClick={onBack}>
          ← 차량 리스트로
        </button>

        <div className="title-box">
          <h1>사고 이력 조회</h1>
          <div className="title-underline" />
        </div>

        <button className="refresh-button" onClick={fetchAccidents}>
          새로고침
        </button>
      </header>

      <main className="content">
        {loading && (
          <section className="message-card">
            사고 이력을 조회 중입니다...
          </section>
        )}

        {!loading && result === "EMPTY" && (
          <section className="message-card">
            <h2>사고 이력이 없습니다.</h2>
            <p>Media RPi에 저장된 사고 기록이 없습니다.</p>
          </section>
        )}

        {!loading && result === "INTERNAL_ERROR" && (
          <section className="message-card error">
            <h2>조회 중 오류가 발생했습니다.</h2>
            <p>{errorMessage}</p>
          </section>
        )}

        {!loading && result === "OK" && accidents.length > 0 && (
          <section className="accident-list">
            {accidents.map((accident) => (
              <AccidentCard
                key={accident.accident_id}
                accident={accident}
                onSelectVideo={handleSelectVideo}
              />
            ))}
          </section>
        )}
      </main>

      <VideoModal
        videoUrl={selectedVideoUrl}
        videoTitle={selectedVideoTitle}
        onClose={handleCloseVideo}
      />
    </div>
  );
}

export default AccidentHistoryPage;