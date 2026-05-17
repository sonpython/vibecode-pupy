const SOURCES = ["claude", "codex"];
const POLL_MS = 15000;

const byId = (id) => document.getElementById(id);

function pctText(value) {
  return value >= 0 ? `${value}%` : "--%";
}

function clampPct(value) {
  if (value < 0) return 0;
  return Math.max(0, Math.min(100, value));
}

function ageText(seconds) {
  if (seconds < 0) return "no data";
  if (seconds < 60) return `${seconds}s ago`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
  return `${Math.floor(seconds / 3600)}h ago`;
}

function level(value) {
  if (value >= 85) return "danger";
  if (value >= 65) return "warn";
  return "ok";
}

function setBar(source, kind, value) {
  const bar = byId(`${source}-${kind}-bar`);
  bar.className = "";
  const barLevel = level(value);
  if (barLevel !== "ok") bar.classList.add(barLevel);
  bar.style.width = `${clampPct(value)}%`;
  byId(`${source}-${kind}-text`).textContent = pctText(value);
}

function renderSource(source, data) {
  const status = byId(`${source}-status`);
  status.textContent = data.status;
  status.className = `badge ${data.status}`;
  byId(`${source}-meta`).textContent = `snapshot ${ageText(data.stale_sec)}`;
  setBar(source, "current", data.current_pct);
  setBar(source, "weekly", data.weekly_pct);
}

function renderRobot(data) {
  const worstPct = Math.max(
    ...SOURCES.flatMap((source) => [
      data[source].current_pct,
      data[source].weekly_pct,
    ]),
  );
  const hasHardError = SOURCES.some((source) =>
    ["auth_expired", "error"].includes(data[source].status),
  );
  const face = byId("robot-face");
  face.className = "robot-face";
  if (hasHardError || worstPct >= 85) face.classList.add("danger");
  else if (worstPct >= 65) face.classList.add("warn");
}

function setLiveState(mode, text) {
  const liveState = byId("live-state");
  liveState.className = `live-pill ${mode}`;
  liveState.querySelector("span:last-child").textContent = text;
  byId("api-state").textContent = text.toLowerCase();
}

async function refresh() {
  try {
    const response = await fetch("/public/status", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    SOURCES.forEach((source) => renderSource(source, data[source]));
    renderRobot(data);
    setLiveState("live", "Live");
    byId("last-refresh").textContent = new Date(data.ts).toLocaleTimeString([], {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
  } catch (error) {
    setLiveState("error", "Error");
    byId("api-state").textContent = error.message;
  }
}

refresh();
window.setInterval(refresh, POLL_MS);
