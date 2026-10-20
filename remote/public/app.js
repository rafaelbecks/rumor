const $ = (id) => document.getElementById(id);
const deviceSelect = $("device");
const app = $("app");
const dot = $("dot");
const statusEl = $("status");
const authToken = new URLSearchParams(location.search).get("token") || "";

const TABS = ["serial", "sinte", "paulxstretch", "salida"];
const SYNTH = ["osc.timbre", "amplitude", "filter.cutoff", "filter.resonance", "env.attack", "env.release", "pitch.offset"];
const MOD = [
  "chord.spread", "folder.drive", "folder.mix", "granular.mix", "granular.density", "granular.pitch",
  "granular.size", "granular.position", "granular.spray", "folder.symmetry",
];
const BAUDS = [9600, 19200, 38400, 57600, 115200];

const known = new Map();
const ui = {
  id: null,
  catalog: null,
  state: null,
  lines: [],
  tab: "sinte",
  socket: null,
  pxDrag: null,
};

function cmd(message) {
  if (!ui.socket || !ui.id) return;
  ui.socket.emit("cmd", { deviceId: ui.id, ...message });
}

function esc(value) {
  return String(value ?? "").replace(/[&<>"']/g, (ch) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[ch]));
}

function stepFor(min, max) {
  const span = Math.abs(max - min);
  if (span <= 2) return 0.001;
  if (span <= 20) return 0.01;
  if (span <= 200) return 0.1;
  return 1;
}

function format(value, min, max) {
  const span = Math.abs(max - min);
  const n = Number(value);
  if (span <= 2) return n.toFixed(3);
  if (span <= 20) return n.toFixed(2);
  if (span <= 200) return n.toFixed(1);
  return String(Math.round(n));
}

function mapValue(raw, lo, hi, outLo, outHi, invert) {
  const span = hi - lo;
  let t = span === 0 ? 0 : (raw - lo) / span;
  t = Math.min(1, Math.max(0, t));
  if (invert) t = 1 - t;
  return outLo + t * (outHi - outLo);
}

function focusedInside(node) {
  return node && node.contains(document.activeElement);
}

function byId(list) {
  return new Map((list || []).map((item) => [item.id, item]));
}

function wrapPhase(phase) {
  return phase - Math.floor(phase);
}

function waveformSample(wave, phase, timbre) {
  const twoPi = Math.PI * 2;
  const index = timbre * 17.5;
  const phi = phase * twoPi;
  const modPhase = wrapPhase(phase + (index / twoPi) * Math.sin(phi));
  if (wave === 0) return Math.sin(phi + index * Math.sin(phi));
  if (wave === 1) return 2 * modPhase - 1;
  if (wave === 2) return modPhase < 0.5 ? 1 : -1;
  return -(4 * Math.abs(modPhase - 0.5) - 1);
}

function morphSample(position, phase, timbre) {
  const pos = Math.min(3, Math.max(0, position));
  const i0 = Math.floor(pos);
  const i1 = i0 < 3 ? i0 + 1 : i0;
  const t = pos - i0;
  const a = waveformSample(i0, phase, timbre);
  const b = waveformSample(i1, phase, timbre);
  return a + (b - a) * t;
}

function setConnected(on) {
  document.body.classList.toggle("idle", !on);
  app.hidden = !on;
  dot.classList.toggle("on", on);
  statusEl.textContent = on ? "en vivo" : "inactive";
}

function showTab(name) {
  ui.tab = name;
  document.querySelectorAll(".tabs button").forEach((button) => {
    button.classList.toggle("on", button.dataset.tab === name);
  });
  const ready = Boolean(ui.id && ui.catalog);
  setConnected(ready);
  for (const id of TABS) {
    const section = $(id);
    if (section) section.hidden = !ready || id !== name;
  }
  if (name === "sinte") requestAnimationFrame(drawWave);
  if (name === "paulxstretch") requestAnimationFrame(drawPx);
}

function connect() {
  if (ui.socket) ui.socket.close();
  ui.socket = io({ auth: { token: authToken } });
  ui.socket.on("connect", () => {
    if (!ui.id) setConnected(false);
  });
  ui.socket.on("connect_error", () => setConnected(false));
  ui.socket.on("devices", (list) => {
    const devices = list || [];
    if (!devices.some((device) => device.id === ui.id)) ui.id = devices[0]?.id || null;
    deviceSelect.hidden = devices.length < 2;
    deviceSelect.innerHTML = devices.map((device) => `<option value="${esc(device.id)}">${esc(device.name)} ${esc(device.id)}</option>`).join("");
    if (ui.id) deviceSelect.value = ui.id;
    adopt();
  });
  ui.socket.on("hello", (msg) => {
    const entry = known.get(msg.id) || {};
    entry.catalog = msg.catalog;
    known.set(msg.id, entry);
    if (!ui.id) ui.id = msg.id;
    if (msg.id === ui.id) adopt();
  });
  ui.socket.on("state", (msg) => {
    const entry = known.get(msg.id) || {};
    entry.state = msg.state;
    known.set(msg.id, entry);
    if (msg.id !== ui.id) return;
    ui.state = msg.state;
    dot.classList.add("on");
    statusEl.textContent = "en vivo";
    if (!ui.catalog && entry.catalog) adopt();
    else paint();
  });
  ui.socket.on("debug", (msg) => {
    if (msg.id !== ui.id || !msg.text) return;
    for (const line of String(msg.text).split("\n")) if (line) ui.lines.push(line);
    while (ui.lines.length > 400) ui.lines.shift();
    paintLog();
  });
  ui.socket.on("bye", (msg) => {
    known.delete(msg.id);
    if (msg.id !== ui.id) return;
    ui.id = null;
    ui.catalog = null;
    ui.state = null;
    showTab(ui.tab);
  });
}

function adopt() {
  const entry = ui.id ? known.get(ui.id) : null;
  if (!entry?.catalog) {
    ui.catalog = null;
    ui.state = null;
    showTab(ui.tab);
    return;
  }
  const changed = ui.catalog !== entry.catalog;
  ui.catalog = entry.catalog;
  ui.state = entry.state || null;
  if (changed) build();
  else paint();
  showTab(ui.tab);
}

function paramRow(param) {
  if (!param) return "";
  if (param.bool) {
    return `<label class="check"><input type="checkbox" data-param="${esc(param.id)}">${esc(param.label)}</label>`;
  }
  const step = stepFor(param.min, param.max);
  return `<label class="param"><span>${esc(param.label)}</span><input data-param="${esc(param.id)}" type="range" min="${param.min}" max="${param.max}" step="${step}"><output>${format(param.min, param.min, param.max)}</output></label>`;
}

function rows(catalog, ids) {
  const map = byId(catalog.params);
  return ids.map((id) => paramRow(map.get(id))).join("");
}

function build() {
  const cat = ui.catalog;
  const waves = (cat.waves || ["sine", "saw", "square", "triangle"]).map((name) => `<option>${esc(name)}</option>`).join("");
  const scales = (cat.scales || ["off"]).map((name) => `<option>${esc(name)}</option>`).join("");
  const dests = cat.destinations || [];
  const px = byId(cat.params);
  const pxSliders = ["px.main_volume", "px.stretch", "px.fft_size", "px.pitch_shift", "px.freq_shift", "px.spread", "px.filter_low", "px.filter_high", "px.onset"]
    .map((id) => paramRow(px.get(id))).join("");
  const pxToggles = [...px.values()].filter((param) => param.group === "paulxstretch" && param.bool && param.id.startsWith("px.enab_"));

  $("serial").innerHTML = `
    <section class="card">
      <h2>serial</h2>
      <div class="row"><label>puerto</label><select id="port"></select><button type="button" id="refreshPorts">actualizar</button></div>
      <div class="row"><label>baud</label><select id="baud">${BAUDS.map((baud) => `<option>${baud}</option>`).join("")}</select></div>
      <div class="row"><label>midi out</label><select id="midiOut"></select></div>
      <div class="row"><label>entrada</label><input id="inLo" type="number" inputmode="numeric"><input id="inHi" type="number" inputmode="numeric">
        <label class="check"><input id="debugOn" type="checkbox">debug</label></div>
      <div id="dests"></div>
      <div class="row actions">
        <button type="button" id="addDest">agregar</button>
        <button type="button" id="applyMap">aplicar</button>
        <button type="button" class="primary" id="connectSerial">conectar</button>
        <button type="button" id="disconnectSerial">desconectar</button>
      </div>
      <p class="hint" id="serialHint"></p>
    </section>
    <section class="card" id="debugCard">
      <h2>debug</h2>
      <div class="row"><label>filtro</label><input id="filter" placeholder="chord.spread, px.freq_shift"></div>
      <pre id="log"></pre>
    </section>`;

  $("sinte").innerHTML = `
    <section class="card">
      <div class="row"><label>wave</label><select id="wave">${waves}</select><label>scale</label><select id="scale">${scales}</select></div>
      <canvas class="wave" id="waveCanvas" height="72"></canvas>
      <label class="param"><span>drone</span><input id="drone" type="range" min="0" max="127" step="1"><output id="droneOut">55</output></label>
      <div class="readout" id="droneReadout"></div>
      ${rows(cat, SYNTH)}
    </section>
    <section class="card"><h2>modulacion</h2>${rows(cat, MOD)}</section>`;

  $("paulxstretch").innerHTML = `
    <section class="card">
      <h2>paulxstretch</h2>
      <p class="hint">misma instancia · ruteo en salida</p>
      <div class="readout" id="pxSource">fuente: (ninguna)</div>
      <div class="readout" id="pxPlay"></div>
      <div class="row">
        <input id="pxPath" placeholder="ruta en la máquina de RUMOR" autocomplete="off">
        <button type="button" id="pxLoad">cargar</button>
        <button type="button" id="pxClear">limpiar</button>
        <button type="button" id="pxArm">activar + start</button>
      </div>
      <div class="switches">
        <label class="check"><input id="pxFreeze" type="checkbox" data-param="px.freeze">freeze</label>
        <label class="check"><input id="pxPause" type="checkbox" data-param="px.pause">pause</label>
      </div>
      <canvas class="px" id="pxCanvas" height="96"></canvas>
      <div class="split">
        <div>${pxSliders}</div>
        <div><h2>modules</h2>${pxToggles.map((param) => paramRow(param)).join("")}</div>
      </div>
    </section>`;

  $("salida").innerHTML = `
    <section class="card">
      <h2>ruteo</h2>
      <h2>sinte</h2>
      ${rows(cat, ["route.gran.level", "route.gran.balance"])}
      <h2>paulxstretch</h2>
      ${rows(cat, ["route.px.level", "route.px.balance"])}
      <h2>master</h2>
      ${rows(cat, ["out.balance", "out.gainL", "out.gainR"])}
      <p class="hint">l sub · r dayton</p>
      <div class="switches">
        <label class="check"><input type="checkbox" data-param="out.monoSum">mono→pan</label>
        <label class="check"><input type="checkbox" data-param="out.swap">swap l/r</label>
        <button type="button" id="soloL">solo l</button>
        <button type="button" id="soloR">solo r</button>
        <button type="button" id="soloBoth">ambos</button>
      </div>
    </section>`;

  bindStatic();
  paint();
}

function bindStatic() {
  $("refreshPorts").onclick = () => cmd({ t: "refresh" });
  $("debugOn").onchange = () => cmd({ t: "debug", on: $("debugOn").checked });
  $("addDest").onclick = () => {
    const first = (ui.catalog.destinations || [])[0];
    addDestRow(first ? { parameter: first.id, min: first.lo, max: first.hi, invert: false } : {});
  };
  $("applyMap").onclick = () => sendMap(false);
  $("connectSerial").onclick = () => sendMap(true);
  $("disconnectSerial").onclick = () => cmd({ t: "serial", connect: false });
  $("filter").oninput = paintLog;
  $("wave").onchange = () => {
    if (ui.state) {
      ui.state.params = ui.state.params || {};
      ui.state.params["osc.waveform"] = $("wave").selectedIndex;
    }
    cmd({ t: "setConfig", config: { waveform: $("wave").value } });
    drawWave();
  };
  $("scale").onchange = () => cmd({ t: "setConfig", config: { quantize: $("scale").value } });
  $("drone").oninput = () => {
    $("droneOut").textContent = $("drone").value;
    cmd({ t: "setParam", id: "drone", value: Number($("drone").value) });
    drawWave();
  };
  $("pxLoad").onclick = () => {
    const path = $("pxPath").value.trim();
    if (path) cmd({ t: "pxSource", path });
  };
  $("pxClear").onclick = () => cmd({ t: "pxSource", clear: true });
  $("pxArm").onclick = () => cmd({ t: "pxSource", arm: true });
  $("soloL").onclick = () => cmd({ t: "setParam", id: "out.balance", value: -1 });
  $("soloR").onclick = () => cmd({ t: "setParam", id: "out.balance", value: 1 });
  $("soloBoth").onclick = () => {
    cmd({ t: "setParam", id: "out.balance", value: 0 });
    cmd({ t: "setParam", id: "out.gainL", value: 1 });
    cmd({ t: "setParam", id: "out.gainR", value: 1 });
  };
  $("midiOut").onchange = () => cmd({ t: "midi", id: $("midiOut").value });
  document.querySelectorAll("[data-param]").forEach((input) => {
    input.addEventListener("input", () => {
      const out = input.parentElement.querySelector("output");
      if (out && input.type !== "checkbox") out.textContent = format(input.value, input.min, input.max);
      const value = input.type === "checkbox" ? (input.checked ? 1 : 0) : Number(input.value);
      cmd({ t: "setParam", id: input.dataset.param, value });
      if (input.dataset.param === "osc.timbre" || input.dataset.param === "osc.waveform") {
        if (ui.state) {
          ui.state.params = ui.state.params || {};
          ui.state.params[input.dataset.param] = Number(input.value);
        }
        drawWave();
      }
      if (input.dataset.param === "px.sound_start" || input.dataset.param === "px.sound_end") drawPx();
    });
  });
  const pxCanvas = $("pxCanvas");
  pxCanvas.addEventListener("pointerdown", onPxPointer);
  pxCanvas.addEventListener("pointermove", onPxPointer);
  pxCanvas.addEventListener("pointerup", endPxPointer);
  pxCanvas.addEventListener("pointercancel", endPxPointer);
}

function sendMap(connectSerial) {
  const input = [Number($("inLo").value), Number($("inHi").value)];
  const destinations = readDests();
  cmd({ t: "setConfig", config: { input, destinations } });
  const serial = { t: "serial", port: $("port").value, baud: Number($("baud").value) };
  if (connectSerial) serial.connect = true;
  cmd(serial);
}

function destOptions(selected) {
  return (ui.catalog.destinations || [])
    .map((dest) => `<option value="${esc(dest.id)}" ${dest.id === selected ? "selected" : ""}>${esc(dest.id)}</option>`)
    .join("");
}

function addDestRow(route) {
  const row = document.createElement("div");
  row.className = "dest";
  row.innerHTML = `
    <select class="paramSel">${destOptions(route.parameter)}</select>
    <input class="cc" type="number" min="0" max="127" value="${route.cc ?? 1}" inputmode="numeric">
    <input class="min" type="number" step="any" value="${route.min ?? 0}">
    <input class="max" type="number" step="any" value="${route.max ?? 1}">
    <label class="check"><input class="inv" type="checkbox" ${route.invert ? "checked" : ""}>inv</label>
    <span class="live"></span>
    <button type="button" class="remove">quitar</button>`;
  row.querySelector(".remove").onclick = () => row.remove();
  row.querySelector(".paramSel").onchange = () => {
    const spec = (ui.catalog.destinations || []).find((dest) => dest.id === row.querySelector(".paramSel").value);
    row.querySelector(".cc").hidden = row.querySelector(".paramSel").value !== "midi.cc";
    if (spec) {
      row.querySelector(".min").value = spec.lo;
      row.querySelector(".max").value = spec.hi;
    }
  };
  row.querySelector(".cc").hidden = (route.parameter || "") !== "midi.cc";
  $("dests").appendChild(row);
}

function readDests() {
  return [...document.querySelectorAll("#dests .dest")].map((row) => {
    const parameter = row.querySelector(".paramSel").value;
    const route = {
      parameter,
      min: Number(row.querySelector(".min").value),
      max: Number(row.querySelector(".max").value),
      invert: row.querySelector(".inv").checked,
    };
    if (parameter === "midi.cc") route.cc = Number(row.querySelector(".cc").value);
    return route;
  });
}

function fillSelect(select, options, value) {
  if (!select) return;
  const sig = options.map((option) => option.value).join("\0");
  if (select.dataset.sig !== sig) {
    select.dataset.sig = sig;
    select.innerHTML = options.map((option) => `<option value="${esc(option.value)}">${esc(option.label)}</option>`).join("");
  }
  if (document.activeElement !== select && options.some((option) => option.value === value)) select.value = value;
}

function paint() {
  const state = ui.state;
  if (!state || !ui.catalog || !$("port")) return;
  const serial = state.serial || {};
  const config = state.config || {};
  const values = state.params || {};
  const locked = new Set(state.locked || []);
  const input = config.input || serial.input || [0, 1023];

  const ports = (serial.ports || []).map(String);
  if (serial.port && !ports.includes(String(serial.port))) ports.unshift(String(serial.port));
  fillSelect($("port"), ports.map((port) => ({ value: port, label: port })), serial.port || "");
  if (document.activeElement !== $("baud")) $("baud").value = String(serial.baud || 9600);
  const midi = [{ value: "", label: "host only" }, ...(state.midiOuts || []).map((device) => ({ value: device.id, label: device.name }))];
  fillSelect($("midiOut"), midi, state.midiOut || "");

  const map = $("dests")?.closest(".card");
  if (map && !focusedInside(map)) {
    if (document.activeElement !== $("inLo")) $("inLo").value = input[0];
    if (document.activeElement !== $("inHi")) $("inHi").value = input[1];
    const sig = JSON.stringify(config.destinations || []);
    if (map.dataset.sig !== sig) {
      map.dataset.sig = sig;
      $("dests").replaceChildren();
      for (const route of config.destinations || []) addDestRow(route);
    }
  }
  if (document.activeElement !== $("debugOn")) $("debugOn").checked = !!state.debug;
  $("debugCard").hidden = !$("debugOn").checked && !state.debug;
  const smoothed = Number(serial.smoothed);
  const lo = Number($("inLo").value);
  const hi = Number($("inHi").value);
  document.querySelectorAll("#dests .dest").forEach((row) => {
    const live = row.querySelector(".live");
    if (smoothed < 0) {
      live.textContent = "";
      return;
    }
    const value = mapValue(smoothed, lo, hi, Number(row.querySelector(".min").value), Number(row.querySelector(".max").value), row.querySelector(".inv").checked);
    live.textContent = Number.isFinite(value) ? value.toFixed(2) : "";
  });
  $("serialHint").textContent = serial.connected
    ? `${serial.port} @ ${serial.baud} · raw ${serial.raw} · ${smoothed.toFixed(1)}`
    : "serial desconectado";

  paintToggle($("engine"), !!state.engineOn, state.engineOn ? "stop" : "start");
  paintToggle($("granular"), !!state.granular, "sinte");
  paintToggle($("pxEnable"), !!state.paulxstretch, "paulxstretch");
  const drone = $("drone");
  if (drone && document.activeElement !== drone) {
    drone.value = state.drone ?? 55;
    $("droneOut").textContent = drone.value;
  }
  drone.disabled = locked.has("drone");
  setSelect($("wave"), config.waveform);
  setSelect($("scale"), config.quantize);
  const midiNote = Number(state.pitch);
  const midiText = Math.abs(midiNote - Math.round(midiNote)) < 0.05 ? String(Math.round(midiNote)) : midiNote.toFixed(2);
  $("droneReadout").textContent = `MIDI ${midiText}  |  ${Number(state.hz).toFixed(1)} Hz${locked.has("drone") ? "  (serial)" : ""}`;

  document.querySelectorAll("[data-param]").forEach((input) => {
    const id = input.dataset.param;
    input.disabled = locked.has(id);
    if (document.activeElement === input) return;
    const value = values[id];
    if (value == null) return;
    if (input.type === "checkbox") input.checked = Number(value) >= 0.5;
    else {
      input.value = value;
      const out = input.parentElement.querySelector("output");
      if (out) out.textContent = format(value, input.min, input.max);
    }
  });

  $("pxSource").textContent = state.pxSource ? `fuente: ${state.pxSource}` : "fuente: (ninguna)";
  $("pxPlay").textContent = `playhead ${(Number(state.pxPlayhead || 0) * 100).toFixed(1)}%   |   prebuffer ${Math.round(Number(state.pxPrebuffer || 0) * 100)}%   |   ${state.engineOn ? "engine on" : "engine off"}`;
  $("paulxstretch").querySelector(".card").style.opacity = state.pxReady === false ? "0.45" : "1";

  drawWave();
  drawPx();
  paintLog();
}

function paintToggle(button, on, label) {
  if (!button) return;
  button.dataset.on = on ? "1" : "0";
  button.classList.toggle("on", on);
  button.textContent = label;
}

function setSelect(select, value) {
  if (!select || document.activeElement === select || value == null) return;
  select.value = value;
}

function paintLog() {
  const log = $("log");
  const filter = $("filter");
  if (!log) return;
  const terms = (filter?.value || "").split(",").map((term) => term.trim().toLowerCase()).filter(Boolean);
  const lines = ui.lines.map((line) => {
    if (!terms.length) return line;
    return line.split(/\s+/).filter((token) => {
      const eq = token.indexOf("=");
      if (eq <= 0) return false;
      const key = token.slice(0, eq).toLowerCase();
      return terms.some((term) => key.includes(term) || term.includes(key));
    }).join(" ");
  }).filter(Boolean);
  const stick = log.scrollHeight - log.scrollTop - log.clientHeight < 40;
  log.textContent = lines.join("\n");
  if (stick) log.scrollTop = log.scrollHeight;
}

function drawWave() {
  const canvas = $("waveCanvas");
  if (!canvas || !ui.state) return;
  const values = ui.state.params || {};
  const position = Number(values["osc.waveform"] ?? 0);
  const timbre = Number(values["osc.timbre"] ?? 0);
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.round(rect.width * dpr));
  const height = Math.max(1, Math.round(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const g = canvas.getContext("2d");
  g.clearRect(0, 0, width, height);
  g.strokeStyle = "rgba(255,255,255,0.13)";
  g.strokeRect(dpr, dpr, width - 2 * dpr, height - 2 * dpr);
  const plotX = 8 * dpr;
  const plotY = 6 * dpr;
  const plotW = width - plotX * 2;
  const plotH = height - plotY * 2;
  const mid = plotY + plotH / 2;
  g.strokeStyle = "rgba(255,255,255,0.1)";
  g.beginPath();
  g.moveTo(plotX, mid);
  g.lineTo(plotX + plotW, mid);
  g.stroke();
  const layers = [
    { offset: 0.08, color: "rgba(196,160,255,0.18)", width: 1.1 * dpr },
    { offset: 0.04, color: "rgba(196,160,255,0.35)", width: 1.1 * dpr },
    { offset: 0, color: "rgba(255,255,255,1)", width: 1.4 * dpr },
  ];
  const points = 256;
  for (const layer of layers) {
    g.beginPath();
    for (let i = 0; i < points; i += 1) {
      const t = i / (points - 1);
      const phase = wrapPhase(t * 4 + layer.offset * timbre);
      const sample = morphSample(position, phase, timbre);
      const x = plotX + t * plotW;
      const y = mid - sample * plotH * 0.42;
      if (i === 0) g.moveTo(x, y);
      else g.lineTo(x, y);
    }
    g.strokeStyle = layer.color;
    g.lineWidth = layer.width;
    g.lineJoin = "round";
    g.stroke();
  }
}

function pxRange() {
  if (ui.pxDrag) return ui.pxDrag;
  const values = ui.state?.params || {};
  return {
    start: Number(values["px.sound_start"] ?? 0),
    end: Number(values["px.sound_end"] ?? 1),
  };
}

function drawPx() {
  const canvas = $("pxCanvas");
  if (!canvas) return;
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.round(rect.width * dpr));
  const height = Math.max(1, Math.round(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const g = canvas.getContext("2d");
  g.clearRect(0, 0, width, height);
  const { start, end } = pxRange();
  const x0 = start * width;
  const x1 = end * width;
  g.fillStyle = "rgba(0,0,0,0.28)";
  g.fillRect(0, 0, x0, height);
  g.fillRect(x1, 0, width - x1, height);
  g.fillStyle = "rgba(196,160,255,0.18)";
  g.fillRect(x0, 0, Math.max(1, x1 - x0), height);
  g.strokeStyle = "rgba(196,160,255,0.9)";
  g.lineWidth = 1.5 * dpr;
  g.beginPath();
  g.moveTo(x0, 0);
  g.lineTo(x0, height);
  g.moveTo(x1, 0);
  g.lineTo(x1, height);
  g.stroke();
  if (!ui.state?.pxSource) {
    g.fillStyle = "#8a8a92";
    g.font = `${12 * dpr}px Lato, sans-serif`;
    g.textAlign = "center";
    g.fillText("carga un audio para ver la forma de onda", width / 2, height / 2);
  }
  const play = Number(ui.state?.pxPlayhead || 0);
  g.strokeStyle = "rgba(255,255,255,0.85)";
  g.beginPath();
  g.moveTo(play * width, 0);
  g.lineTo(play * width, height);
  g.stroke();
}

function pxNorm(event) {
  const rect = $("pxCanvas").getBoundingClientRect();
  return Math.min(1, Math.max(0, (event.clientX - rect.left) / rect.width));
}

function onPxPointer(event) {
  const canvas = $("pxCanvas");
  if (event.type === "pointermove" && !ui.pxDrag) return;
  const x = pxNorm(event);
  if (event.type === "pointerdown") {
    const range = pxRange();
    const rect = canvas.getBoundingClientRect();
    const px = 10 / rect.width;
    let mode = "region";
    if (Math.abs(x - range.start) <= px) mode = "start";
    else if (Math.abs(x - range.end) <= px) mode = "end";
    else if (x < range.start || x > range.end) mode = x < (range.start + range.end) / 2 ? "start" : "end";
    ui.pxDrag = { ...range, mode, grab: x - range.start };
    canvas.setPointerCapture(event.pointerId);
  }
  const drag = ui.pxDrag;
  if (drag.mode === "start") drag.start = Math.min(x, drag.end - 0.002);
  else if (drag.mode === "end") drag.end = Math.max(x, drag.start + 0.002);
  else {
    const len = drag.end - drag.start;
    drag.start = Math.min(1 - len, Math.max(0, x - drag.grab));
    drag.end = drag.start + len;
  }
  drawPx();
}

function endPxPointer() {
  if (!ui.pxDrag) return;
  const { start, end } = ui.pxDrag;
  ui.pxDrag = null;
  cmd({ t: "setParam", id: "px.sound_start", value: start });
  cmd({ t: "setParam", id: "px.sound_end", value: end });
}

$("engine").onclick = () => cmd({ t: "engine", on: $("engine").dataset.on !== "1" });
$("granular").onclick = () => cmd({ t: "engine", granular: $("granular").dataset.on !== "1" });
$("pxEnable").onclick = () => cmd({ t: "engine", paulxstretch: $("pxEnable").dataset.on !== "1" });
document.querySelectorAll(".tabs button").forEach((button) => {
  button.onclick = () => showTab(button.dataset.tab);
});
deviceSelect.onchange = () => {
  ui.id = deviceSelect.value;
  ui.lines = [];
  adopt();
};
window.addEventListener("resize", () => {
  drawWave();
  drawPx();
});
connect();
showTab("sinte");
