// test_page.mjs: drives the settings page in headless Chrome (its DevTools protocol over Node's own WebSocket, nothing
// installed) at a phone size and a PC size: every tab, every control through its real handler, a picture after
// each, and what the server got (the stand-in's /__log). Also: console errors, sideways scrolling, tap target sizes.
//   node test_page.mjs URL OUTDIR [phone|pc|both]
import { spawn } from "node:child_process";
import { mkdirSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const [URL_, OUT, which = "both"] = process.argv.slice(2);
mkdirSync(OUT, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const PORT = 9300 + Math.floor(Math.random() * 500);
const prof = mkdtempSync(join(tmpdir(), "orbs-page-test-"));
const chrome = spawn("google-chrome-stable", ["--headless=new", `--remote-debugging-port=${PORT}`, `--user-data-dir=${prof}`,
  "--no-first-run", "--no-default-browser-check", "--disable-extensions", "--mute-audio", "about:blank"], { stdio: "ignore" });
let ws, seq = 0;
const waiting = new Map(), events = [], dialogs = [];
async function connect() {
  for (let i = 0; i < 50; i++) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
      const pg = list.find((t) => t.type === "page");
      if (pg) { ws = new WebSocket(pg.webSocketDebuggerUrl); break; }
    } catch (e) {}
    await sleep(200);
  }
  await new Promise((r) => (ws.onopen = r));
  ws.onmessage = (m) => {
    const d = JSON.parse(m.data);
    if (d.id && waiting.has(d.id)) { waiting.get(d.id)(d); waiting.delete(d.id); }
    else if (d.method) {
      events.push(d);
      // a confirm() box blocks the click that opened it: answered here at once (OK), as a person would
      if (d.method === "Page.javascriptDialogOpening") { dialogs.push(d.params.message); ws.send(JSON.stringify({ id: ++seq, method: "Page.handleJavaScriptDialog", params: { accept: true } })); }
    }
  };
}
const cdp = (method, params = {}) => new Promise((res) => { const id = ++seq; waiting.set(id, res); ws.send(JSON.stringify({ id, method, params })); });
const js = async (expr) => { const r = await cdp("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true }); return r.result?.result?.value; };
// the stand-in keeps a log of what it got; the real XL doesn't: there, what changed in /api/state is what it got
let REAL = false;
const flat = (o, p = "", out = {}) => { if (o && typeof o === "object" && !Array.isArray(o)) { for (const [k, v] of Object.entries(o)) flat(v, p ? `${p}.${k}` : k, out); } else out[p] = JSON.stringify(o); return out; };
async function state() {
  for (let i = 0; i < 40; i++) {
    try { const r = await fetch(new URL("/api/state", URL_)); if (r.ok) return await r.json(); } catch (e) {}
    await sleep(1000);                                   // (the XL restarting: back within ~15 s)
  }
  throw new Error("the orbs stopped answering");
}
let lastState = null;
async function log() {
  if (!REAL) return (await fetch(new URL("/__log", URL_))).json();
  return [];
}
async function realDiff(before) {
  const after = await state();
  const a = flat({ settings: before.settings, showing: before.status.showing, dark: before.status.dark });
  const b = flat({ settings: after.settings, showing: after.status.showing, dark: after.status.dark });
  const d = Object.keys(b).filter((k) => a[k] !== b[k]).map((k) => `${k}=${b[k]}`);
  return { after, d };
}
let shot = 0, DIR = OUT;
async function picture(name) {
  const r = await cdp("Page.captureScreenshot", { format: "jpeg", quality: 75, captureBeyondViewport: false });
  const f = join(DIR, `${String(++shot).padStart(3, "0")}-${name.replace(/[^a-z0-9.-]+/gi, "_")}.jpg`);
  writeFileSync(f, Buffer.from(r.result.data, "base64"));
  return f;
}
async function tapAt(x, y) {
  for (const type of ["mousePressed", "mouseReleased"])
    await cdp("Input.dispatchMouseEvent", { type, x, y, button: "left", clickCount: 1, pointerType: "mouse" });
}
async function tap(selector) {        // a real click where the element is on screen
  const box = await js(`(()=>{const e=document.querySelector(${JSON.stringify(selector)}); if(!e) return null; e.scrollIntoView({block:"center"});
    const r=e.getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2,w:r.width,h:r.height}})()`);
  if (!box) return null;
  await sleep(80);
  const b2 = await js(`(()=>{const e=document.querySelector(${JSON.stringify(selector)}); if(!e) return null; const r=e.getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2,w:r.width,h:r.height}})()`);
  await tapAt((b2 || box).x, (b2 || box).y);         // (redrawn meanwhile: the same place, it hasn't scrolled)
  return b2;
}
let results = [];
async function step(tab, what, how, fn) {
  const before = REAL ? 0 : (await log()).length;
  const sBefore = REAL ? await state() : null;
  const evBefore = events.length;
  let note = "";
  try { note = (await fn()) || ""; } catch (e) { note = "ERROR " + e.message; }
  await sleep(REAL ? 900 : 450);
  let got;
  if (REAL) { const r = await realDiff(sBefore); got = r.d.length ? ["changed " + r.d.join(", ").slice(0, 160)] : []; }
  else { const after = await log(); got = after.slice(before).map((x) => Object.keys(x)[0] + " " + JSON.stringify(Object.values(x)[0]).slice(0, 90)); }
  const errs = events.slice(evBefore).filter((e) => e.method === "Runtime.exceptionThrown" ||
    (e.method === "Runtime.consoleAPICalled" && e.params.type === "error") || (e.method === "Log.entryAdded" && e.params.entry.level === "error"))
    .map((e) => JSON.stringify(e.params).slice(0, 200));
  const pic = await picture(`${tab}-${what}`);
  results.push({ tab, what, how, got, errs, note, pic });
  writeResults();
}
function writeResults() {
  writeFileSync(join(DIR, "results.json"), JSON.stringify(results, null, 1));
  writeFileSync(join(DIR, "results.txt"), results.map((r) => `${r.tab.padEnd(8)} ${String(r.what).padEnd(28).slice(0, 28)} ${r.how.padEnd(22).slice(0, 22)} -> ${r.got.join(" | ") || r.note || "nothing sent"}${r.errs.length ? "  !! " + r.errs.join(" ; ") : ""}`).join("\n") + "\n");
}
async function run(device) {
  shot = 0; results = [];
  const phone = device === "phone";
  // a large phone: 440 x 956 points, 3x; a PC window 1280 x 900
  await cdp("Emulation.setDeviceMetricsOverride", phone ? { width: 440, height: 956, deviceScaleFactor: 3, mobile: true }
    : { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await cdp("Emulation.setTouchEmulationEnabled", { enabled: phone });
  await cdp("Page.navigate", { url: URL_ });
  await sleep(1500);
  await js(`localStorage.clear()`);
  await cdp("Page.reload", {});
  await sleep(1500);
  const tabs = await js(`[...document.querySelectorAll("#tabs button")].map(b=>b.textContent)`);
  for (const [ti, t] of tabs.entries()) {
    await step(t, "open tab", "tap", () => tap(`#tabs button:nth-child(${ti + 1})`));
    const sideways = await js(`document.scrollingElement.scrollWidth - innerWidth`);
    if (sideways > 0) results.push({ tab: t, what: "LAYOUT", how: "", got: [], errs: [`sideways scroll ${sideways}px`], note: "", pic: "" });
    const small = await js(`[...document.querySelectorAll("#main button,#main select,#main input:not([type=checkbox]),#main .sw,#main a")].filter(e=>{const r=e.getBoundingClientRect(); return r.width&&r.height&&(r.height<36||r.width<36)}).map(e=>(e.getAttribute("aria-label")||e.textContent).slice(0,30))`);
    if (small.length) results.push({ tab: t, what: "TAP SIZE", how: "", got: [], errs: [`small targets: ${small.join(", ")}`], note: "", pic: "" });
    await controls(t, ti, "#main");
    // the Sets tab: a Customize panel for each kind of set (a rig set, a weather set, a set drawn as one, Messages),
    // every control inside it
    if (t === "Sets") for (const k of ["rig", "weather", "clock", "messages"]) {
      const opened = await js(`(()=>{const b=document.querySelector('[aria-label="${k} customize"]'); if(!b) return false; if(!b.classList.contains("primary")) b.click(); return true})()`);
      if (!opened) continue;
      await sleep(400);
      await step(t, `${k} customize`, "open the panel", async () => "opened");
      await controls(t, ti, "#main .sub");
    }
  }
  // the top bar: try first, put back, try again and keep, undo
  await step("Top", "try first", "tap the switch", () => tap('[aria-label="try first"]'));
  await sleep(600);
  await tap(`#tabs button:nth-child(1)`); await sleep(400);
  await step("Top", "a change while trying", "brightness slider", async () => { await js(`(()=>{const e=document.querySelector('[aria-label="screens.brightness"]'); e.value=140; e.dispatchEvent(new Event("input",{bubbles:true}))})()`); });
  await sleep(1500);
  await step("Top", "put back", "tap", () => tap('[aria-label="put back"]'));
  await sleep(800);
  await step("Top", "try first again", "tap the switch", () => tap('[aria-label="try first"]'));
  await sleep(600);
  await step("Top", "a change while trying", "brightness slider", async () => { await js(`(()=>{const e=document.querySelector('[aria-label="screens.brightness"]'); e.value=170; e.dispatchEvent(new Event("input",{bubbles:true}))})()`); });
  await sleep(1500);
  await step("Top", "keep", "tap", () => tap('[aria-label="keep"]'));
  await sleep(800);
  await step("Top", "undo", "tap", () => tap('[aria-label="undo"]'));
  // the press buttons (Now tab, 2026-11-17): each tap is a real press on the orbs. On the real orbs always on the Clock,
  // shown again before every press: an XL-drawn set, so no press reaches the rig (never the music); the screens on
  // first (on dark screens a press only wakes them, as the real buttons do)
  {
    const post = (path, body) => fetch(new URL(path, URL_), { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }).then((r) => r.json());
    await post("/api/action", { do: "dark", on: false });
    await tap(`#tabs button:nth-child(1)`); await sleep(400);
    for (const b of ["left", "middle", "right"]) for (const k of ["short", "medium", "long"]) {
      if (REAL) { const r = await post("/api/action", { do: "show", set: "clock" }); await sleep(1500);
        if (!r.ok || (await state()).status.showing !== "clock") { results.push({ tab: "Now", what: `press ${b} ${k}`, how: "", got: [], errs: ["the Clock couldn't be shown first: press skipped"], note: "", pic: "" }); continue; } }
      await step("Now", `press ${b} ${k}`, "tap", async () => { const box = await tap(`[aria-label="press ${b} ${k}"]`); return box ? "" : "NOT FOUND"; });
      if (REAL && b === "middle" && k === "long") {      // (the settings QR on the middle orb: its picture kept)
        await sleep(1500);
        writeFileSync(join(DIR, "press-middle-long-orb3.bmp"), Buffer.from(await (await fetch(new URL("/api/orb/2", URL_))).arrayBuffer()));
      }
    }
  }
  // Rules and a message's size, step by step (the tap-everything pass removes a rule before reaching its fields)
  const tabByText = async (txt) => { await js(`[...document.querySelectorAll("#tabs button")].find(b=>b.textContent===${JSON.stringify(txt)}).click()`); await sleep(500); };
  const mark = async (sel, txt) => js(`(()=>{document.querySelectorAll("[data-t]").forEach(e=>e.removeAttribute("data-t"));
    const e=[...document.querySelectorAll(${JSON.stringify(sel)})].find(e=>${txt == null ? "true" : `e.textContent.trim()===${JSON.stringify(txt)}`}); if(!e) return false; e.setAttribute("data-t","x"); return true})()`);
  const tapMarked = async (sel, txt) => (await mark(sel, txt)) ? (await tap("[data-t]"), "") : `NOT FOUND ${sel} ${txt || ""}`;
  const setValue = (sel, v, ev = "change") => js(`(()=>{const e=document.querySelector(${JSON.stringify(sel)}); if(!e) return "NOT FOUND"; e.value=${JSON.stringify(v)}; e.dispatchEvent(new Event(${JSON.stringify(ev)},{bubbles:true})); return ""})()`);
  const nextOption = (sel) => js(`(()=>{const e=document.querySelector(${JSON.stringify(sel)}); if(!e) return "NOT FOUND"; e.selectedIndex=(e.selectedIndex+1)%e.options.length; e.dispatchEvent(new Event("change",{bubbles:true})); return e.value})()`);
  await tabByText("Rules");
  await step("Rules", "add a time rule", "tap", () => tapMarked('[aria-label="add rule"]'));
  await step("Rules", "rule 1 show", "next option", () => nextOption('[aria-label="rules.0.set"]'));
  await step("Rules", "rule 1 Sunday on", "tap", () => tapMarked('[aria-label="rule 1 sun"]'));
  await step("Rules", "rule 1 Monday off", "tap", () => tapMarked('[aria-label="rule 1 mon"]'));
  await step("Rules", "rule 1 from", "set 22:00", () => setValue('[aria-label="rule 1 start"]', "22:00"));
  await step("Rules", "rule 1 until", "set 06:30", () => setValue('[aria-label="rule 1 end"]', "06:30"));
  await step("Rules", "add a second rule", "tap", () => tapMarked('[aria-label="add rule"]'));
  await step("Rules", "rule 2 to an event", "tap Event", () => tapMarked(".sub:nth-of-type(2) .seg button, .card .sub ~ .sub .seg button", "Event"));
  await step("Rules", "rule 2 when", "next option", () => nextOption('[aria-label="rules.1.event"]'));
  await step("Rules", "rule 2 for", "type 30", async () => { await tap('[aria-label="rule 2 secs"]'); await js(`document.querySelector('[aria-label="rule 2 secs"]').select()`);
    await cdp("Input.insertText", { text: "30" }); await js(`document.querySelector('[aria-label="rule 2 secs"]').blur()`); });
  await step("Rules", "rule 2 show", "next option", () => nextOption('[aria-label="rules.1.set"]'));
  await step("Rules", "rule 1 remove", "tap", () => tapMarked('[aria-label="rule 1 remove"]'));
  await tabByText("Messages");
  await step("Messages", "message 1 a message", "tap Message", () => tapMarked("#main .sub:first-of-type .seg button", "Message"));
  await step("Messages", "message 1 size", "tap Big", () => tapMarked("#main .sub .seg button", "Big"));
  await step("Messages", "message 1 size", "tap Auto", () => tapMarked("#main .sub .seg button", "Auto"));
  // a refused value shows its reason: a colour the XL won't take, typed into the number field for the Matrix
  await js(`[...document.querySelectorAll("#tabs button")].find(b=>b.textContent==="Matrix").click()`); await sleep(300);
  await step("Matrix", "refused value", "longest line 99", async () => {
    await js(`(()=>{const e=document.querySelector('[aria-label="matrix.line_max"]'); e.value="99"; e.dispatchEvent(new Event("change",{bubbles:true}))})()`);
  });
  writeFileSync(join(DIR, "results.json"), JSON.stringify(results, null, 1));
  const lines = results.map((r) => `${r.tab.padEnd(8)} ${String(r.what).padEnd(28).slice(0, 28)} ${r.how.padEnd(22).slice(0, 22)} -> ${r.got.join(" | ") || r.note || "nothing sent"}${r.errs.length ? "  !! " + r.errs.join(" ; ") : ""}`);
  writeFileSync(join(DIR, "results.txt"), lines.join("\n") + "\n");
  return results;
}
async function controls(t, ti, root) {
    const SEL = `${root} button, ${root} select, ${root} input, ${root} a.btn`;
    const count = () => js(`document.querySelectorAll(${JSON.stringify(SEL)}).length`);
    for (let i = 0; i < await count() && i < 400; i++) {
      const info = await js(`(()=>{const e=[...document.querySelectorAll(${JSON.stringify(SEL)})][${i}];
        if(!e) return null; return {tag:e.tagName, type:e.type||"", label:e.getAttribute("aria-label")||e.textContent.trim().slice(0,40), disabled:e.disabled, hidden: e.offsetParent===null && e.type!=="checkbox" && e.type!=="file"}})()`);
      if (!info || info.disabled || info.hidden) continue;
      if (/customize$/.test(info.label)) continue;          // (the panels are opened on purpose, after the tab)
      if (/^press (left|middle|right) (short|medium|long)$/.test(info.label)) continue;   // (pressed on purpose below, on a safe set)
      const pm = /^preset (\d+) (save|delete)$/.exec(info.label);
      if (REAL && pm && (snapshot.presets || [])[Number(pm[1])]) continue;   // (the owner's own presets: never touched)
      const pick = `[...document.querySelectorAll(${JSON.stringify(SEL)})][${i}]`;
      const mark = `(()=>{const e=${pick}; e.setAttribute("data-t","x"); return true})()`;
      const unmark = `document.querySelectorAll("[data-t]").forEach(e=>e.removeAttribute("data-t"))`;
      await js(unmark); await js(mark);
      const label = info.label || info.type;
      if (info.label === "Restart" || info.label === "Defaults") {        // (a confirm box: answered OK by the handler above)
        await step(t, label, "tap + OK", async () => { const n0 = dialogs.length; await tap("[data-t]"); await sleep(300);
          if (REAL && info.label === "Restart") { await sleep(4000); await state(); await cdp("Page.reload", {}); await sleep(2500); }
          return dialogs.length > n0 ? `asked: "${dialogs[dialogs.length - 1]}"` : "NO confirm box"; });
      } else if (info.tag === "A") {
        await step(t, label, "link checked", async () => js(`(async()=>{const r=await fetch(document.querySelector("[data-t]").href); return "GET "+r.status+" "+(await r.text()).length+" bytes"})()`));
      } else if (info.type === "file") {
        await step(t, "restore a copy", "file with the current settings", async () => {
          const r = await cdp("DOM.getDocument", {}); const q = await cdp("DOM.querySelector", { nodeId: r.result.root.nodeId, selector: "[data-t]" });
          const f = join(prof, "orbs-settings.json"); writeFileSync(f, JSON.stringify(await (await fetch(new URL("/api/backup", URL_))).json()));
          await cdp("DOM.setFileInputFiles", { nodeId: q.result.nodeId, files: [f] }); return "file set";
        });
      } else if (info.tag === "SELECT") {
        await step(t, label, "choose the next option", () => js(`(()=>{const e=document.querySelector("[data-t]"); e.focus(); e.selectedIndex=(e.selectedIndex+1)%e.options.length;
          e.dispatchEvent(new Event("change",{bubbles:true})); return e.value})()`));
      } else if (info.type === "checkbox") {
        await step(t, label, "tap the switch", async () => { const e = await js(`(()=>{const e=document.querySelector("[data-t]"); return e.closest(".sw")?1:0})()`);
          await tap("[data-t]"); return e ? "switch" : "box"; });
      } else if (info.type === "range") {
        await step(t, label, "drag to 3/4", async () => {
          const b = await js(`(()=>{const e=document.querySelector("[data-t]"); e.scrollIntoView({block:"center"}); const r=e.getBoundingClientRect(); return {x:r.x,y:r.y+r.height/2,w:r.width}})()`);
          await cdp("Input.dispatchMouseEvent", { type: "mousePressed", x: b.x + b.w * 0.5, y: b.y, button: "left", clickCount: 1 });
          for (let k = 5; k <= 10; k++) await cdp("Input.dispatchMouseEvent", { type: "mouseMoved", x: b.x + b.w * (0.5 + k * 0.025), y: b.y, button: "left", buttons: 1 });
          await cdp("Input.dispatchMouseEvent", { type: "mouseReleased", x: b.x + b.w * 0.75, y: b.y, button: "left", clickCount: 1 });
        });
      } else if (info.type === "color") {
        await step(t, label, "pick #ff3366", () => js(`(()=>{const e=document.querySelector("[data-t]"); e.value="#ff3366"; e.dispatchEvent(new Event("input",{bubbles:true}));
          e.dispatchEvent(new Event("change",{bubbles:true})); return ""})()`));
      } else if (info.type === "datetime-local") {
        await step(t, label, "set 2026-12-25 7:00 AM", () => js(`(()=>{const e=document.querySelector("[data-t]"); e.value="2026-12-25T07:00"; e.dispatchEvent(new Event("change",{bubbles:true})); return ""})()`));
      } else if (info.type === "time") {
        await step(t, label, "set 23:30", () => js(`(()=>{const e=document.querySelector("[data-t]"); e.value="23:30"; e.dispatchEvent(new Event("change",{bubbles:true})); return e.value})()`));
      } else if (info.type === "number" || info.type === "text") {
        const v = info.type === "number" ? await js(`(()=>{const e=document.querySelector("[data-t]"); const n=Number(e.value); const mx=Number(e.max); return String(n+1<=mx?n+1:Math.max(Number(e.min),n-1))})()`) : "Springfield";
        await step(t, label, `type ${v}`, async () => {
          await tap("[data-t]"); await js(`document.querySelector("[data-t]").select()`);
          await cdp("Input.insertText", { text: v });
          if (label === "place search") { await cdp("Input.dispatchKeyEvent", { type: "keyDown", key: "Enter", code: "Enter", windowsVirtualKeyCode: 13 }); await cdp("Input.dispatchKeyEvent", { type: "keyUp", key: "Enter", code: "Enter", windowsVirtualKeyCode: 13 }); }
          else await js(`document.querySelector("[data-t]").blur()`);
        });
      } else {
        await step(t, label, "tap", () => tap("[data-t]"));
      }
      // a tap can switch tabs or redraw: come back to this tab
      const cur = await js(`[...document.querySelectorAll("#tabs button")].findIndex(b=>b.classList.contains("on"))`);
      if (cur !== ti) await tap(`#tabs button:nth-child(${ti + 1})`);
    }
}
let snapshot = null, restored = false;
// the owner's settings back on the orbs: at the end, and also if this test is stopped (a time limit or Ctrl+C skip "finally")
async function restore() {
  if (!snapshot || restored) return;
  restored = true;
  const post = (path, body) => fetch(new URL(path, URL_), { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }).then((r) => r.json());
  // (a "try" left running would roll the settings back 30 s later: put it back first, then the settings)
  await post("/api/action", { do: "revert" }).catch(() => ({}));
  const r1 = await post("/api/settings", snapshot.settings).catch((e) => ({ ok: false, error: String(e) }));
  // presets the test saved into slots that were empty before it: deleted again
  const pnow = (await (await fetch(new URL("/api/presets", URL_))).json()).presets || [];
  const pdel = [];
  for (let i = 0; i < pnow.length; i++)
    if (pnow[i] && !(snapshot.presets || [])[i]) { await post("/api/presets", { do: "delete", slot: i }).catch(() => ({})); pdel.push(i + 1); }
  const pafter = (await (await fetch(new URL("/api/presets", URL_))).json()).presets || [];
  const psame = JSON.stringify(pafter) === JSON.stringify(snapshot.presets);
  const r2 = await post("/api/action", { do: "dark", on: false }).catch(() => ({}));
  const r3 = await post("/api/action", { do: "show", set: snapshot.showing }).catch((e) => ({ ok: false, error: String(e) }));
  const back = await state();
  const same = JSON.stringify(back.settings) === JSON.stringify(snapshot.settings);
  writeFileSync(join(OUT, "restored.txt"), `settings put back: ${r1.ok} (identical: ${same}); screens on: ${r2.ok}; showing ${snapshot.showing}: ${r3.ok}; ` +
    `presets as before: ${psame} (test presets deleted from slots ${pdel.join(", ") || "none"})\n`);
}
for (const sig of ["SIGTERM", "SIGINT"])
  process.on(sig, async () => { try { await restore(); } finally { try { chrome.kill("SIGKILL"); } catch (e) {} rmSync(prof, { recursive: true, force: true }); process.exit(1); } });
try {
  REAL = (await fetch(new URL("/__log", URL_))).status === 404;
  if (REAL) {   // the owner's own settings: kept here and put back at the end, whatever happens
    const st = await state();
    snapshot = { settings: st.settings, showing: st.status.showing,
                 presets: (await (await fetch(new URL("/api/presets", URL_))).json()).presets };
    writeFileSync(join(OUT, "snapshot.json"), JSON.stringify(snapshot));
  }
  await connect();
  for (const d of ["Page", "Runtime", "Log", "DOM"]) await cdp(`${d}.enable`);
  await cdp("Page.setInterceptFileChooserDialog", { enabled: true });   // (a file chooser would wait for a person)
  const devices = which === "both" ? ["phone", "pc"] : [which];
  for (const d of devices) {
    DIR = join(OUT, d); mkdirSync(DIR, { recursive: true });
    await run(d);
  }
} finally {
  await restore();
  try { ws.close(); } catch (e) {}
  chrome.kill("SIGTERM");
  await sleep(500);
  try { chrome.kill("SIGKILL"); } catch (e) {}
  rmSync(prof, { recursive: true, force: true });
}
