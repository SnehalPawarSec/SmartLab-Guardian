// SmartLab Guardian - AI energy analytics (runs in the browser, no server needed)
//
// 1. Anomaly detection: the model learns the "normal" power for each situation
//    (number of people + lights + fans) from the stored history:
//      - learned profile: average power of past readings in the same situation
//        (same lights/fans state, ±1 person), used when there are enough of them;
//      - linear regression  power ≈ b0 + b1·people + b2·lights + b3·fans  otherwise.
//    Readings whose error is more than 3 standard deviations away are flagged unusual.
// 2. Prediction: tomorrow's kWh and occupancy from the same weekday in history
//    (falls back to the average of all days).
// 3. Suggestions: rule-based advice generated from the data.

const MIN_READINGS = 6;

/** Solves A·x = b (small dense system) with Gaussian elimination. */
function solve(A, b) {
  const n = b.length;
  const M = A.map((row, i) => [...row, b[i]]);
  for (let c = 0; c < n; c++) {
    let p = c;
    for (let r = c + 1; r < n; r++) if (Math.abs(M[r][c]) > Math.abs(M[p][c])) p = r;
    [M[c], M[p]] = [M[p], M[c]];
    if (Math.abs(M[c][c]) < 1e-9) return null;
    for (let r = 0; r < n; r++) {
      if (r === c) continue;
      const f = M[r][c] / M[c][c];
      for (let k = c; k <= n; k++) M[r][k] -= f * M[c][k];
    }
  }
  return M.map((row, i) => row[n] / row[i]);
}

const people = (x) => (typeof x.c === "number" ? x.c : x.o ? 1 : 0);
const features = (x) => [1, people(x), x.l ? 1 : 0, x.f ? 1 : 0];

/** Fits power ~ features with a little ridge regularisation (keeps it stable when a feature never changes). */
function fit(records) {
  const k = 4, lambda = 1e-3;
  const XtX = Array.from({ length: k }, () => Array(k).fill(0));
  const Xty = Array(k).fill(0);
  for (const r of records) {
    const x = features(r);
    for (let i = 0; i < k; i++) {
      Xty[i] += x[i] * (r.p || 0);
      for (let j = 0; j < k; j++) XtX[i][j] += x[i] * x[j];
    }
  }
  for (let i = 1; i < k; i++) XtX[i][i] += lambda * records.length;
  return solve(XtX, Xty);
}

const predictPower = (w, r) => features(r).reduce((s, v, i) => s + v * w[i], 0);

/** Learned profile: sum/count of power per (lights, fans, people) situation. */
function buildProfile(records) {
  const m = new Map();
  for (const r of records) {
    const k = `${r.l ? 1 : 0}${r.f ? 1 : 0}|${people(r)}`;
    const b = m.get(k) || { sum: 0, n: 0 };
    b.sum += r.p || 0; b.n++;
    m.set(k, b);
  }
  return m;
}
function expectedPower(profile, w, r) {
  const c = people(r), lf = `${r.l ? 1 : 0}${r.f ? 1 : 0}`;
  let sum = 0, n = 0;
  for (const d of [-1, 0, 1]) {
    const b = profile.get(`${lf}|${c + d}`);
    if (b) { sum += b.sum; n += b.n; }
  }
  if (n >= 8) return sum / n;
  return w ? predictPower(w, r) : null;
}

/** Energy (kWh) per record interval, from the cumulative meter value. */
function intervals(records) {
  const out = [];
  for (let i = 1; i < records.length; i++) {
    const a = records[i - 1], b = records[i];
    let d = (b.e ?? 0) - (a.e ?? 0);
    if (d < 0) d = b.e ?? 0;
    out.push({ t: b.t, kwh: d, rec: b, dt: b.t - a.t });
  }
  return out;
}

const dayKey = (t) => { const d = new Date(t); return `${d.getFullYear()}-${d.getMonth() + 1}-${d.getDate()}`; };
const WEEKDAYS = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];

/**
 * Full analysis.
 * @param records energy log, sorted by t: {t, p, e, o, l, f, c?}
 * @param settings lab settings (tariff, labOpenHour, labCloseHour, seats, ...)
 * @param computers /smartlab/computers value (optional, for PC advice)
 * @param pcStateFn function(pc) -> ACTIVE | IDLE | OFFLINE
 */
export function analyse(records, settings, computers = {}, pcStateFn = null) {
  if (!records || records.length < MIN_READINGS) {
    return { ready: false, message: `Need at least ${MIN_READINGS} readings (have ${records ? records.length : 0}). Keep the ESP32 running; it stores one reading per minute.` };
  }

  // ---------- 1. anomaly detection
  const w = fit(records);
  const profile = buildProfile(records);
  const expect = (r) => expectedPower(profile, w, r) ?? (r.p || 0);
  let anomalies = [], std = 0, latest = null;
  if (w) {
    const res = records.map((r) => (r.p || 0) - expect(r));
    const mean = res.reduce((a, b) => a + b, 0) / res.length;
    std = Math.sqrt(res.reduce((a, b) => a + (b - mean) ** 2, 0) / res.length) || 1;
    const floor = Math.max(std, 15);               // ignore tiny noise on very stable data
    records.forEach((r, i) => {
      const z = (res[i] - mean) / floor;
      if (Math.abs(z) > 3) anomalies.push({ t: r.t, p: r.p, expected: expect(r), z });
    });
    const last = records[records.length - 1];
    latest = { p: last.p, expected: expect(last), z: (res[res.length - 1] - mean) / floor };
  }
  const recentWindow = records[records.length - 1].t - 60 * 60 * 1000;
  const recentAnomalies = anomalies.filter((a) => a.t >= recentWindow);

  // ---------- 2. daily energy + prediction
  const iv = intervals(records);
  const byDay = new Map();
  for (const x of iv) {
    const k = dayKey(x.t);
    if (!byDay.has(k)) byDay.set(k, { key: k, t: x.t, kwh: 0, occSum: 0, occN: 0, covered: 0 });
    const d = byDay.get(k);
    d.kwh += x.kwh;
    if (x.dt < 3600000) d.covered += x.dt;
    const h = new Date(x.t).getHours();
    if (h >= settings.labOpenHour && h < settings.labCloseHour) { d.occSum += people(x.rec); d.occN++; }
  }
  const days = [...byDay.values()];
  const today = dayKey(Date.now());
  // only days with (almost) complete data, so a half-recorded day doesn't skew averages
  const fullDays = days.filter((d) => d.key !== today && d.covered >= 20 * 3600000);
  const tomorrow = new Date(Date.now() + 86400000);
  const sameDay = fullDays.filter((d) => new Date(d.t).getDay() === tomorrow.getDay());
  const basis = sameDay.length ? sameDay : fullDays;
  let predictedKwh = null, predictedOcc = null, basisText = "";
  if (basis.length) {
    predictedKwh = basis.reduce((s, d) => s + d.kwh, 0) / basis.length;
    const occ = basis.filter((d) => d.occN);
    predictedOcc = occ.length ? occ.reduce((s, d) => s + d.occSum / d.occN, 0) / occ.length : null;
    basisText = sameDay.length ? `average of ${sameDay.length} past ${WEEKDAYS[tomorrow.getDay()]}(s)` : `average of ${basis.length} past day(s)`;
  } else {
    // Only today's data: scale today's usage rate up to a full lab day.
    const todayRec = iv.filter((x) => dayKey(x.t) === today);
    const hours = todayRec.reduce((s, x) => s + x.dt, 0) / 3600000;
    const kwh = todayRec.reduce((s, x) => s + x.kwh, 0);
    if (hours > 0) predictedKwh = (kwh / hours) * (settings.labCloseHour - settings.labOpenHour);
    const occ = todayRec.map((x) => people(x.rec));
    predictedOcc = occ.length ? occ.reduce((a, b) => a + b, 0) / occ.length : null;
    basisText = "today's usage rate (collect more days for a better forecast)";
  }

  // ---------- 3. hourly profile + suggestions
  const hourly = Array.from({ length: 24 }, () => ({ sum: 0, n: 0 }));
  for (const r of records) { const h = new Date(r.t).getHours(); hourly[h].sum += r.p || 0; hourly[h].n++; }
  const hourlyAvg = hourly.map((h) => (h.n ? h.sum / h.n : 0));
  const peakHour = hourlyAvg.indexOf(Math.max(...hourlyAvg));

  const suggestions = [];
  const emptyWaste = iv.filter((x) => !people(x.rec) && (x.rec.l || x.rec.f));
  const wasteKwh = emptyWaste.reduce((s, x) => s + x.kwh, 0);
  const wasteMin = emptyWaste.reduce((s, x) => s + x.dt, 0) / 60000;
  if (wasteMin > 5) suggestions.push(`Lights/fans stayed ON for ${Math.round(wasteMin)} min while the lab was empty (${wasteKwh.toFixed(2)} kWh, ₹${(wasteKwh * settings.tariff).toFixed(0)}). Keep AUTO mode on or reduce the empty-lab timeout.`);

  // After-hours energy above the normal standby load (10th percentile of power)
  const sortedP = records.map((x) => x.p || 0).sort((a, b) => a - b);
  const standbyW = sortedP[Math.floor(sortedP.length * 0.1)] || 0;
  const afterKwh = iv.filter((x) => { const h = new Date(x.t).getHours(); return h < settings.labOpenHour || h >= settings.labCloseHour; })
    .reduce((s, x) => s + (x.dt < 3600000 ? Math.max(0, (x.rec.p || 0) - standbyW) * x.dt / 3.6e9 : 0), 0);
  if (afterKwh > 0.2) suggestions.push(`${afterKwh.toFixed(2)} kWh above the normal standby load (${Math.round(standbyW)} W) was used outside lab hours (${settings.labOpenHour}:00–${settings.labCloseHour}:00). Check for PCs or equipment left ON.`);

  if (hourlyAvg[peakHour] > 0) suggestions.push(`Peak usage is around ${String(peakHour).padStart(2, "0")}:00 (avg ${Math.round(hourlyAvg[peakHour])} W). Spread heavy practicals away from this hour to cut peak load.`);

  if (fullDays.length >= 3) {
    const wd = new Map();
    for (const d of fullDays) { const k = new Date(d.t).getDay(); if (!wd.has(k)) wd.set(k, []); wd.get(k).push(d); }
    const avgOcc = [...wd.entries()].map(([k, arr]) => ({ k, occ: arr.reduce((s, d) => s + (d.occN ? d.occSum / d.occN : 0), 0) / arr.length })).filter((x) => x.k !== 0);
    if (avgOcc.length >= 2) {
      const low = avgOcc.reduce((a, b) => (b.occ < a.occ ? b : a));
      suggestions.push(`${WEEKDAYS[low.k]} has the lowest average occupancy (${low.occ.toFixed(1)} people). Consider combining lab sessions on that day to save energy.`);
    }
  }

  if (pcStateFn) {
    const pcs = Object.values(computers || {});
    const idle = pcs.filter((p) => pcStateFn(p) === "IDLE");
    if (idle.length) {
      const w = idle.reduce((s, p) => s + (p.powerW || 0), 0);
      suggestions.push(`${idle.length} PC(s) are idle right now, using about ${Math.round(w)} W. Shutting them down would save ≈ ₹${((w / 1000) * 8 * settings.tariff).toFixed(0)} per 8-hour day.`);
    }
  }
  if (recentAnomalies.length) suggestions.unshift(`${recentAnomalies.length} unusual power reading(s) in the last hour. Check for equipment left ON (heater, AC, projector) or a wiring fault.`);
  if (!suggestions.length) suggestions.push("Energy usage matches occupancy. No action needed.");

  return {
    ready: true,
    readings: records.length,
    // Easy-to-read model: expected power for an empty lab and for 10 people with lights + fans ON
    model: w ? { emptyW: expect({ c: 0, l: 0, f: 0 }), tenW: expect({ c: 10, l: 1, f: 1 }), std } : null,
    standbyW,
    anomalies, recentAnomalies, latest,
    predictedKwh, predictedOcc, predictedCost: predictedKwh != null ? predictedKwh * settings.tariff : null, basisText,
    tomorrowName: WEEKDAYS[tomorrow.getDay()],
    days, hourlyAvg, peakHour, wasteKwh, afterKwh,
    suggestions,
    status: recentAnomalies.length ? "UNUSUAL" : "NORMAL",
  };
}

/** HTML block summarising the analysis (used on Dashboard and Analytics). */
export function renderAi(a, esc) {
  if (!a.ready) return `<div class="ai-box">${esc(a.message)}</div>`;
  const m = a.model;
  const head = a.status === "NORMAL"
    ? `<b>✓ Normal energy pattern</b> across ${a.readings} readings.`
    : `<b>⚠ Unusual energy pattern:</b> ${a.recentAnomalies.length} reading(s) in the last hour were far from the expected power.`;
  const latest = a.latest ? `<div style="margin-top:6px">Latest: ${Math.round(a.latest.p)} W, expected ≈ ${Math.round(a.latest.expected)} W (score ${a.latest.z.toFixed(1)}).</div>` : "";
  const learned = m ? `<div style="margin-top:6px;color:var(--muted);font-size:13px">Model learned: empty lab ≈ ${Math.round(m.emptyW)} W, 10 people with lights + fans ≈ ${Math.round(m.tenW)} W (±${Math.round(m.std)} W).</div>` : "";
  const pred = a.predictedKwh != null
    ? `<div style="margin-top:8px"><b>Tomorrow (${a.tomorrowName}):</b> ≈ ${a.predictedKwh.toFixed(2)} kWh (₹${a.predictedCost.toFixed(0)})${a.predictedOcc != null ? `, ≈ ${Math.round(a.predictedOcc)} people on average` : ""}.<div style="color:var(--muted);font-size:12.5px">Based on ${esc(a.basisText)}.</div></div>`
    : "";
  return `<div class="ai-box ${a.status === "NORMAL" ? "good" : "warn"}">${head}${latest}${learned}${pred}
    <ul>${a.suggestions.slice(0, 4).map((s) => `<li>${esc(s)}</li>`).join("")}</ul></div>`;
}
