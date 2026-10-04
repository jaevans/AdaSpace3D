#ifndef CONFIG_PAGE_H
#define CONFIG_PAGE_H

#include <stdint.h>

// Served as CONFIG.HTM on the config-mode drive (see VirtualDrive.h).
// Talks to the firmware's serial command set; PROTOCOL_VERSION in
// AdaSpace3D.ino must match the "proto=" value checked below.
static const char CONFIG_PAGE[] = R"ADAPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AdaSpace3D Config</title>
<style>
:root { --bg:#f6f7f9; --fg:#1b1f24; --muted:#5b6470; --card:#fff; --line:#d5d9df; --accent:#0a7cbe; --err:#c0392b; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#14171b; --fg:#e6e9ed; --muted:#9aa3ad; --card:#1d2126; --line:#333a42; --accent:#4fb3ee; --err:#ff6b5b; }
}
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--fg); font:15px/1.45 system-ui, sans-serif; }
main { max-width:640px; margin:0 auto; padding:24px 16px; }
h1 { font-size:22px; margin:0 0 4px; }
p.sub { margin:0 0 20px; color:var(--muted); }
section { background:var(--card); border:1px solid var(--line); border-radius:8px; padding:16px; margin-bottom:16px; }
h2 { font-size:15px; margin:0 0 12px; }
.row { display:flex; align-items:center; justify-content:space-between; gap:12px; padding:6px 0; }
.grid { display:grid; grid-template-columns:repeat(2, 1fr); gap:8px 16px; }
label { color:var(--muted); }
select, input[type=number] { font:inherit; color:var(--fg); background:var(--bg); border:1px solid var(--line); border-radius:6px; padding:4px 8px; width:90px; }
button { font:inherit; padding:8px 14px; border-radius:6px; border:1px solid var(--line); background:var(--bg); color:var(--fg); cursor:pointer; }
button.primary { background:var(--accent); border-color:var(--accent); color:#fff; }
button:disabled { opacity:.5; cursor:default; }
.actions { display:flex; flex-wrap:wrap; gap:8px; }
#status { margin-top:8px; color:var(--muted); min-height:1.4em; }
#status.err { color:var(--err); }
canvas { display:block; margin:8px auto 0; max-width:100%; }
</style>
</head>
<body>
<main>
<h1>AdaSpace3D</h1>
<p class="sub">Sensor orientation and button mapping. Requires Chrome or Edge.</p>

<section>
  <div class="actions">
    <button id="connect" class="primary">Connect</button>
    <button id="bootloader" disabled>Reboot to bootloader</button>
  </div>
  <div id="status"></div>
</section>

<section>
  <h2>Orientation</h2>
  <div class="row"><label for="orientation">Sensor rotation</label>
    <select id="orientation" disabled>
      <option value="0">0&deg;</option><option value="90">90&deg;</option>
      <option value="180">180&deg;</option><option value="270">270&deg;</option>
    </select></div>
  <div class="row"><label for="invert_x">Invert X</label><input type="checkbox" id="invert_x" disabled></div>
  <div class="row"><label for="invert_y">Invert Y</label><input type="checkbox" id="invert_y" disabled></div>
</section>

<section>
  <h2>Buttons</h2>
  <div class="grid">
    <div class="row"><label for="button1">Switch 1 (A0)</label><input type="number" id="button1" min="1" max="32" disabled></div>
    <div class="row"><label for="button2">Switch 2 (A1)</label><input type="number" id="button2" min="1" max="32" disabled></div>
    <div class="row"><label for="button3">Switch 3 (A2)</label><input type="number" id="button3" min="1" max="32" disabled></div>
    <div class="row"><label for="button4">Switch 4 (A3)</label><input type="number" id="button4" min="1" max="32" disabled></div>
  </div>
  <p class="sub" style="margin:8px 0 0">At plug-in, the switch with the lowest number enters config mode, and the lowest and highest together enter the bootloader. Takes effect once saved.</p>
</section>

<section>
  <div class="actions">
    <button id="apply" class="primary" disabled>Apply</button>
    <button id="save" disabled>Save to device</button>
    <button id="defaults" disabled>Restore defaults</button>
  </div>
  <p class="sub" style="margin:8px 0 0">Apply takes effect immediately. Save keeps it after unplugging.</p>
</section>

<section>
  <div class="row"><h2 style="margin:0">Live view</h2><button id="live" disabled>Start</button></div>
  <canvas id="view" width="240" height="240"></canvas>
</section>
</main>

<script>
const KEYS = ["orientation", "invert_x", "invert_y", "button1", "button2", "button3", "button4"];
const $ = id => document.getElementById(id);
const statusEl = $("status");
let port = null, writer = null, reader = null, readClosed = null, writeClosed = null;
let current = {}, streaming = false;
// Reply lines queue here until a command takes them, so a reply that arrives
// before anyone is waiting (several lines in one chunk) is never lost.
let lines = [], lineArrived = null, queue = Promise.resolve();
let scale = 2, lastXY = [0, 0];

function status(msg, isErr) {
  statusEl.textContent = msg;
  statusEl.className = isErr ? "err" : "";
}

function setEnabled(on) {
  for (const k of KEYS) $(k).disabled = !on;
  for (const b of ["apply", "save", "defaults", "live", "bootloader"]) $(b).disabled = !on;
  $("connect").textContent = on ? "Disconnect" : "Connect";
}

function onLine(line) {
  if (line.startsWith("xy ")) {
    const p = line.split(" ");
    lastXY = [parseFloat(p[1]), parseFloat(p[2])];
    draw();
    return;
  }
  lines.push(line);
  if (lineArrived) { const wake = lineArrived; lineArrived = null; wake(); }
}

async function nextLine(deadline) {
  while (lines.length === 0) {
    const left = deadline - Date.now();
    if (left <= 0) return null;
    await new Promise(res => { lineArrived = res; setTimeout(res, left); });
  }
  return lines.shift();
}

async function readLoop() {
  let buf = "";
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      buf += value;
      let i;
      while ((i = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, i).replace(/\r$/, "");
        buf = buf.slice(i + 1);
        onLine(line);
      }
    }
  } catch (e) {
    if (port) status("Connection lost: " + e.message, true);
  }
}

// One command at a time, so replies cannot be matched to the wrong command.
function command(text, timeoutMs = 2000) {
  const run = async () => {
    await writer.write(text + "\n");
    const line = await nextLine(Date.now() + timeoutMs);
    if (line === null) throw new Error("no reply to \"" + text + "\"");
    return line;
  };
  const p = queue.then(run, run);
  queue = p.catch(() => {});
  return p;
}

async function expectOk(text) {
  const r = await command(text);
  if (!r.startsWith("ok")) throw new Error(r);
  return r;
}

async function waitForInfo() {
  // Stale replies from an earlier session may arrive first; skip until info answers.
  const deadline = Date.now() + 3000;
  await writer.write("stream off\ninfo\n");
  for (;;) {
    const line = await nextLine(deadline);
    if (line === null) return null;
    if (line.startsWith("ok adaspace3d")) return line;
  }
}

async function refresh() {
  const r = await expectOk("get");
  current = {};
  for (const kv of r.split(" ").slice(1)) {
    const [k, v] = kv.split("=");
    current[k] = v;
  }
  $("orientation").value = current.orientation;
  $("invert_x").checked = current.invert_x === "1";
  $("invert_y").checked = current.invert_y === "1";
  for (let n = 1; n <= 4; n++) $("button" + n).value = current["button" + n];
}

function formValues() {
  const v = {
    orientation: $("orientation").value,
    invert_x: $("invert_x").checked ? "1" : "0",
    invert_y: $("invert_y").checked ? "1" : "0",
  };
  for (let n = 1; n <= 4; n++) v["button" + n] = String(parseInt($("button" + n).value, 10));
  return v;
}

async function connect() {
  if (!("serial" in navigator)) {
    status("This browser has no Web Serial. Open this page in Chrome or Edge.", true);
    return;
  }
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    lines = [];
    const enc = new TextEncoderStream();
    writeClosed = enc.readable.pipeTo(port.writable);
    writer = enc.writable.getWriter();
    const dec = new TextDecoderStream();
    readClosed = port.readable.pipeTo(dec.writable);
    reader = dec.readable.getReader();
    readLoop();

    const info = await waitForInfo();
    if (!info || !info.includes("proto=1")) {
      await disconnect();
      status(info ? "Unsupported firmware: " + info : "That port is not an AdaSpace3D.", true);
      return;
    }
    await refresh();
    setEnabled(true);
    status("Connected.");
  } catch (e) {
    await disconnect();
    status(e.message, true);
  }
}

// port.close() rejects while its streams are locked, so tear down in the order
// the Web Serial spec requires: cancel the reader, close the writer, then close.
async function disconnect() {
  streaming = false;
  $("live").textContent = "Start";
  setEnabled(false);
  const p = port;
  port = null;
  try { if (reader) await reader.cancel(); } catch (e) {}
  try { if (readClosed) await readClosed; } catch (e) {}
  try { if (writer) await writer.close(); } catch (e) {}
  try { if (writeClosed) await writeClosed; } catch (e) {}
  try { if (p) await p.close(); } catch (e) { status("Could not close the port: " + e.message, true); }
  reader = writer = readClosed = writeClosed = null;
  lines = [];
  queue = Promise.resolve();
}

async function run(fn, okMsg) {
  try { await fn(); if (okMsg) status(okMsg); }
  catch (e) { status(e.message, true); }
}

$("connect").onclick = () => port ? disconnect().then(() => status("Disconnected.")) : connect();

$("apply").onclick = () => run(async () => {
  const v = formValues();
  for (const k of KEYS) if (v[k] !== current[k]) await expectOk("set " + k + " " + v[k]);
  await refresh();
}, "Applied. Not saved yet.");

$("save").onclick = () => run(() => expectOk("save"), "Saved to device.");

$("defaults").onclick = () => run(async () => {
  await expectOk("defaults");
  await refresh();
}, "Defaults restored. Not saved yet.");

$("bootloader").onclick = () => run(async () => {
  await expectOk("bootloader");
  await disconnect();
}, "Rebooting. An RPI-RP2 drive should appear.");

$("live").onclick = () => run(async () => {
  streaming = !streaming;
  await expectOk(streaming ? "stream on" : "stream off");
  $("live").textContent = streaming ? "Stop" : "Start";
  if (!streaming) { lastXY = [0, 0]; draw(); }
});

function draw() {
  const c = $("view"), g = c.getContext("2d");
  const w = c.width, h = c.height, cx = w / 2, cy = h / 2, r = w / 2 - 12;
  const css = getComputedStyle(document.documentElement);
  g.clearRect(0, 0, w, h);
  g.strokeStyle = css.getPropertyValue("--line");
  g.lineWidth = 1;
  g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); g.stroke();
  g.beginPath(); g.moveTo(cx - r, cy); g.lineTo(cx + r, cy); g.moveTo(cx, cy - r); g.lineTo(cx, cy + r); g.stroke();

  // Drawn as the translation sent to the computer: tx = -x, ty = -y. The
  // 3Dconnexion driver treats +ty as "away from you", so it is drawn upwards
  // (checked against Fusion on hardware).
  const tx = -lastXY[0], ty = -lastXY[1];
  scale = Math.max(2, scale * 0.995, Math.abs(tx), Math.abs(ty));
  const ex = cx + (tx / scale) * r, ey = cy - (ty / scale) * r;
  g.strokeStyle = g.fillStyle = css.getPropertyValue("--accent");
  g.lineWidth = 4;
  g.beginPath(); g.moveTo(cx, cy); g.lineTo(ex, ey); g.stroke();
  const a = Math.atan2(ey - cy, ex - cx);
  if (Math.hypot(ex - cx, ey - cy) > 8) {
    g.beginPath();
    g.moveTo(ex, ey);
    g.lineTo(ex - 12 * Math.cos(a - 0.4), ey - 12 * Math.sin(a - 0.4));
    g.lineTo(ex - 12 * Math.cos(a + 0.4), ey - 12 * Math.sin(a + 0.4));
    g.closePath(); g.fill();
  }
}

if (!("serial" in navigator)) status("This browser has no Web Serial. Open this page in Chrome or Edge.", true);
draw();
</script>
</body>
</html>
)ADAPAGE";

static const uint32_t CONFIG_PAGE_LEN = sizeof(CONFIG_PAGE) - 1;

#endif // CONFIG_PAGE_H
