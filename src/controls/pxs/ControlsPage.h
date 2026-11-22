#pragma once
// The page an application serves for its controls (Server.h): a single file, no library, no external request. Its style and its script
// are hashed into the Content-Security-Policy at start-up, so that nothing but this exact code can run in it.
//
// The controls are laid out on a free 12-column grid, a page for each top-level Group; in Edit mode a tile is dragged, resized and
// set (its widget, range, curve, accent). Every widget is drawn in SVG from a few tokens (--a, the accent). It runs on old iPads
// too (iOS 12: no Pointer Events, no ResizeObserver): `press` falls back on touch and mouse events.
//
// A script literal stays under the 16 KB a compiler takes: the script is several pieces joined (script()).
#include <QByteArray>

namespace pxs::controls::page {
inline constexpr char kStyleA[] = R"CSS(
:root { color-scheme: dark; --bg:#121215; --panel:#1c1c21; --panel2:#25252b; --line:#31313a; --text:#ececf0; --dim:#8a8a94; --acc:#29c7e0; }
* { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
[hidden] { display:none !important; }
html, body { margin:0; background:var(--bg); color:var(--text); font:15px/1.3 -apple-system, system-ui, "Segoe UI", sans-serif; -webkit-user-select:none; user-select:none; }
header { position:-webkit-sticky; position:sticky; top:0; z-index:5; display:flex; align-items:center; padding:10px 14px; background:var(--bg); border-bottom:1px solid var(--line); }
header > * { margin-right:10px; }
header h1 { flex:1; margin:0 10px 0 0; font-size:16px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
#state { font-size:12px; color:var(--dim); }
#state.ok::before { content:"● "; color:#46c46a; }
#state.bad::before { content:"● "; color:#e5564a; }
.hb { font:inherit; font-size:13px; color:var(--text); text-decoration:none; white-space:nowrap; border:1px solid var(--line); background:var(--panel); border-radius:8px; padding:7px 12px; }
.hb.on { background:var(--acc); border-color:var(--acc); color:#06222a; font-weight:600; }
#tabs { display:flex; overflow-x:auto; padding:8px 14px 0; border-bottom:1px solid var(--line); -webkit-overflow-scrolling:touch; }
#tabs button { font:inherit; font-size:14px; color:var(--dim); background:none; border:0; border-bottom:3px solid transparent; padding:8px 14px; white-space:nowrap; }
#tabs button.on { color:var(--text); border-bottom-color:var(--acc); }
#tabs .tbin { margin-left:8px; font-size:12px; opacity:.55; } #tabs .tbin:hover { opacity:1; color:#f08a82; }
#grid { position:relative; margin:10px; }
#wires { position:absolute; left:0; top:0; width:100%; height:100%; overflow:visible; pointer-events:none; z-index:5; }
.wire { stroke:var(--acc); stroke-width:1.5; stroke-dasharray:4 3; opacity:.8; }
.wdot { fill:var(--acc); }
.tile { position:absolute; transition:left .12s, top .12s, width .12s, height .12s; }
.tile.moving { transition:none; z-index:4; }
.face { position:absolute; left:4px; top:4px; right:4px; bottom:4px; background:var(--panel); border:1px solid var(--line); border-radius:14px; padding:9px 12px 10px; display:flex; flex-direction:column; overflow:hidden; --a:var(--acc); }
.tile.sec .face { background:transparent; border:0; border-bottom:1px solid var(--line); border-radius:0; padding:12px 2px 0; }
.sec .hd { font-size:13px; }
.hd { display:flex; justify-content:space-between; align-items:baseline; font-size:11px; letter-spacing:.06em; text-transform:uppercase; color:var(--dim); white-space:nowrap; }
.tile .face .hd span:first-child { font-size:15px; text-transform:none; letter-spacing:0; color:var(--text); font-weight:600; }
.face.sig { outline:1px dashed var(--acc); outline-offset:-4px; }
.sigtag { font-size:8px; letter-spacing:.08em; color:var(--acc); border:1px solid var(--acc); border-radius:4px; padding:0 4px; margin-left:6px; opacity:.85; font-weight:600; }
.dest { font-size:9px; line-height:1.25; color:var(--dim); opacity:.8; overflow:hidden; word-break:break-all; display:-webkit-box; -webkit-line-clamp:2; -webkit-box-orient:vertical; font-family:ui-monospace,Menlo,monospace; }
.hd span { overflow:hidden; text-overflow:ellipsis; }
.hd b { color:var(--text); font-weight:600; font-variant-numeric:tabular-nums; letter-spacing:0; margin-left:8px; }
.bd { flex:1; min-height:0; position:relative; display:flex; align-items:center; justify-content:center; }
.slab { position:relative; width:100%; height:100%; align-self:stretch; min-height:56px; border-radius:12px; background:var(--panel2); overflow:hidden; touch-action:none; cursor:pointer; }
.slab .sfill { position:absolute; left:0; bottom:0; top:0; background:var(--a); opacity:.3; }
.slab .sedge { position:absolute; top:0; bottom:0; width:4px; background:var(--a); box-shadow:0 0 14px var(--a); border-radius:2px; }
.slab.v .sfill { left:0; right:0; top:auto; width:auto; }
.slab.v .sedge { left:0; right:0; top:auto; bottom:0; width:auto; height:4px; }
.fader { width:100%; padding:10px 6px; touch-action:none; }
.fader .trk { position:relative; height:8px; background:var(--panel2); border-radius:6px; }
.fader .fill { position:absolute; left:0; top:0; bottom:0; border-radius:6px; background:var(--a); box-shadow:0 0 10px var(--a); }
.fader .cap { position:absolute; top:50%; width:22px; height:22px; margin:-11px 0 0 -11px; border-radius:50%; background:#f2f2f5; border:3px solid var(--a); box-shadow:0 0 8px var(--a); }
.fader.v { width:auto; height:100%; padding:6px 10px; }
.fader.v .trk { width:8px; height:100%; }
.fader.v .fill { left:0; right:0; top:auto; bottom:0; }
.fader.v .cap { left:50%; top:auto; margin:0 0 -11px -11px; }
.knob { height:100%; max-height:100%; width:100%; display:flex; justify-content:center; touch-action:none; }
.knob svg { height:100%; max-width:100%; }
.kbg { fill:none; stroke:var(--panel2); stroke-width:8; stroke-linecap:round; }
.kval { fill:none; stroke:var(--a); stroke-width:8; stroke-linecap:round; filter:drop-shadow(0 0 3px var(--a)); }
.kdot { stroke:#f2f2f5; stroke-width:3.5; stroke-linecap:round; }
.ktx { fill:var(--text); font:600 15px -apple-system, system-ui, sans-serif; text-anchor:middle; font-variant-numeric:tabular-nums; }
.multi { display:flex; width:100%; height:100%; align-self:stretch; touch-action:none; }
.mb { flex:1; position:relative; margin-right:3px; background:var(--panel2); border-radius:6px; overflow:hidden; }
.mb:last-child { margin-right:0; }
.mf { position:absolute; left:0; right:0; bottom:0; background:var(--a); opacity:.3; }
.me { position:absolute; left:0; right:0; height:4px; background:var(--a); box-shadow:0 0 10px var(--a); border-radius:2px; }
.multi.h { flex-direction:column; }
.multi.h .mb { margin:0 0 3px; } .multi.h .mb:last-child { margin:0; }
.multi.h .mf { right:auto; top:0; height:auto; }
.multi.h .me { top:0; bottom:0; right:auto; width:4px; height:auto; }
.adsr { position:relative; width:100%; height:100%; align-self:stretch; touch-action:none; }
.adsr svg { display:block; }
.aarea { fill:var(--a); opacity:.14; }
.aline { fill:none; stroke:var(--a); stroke-width:3; stroke-linejoin:round; filter:drop-shadow(0 0 4px var(--a)); }
.ahdl { fill:#f2f2f5; stroke:var(--a); stroke-width:3; }
.alab { position:absolute; left:0; right:0; bottom:0; text-align:center; font-size:12px; color:var(--dim); font-variant-numeric:tabular-nums; white-space:pre; }
.meter { position:relative; width:100%; height:100%; min-height:40px; border-radius:10px; background:var(--panel2); overflow:hidden; }
.meter .mfill { position:absolute; left:0; bottom:0; top:0; width:0; background:var(--a); opacity:.55; }
.meter.v .mfill { right:0; top:auto; width:auto; height:0; }
.meter .mpeak { position:absolute; left:0; top:0; bottom:0; width:3px; background:var(--a); box-shadow:0 0 10px var(--a); }
.meter.v .mpeak { left:0; right:0; top:auto; bottom:0; width:auto; height:3px; }
.readout { width:100%; text-align:center; font-size:30px; font-weight:600; font-variant-numeric:tabular-nums; color:var(--a); }
.graph { width:100%; height:100%; min-height:40px; border-radius:10px; background:var(--panel2); overflow:hidden; }
.graph svg { width:100%; height:100%; display:block; }
.gline { stroke:var(--a); }
.led { width:34px; height:34px; border-radius:50%; margin:auto; background:var(--panel2); border:1px solid var(--line); }
.led.on { background:var(--a); box-shadow:0 0 18px var(--a); }
.num { display:flex; align-items:center; width:100%; }
.num button { width:42px; height:42px; border-radius:50%; border:1px solid var(--line); background:var(--panel2); color:var(--text); font-size:22px; line-height:1; }
.num output { flex:1; text-align:center; font-size:26px; font-weight:600; font-variant-numeric:tabular-nums; }
.sw, .tapb, .holdb { width:100%; height:100%; min-height:44px; max-height:84px; border-radius:12px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font:inherit; font-size:15px; touch-action:none; position:relative; }
.sw::after { content:""; position:absolute; left:14px; top:50%; width:12px; height:12px; margin-top:-6px; border-radius:50%; background:#3a3a43; }
.sw.on { border-color:var(--a); } .sw.on::after { background:var(--a); box-shadow:0 0 10px var(--a); }
.holdb.on, .tapb.on { background:var(--a); color:#06222a; border-color:var(--a); box-shadow:0 0 14px var(--a); }
.choice { display:flex; width:100%; height:100%; max-height:84px; }
.choice button { flex:1; min-width:0; margin-right:6px; border-radius:10px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font:inherit; font-size:14px; padding:0 4px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.choice button:last-child { margin-right:0; }
.choice button.on { border-color:var(--a); color:var(--a); box-shadow:0 0 8px var(--a) inset; }
select.menu { width:100%; height:44px; border-radius:10px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font:inherit; font-size:16px; padding:0 10px; }
input[type=color] { width:100%; height:100%; min-height:40px; max-height:80px; border:1px solid var(--line); border-radius:12px; background:var(--panel2); padding:4px; }
.pad { position:relative; width:100%; height:100%; border-radius:10px; background:var(--panel2); touch-action:none; overflow:hidden; }
.pad::before, .pad::after { content:""; position:absolute; background:#34343d; }
.pad::before { left:50%; top:0; bottom:0; width:1px; } .pad::after { top:50%; left:0; right:0; height:1px; }
.pad i.pt { display:flex; align-items:center; justify-content:center; font:700 12px -apple-system, system-ui, sans-serif; color:#14141a; background:#f2f2f5; border-color:hsl(var(--h),70%,60%); box-shadow:0 0 12px hsl(var(--h),70%,60%); pointer-events:none; }
.rst { flex:none; width:24px; height:24px; padding:0; margin-left:6px; border-radius:6px; border:1px solid var(--line); background:var(--panel2); color:var(--dim); font-size:14px; line-height:1; cursor:pointer; }
.rst:active { color:var(--text); }
.pad i.pt.col { border-color:var(--bc); box-shadow:0 0 12px var(--bc); }
.pad i.pt.hot { background:hsl(var(--h),70%,70%); }
.pad i { position:absolute; width:26px; height:26px; margin:-13px 0 0 -13px; border-radius:50%; background:#f2f2f5; border:4px solid var(--a); box-shadow:0 0 12px var(--a); pointer-events:none; }
)CSS";

inline constexpr char kStyleB[] = R"CSS(
.edit .tile .face { border-style:dashed; }
.edit .hd { padding-right:34px; }
.shield { position:absolute; left:0; top:0; right:0; bottom:0; z-index:3; cursor:grab; border-radius:14px; touch-action:none; }
.tile.sel .face { border-color:var(--acc); border-style:solid; box-shadow:0 0 0 2px rgba(41,199,224,.35); }
.rz { position:absolute; right:4px; bottom:4px; width:34px; height:34px; z-index:4; touch-action:none; cursor:nwse-resize; }
.rz::after { content:""; position:absolute; right:8px; bottom:8px; width:12px; height:12px; border-right:3px solid var(--acc); border-bottom:3px solid var(--acc); border-radius:0 0 4px 0; }
.gear { position:absolute; right:10px; top:8px; z-index:4; width:30px; height:30px; border-radius:15px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font-size:15px; line-height:1; }
.gear.bin { border-color:#6b2b27; color:#f08a82; }
/* The tiles keep the width they have in play: the inspector lies over the right edge, and the page scrolls sideways to what it covers */
@media (min-width:760px) { body.insp { position:relative; overflow-x:auto; } body.insp::after { content:''; position:absolute; top:0; left:calc(100% + min(360px,100%)); width:1px; height:1px; } }
.marq { position:absolute; z-index:6; border:1px solid var(--acc); background:rgba(41,199,224,.12); pointer-events:none; }
.tile.sel { outline-offset:0; }
#grid.compact .tile { position:relative; transition:none; }
#empty { color:var(--dim); text-align:center; padding:50px 16px; }
#sheet { position:fixed; right:0; top:0; bottom:0; width:min(360px,100%); z-index:20; background:var(--panel); border-left:1px solid var(--line); padding:14px 16px 24px; overflow-y:auto; transform:translateX(105%); transition:transform .18s; -webkit-user-select:auto; user-select:auto; }
#sheet.open { transform:none; box-shadow:-12px 0 30px rgba(0,0,0,.5); }
#sheet h2 { margin:0 0 12px; font-size:16px; display:flex; justify-content:space-between; align-items:center; }
#sheet label { display:block; margin:12px 0 4px; font-size:11px; letter-spacing:.06em; text-transform:uppercase; color:var(--dim); }
#sheet input[type=text], #sheet input[type=number], #sheet select { width:100%; height:42px; border-radius:9px; border:1px solid var(--line); background:var(--panel2); color:var(--text); font:inherit; font-size:16px; padding:0 10px; }
#sheet .row { display:flex; } #sheet .row > div { flex:1; margin-right:8px; } #sheet .row > div:last-child { margin-right:0; }
#sheet input[type=range] { width:100%; height:34px; }
#sheet .sw8 { display:flex; flex-wrap:wrap; }
#sheet .sw8 button { width:34px; height:34px; margin:0 8px 8px 0; border-radius:17px; border:2px solid transparent; }
#sheet .sw8 button.on { border-color:#fff; }
#sheet .del { margin-top:22px; width:100%; height:44px; border-radius:10px; border:1px solid #6b2b27; background:#2a1716; color:#f08a82; font:inherit; font-size:15px; }
#sheet .hint { color:var(--dim); font-size:12px; margin-top:4px; }
#login { max-width:340px; margin:18vh auto 0; padding:0 20px; text-align:center; }
#login input { width:100%; padding:14px; font-size:22px; text-align:center; letter-spacing:.3em; background:var(--panel); color:var(--text); border:1px solid var(--line); border-radius:10px; }
#login p { color:var(--dim); }
#login button { width:100%; margin-top:12px; min-height:56px; border-radius:10px; border:0; background:var(--acc); color:#06222a; font:inherit; font-size:18px; font-weight:600; }
)CSS";

inline constexpr char kBody[] = R"HTML(
<header><h1 id="title">Controls</h1><span id="state">…</span><button class="hb" id="edit" hidden>Edit</button><a class="hb" id="toShow" href="/show" hidden>Show control →</a></header>
<nav id="tabs" hidden></nav>
<div id="login" hidden><h2>NyxMap remote</h2><p id="loginText">Enter the code set in NyxMap (Settings › OSC › Remote).</p><input id="pin" type="password" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" inputmode="numeric" placeholder="PIN"><button id="go">Connect</button></div>
<main id="grid" hidden></main>
<aside id="sheet"></aside>
)HTML";

// --- Script, in pieces -------------------------------------------------------------------------------------------------------
// A: state, the socket, helpers, `press` (pointer, touch or mouse), the value <-> position maps
inline constexpr char kScriptA[] = R"JS(
(() => {
  'use strict';
  const $ = s => document.querySelector(s);
  const NS = 'http://www.w3.org/2000/svg';
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
  const sv = (tag, attrs, parent) => { const e = document.createElementNS(NS, tag); for (const k in attrs || {}) e.setAttribute(k, attrs[k]); if (parent) parent.appendChild(e); return e; };
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  let pin = new URLSearchParams(location.search).get('pin') || sessionStorage.getItem('nyxmap-pin') || '', needPin = true;
  let filterCat = null, busyAt = 0, ws = null, opened = false, failures = 0, controls = [], sizes = {}, inspectorMode = 'always', nameParts = 2, sharedPage = false, sel = new Set(), values = {}, byId = {}, pageId = -1, editing = false, busy = 0, again = false, sheetId = 0;
  const active = new Set(), sendAt = {}, tiles = {};
  const state = (text, cls) => { const s = $('#state'); s.textContent = text; s.className = cls || ''; };
  const send = obj => { if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj)); };
  const throttled = (key, fn) => { const now = performance.now(); if (now - (sendAt[key] || 0) > 33) { sendAt[key] = now; fn(); return true; } return false; };
  const fmt = (c, v) => { const st = c.step || 0.01, d = st >= 1 ? 0 : Math.min(4, Math.ceil(-Math.log10(st))); return Number(v).toFixed(d) + (c.unit || ''); };
  const hex = rgb => '#' + rgb.map(x => ('0' + Math.round(clamp(x, 0, 1) * 255).toString(16)).slice(-2)).join('');
  const unhex = h => [1, 3, 5].map(i => parseInt(h.substr(i, 2), 16) / 255);
  // The position of a value on a slider (0..1) and back, through the curve of its gesture
  const toPos = (c, v) => { const span = c.max - c.min; return span ? Math.pow(clamp((v - c.min) / span, 0, 1), 1 / (c.curve || 1)) : 0; };
  const fromPos = (c, p) => { let v = c.min + (c.max - c.min) * Math.pow(clamp(p, 0, 1), c.curve || 1); const st = c.step || 0.01; v = Math.round((v - c.min) / st) * st + c.min; return clamp(Number(v.toFixed(6)), Math.min(c.min, c.max), Math.max(c.min, c.max)); };
  // A gesture on an element, whatever the browser has: pointer events, else touch and mouse. h.down(p) may refuse (false)
  function press(elm, h) {
    let on = false, id = null;
    if (window.PointerEvent) {
      elm.addEventListener('pointerdown', e => { if (e.button > 0 || on) return; if (h.down({ x: e.clientX, y: e.clientY, e }) === false) return; on = true; id = e.pointerId; try { elm.setPointerCapture(id); } catch (_) {} e.preventDefault(); });
      elm.addEventListener('pointermove', e => { if (on && e.pointerId === id) h.move({ x: e.clientX, y: e.clientY, e }); });
      const end = e => { if (on && e.pointerId === id) { on = false; h.up({ x: e.clientX, y: e.clientY, e }); } };
      elm.addEventListener('pointerup', end); elm.addEventListener('pointercancel', end);
    } else {
      const pt = e => { const t = e.changedTouches ? e.changedTouches[0] : e; return { x: t.clientX, y: t.clientY, e }; };
      elm.addEventListener('touchstart', e => { if (on) return; if (h.down(pt(e)) === false) return; on = true; e.preventDefault(); }, { passive: false });
      elm.addEventListener('touchmove', e => { if (on) { h.move(pt(e)); e.preventDefault(); } }, { passive: false });
      const tend = e => { if (on) { on = false; h.up(pt(e)); } };
      elm.addEventListener('touchend', tend); elm.addEventListener('touchcancel', tend);
      elm.addEventListener('mousedown', e => {
        if (on || h.down(pt(e)) === false) return; on = true; e.preventDefault();
        const mv = ev => h.move(pt(ev)), up = ev => { on = false; window.removeEventListener('mousemove', mv); window.removeEventListener('mouseup', up); h.up(pt(ev)); };
        window.addEventListener('mousemove', mv); window.addEventListener('mouseup', up);
      });
    }
  }
  // Several fingers at once on an element: down, move and up each get the finger's own id
  function presses(elm, h) {
    const on = {};
    if (window.PointerEvent) {
      elm.addEventListener('pointerdown', e => { if (e.button > 0) return; if (h.down({ id: e.pointerId, x: e.clientX, y: e.clientY }) === false) return; on[e.pointerId] = 1; try { elm.setPointerCapture(e.pointerId); } catch (_) {} e.preventDefault(); });
      elm.addEventListener('pointermove', e => { if (on[e.pointerId]) h.move({ id: e.pointerId, x: e.clientX, y: e.clientY }); });
      const end = e => { if (on[e.pointerId]) { delete on[e.pointerId]; h.up({ id: e.pointerId, x: e.clientX, y: e.clientY }); } };
      elm.addEventListener('pointerup', end); elm.addEventListener('pointercancel', end);
    } else {
      const each = (e, fn) => { for (let i = 0; i < e.changedTouches.length; i++) { const t = e.changedTouches[i]; fn({ id: 't' + t.identifier, x: t.clientX, y: t.clientY }); } e.preventDefault(); };
      elm.addEventListener('touchstart', e => each(e, p => { if (h.down(p) !== false) on[p.id] = 1; }), { passive: false });
      elm.addEventListener('touchmove', e => each(e, p => { if (on[p.id]) h.move(p); }), { passive: false });
      const tend = e => each(e, p => { if (on[p.id]) { delete on[p.id]; h.up(p); } });
      elm.addEventListener('touchend', tend, { passive: false }); elm.addEventListener('touchcancel', tend, { passive: false });
      elm.addEventListener('mousedown', e => { const p = { id: 'm', x: e.clientX, y: e.clientY }; if (on.m || h.down(p) === false) return; on.m = 1; e.preventDefault();
        const mv = ev => h.move({ id: 'm', x: ev.clientX, y: ev.clientY }), up = ev => { delete on.m; window.removeEventListener('mousemove', mv); window.removeEventListener('mouseup', up); h.up({ id: 'm', x: ev.clientX, y: ev.clientY }); };
        window.addEventListener('mousemove', mv); window.addEventListener('mouseup', up); });
    }
  }
  function login(message) {
    $('#grid').hidden = true; $('#login').hidden = false;
    if (message) $('#loginText').textContent = message;
    const input = $('#pin'); input.value = ''; input.focus();
    const go = () => { if (!input.value) return; pin = input.value.trim(); failures = 0; $('#login').hidden = true; connect(); };
    input.onkeydown = e => { if (e.key === 'Enter') go(); };
    $('#go').onclick = go;
  }
  function connect() {
    if (needPin && !pin) return login();
    state('connecting…'); opened = false;
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws' + (needPin ? '?pin=' + encodeURIComponent(pin) : ''));
    ws.onopen = () => { opened = true; failures = 0; sessionStorage.setItem('nyxmap-pin', pin); state('connected', 'ok'); };
    ws.onmessage = e => {
      let m; try { m = JSON.parse(e.data); } catch (_) { return; }
      if (m.t === 'controls') {
        controls = m.list || []; if (m.filters) filterCat = m.filters; sizes = m.sizes || {}; inspectorMode = m.inspector === 'onDemand' ? 'onDemand' : 'always'; nameParts = Math.max(1, Math.min(4, m.nameParts | 0 || 2)); if (!editing) document.body.classList.remove('insp'); byId = {}; $('#title').textContent = m.title || 'Controls'; $('#toShow').hidden = !m.show;
        controls.forEach(c => { byId[c.id] = c; values[c.id] = c.kind === 'color' ? c.rgb : c.kind === 'pad' ? [c.x, c.y] : c.kind === 'adsr' ? c.adsr : c.kind === 'multi' ? c.values : c.kind === 'points' ? [].concat.apply([], c.points) : c.value; });
        sharedPage = !!m.sharedPage; if (sharedPage && m.page) showPageId(m.page, false);
        if (busy && Date.now() - busyAt < 4000) again = true; else { busy = 0; render(); } // a gesture that never ended must not freeze the page
      } else if (m.t === 'values') { Object.assign(values, m.v || {}); update();
      } else if (m.t === 'page') { showPageId(m.id, true); }
    };
    ws.onclose = () => {
      state('offline', 'bad');
      if (needPin && !opened && ++failures >= 2) { sessionStorage.removeItem('nyxmap-pin'); pin = ''; login('Wrong PIN, or NyxMap cannot be reached. Enter the PIN again.'); return; }
      setTimeout(connect, Math.min(4000, 400 + failures * 500));
    };
  }
)JS";

// B: the widgets, in SVG and CSS. Each takes its control and `io` (begin, input(v), end) and gives { el, set(v) }
inline constexpr char kScriptB[] = R"JS(
  const wDefault = { slider: 'slim', toggle: 'switch', choice: 'buttons', button: 'hold', color: 'picker', pad: 'pad', adsr: 'adsr', multi: 'multi', points: 'pad' };
  const wChoices = { slider: ['fader', 'vfader', 'knob', 'number', 'slim', 'vslim', 'meter', 'hmeter', 'readout', 'graph'], toggle: ['switch', 'led'], choice: ['buttons', 'menu', 'readout'], button: ['hold', 'tap'], color: ['picker'], pad: ['pad'], adsr: ['adsr'], multi: ['multi', 'hmulti'], points: ['pad'] };
  const wName = { fader: 'Fader', vfader: 'Vertical fader', slim: 'Slim fader', vslim: 'Slim vertical fader', knob: 'Encoder', number: 'Number', switch: 'Switch', buttons: 'Buttons', menu: 'Menu', hold: 'Hold button', tap: 'Tap button', picker: 'Color', pad: 'XY pad', adsr: 'ADSR', multi: 'Multi-slider', hmulti: 'Horizontal multi-slider', meter: 'Meter', hmeter: 'Horizontal meter', readout: 'Readout', graph: 'Graph', led: 'LED' };
  const isView = c => ['meter', 'hmeter', 'readout', 'graph', 'led'].indexOf(widgetOf(c)) >= 0; // a view: it shows the value, it does not set it
  // A control with a physics shows where the physics has put it, not where the hand is: the hand sets where it should go
  const ballPhys = (c, b) => (b && b.own) ? b.physics : c.physics; // a ball's own physics, or the control's
  const physOf = c => c.kind === 'points' && c.balls && c.balls.length ? c.balls.some(b => { const p = ballPhys(c, b); return p && p.mode > 0; }) : !!(c.physics && c.physics.mode > 0);
  const widgetOf = c => (wChoices[c.kind] || []).indexOf(c.widget) >= 0 ? c.widget : wDefault[c.kind];
  // The big one, as TouchOSC and Lemur have it: the whole tile is the fader, a finger anywhere puts the level there, and the level is
  // the filled part, ended by a bright edge. Nothing small to catch
  function slab(c, io, vertical) {
    const root = el('div', 'slab' + (vertical ? ' v' : '')), fill = el('div', 'sfill'), edge = el('div', 'sedge');
    root.appendChild(fill); root.appendChild(edge);
    let pos = 0, down = false;
    const ph = physOf(c);
    const draw = () => { const p = (pos * 100) + '%'; if (vertical) { fill.style.height = p; edge.style.bottom = 'calc(' + p + ' - 2px)'; } else { fill.style.width = p; edge.style.left = 'calc(' + p + ' - 2px)'; } };
    const at = p => { const r = root.getBoundingClientRect(); return clamp(vertical ? 1 - (p.y - r.top) / r.height : (p.x - r.left) / r.width, 0, 1); };
    const move = p => { pos = at(p); if (!ph) draw(); io.input(fromPos(c, pos)); };
    press(root, { down: p => { down = true; io.begin(); move(p); }, move, up: p => { move(p); down = false; io.end(); } });
    return { el: root, set: v => { if (!down || ph) { pos = toPos(c, v); draw(); } } };
  }
  function fader(c, io, vertical) {
    const root = el('div', 'fader' + (vertical ? ' v' : '')), trk = el('div', 'trk'), fill = el('div', 'fill'), cap = el('div', 'cap');
    trk.appendChild(fill); trk.appendChild(cap); root.appendChild(trk);
    let pos = 0, down = false;
    const ph = physOf(c);
    const draw = () => { const p = (pos * 100) + '%'; if (vertical) { fill.style.height = p; cap.style.bottom = p; } else { fill.style.width = p; cap.style.left = p; } };
    const at = p => { const r = trk.getBoundingClientRect(); return clamp(vertical ? 1 - (p.y - r.top) / r.height : (p.x - r.left) / r.width, 0, 1); };
    const move = p => { pos = at(p); if (!ph) draw(); io.input(fromPos(c, pos)); };
    press(root, { down: p => { down = true; io.begin(); move(p); }, move, up: p => { move(p); down = false; io.end(); } });
    return { el: root, set: v => { if (!down || ph) { pos = toPos(c, v); draw(); } } };
  }
  function knob(c, io) {
    const root = el('div', 'knob'), s = sv('svg', { viewBox: '0 0 100 100' }, root), R = 38;
    const pt = a => [50 + R * Math.cos(a * Math.PI / 180), 50 + R * Math.sin(a * Math.PI / 180)];
    const arc = (a0, a1) => { const p0 = pt(a0), p1 = pt(a1); return 'M' + p0[0] + ' ' + p0[1] + 'A' + R + ' ' + R + ' 0 ' + (a1 - a0 > 180 ? 1 : 0) + ' 1 ' + p1[0] + ' ' + p1[1]; };
    sv('path', { d: arc(135, 405), 'class': 'kbg' }, s);
    const val = sv('path', { 'class': 'kval' }, s), dot = sv('line', { 'class': 'kdot' }, s), tx = sv('text', { 'class': 'ktx', x: 50, y: 56 }, s);
    let pos = 0, down = false, y0 = 0, p0 = 0;
    const ph = physOf(c);
    const draw = v => { const a = 135 + 270 * pos, e = pt(a), i = [50 + 22 * Math.cos(a * Math.PI / 180), 50 + 22 * Math.sin(a * Math.PI / 180)];
      val.setAttribute('d', pos > 0.004 ? arc(135, a) : ''); dot.setAttribute('x1', i[0]); dot.setAttribute('y1', i[1]); dot.setAttribute('x2', e[0]); dot.setAttribute('y2', e[1]); tx.textContent = fmt(c, v); };
    press(root, { down: p => { down = true; y0 = p.y; p0 = pos; io.begin(); }, move: p => { pos = clamp(p0 + (y0 - p.y) / (p.e && p.e.shiftKey ? 600 : 170), 0, 1); const v = fromPos(c, pos); if (!ph) draw(v); io.input(v); }, up: () => { down = false; io.end(); } });
    return { el: root, set: v => { if (!down || ph) { pos = toPos(c, v); draw(v); } } };
  }
  function number(c, io) {
    const root = el('div', 'num'), m = el('button', '', '−'), p = el('button', '', '+'), out = el('output');
    let v = c.min;
    const step = d => { v = clamp(Number((v + d * c.step).toFixed(6)), Math.min(c.min, c.max), Math.max(c.min, c.max)); out.textContent = fmt(c, v); io.begin(); io.input(v); io.end(); };
    m.onclick = () => step(-1); p.onclick = () => step(1);
    root.appendChild(m); root.appendChild(out); root.appendChild(p);
    return { el: root, set: x => { v = x; out.textContent = fmt(c, x); } };
  }
  function widget(c, io) {
    const w = widgetOf(c);
    if (w === 'meter' || w === 'hmeter') return meter(c, w === 'meter');
    if (w === 'readout') return readout(c);
    if (w === 'graph') return graph(c);
    if (w === 'led') return led(c);
    if (c.kind === 'slider') return w === 'knob' ? knob(c, io) : w === 'number' ? number(c, io) : w === 'slim' || w === 'vslim' ? fader(c, io, w === 'vslim') : slab(c, io, w === 'vfader');
    if (c.kind === 'toggle') { const b = el('button', 'sw', c.name); b.onclick = () => { io.begin(); io.input(values[c.id] >= 0.5 ? 0 : 1); io.end(); }; return { el: b, set: v => b.classList.toggle('on', v >= 0.5) }; }
    if (c.kind === 'button') {
      const b = el('button', w === 'tap' ? 'tapb' : 'holdb', w === 'tap' ? 'Tap' : 'Hold');
      press(b, { down: () => { io.begin(); io.input(1); }, move: () => {}, up: () => { if (w === 'tap') setTimeout(() => { io.input(0); io.end(); }, 90); else { io.input(0); io.end(); } } });
      b.addEventListener('contextmenu', e => e.preventDefault());
      return { el: b, set: v => b.classList.toggle('on', v >= 0.5) };
    }
    if (c.kind === 'choice') {
      if (w === 'menu') { const s = el('select', 'menu'); c.choices.forEach((n, i) => { const o = el('option', '', n); o.value = i; s.appendChild(o); }); s.onchange = () => { io.begin(); io.input(Number(s.value)); io.end(); }; return { el: s, set: v => { s.value = Math.round(v); } }; }
      const row = el('div', 'choice'), bs = c.choices.map((n, i) => { const b = el('button', '', n); b.onclick = () => { io.begin(); io.input(i); io.end(); }; row.appendChild(b); return b; });
      return { el: row, set: v => bs.forEach((b, i) => b.classList.toggle('on', i === Math.round(v))) };
    }
    if (c.kind === 'color') {
      const p = el('input'); p.type = 'color';
      p.addEventListener('input', () => { io.begin(); throttled(c.id, () => send({ t: 'color', id: c.id, rgb: unhex(p.value) })); });
      p.addEventListener('change', () => { send({ t: 'color', id: c.id, rgb: unhex(p.value) }); io.end(); });
      return { el: p, set: v => { if (!active.has(c.id)) p.value = hex(v); } };
    }
    if (c.kind === 'pad') {
      const pad = el('div', 'pad'), dot = el('i'); pad.appendChild(dot);
      const span = c.max - c.min, draw = v => { dot.style.left = ((v[0] - c.min) / span * 100) + '%'; dot.style.top = ((1 - (v[1] - c.min) / span) * 100) + '%'; };
      const at = p => { const r = pad.getBoundingClientRect(); return [c.min + clamp((p.x - r.left) / r.width, 0, 1) * span, c.min + clamp(1 - (p.y - r.top) / r.height, 0, 1) * span]; };
      const ph = physOf(c), move = p => { const v = at(p); values[c.id] = v; if (!ph) draw(v); throttled(c.id, () => send({ t: 'point', id: c.id, xy: v })); };
      let down = false;
      press(pad, { down: p => { down = true; active.add(c.id); move(p); }, move, up: p => { down = false; active.delete(c.id); send({ t: 'point', id: c.id, xy: at(p) }); send({ t: 'rel', id: c.id }); } });
      return { el: pad, set: v => { if (!down || ph) draw(v); } };
    }
    if (c.kind === 'adsr') return adsr(c, io);
    if (c.kind === 'multi') return multi(c, io, w === 'hmulti');
    if (c.kind === 'points') return pointsPad(c, io);
    return { el: el('div'), set: () => {} };
  }
)JS";

// B2: more widgets (a literal stays under the compilers' limit)
inline constexpr char kScriptB2[] = R"JS(
  // The views: they show a value (a meter with a peak that falls back, a readout, a graph of the last seconds, a lamp) and set nothing
  function meter(c, vertical) {
    const root = el('div', 'meter' + (vertical ? ' v' : '')), fill = el('div', 'mfill'), peak = el('div', 'mpeak'), span = (c.max - c.min) || 1;
    root.appendChild(fill); root.appendChild(peak);
    let pk = 0;
    return { el: root, set: x => {
      const p = clamp((x - c.min) / span, 0, 1); pk = Math.max(p, pk - 0.012);
      fill.style[vertical ? 'height' : 'width'] = (p * 100) + '%'; peak.style[vertical ? 'bottom' : 'left'] = (pk * 100) + '%';
    } };
  }
  function readout(c) {
    const out = el('div', 'readout');
    return { el: out, set: x => { out.textContent = c.kind === 'choice' ? (c.choices || [])[Math.round(x)] || '' : fmt(c, x); } };
  }
  function graph(c) {
    const root = el('div', 'graph'), s = sv('svg', { viewBox: '0 0 100 40', preserveAspectRatio: 'none' }, root), line = sv('polyline', { fill: 'none', 'stroke-width': '1.5', 'vector-effect': 'non-scaling-stroke' }, s);
    line.setAttribute('class', 'gline');
    const span = (c.max - c.min) || 1, hist = []; let at = 0;
    const draw = () => line.setAttribute('points', hist.map((h, i) => (i * 100 / 119).toFixed(1) + ',' + (38 - h * 36).toFixed(1)).join(' '));
    return { el: root, set: x => { hist.push(clamp((x - c.min) / span, 0, 1)); if (hist.length > 120) hist.shift(); draw(); } };
  }
  function led(c) { const root = el('div', 'led'); return { el: root, set: x => root.classList.toggle('on', x >= 0.5) }; }
  // A pad with several points: each finger takes the nearest free point and moves it (as many at once as there are fingers)
  function pointsPad(c, io) {
    const pad = el('div', 'pad'), n = c.count, dots = [], held = {}, balls = c.balls || [];
    const rg = i => balls[i] ? [balls[i].minX, balls[i].maxX, balls[i].minY, balls[i].maxY] : [c.min, c.max, c.min, c.max];
    const physical = i => { const p = ballPhys(c, balls[i]); return !!(p && p.mode > 0); }; // its dot shows where the physics has put it, not the hand
    let v = [].concat.apply([], c.points || []); while (v.length < n * 2) { const r = rg(v.length >> 1); v.push((v.length & 1 ? r[2] + r[3] : r[0] + r[1]) / 2); }
    for (let i = 0; i < n; i++) {
      const d = el('i', 'pt', String(i + 1)); d.style.setProperty('--h', String(Math.round((190 + 62 * i) % 360)));
      if (balls[i] && balls[i].accent) { d.classList.add('col'); d.style.setProperty('--bc', balls[i].accent); }
      pad.appendChild(d); dots.push(d);
    }
    const draw = () => dots.forEach((d, i) => { const r = rg(i); d.style.left = ((v[2 * i] - r[0]) / (r[1] - r[0]) * 100) + '%'; d.style.top = ((1 - (v[2 * i + 1] - r[2]) / (r[3] - r[2])) * 100) + '%'; });
    const at = (p, i) => { const b = pad.getBoundingClientRect(), r = rg(i); return [r[0] + clamp((p.x - b.left) / b.width, 0, 1) * (r[1] - r[0]), r[2] + clamp(1 - (p.y - b.top) / b.height, 0, 1) * (r[3] - r[2])]; };
    const taken = i => Object.keys(held).some(k => held[k] === i);
    const put = p => { const i = held[p.id]; if (i === undefined) return; const q = at(p, i), w = v.slice(); w[2 * i] = Number(q[0].toFixed(4)); w[2 * i + 1] = Number(q[1].toFixed(4));
      if (!physical(i)) v = w; dots[i].classList.add('hot'); draw(); io.input(w, [i]); return w; };
    // The nearest free ball to the finger, in the unit square (the balls have ranges of their own)
    const unit = (i, q) => { const r = rg(i); return [(q[0] - r[0]) / (r[1] - r[0]), (q[1] - r[2]) / (r[3] - r[2])]; };
    presses(pad, { down: p => { let best = -1, bd = 1e18; const b = pad.getBoundingClientRect(), f = [clamp((p.x - b.left) / b.width, 0, 1), clamp(1 - (p.y - b.top) / b.height, 0, 1)];
        for (let i = 0; i < n; i++) { if (taken(i)) continue; const u = unit(i, [v[2 * i], v[2 * i + 1]]), dx = u[0] - f[0], dy = u[1] - f[1], dd = dx * dx + dy * dy; if (dd < bd) { bd = dd; best = i; } }
        if (best < 0) return false; if (!Object.keys(held).length) io.begin(); held[p.id] = best; put(p); },
      move: put, up: p => { const i = held[p.id], w = put(p); if (i !== undefined) { dots[i].classList.remove('hot'); io.letGo(w, i); } delete held[p.id]; if (!Object.keys(held).length) io.end(); } });
    return { el: pad, resize: draw, set: x => { const any = Object.keys(held).length > 0; for (let i = 0; i < x.length && i < n * 2; i++) if (!any || physical(i >> 1)) v[i] = x[i]; draw(); } };
  }
  // A row of sliders in one tile (as Lemur's and NexusUI's multi-slider): a finger across the bars sets each one it crosses
  function multi(c, io, horizontal) {
    const root = el('div', 'multi' + (horizontal ? ' h' : '')), n = c.count, span = (c.max - c.min) || 1, bars = [];
    let v = (c.values || []).slice(), down = false, last = -1, lastF = 0;
    const ph = physOf(c);
    while (v.length < n) v.push(c.min);
    for (let i = 0; i < n; i++) { const b = el('div', 'mb'), f = el('div', 'mf'), e = el('div', 'me'); b.appendChild(f); b.appendChild(e); root.appendChild(b); bars.push({ f, e }); }
    const draw = () => bars.forEach((b, i) => { const p = (clamp((v[i] - c.min) / span, 0, 1) * 100) + '%';
      if (horizontal) { b.f.style.width = p; b.e.style.left = 'calc(' + p + ' - 2px)'; } else { b.f.style.height = p; b.e.style.bottom = 'calc(' + p + ' - 2px)'; } });
    const snap = f => { const st = c.step || 0.01; return clamp(Number((Math.round((c.min + f * span - c.min) / st) * st + c.min).toFixed(6)), Math.min(c.min, c.max), Math.max(c.min, c.max)); };
    const put = p => {
      const r = root.getBoundingClientRect();
      const i = clamp(Math.floor((horizontal ? (p.y - r.top) / r.height : (p.x - r.left) / r.width) * n), 0, n - 1);
      const f = clamp(horizontal ? (p.x - r.left) / r.width : 1 - (p.y - r.top) / r.height, 0, 1);
      const from = last < 0 ? i : last, lo = Math.min(from, i), hi = Math.max(from, i); // a fast swipe crosses bars between two samples
      const w = ph ? v.slice() : v;
      for (let k = lo; k <= hi; k++) w[k] = snap(hi === lo ? f : lastF + (f - lastF) * ((k - from) / (i - from || 1)));
      last = i; lastF = f; if (!ph) draw(); io.input(w.slice());
    };
    press(root, { down: p => { down = true; last = -1; io.begin(); put(p); }, move: put, up: p => { put(p); down = false; io.end(); } });
    return { el: root, resize: draw, set: x => { if (!down || ph) { v = x.slice(); while (v.length < n) v.push(c.min); draw(); } } };
  }
  // An envelope in the hand: attack, decay, sustain and release, each by its own handle (the times along four slots, the sustain up and down)
  function adsr(c, io) {
    const root = el('div', 'adsr'), s = sv('svg', {}, root), area = sv('path', { 'class': 'aarea' }, s), line = sv('path', { 'class': 'aline' }, s);
    const hs = [0, 1, 2, 3].map(() => sv('circle', { 'class': 'ahdl', r: 9 }, s)), lbl = el('div', 'alab');
    root.appendChild(lbl);
    let v = c.adsr.slice(), W = 1, H = 1, grab = -1;
    const slot = 0.28, hold = 0.72, pad = 12;
    const pts = () => { const w = W - 2 * pad, h = H - 2 * pad, m = c.max || 2, X = f => pad + w * f, Y = l => pad + h * (1 - l);
      return [[X(slot * Math.min(1, v[0] / m)), Y(1)], [X(slot + slot * Math.min(1, v[1] / m)), Y(v[2])], [X(hold), Y(v[2])], [X(hold + slot * Math.min(1, v[3] / m)), Y(0)]]; };
    const draw = () => {
      W = root.clientWidth || 1; H = (root.clientHeight || 1) - 18; s.setAttribute('viewBox', '0 0 ' + W + ' ' + Math.max(H, 1)); s.setAttribute('width', W); s.setAttribute('height', Math.max(H, 1));
      const p = pts(), base = H - pad, d = 'M' + pad + ' ' + base + p.map(q => 'L' + q[0] + ' ' + q[1]).join('');
      line.setAttribute('d', d); area.setAttribute('d', d + 'L' + p[3][0] + ' ' + base + 'Z');
      p.forEach((q, i) => { hs[i].setAttribute('cx', q[0]); hs[i].setAttribute('cy', q[1]); });
      lbl.textContent = 'A ' + v[0].toFixed(2) + ' s   D ' + v[1].toFixed(2) + ' s   S ' + v[2].toFixed(2) + '   R ' + v[3].toFixed(2) + ' s';
    };
    const apply = p => { const r = s.getBoundingClientRect(), x = p.x - r.left - pad, y = p.y - r.top - pad, w = W - 2 * pad, h = H - 2 * pad, m = c.max || 2, f = a => clamp(a, 0, 1) * m;
      if (grab === 0) v[0] = f(x / (w * slot)); else if (grab === 1) v[1] = f((x - w * slot) / (w * slot)); else if (grab === 2) v[2] = clamp(1 - y / h, 0, 1); else v[3] = f((x - w * hold) / (w * slot));
      v = v.map((a, i) => Number(a.toFixed(3))); draw(); io.input(v); };
    press(root, { down: p => { const r = s.getBoundingClientRect(), q = pts(); let best = 1e9; grab = -1;
        q.forEach((h, i) => { const dd = Math.hypot(p.x - r.left - h[0], p.y - r.top - h[1]); if (dd < best) { best = dd; grab = i; } });
        if (best > 40) { grab = -1; return false; } io.begin(); apply(p); }, move: p => { if (grab >= 0) apply(p); }, up: () => { grab = -1; io.end(); } });
    return { el: root, resize: draw, set: x => { if (grab < 0) { v = x.slice(); draw(); } } };
  }
)JS";

// C: pages, the grid, the tiles, Edit mode (drag, resize)
inline constexpr char kScriptC[] = R"JS(
  const sizeOf = c => { const w = widgetOf(c), d = sizes[w]; if (d && c.kind !== 'group') return [d[0], d[1]]; return c.kind === 'group' ? [12, 1] : c.kind === 'pad' || c.kind === 'points' ? [5, 5] : c.kind === 'adsr' ? [6, 3] : c.kind === 'multi' ? (widgetOf(c) === 'hmulti' ? [6, 5] : [8, 4]) : c.kind === 'choice' ? [6, 2] : c.kind === 'slider' ? (w === 'meter' ? [1, 5] : w === 'hmeter' ? [4, 1] : w === 'graph' ? [4, 2] : w === 'readout' ? [3, 2] : w === 'knob' ? [2, 2] : w === 'vfader' ? [2, 5] : w === 'vslim' ? [1, 5] : w === 'number' ? [3, 2] : w === 'slim' ? [2, 1] : w === 'fader' ? [4, 2] : [2, 5]) : [3, 2]; };
  const minOf = c => isView(c) ? [1, 1] : c.kind === 'pad' ? [2, 2] : c.kind === 'slider' && widgetOf(c) === 'knob' ? [2, 2] : c.kind === 'slider' && (widgetOf(c) === 'vfader' || widgetOf(c) === 'vslim') ? [1, 3] : c.kind === 'group' ? [2, 1] : [2, 1];
  const hit = (a, b) => a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
  const topOf = c => { let p = c.group; while (p && byId[p] && byId[p].group) p = byId[p].group; return p || 0; };
  function pages() {
    const tops = controls.filter(c => c.kind === 'group' && !c.group), out = [];
    if (controls.some(c => !c.group && c.kind !== 'group')) out.push({ id: 0, name: tops.length ? 'Main' : 'Controls' });
    tops.forEach(g => out.push({ id: g.id, name: g.name }));
    return out;
  }
  const pageItems = id => controls.filter(c => c.id !== id && (id === 0 ? !c.group && c.kind !== 'group' : topOf(c) === id));
  function layoutOf(list) { // [{c, x, y, w, h}]: where each is saved, the rest flowed into the first free place
    const placed = [], rest = [];
    list.forEach(c => (c.layout ? placed.push({ c, x: c.layout.x, y: c.layout.y, w: c.layout.w, h: c.layout.h }) : rest.push(c)));
    rest.forEach(c => { const s = sizeOf(c); let found = false; for (let y = 0; !found; y++) for (let x = 0; x <= 12 - s[0] && !found; x++) if (!placed.some(p => hit(p, { x, y, w: s[0], h: s[1] }))) { placed.push({ c, x, y, w: s[0], h: s[1] }); found = true; } });
    return placed;
  }
  function resolve(items, fixed) { // the one moved (or those moved) stay; those they land on are pushed down
    const fx = Array.isArray(fixed) ? fixed : [fixed], rest = items.filter(i => fx.indexOf(i) < 0).sort((a, b) => a.y - b.y || a.x - b.x), placed = fx.slice();
    rest.forEach(it => { let y = it.y, at; while ((at = placed.find(p => hit(p, { x: it.x, y, w: it.w, h: it.h })))) y = at.y + at.h; it.y = y; placed.push(it); });
  }
  let items = [], cell = 60, rowH = 60, compact = false;
  function geometry() { const w = $('#grid').clientWidth; compact = w < 560; cell = w / 12; rowH = clamp(cell, 46, 84); }
  function place(it, t) {
    if (compact) { t.el.style.height = (it.h * 54) + 'px'; t.el.style.width = ''; t.el.style.left = t.el.style.top = ''; if (t.api && t.api.resize) t.api.resize(); return; }
    t.el.style.left = (it.x * cell) + 'px'; t.el.style.top = (it.y * rowH) + 'px'; t.el.style.width = (it.w * cell) + 'px'; t.el.style.height = (it.h * rowH) + 'px';
    if (t.api && t.api.resize) t.api.resize();
  }
  const leaf = n => n.split(' / ').map(s => { const w = s.split('/').filter(x => x); return w.length ? w.slice(-nameParts).join('/') : s; }).join(' / '); // the node's name (the last words of its path), not the whole path (an XY keeps both of its names)
  // In edit mode, a thin wire from each physics widget to the control it sets
  function wires() {
    const old = $('#wires'); if (old) old.remove();
    if (!editing || compact) return;
    const svg = sv('svg', { id: 'wires' }, $('#grid')), box = it => ({ x: it.x * cell, y: it.y * rowH, w: it.w * cell, h: it.h * rowH });
    items.forEach(a => {
      const po = a.c.physicsOf, b = po && items.find(i => String(i.c.id) === String(po.id)); if (!b) return;
      const r = box(a), q = box(b), p0 = [r.x + r.w / 2, r.y + r.h / 2], p1 = [q.x + q.w / 2, q.y + q.h / 2], dx = p1[0] - p0[0], dy = p1[1] - p0[1];
      const edge = (rc, c, sx, sy) => { const k = Math.min(rc.w / 2 / (Math.abs(sx) || 1e-9), rc.h / 2 / (Math.abs(sy) || 1e-9)); return [c[0] + sx * k, c[1] + sy * k]; };
      const e0 = edge(r, p0, dx, dy), e1 = edge(q, p1, -dx, -dy);
      sv('line', { x1: e0[0], y1: e0[1], x2: e1[0], y2: e1[1], class: 'wire' }, svg);
      sv('circle', { cx: e0[0], cy: e0[1], r: 3, class: 'wdot' }, svg); sv('circle', { cx: e1[0], cy: e1[1], r: 3, class: 'wdot' }, svg);
    });
  }
  function placeAll() {
    geometry(); $('#grid').classList.toggle('compact', compact);
    const order = compact ? items.slice().sort((a, b) => a.y - b.y || a.x - b.x) : items;
    order.forEach(it => { if (compact) $('#grid').appendChild(tiles[it.c.id].el); place(it, tiles[it.c.id]); });
    if (!compact) $('#grid').style.height = (Math.max.apply(null, items.map(i => i.y + i.h).concat([1])) * rowH + (editing ? rowH * 2 : 0)) + 'px'; else $('#grid').style.height = '';
    $('#edit').hidden = false; $('#edit').style.display = compact ? 'none' : '';
    wires();
  }
  function tile(it) {
    const c = it.c, t = { it, el: el('div', 'tile' + (c.kind === 'group' ? ' sec' : '')), api: null };
    const face = el('div', 'face'), hd = el('div', 'hd'), name = el('span', '', leaf(c.name)), val = el('b');
    if (c.signal) face.classList.add('sig');
    if (c.dest) name.title = c.dest; // only the node's name shows; the whole address is on hover
    face.style.setProperty('--a', c.accent || '');
    if (!c.accent) face.style.removeProperty('--a');
    hd.appendChild(name); if (c.signal) { const sg = el('span', 'sigtag', 'SIGNAL'); sg.title = 'A signal the device emits: shown here, it cannot be set'; hd.appendChild(sg); } if (c.kind === 'slider') hd.appendChild(val);
    if (c.reset) { const rb = el('button', 'rst', '⟲'); rb.title = 'Reset to its default'; rb.onclick = e => { e.stopPropagation(); send({ t: 'reset', id: c.id }); }; hd.appendChild(rb); } // the default of what it shows, put back
    face.appendChild(hd);
    if (c.kind !== 'group' && c.kind !== 'toggle') {
      const bd = el('div', 'bd');
      const kindMsg = (v, h) => c.kind === 'adsr' ? { t: 'adsr', id: c.id, v } : c.kind === 'multi' ? { t: 'multi', id: c.id, v } : c.kind === 'points' ? { t: 'points', id: c.id, v, h } : { t: 'set', id: c.id, v };
      const io = { begin: () => active.add(c.id), input: (v, h) => { values[c.id] = v; if (val) val.textContent = fmt(c, v); throttled(c.id, () => send(kindMsg(v, h))); },
        letGo: (v, i) => { send(kindMsg(v, [i])); send({ t: 'rel', id: c.id, i }); }, // one ball let go: the others go on
        end: () => { active.delete(c.id); if (c.kind !== 'points') { send(kindMsg(values[c.id])); send({ t: 'rel', id: c.id }); } } };
      t.api = widget(c, io); t.val = val; bd.appendChild(t.api.el); face.appendChild(bd);
    } else if (c.kind === 'toggle') {
      const bd = el('div', 'bd'), io = { begin() {}, input: v => send({ t: 'set', id: c.id, v }), end() {} };
      t.api = widget(c, io); bd.appendChild(t.api.el); face.appendChild(bd);
    }
    t.el.appendChild(face); t.face = face;
    if (editing && !compact) {
      const shield = el('div', 'shield'), rz = el('div', 'rz'), gear = el('button', 'gear bin', '🗑');
      t.el.appendChild(shield); t.el.appendChild(rz); t.el.appendChild(gear);
      gear.title = 'Delete'; gear.addEventListener('click', e => { e.stopPropagation(); if (sel.size > 1 && sel.has(String(c.id))) removeSelected(); else { send({ t: 'remove', id: c.id }); sel.delete(String(c.id)); if (sheetId === c.id) sheetId = 0; } });
      gear.addEventListener('touchend', e => { e.stopPropagation(); }, { passive: true });
      const gesture = (grab, apply) => { let x0 = 0, y0 = 0, o, base, movers; return {
        down: p => {
          const mod = p.e && (p.e.shiftKey || p.e.metaKey || p.e.ctrlKey), key = String(c.id);
          if (mod && !grab) { if (sel.has(key)) sel.delete(key); else sel.add(key); selectionChanged(); return false; } // shift or Cmd/Ctrl: in or out of the selection
          if (!sel.has(key)) { sel = new Set([key]); }
          x0 = p.x; y0 = p.y; o = { x: it.x, y: it.y, w: it.w, h: it.h }; base = items.map(i => ({ i, x: i.x, y: i.y }));
          movers = !grab && sel.size > 1 ? items.filter(i => sel.has(String(i.c.id))) : [it];
          busy++; busyAt = Date.now(); t.el.classList.add('moving'); selectionChanged();
        },
        move: p => {
          base.forEach(b => { if (movers.indexOf(b.i) < 0) { b.i.x = b.x; b.i.y = b.y; } });
          const dx = Math.round((p.x - x0) / cell), dy = Math.round((p.y - y0) / rowH);
          apply(o, dx, dy);
          if (movers.length > 1) movers.forEach(m => { if (m === it) return; const b = base.find(q => q.i === m); m.x = clamp(b.x + dx, 0, 12 - m.w); m.y = Math.max(0, b.y + dy); });
          resolve(items, movers); items.forEach(i => place(i, tiles[i.c.id])); $('#grid').style.height = (Math.max.apply(null, items.map(i => i.y + i.h)) * rowH + rowH * 2) + 'px';
        },
        up: () => { t.el.classList.remove('moving'); busy--; saveLayout(); } }; };
      press(shield, gesture(0, (o, dx, dy) => { it.x = clamp(o.x + dx, 0, 12 - it.w); it.y = Math.max(0, o.y + dy); }));
      const mn = minOf(c);
      press(rz, gesture(1, (o, dx, dy) => { it.w = clamp(o.w + dx, mn[0], 12 - o.x); it.h = clamp(o.h + dy, mn[1], 40); }));
    }
    tiles[c.id] = t;
    return t;
  }
  function selectTile(id) { sel = id ? new Set([String(id)]) : new Set(); markSel(); }
  function markSel() { Object.keys(tiles).forEach(k => tiles[k].el.classList.toggle('sel', sel.has(k))); }
  function saveLayout() {
    const o = {}; items.forEach(i => { o[i.c.id] = { x: i.x, y: i.y, w: i.w, h: i.h }; i.c.layout = { x: i.x, y: i.y, w: i.w, h: i.h }; });
    send({ t: 'layout', items: o });
    if (again) { again = false; render(); }
  }
  function render() {
    const list = pages(), grid = $('#grid');
    $('#login').hidden = true; grid.hidden = false;
    if (!list.some(p => p.id === pageId)) pageId = list.length ? list[0].id : -1;
    const tabs = $('#tabs'); tabs.textContent = ''; tabs.hidden = list.length < 2;
    list.forEach(p => {
      const b = el('button', p.id === pageId ? 'on' : '', p.name); b.onclick = () => { pageId = p.id; if (sharedPage) send({ t: 'showPage', id: String(p.id) }); render(); }; tabs.appendChild(b);
      if (editing) { // a bin beside the name: the page and what is on it go at once (the undo brings them back)
        const x = el('span', 'tbin', '🗑'); x.title = 'Delete this page';
        x.onclick = e => { e.stopPropagation(); const ids = pageItems(p.id).map(c => c.id); if (p.id) ids.push(p.id); if (ids.length) { send({ t: 'remove', ids }); sel = new Set(); sheetId = 0; } };
        const cl = el('span', 'tbin', '🧹'); cl.title = 'Clear this page (keep the page)';
        cl.onclick = e => { e.stopPropagation(); const ids = pageItems(p.id).map(c => c.id); if (ids.length) { send({ t: 'remove', ids }); sel = new Set(); sheetId = 0; } };
        b.appendChild(cl); b.appendChild(x);
      }
    });
    grid.textContent = ''; Object.keys(tiles).forEach(k => delete tiles[k]);
    grid.classList.toggle('edit', editing);
    document.body.classList.toggle('insp', editing && inspectorMode === 'always');
    if (pageId < 0) { grid.appendChild(el('div', '', 'No control yet. Add a parameter from the application: right-click it › Add to Controls Page, or drag its address here.')).id = 'empty'; $('#edit').hidden = true; return; }
    items = layoutOf(pageItems(pageId));
    items.forEach(it => grid.appendChild(tile(it).el));
    placeAll(); update(); if (editing) { sel.forEach(k => { if (!byId[k]) sel.delete(k); }); if (sheetId && !byId[sheetId]) sheetId = 0; markSel(); if (sel.size > 1) multiInspector(); else if (sheetId) selectTile(sheetId); else inspector(); }
  }
  function update() {
    for (const id in tiles) {
      const t = tiles[id], c = t.it.c;
      if (!t.api || (active.has(c.id) && !physOf(c))) continue;
      const v = values[c.id]; t.api.set(v);
      if (t.val) t.val.textContent = fmt(c, v);
    }
  }
  window.addEventListener('resize', () => { if (items.length) placeAll(); });
  window.addEventListener('orientationchange', () => setTimeout(() => { if (items.length) placeAll(); }, 250));
  document.addEventListener('keydown', e => { // Cmd/Ctrl+Z undoes the last change of the page, with Shift (or Ctrl+Y) redoes it
    const k = (e.key || '').toLowerCase(), t = e.target && e.target.tagName;
    if (!(e.metaKey || e.ctrlKey) || e.altKey || t === 'INPUT' || t === 'TEXTAREA' || t === 'SELECT') return;
    if (k === 'z' || k === 'y') { e.preventDefault(); send({ t: (k === 'y' || e.shiftKey) ? 'redo' : 'undo' }); }
  });
  document.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && !e.shiftKey && !e.altKey && (e.key === 'k' || e.key === 'K') && window.parent !== window) { e.preventDefault(); try { parent.document.dispatchEvent(new parent.KeyboardEvent('keydown', { key: 'k', metaKey: e.metaKey, ctrlKey: e.ctrlKey, bubbles: true })); } catch (_) {} } }); // Cmd/Ctrl+K belongs to the page that frames this one
  document.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && !e.shiftKey && !e.altKey && (e.key === 'e' || e.key === 'E') && $('#edit').style.display !== 'none' && !$('#edit').hidden) { e.preventDefault(); $('#edit').click(); } }); // Cmd/Ctrl+E
  // Something dragged onto the page from outside (an address from a tree beside it): the application is told where
  window.pxsPageId = () => pageId; // the page shown now (an application's front page puts what it adds there)
  const dropType = 'application/x-pxs-address';
  const droppable = e => e.dataTransfer && Array.prototype.indexOf.call(e.dataTransfer.types || [], dropType) >= 0;
  document.addEventListener('dragover', e => { if (droppable(e)) { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; } });
  document.addEventListener('drop', e => {
    if (!droppable(e)) return;
    e.preventDefault();
    const r = $('#grid').getBoundingClientRect(), tl = e.target && e.target.closest ? e.target.closest('.tile') : null; // a macro's tile: the address is added to it
    const onto = tl ? Object.keys(tiles).find(k => tiles[k].el === tl) : null, macro = onto && byId[onto] && byId[onto].targets && byId[onto].targets.length;
    send({ t: 'drop', data: e.dataTransfer.getData(dropType), onto: macro ? onto : undefined, page: String(pageId), x: compact ? 0 : clamp(Math.floor((e.clientX - r.left) / cell), 0, 11), y: compact ? 9999 : Math.max(0, Math.floor((e.clientY - r.top) / rowH)) });
  });
  $('#edit').onclick = () => { editing = !editing; $('#edit').classList.toggle('on', editing); document.body.classList.toggle('insp', editing && inspectorMode === 'always'); if (!editing) { closeSheet(); sel = new Set(); } render(); };
)JS";

// D: the settings of a tile, then the start
inline constexpr char kScriptD[] = R"JS(
  const accents = ['', '#29c7e0', '#46c46a', '#e5c14a', '#f0883e', '#e5564a', '#d36bd9', '#7b8cff'];
  let sheetTimer = 0, sheetProps = {}, sheetFor = 0;
  const flush = () => { clearTimeout(sheetTimer); if (sheetFor && Object.keys(sheetProps).length) send({ t: 'edit', id: sheetFor, props: sheetProps }); sheetProps = {}; };
  const push = props => { sheetFor = sheetId; sheetProps = Object.assign(sheetProps, props); clearTimeout(sheetTimer); sheetTimer = setTimeout(flush, 220); };
  function closeSheet() { flush(); sheetId = 0; $('#sheet').classList.remove('open', 'pinned'); selectTile(0); }
  // In edit mode the inspector is always there (when the preferences say so): it shows the tile that is selected, or asks for one
  function inspector() {
    if (inspectorMode !== 'always') return;
    const sh = $('#sheet'); sh.textContent = ''; sh.appendChild(el('h2', '', 'Inspector')); sh.appendChild(el('div', 'hint', 'Select a tile to edit it.')); sh.classList.add('open', 'pinned');
  }
  // The physics of a number, as Lemur has it: Interpolate (it glides to the hand) or Mass-Spring (a spring to the hand; once let go it goes on by
  // itself with the push it got: friction, the bounce on the walls)
  function physFields(host, ph, save, widget, title) {
    const p = Object.assign({}, ph), box = el('div');
    host.appendChild(el('label', '', title || 'Physics'));
    const mode = el('select'); [['None', 0], ['Interpolate (glides to the hand)', 1], ['Mass-Spring (thrown by the finger)', 2]].forEach(m => { const o = el('option', '', m[0]); o.value = m[1]; mode.appendChild(o); });
    mode.value = p.mode || 0; host.appendChild(mode); host.appendChild(box);
    const knob = (label, key, lo, hi, root) => { box.appendChild(el('label', '', label)); const r = el('input'), h = el('div', 'hint'); r.type = 'range'; r.min = 0; r.max = 1000;
      const to = t => root ? lo + t * t * (hi - lo) : lo + t * (hi - lo), from = v => { const t = Math.max(0, Math.min(1, (v - lo) / (hi - lo))); return root ? Math.sqrt(t) : t; };
      r.value = Math.round(from(p[key]) * 1000); const say = () => { h.textContent = Number(p[key]).toFixed(hi > 50 ? 0 : 2); };
      r.oninput = () => { p[key] = Number(to(Number(r.value) / 1000).toFixed(3)); say(); save(Object.assign({}, p)); }; say(); box.appendChild(r); box.appendChild(h); };
    const build = () => { box.textContent = '';
      if (p.mode === 1) knob('Speed', 'rate', 0.5, 60, true);
      if (p.mode === 2) { knob('Attraction to the finger', 'attraction', 0, 400, true); knob('Tension (pulls back to where the finger was; 0: thrown)', 'tension', 0, 400, true); knob('Friction', 'friction', 0, 30, true); knob('Wall bounce', 'wall', 0, 1, false); } };
    mode.onchange = () => { p.mode = Number(mode.value); build(); save(Object.assign({}, p)); }; build();
    if (widget) { // a slider on each setting, to play it from the page
      host.appendChild(el('label', '', 'Widgets for these settings'));
      const row = el('div', 'row'); [['attraction', 'Attraction'], ['tension', 'Tension'], ['friction', 'Friction'], ['wall', 'Wall'], ['rate', 'Speed']].forEach(k => { const b = el('button', '', '+ ' + k[1]); b.title = 'Add a slider for the ' + k[1].toLowerCase(); b.onclick = () => widget(k[0]); row.appendChild(b); }); host.appendChild(row);
    }
  }
  // A pad of balls: a menu of the balls with + and -, and the range, addresses, color and physics of the chosen one
  function ballEditor(sh, c, field, text, numeric) {
    const balls = c.balls.map(b => JSON.parse(JSON.stringify(b))); let sel = 0;
    const save = () => push({ balls: balls.map(b => Object.assign({}, b)) });
    const bar = el('div', 'row'), menu = el('select'), plus = el('button', '', '+'), minus = el('button', '', '−'), box = el('div');
    const fill = () => { menu.textContent = ''; balls.forEach((b, i) => { const o = el('option', '', 'Ball ' + (i + 1) + (b.name ? ' · ' + b.name : '')); o.value = i; menu.appendChild(o); }); menu.value = sel; };
    plus.title = 'Add a ball'; minus.title = 'Remove this ball';
    plus.onclick = () => { if (balls.length >= 16) return; balls.push(Object.assign({}, JSON.parse(JSON.stringify(balls[sel])), { x: '', y: '', name: '' })); sel = balls.length - 1; save(); fill(); show(); };
    minus.onclick = () => { if (balls.length < 2) return; balls.splice(sel, 1); sel = Math.min(sel, balls.length - 1); save(); fill(); show(); };
    menu.onchange = () => { sel = Number(menu.value); show(); };
    physFields(sh, c.physics || {}, ph => push({ physics: ph }), key => { flush(); send({ t: 'physicsWidget', id: c.id, key }); }, 'Physics of all the balls');
    bar.appendChild(menu); bar.appendChild(plus); bar.appendChild(minus); sh.appendChild(el('label', '', 'Balls')); sh.appendChild(bar); sh.appendChild(box);
    const show = () => { const b = balls[sel]; box.textContent = '';
      const add = (l, n) => { box.appendChild(el('label', '', l)); box.appendChild(n); };
      add('Ball name', text(b.name || '', v => { b.name = v; save(); }));
      add('OSC address X', text(b.x || '', v => { b.x = v; save(); })); add('OSC address Y', text(b.y || '', v => { b.y = v; save(); }));
      const r1 = el('div', 'row'), r2 = el('div', 'row'), cell = (l, n) => { const d = el('div'); d.appendChild(el('label', '', l)); d.appendChild(n); return d; };
      r1.appendChild(cell('X min', numeric(b.minX, v => { b.minX = v; save(); }))); r1.appendChild(cell('X max', numeric(b.maxX, v => { b.maxX = v; save(); })));
      r2.appendChild(cell('Y min', numeric(b.minY, v => { b.minY = v; save(); }))); r2.appendChild(cell('Y max', numeric(b.maxY, v => { b.maxY = v; save(); }))); box.appendChild(r1); box.appendChild(r2);
      const sw = el('div', 'sw8'); accents.forEach(a => { const k = el('button', (b.accent || '') === a ? 'on' : '', ''); k.style.background = a || '#34343b'; k.onclick = () => { b.accent = a; save(); sw.querySelectorAll('button').forEach(o => o.classList.remove('on')); k.classList.add('on'); }; sw.appendChild(k); });
      add('Color', sw);
      const own = el('label', 'chk'), cb = el('input'); cb.type = 'checkbox'; cb.checked = !!b.own; own.appendChild(cb); own.appendChild(document.createTextNode(' Its own physics (instead of the pad\'s)')); box.appendChild(own);
      const mine = el('div'); box.appendChild(mine);
      const showOwn = () => { mine.textContent = ''; if (b.own) physFields(mine, b.physics || {}, ph => { b.physics = ph; save(); }, key => { flush(); send({ t: 'physicsWidget', id: c.id, key: (sel + 1) + '.' + key }); }, 'Physics of this ball'); };
      cb.onchange = () => { b.own = cb.checked; if (b.own && !b.physics) b.physics = Object.assign({}, c.physics); save(); showOwn(); }; showOwn();
    };
    fill(); show();
  }
  function openSheet(id) {
    const c = byId[id]; if (!c) return;
    sheetId = id; selectTile(id);
    const sh = $('#sheet'); sh.textContent = '';
    const h2 = el('h2', '', c.kind === 'group' ? 'Section' : 'Tile'), x = el('button', 'hb', 'Done'); x.onclick = closeSheet; if (inspectorMode !== 'always') h2.appendChild(x); sh.appendChild(h2);
    const field = (label, node) => { sh.appendChild(el('label', '', label)); sh.appendChild(node); return node; };
    const text = (v, on) => { const i = el('input'); i.type = 'text'; i.value = v; i.oninput = () => on(i.value); return i; };
    const numeric = (v, on, step) => { const i = el('input'); i.type = 'number'; i.value = v; i.step = step || 'any'; i.inputMode = 'decimal'; i.onchange = () => { if (i.value !== '' && isFinite(Number(i.value))) on(Number(i.value)); }; return i; };
    field('Name', text(c.name, v => push({ name: v })));
    if (c.kind !== 'group') {
      const ws = wChoices[c.kind] || [];
      if (ws.length > 1) {
        const s = el('select'); ws.forEach(w => { const o = el('option', '', wName[w]); o.value = w; s.appendChild(o); }); s.value = widgetOf(c);
        s.onchange = () => send({ t: 'edit', id: c.id, props: { widget: s.value } }); // the tile keeps the place and the size it has been given
        field('Widget', s);
      }
      const sw = el('div', 'sw8'); accents.forEach(a => { const b = el('button', (c.accent || '') === a ? 'on' : '', ''); b.style.background = a || '#34343b'; b.title = a || 'default'; b.onclick = () => { push({ accent: a }); sw.querySelectorAll('button').forEach(o => o.classList.remove('on')); b.classList.add('on'); }; sw.appendChild(b); });
      field('Accent', sw);
    }
    if (c.kind === 'adsr') field('Longest time (s)', numeric(c.max, v => push({ max: v })));
    if (c.kind === 'multi') field('Sliders', numeric(c.count, v => push({ count: Math.round(v) }), 1));
    if (c.kind === 'points' && !(c.balls && c.balls.length)) { field('Points', numeric(c.count, v => push({ count: Math.round(v) }), 1)); const r3 = el('div', 'row'), c3 = (l, n) => { const d = el('div'); d.appendChild(el('label', '', l)); d.appendChild(n); return d; }; r3.appendChild(c3('Min', numeric(c.min, v => push({ min: v })))); r3.appendChild(c3('Max', numeric(c.max, v => push({ max: v })))); sh.appendChild(r3); }
    if (c.kind === 'slider' || c.kind === 'multi') {
      const row = el('div', 'row'), cell2 = (l, n) => { const d = el('div'); d.appendChild(el('label', '', l)); d.appendChild(n); return d; };
      row.appendChild(cell2('Min', numeric(c.min, v => push({ min: v })))); row.appendChild(cell2('Max', numeric(c.max, v => push({ max: v })))); sh.appendChild(row);
      const row2 = el('div', 'row'); row2.appendChild(cell2('Step', numeric(c.step, v => push({ step: v })))); row2.appendChild(cell2('Unit', text(c.unit || '', v => push({ unit: v })))); sh.appendChild(row2);
      if (c.kind === 'slider') {
      sh.appendChild(el('label', '', 'Curve'));
      const r = el('input'), hint = el('div', 'hint'); r.type = 'range'; r.min = -100; r.max = 100; r.value = Math.round(Math.log(c.curve || 1) / Math.log(4) * 100);
      const say = k => { hint.textContent = Math.abs(k - 1) < 0.03 ? 'Linear' : k > 1 ? 'Fine at the low end (×' + k.toFixed(2) + ')' : 'Fine at the high end (×' + k.toFixed(2) + ')'; };
      r.oninput = () => { const k = Math.pow(4, Number(r.value) / 100); say(k); push({ curve: Number(k.toFixed(3)) }); };
      say(c.curve || 1); sh.appendChild(r); sh.appendChild(hint);
      }
    }
    if (c.targets && c.targets.length) targetEditor(sh, c, numeric);
    if (c.kind === 'points' && c.balls && c.balls.length) ballEditor(sh, c, field, text, numeric);
    else if (c.physics) physFields(sh, c.physics, ph => push({ physics: ph }), key => { flush(); send({ t: 'physicsWidget', id: c.id, key }); });
    if ((c.kind === 'slider' || c.kind === 'toggle') && !c.physTarget && !c.signal && filterCat) filterEditor(sh, c);
    const del = el('button', 'del', 'Delete this control'); del.onclick = () => { send({ t: 'remove', id: c.id }); sheetId = 0; };
    sh.appendChild(del);
    sh.classList.add('open'); sh.classList.toggle('pinned', inspectorMode === 'always');
  }
)JS";

inline constexpr char kScriptE[] = R"JS(
  // Several tiles: shift or Cmd/Ctrl-click to add or remove one, a rectangle dragged on the empty grid to take those it touches, then move,
  // copy (Cmd/Ctrl+C), cut (X), paste (V) or delete them. The clipboard is the server's, so a tile cut here can be pasted on any page
  function showPageId(id, now) { const p = pages().find(q => String(q.id) === String(id)); if (!p) return; pageId = p.id; if (now) render(); }
  function selectionChanged() {
    if (sel.size === 1) { const id = Array.from(sel)[0]; if (sheetId === id && byId[id]) markSel(); else openSheet(id); return; }
    flush(); sheetId = 0; markSel(); if (sel.size > 1) multiInspector(); else inspector();
  }
  function multiInspector() {
    const sh = $('#sheet'); sh.textContent = ''; sh.appendChild(el('h2', '', sel.size + ' tiles'));
    sh.appendChild(el('div', 'hint', 'Shift or Cmd/Ctrl-click adds or removes a tile; drag on the empty grid to select a few. Drag one to move them all.'));
    const row = el('div', 'row'), mk = (t, f) => { const b = el('button', 'hb', t); b.onclick = f; const d = el('div'); d.appendChild(b); row.appendChild(d); };
    mk('Copy', copySelected); mk('Cut', () => copySelected(true));
    sh.appendChild(row);
    const del = el('button', 'del', 'Delete these ' + sel.size + ' tiles'); del.onclick = removeSelected; sh.appendChild(del);
    sh.classList.add('open'); sh.classList.toggle('pinned', inspectorMode === 'always');
  }
  // A macro: the parameters it sends to, each with its own range (the value where the control is lowest, and highest), curve and, for a pad, axis
  function targetEditor(sh, c, numeric) {
    sh.appendChild(el('label', '', 'Sends to'));
    const rows = c.targets.map((t, i) => ({ i, min: t.min, max: t.max, curve: t.curve, axis: t.axis }));
    const send2 = () => push({ targets: rows });
    const box = el('div');
    const draw = () => {
      box.textContent = '';
      rows.forEach((r, k) => {
        const t = c.targets[r.i], line = el('div'); line.style.cssText = 'border:1px solid var(--line);border-radius:9px;padding:8px;margin:0 0 8px';
        const head = el('div'); head.style.cssText = 'display:flex;justify-content:space-between;align-items:center;margin-bottom:6px';
        const nm = el('span', '', t.label || t.path); nm.title = t.path; nm.style.cssText = 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:13px';
        const del = el('button', 'hb', '✕'); del.title = 'Stop sending to this one'; del.onclick = () => { rows.splice(k, 1); send2(); draw(); };
        head.appendChild(nm); head.appendChild(del); line.appendChild(head);
        const fields = el('div', 'row');
        [['min', 'From'], ['max', 'To'], ['curve', 'Curve']].forEach(f => {
          const d = el('div'); d.appendChild(el('label', '', f[1]));
          const i = numeric(r[f[0]], v => { r[f[0]] = f[0] === 'curve' ? Math.max(0.1, Math.min(10, v)) : v; send2(); }, f[0] === 'curve' ? '0.1' : 'any'); d.appendChild(i); fields.appendChild(d);
        });
        line.appendChild(fields);
        if (c.kind === 'pad') {
          const sel = el('select'); [['0', 'X'], ['1', 'Y']].forEach(o => sel.appendChild(Object.assign(el('option', '', o[1]), { value: o[0] }))); sel.value = String(r.axis);
          sel.onchange = () => { r.axis = Number(sel.value); send2(); }; line.appendChild(el('label', '', 'Axis')); line.appendChild(sel);
        }
        box.appendChild(line);
      });
      if (!rows.length) box.appendChild(el('div', 'hint', 'Nothing left to send to: drag an address onto this tile to add one.'));
    };
    draw(); sh.appendChild(box);
    sh.appendChild(el('div', 'hint', 'Drag an address from the tree onto this tile to send to it too. A curve above 1 is fine at the low end.'));
  }
  const selIds = () => Array.from(sel).filter(k => byId[k]);
  function removeSelected() { const ids = selIds(); if (!ids.length) return; send({ t: 'remove', ids }); sel = new Set(); sheetId = 0; }
  function copySelected(cut) { const ids = selIds(); if (!ids.length) return; send({ t: cut === true ? 'cut' : 'copy', ids }); if (cut === true) { sel = new Set(); sheetId = 0; } }
  const typing = e => { const t = e.target && e.target.tagName; return t === 'INPUT' || t === 'TEXTAREA' || t === 'SELECT' || (e.target && e.target.isContentEditable); };
  document.addEventListener('keydown', e => {
    if (!editing || typing(e)) return;
    const k = (e.key || '').toLowerCase(), mod = e.metaKey || e.ctrlKey;
    if (mod && !e.altKey && !e.shiftKey && k === 'c') { if (sel.size) { e.preventDefault(); copySelected(); } }
    else if (mod && !e.altKey && !e.shiftKey && k === 'x') { if (sel.size) { e.preventDefault(); copySelected(true); } }
    else if (mod && !e.altKey && !e.shiftKey && k === 'v') { e.preventDefault(); send({ t: 'paste', page: String(pageId < 0 ? 0 : pageId) }); }
    else if (mod && !e.altKey && !e.shiftKey && k === 'a') { e.preventDefault(); sel = new Set(items.map(i => String(i.c.id))); selectionChanged(); }
    else if ((k === 'delete' || k === 'backspace') && !mod && sel.size) { e.preventDefault(); removeSelected(); }
    else if (k === 'escape' && sel.size) { sel = new Set(); selectionChanged(); }
  });
  (() => {
    const grid = $('#grid'); let box = null, x0 = 0, y0 = 0, base = null, moved = false;
    grid.addEventListener('mousedown', e => {
      if (!editing || compact || e.button > 0 || e.target !== grid) return;
      const r = grid.getBoundingClientRect(); x0 = e.clientX - r.left; y0 = e.clientY - r.top; moved = false;
      base = e.shiftKey || e.metaKey || e.ctrlKey ? new Set(sel) : new Set();
      box = el('div', 'marq'); grid.appendChild(box); e.preventDefault();
      const mv = ev => {
        moved = true; const x = ev.clientX - r.left, y = ev.clientY - r.top, l = Math.min(x, x0), t = Math.min(y, y0), w = Math.abs(x - x0), h = Math.abs(y - y0);
        Object.assign(box.style, { left: l + 'px', top: t + 'px', width: w + 'px', height: h + 'px' });
        const next = new Set(base);
        items.forEach(i => { const a = i.x * cell, b = i.y * rowH; if (a < l + w && a + i.w * cell > l && b < t + h && b + i.h * rowH > t) next.add(String(i.c.id)); });
        sel = next; markSel();
      };
      const up = () => { window.removeEventListener('mousemove', mv); window.removeEventListener('mouseup', up); box.remove(); box = null; if (!moved && !base.size) sel = new Set(); selectionChanged(); };
      window.addEventListener('mousemove', mv); window.addEventListener('mouseup', up);
    });
  })();
  fetch('/info').then(r => r.json()).then(i => { needPin = !!i.pin; }).catch(() => {}).then(connect);
})();
)JS";

inline QByteArray style() { return QByteArray(kStyleA) + kStyleB; }
// The filters of a tile: what it sends goes through them in their order (the interaction filters of pxs), each with its own settings
inline constexpr char kScriptF[] = R"JS(
  function filterEditor(sh, c) {
    const cat = filterCat, by = {}; cat.filters.forEach(f => { by[f.key] = f; });
    let stages = (c.filters || []).map(s => Object.assign({}, s));
    const save = () => push({ filters: stages });
    sh.appendChild(el('label', '', 'Filters'));
    const box = el('div'), pad = 'border:1px solid var(--line);border-radius:9px;padding:8px;margin:0 0 8px';
    const stageSettings = (s, f, card) => {
      let row = null;
      f.settings.forEach((st, n) => {
        if (n % 2 === 0) { row = el('div', 'row'); card.appendChild(row); }
        const d = el('div'); d.appendChild(el('label', '', st.name + (st.suffix ? ' (' + st.suffix + ')' : '')));
        const i = el('input'); i.type = 'number'; i.step = 'any'; i.inputMode = 'decimal'; i.min = st.min; i.max = st.max; i.value = s[st.id] != null ? s[st.id] : st.def;
        i.onchange = () => { if (i.value !== '' && isFinite(Number(i.value))) { s[st.id] = Math.max(st.min, Math.min(st.max, Number(i.value))); i.value = s[st.id]; save(); } };
        d.appendChild(i); row.appendChild(d);
      });
    };
    const draw = () => {
      box.textContent = '';
      stages.forEach((s, k) => {
        const f = by[s.type]; if (!f) return;
        const card = el('div'); card.style.cssText = pad + (s.enabled === false ? ';opacity:.55' : '');
        const head = el('div'); head.style.cssText = 'display:flex;align-items:center;gap:6px;margin-bottom:4px';
        const on = el('input'); on.type = 'checkbox'; on.checked = s.enabled !== false; on.title = 'On / off'; on.onchange = () => { s.enabled = on.checked; save(); draw(); };
        const nm = el('b', '', f.name); nm.style.cssText = 'flex:1;font-size:13px';
        const mv = (d) => () => { const j = k + d; if (j < 0 || j >= stages.length) return; const t = stages[k]; stages[k] = stages[j]; stages[j] = t; save(); draw(); };
        const up = el('button', 'hb', '▲'), dn = el('button', 'hb', '▼'), del = el('button', 'hb', '✕');
        up.title = 'Earlier'; dn.title = 'Later'; del.title = 'Remove this filter';
        up.onclick = mv(-1); dn.onclick = mv(1); del.onclick = () => { stages.splice(k, 1); save(); draw(); };
        [on, nm, up, dn, del].forEach(x => head.appendChild(x)); card.appendChild(head);
        card.appendChild(el('div', 'hint', f.about));
        if (s.type === 'math') {
          const sel = el('select'); cat.ops.forEach(o => sel.appendChild(Object.assign(el('option', '', o.name), { value: o.key }))); sel.value = s.op || 'multiply';
          sel.onchange = () => { s.op = sel.value; save(); draw(); }; card.appendChild(el('label', '', 'Operation')); card.appendChild(sel);
          const o = cat.ops.find(q => q.key === sel.value); if (o && !o.value) { /* no value: Abs, Floor… */ } else stageSettings(s, f, card);
        } else if (s.type === 'curve') {
          const sel = el('select'); cat.curves.forEach((q, i) => sel.appendChild(Object.assign(el('option', '', q.name), { value: i })));
          const cur = cat.curves.findIndex(q => JSON.stringify(q.curve) === JSON.stringify(s.curve)); const extra = el('option', '', 'Custom'); extra.value = -1; if (cur < 0) sel.appendChild(extra); sel.value = cur;
          sel.onchange = () => { if (Number(sel.value) >= 0) { s.curve = cat.curves[Number(sel.value)].curve; save(); } }; card.appendChild(el('label', '', 'Shape')); card.appendChild(sel);
        } else stageSettings(s, f, card);
        box.appendChild(card);
      });
      if (!stages.length) box.appendChild(el('div', 'hint', 'None: the tile sends what you set.'));
    };
    draw(); sh.appendChild(box);
    if (stages.length >= cat.max) { sh.appendChild(el('div', 'hint', 'At most ' + cat.max + ' filters on a tile.')); } else {
      const add = el('select'); add.appendChild(Object.assign(el('option', '', '+ Add a filter…'), { value: '' }));
      const groups = {}; cat.filters.forEach(f => { (groups[f.group] = groups[f.group] || []).push(f); });
      Object.keys(groups).forEach(g => { const og = el('optgroup'); og.label = g; groups[g].forEach(f => og.appendChild(Object.assign(el('option', '', f.name), { value: f.key }))); add.appendChild(og); });
      add.onchange = () => {
        const f = by[add.value]; if (!f) return; const s = { type: f.key }; f.settings.forEach(st => { s[st.id] = st.def; });
        if (f.key === 'curve') s.curve = cat.curves[0].curve; if (f.key === 'math') s.op = 'multiply';
        stages.push(s); save(); add.value = ''; draw();
      };
      sh.appendChild(add);
    }
    sh.appendChild(el('div', 'hint', 'In this order, on what this tile sends (0 to 1 of its range). A smoothing goes on after you let go.'));
  }
)JS";

inline QByteArray script() { return QByteArray(kScriptA) + kScriptB + kScriptB2 + kScriptC + kScriptD + kScriptF + kScriptE; }
} // namespace pxs::controls::page
