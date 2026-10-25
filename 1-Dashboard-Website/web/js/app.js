// Shared code for every dashboard page: Firebase, login guard, navbar, helpers.
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import {
  getAuth, onAuthStateChanged, signInWithEmailAndPassword, createUserWithEmailAndPassword, signOut,
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-auth.js";
import {
  getDatabase, ref, onValue, set, update, push, remove, get, serverTimestamp,
  query, orderByChild, startAt, limitToLast,
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig, USERS, ROOT, HARDWARE_OFFLINE_MS } from "./config.js";

export const fbApp = initializeApp(firebaseConfig);
export const auth = getAuth(fbApp);
export const db = getDatabase(fbApp);
export { ref, onValue, set, update, push, remove, get, serverTimestamp, query, orderByChild, startAt, limitToLast };
export { HARDWARE_OFFLINE_MS };

/** Reference below /smartlab, e.g. r("hardware/light") */
export const r = (path = "") => ref(db, path ? `${ROOT}/${path}` : ROOT);

// ---------------------------------------------------------------- defaults
export const DEFAULT_SETTINGS = {
  labName: "Computer Lab 1",
  seats: 40,
  tariff: 8,              // Rs per kWh
  labOpenHour: 8,
  labCloseHour: 18,
  highPowerW: 2000,       // total lab power alert (computers + lights + fan)
  pcIdleMin: 10,
  maxPeople: 5,           // crowd alert (camera count) -> buzzer
  lightsW: 0.08,          // 5V LED strip (~15 mA) - used for the energy-saved estimate
  fansW: 0.6,             // 5V fan 0.12 A
};
export const withDefaults = (s) => ({ ...DEFAULT_SETTINGS, ...(s || {}) });

// ---------------------------------------------------------------- server clock
// Device timestamps come from the Firebase server; compare with server time, not the laptop clock.
let serverOffset = 0;
onValue(ref(db, ".info/serverTimeOffset"), (s) => { serverOffset = s.val() || 0; });
export const serverNow = () => Date.now() + serverOffset;

// ---------------------------------------------------------------- auth
export function userFromEmail(email) {
  const entry = Object.entries(USERS).find(([, u]) => u.email === email);
  if (!entry) return { username: email, role: "STAFF", title: "Lab Staff" };
  return { username: entry[0], role: entry[1].role, title: entry[1].title };
}

export async function login(username, password) {
  const u = USERS[username.trim().toLowerCase()];
  if (!u) throw new Error("Invalid username or password.");
  try {
    return await signInWithEmailAndPassword(auth, u.email, password);
  } catch (e) {
    // First ever login with a demo account: create it in Firebase Auth.
    if (password === u.password && ["auth/invalid-credential", "auth/user-not-found", "auth/invalid-login-credentials"].includes(e.code)) {
      try {
        return await createUserWithEmailAndPassword(auth, u.email, password);
      } catch (e2) {
        if (e2.code !== "auth/email-already-in-use") throw friendly(e2);
      }
    }
    throw friendly(e);
  }
}

function friendly(e) {
  const map = {
    "auth/network-request-failed": "No internet connection. Check Wi-Fi and try again.",
    "auth/too-many-requests": "Too many attempts. Wait a minute and try again.",
    "auth/operation-not-allowed": "Email/Password sign-in is disabled in Firebase Authentication.",
  };
  return new Error(map[e.code] || "Invalid username or password.");
}

export const logout = () => signOut(auth).then(() => location.replace("login.html"));

/** Page guard: waits for Firebase Auth, redirects to login if needed, draws the navbar. */
export function requireLogin(active) {
  return new Promise((resolve) => {
    const stop = onAuthStateChanged(auth, (fbUser) => {
      stop();
      if (!fbUser) { location.replace("login.html"); return; }
      const user = { ...userFromEmail(fbUser.email), uid: fbUser.uid, email: fbUser.email };
      // HOD = summary view (Dashboard, Alerts, Analytics). Staff = everything incl. all controls.
      if (user.role === "HOD" && !PAGES.find(([href, , hod]) => href === active && hod)) { location.replace("dashboard.html"); return; }
      renderNav(active, user);
      document.body.classList.add("ready");
      if (user.role === "HOD") document.body.classList.add("is-hod");
      resolve(user);
    });
  });
}

// ---------------------------------------------------------------- navbar
// [page, label, visible to HOD]
const PAGES = [
  ["dashboard.html", "Dashboard", true],
  ["pcs.html", "PC Monitoring", false],
  ["alerts.html", "Alerts", true],
  ["events.html", "Events", false],
  ["analytics.html", "Analytics", true],
  ["controls.html", "Controls", false],
  ["data.html", "JSON Data", false],
];

function renderNav(active, user) {
  const nav = document.createElement("header");
  nav.className = "topbar";
  nav.innerHTML = `
    <a class="brand" href="dashboard.html"><b>SmartLab</b> Guardian</a>
    <nav class="links">${PAGES.filter(([, , hod]) => user.role !== "HOD" || hod).map(([href, label]) =>
      `<a href="${href}" class="${href === active ? "active" : ""}">${label}${href === "alerts.html" ? ' <span class="nav-badge" id="navAlertCount" hidden></span>' : ""}</a>`).join("")}
    </nav>
    <div class="who"><span>${esc(user.title)}</span><span class="role">${user.role === "HOD" ? "HOD" : "Staff"}</span>
      <button class="link-btn" id="logoutBtn">Logout</button></div>`;
  document.body.prepend(nav);
  document.getElementById("logoutBtn").onclick = logout;
  onValue(query(r("alerts"), limitToLast(200)), (s) => {
    const open = Object.values(s.val() || {}).filter((a) => !a.resolved).length;
    const b = document.getElementById("navAlertCount");
    b.hidden = open === 0;
    b.textContent = open;
  });
}

// ---------------------------------------------------------------- live connection badge
/** Shows "Live" / "Connecting" / "Hardware offline" in the element with this id. */
export function connectionBadge(id) {
  const el = document.getElementById(id);
  if (!el) return;
  let connected = false, lastSeen = 0, device = "";
  const draw = () => {
    const hwOnline = lastSeen && serverNow() - lastSeen < HARDWARE_OFFLINE_MS;
    if (!connected) { el.className = "badge warn"; el.textContent = "● Connecting"; }
    else if (!hwOnline) { el.className = "badge bad"; el.textContent = "● Hardware offline"; }
    else { el.className = "badge ok"; el.textContent = `● Live · ${device || "ESP32"}`; }
  };
  onValue(ref(db, ".info/connected"), (s) => { connected = !!s.val(); draw(); });
  onValue(r("hardware"), (s) => { const h = s.val() || {}; lastSeen = h.lastSeen || 0; device = h.device || ""; draw(); });
  setInterval(draw, 3000);
}

// ---------------------------------------------------------------- audit log
/** Writes an entry to /smartlab/events (shown on the Events page). */
export function logEvent(type, msg, user) {
  return push(r("events"), { t: serverTimestamp(), type, msg, source: user ? `dashboard:${user.username}` : "dashboard" });
}

// ---------------------------------------------------------------- PC helpers
/** Returns "ACTIVE" | "IDLE" | "OFFLINE" for a /computers entry. */
export function pcState(pc) {
  if (!pc || pc.status === "OFFLINE") return "OFFLINE";
  const interval = (pc.interval || 10) * 1000;
  if (!pc.lastSeen || serverNow() - pc.lastSeen > Math.max(3 * interval, 30000)) return "OFFLINE";
  return pc.status === "IDLE" ? "IDLE" : "ACTIVE";
}

// ---------------------------------------------------------------- lab totals
/** Live totals shown on the dashboard. Uses the ESP32's numbers (same as its OLED) while it is online,
 *  otherwise adds up the PC agents directly (lights + fan then count as 0). */
export function labTotals(energy, computers, hwOnline, settings) {
  const e = energy || {};
  if (hwOnline && e.totalPowerW !== undefined) {
    return {
      totalW: e.totalPowerW, pcW: e.pcPowerW || 0, lfW: e.power || 0,
      totalKwh: e.totalKwhToday || 0, pcKwh: e.pcKwhToday || 0, lfKwh: e.energyKwh || 0,
      cost: e.costToday || 0, tariff: e.tariff ?? settings.tariff,
      pcOnline: e.pcOnline || 0, pcActive: e.pcActive || 0, pcIdle: e.pcIdle || 0, pcTotal: e.pcTotal || 0,
    };
  }
  const pcs = Object.values(computers || {}).filter((pc) => !pc.simulated);
  const online = pcs.filter((pc) => pcState(pc) !== "OFFLINE");
  const pcW = online.reduce((s, pc) => s + (pc.powerW || 0), 0);
  const pcKwh = pcs.reduce((s, pc) => s + (pc.energyTodayKwh || 0), 0);
  return {
    totalW: pcW, pcW, lfW: 0, totalKwh: pcKwh, pcKwh, lfKwh: 0,
    cost: pcKwh * settings.tariff, tariff: settings.tariff,
    pcOnline: online.length, pcActive: online.filter((pc) => pcState(pc) === "ACTIVE").length,
    pcIdle: online.filter((pc) => pcState(pc) === "IDLE").length, pcTotal: pcs.length,
  };
}

// ---------------------------------------------------------------- energy helpers
/** kWh used between records, from the cumulative meter value `e` (survives meter resets). */
export function kwhUsed(records) {
  let used = 0;
  for (let i = 1; i < records.length; i++) {
    const d = (records[i].e ?? 0) - (records[i - 1].e ?? 0);
    used += d >= 0 ? d : (records[i].e ?? 0);   // meter was reset -> count from 0
  }
  return used;
}

export function startOfToday() {
  const d = new Date(serverNow());
  d.setHours(0, 0, 0, 0);
  return d.getTime();
}

/** Energy log records as a sorted array. */
export const logArray = (val) => Object.values(val || {}).filter((x) => x && typeof x.t === "number").sort((a, b) => a.t - b.t);

// ---------------------------------------------------------------- formatting
export const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
export const fmt = (n, d = 0) => (n === undefined || n === null || Number.isNaN(+n) ? "--" : (+n).toLocaleString("en-IN", { minimumFractionDigits: d, maximumFractionDigits: d }));
export const timeStr = (t) => (t ? new Date(t).toLocaleTimeString("en-IN", { hour: "2-digit", minute: "2-digit", second: "2-digit" }) : "--");
export const dateTimeStr = (t) => (t ? new Date(t).toLocaleString("en-IN", { day: "2-digit", month: "short", hour: "2-digit", minute: "2-digit", second: "2-digit" }) : "--");
export function ago(t) {
  if (!t) return "never";
  const s = Math.max(0, Math.round((serverNow() - t) / 1000));
  if (s < 60) return `${s}s ago`;
  if (s < 3600) return `${Math.floor(s / 60)} min ago`;
  if (s < 86400) return `${Math.floor(s / 3600)} h ago`;
  return `${Math.floor(s / 86400)} d ago`;
}
export const $ = (id) => document.getElementById(id);

/** Small toast message at the bottom of the page. */
export function toast(msg, kind = "ok") {
  let box = document.getElementById("toastBox");
  if (!box) { box = document.createElement("div"); box.id = "toastBox"; document.body.appendChild(box); }
  const t = document.createElement("div");
  t.className = `toast ${kind}`;
  t.textContent = msg;
  box.appendChild(t);
  setTimeout(() => t.remove(), 3500);
}

// ---------------------------------------------------------------- charts
export const CHART_COLORS = { power: "#3b9dd8", occupancy: "#e0507f", energy: "#1f8a5b", idle: "#d9a53a", off: "#9aa7a1" };
export function chartDefaults() {
  const css = getComputedStyle(document.documentElement);
  window.Chart.defaults.font.family = css.getPropertyValue("--font").trim() || "Segoe UI, sans-serif";
  window.Chart.defaults.color = css.getPropertyValue("--muted").trim() || "#5d6b65";
  window.Chart.defaults.borderColor = css.getPropertyValue("--grid").trim() || "#e6ece9";
}
