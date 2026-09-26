#pragma once

// Served at "/". Status comes from /status (JSON), uploads go to /upload
// (target .ihx) and /update (flasher .ino.bin). Look: the jig's own PCB —
// soldermask green, silkscreen white, gold pads for anything you act on.
static const char PAGE[] = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>STM8Flasher</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Barlow+Semi+Condensed:wght@400;500;600&display=swap" rel="stylesheet">
<style>
:root{
  --mask:#0E3B2E; --mask-hi:#185240; --silk:#F2F1EC; --silk-dim:#A8C3B7;
  --enig:#D6B25E; --epoxy:#26282B; --laser:#A7ABB0;
  --good:#8FE3AE; --bad:#FF9B8C;
}
*{box-sizing:border-box}
body{margin:0;background:var(--mask);color:var(--silk);
  font:18px/1.5 "Barlow Semi Condensed","Arial Narrow",system-ui,sans-serif}
main{max-width:660px;margin:0 auto;padding:36px 16px 56px}
h1{margin:0;font-size:2.3rem;line-height:1.1;font-weight:600;letter-spacing:.01em}
h2{margin:0 0 4px;font-size:1.35rem;font-weight:600}
p{margin:0}

.jig{display:grid;grid-template-columns:minmax(0,1.15fr) minmax(0,1fr);gap:28px;align-items:center;
  margin:28px 0 40px}
.jig svg{width:100%;height:auto;display:block}
#pads rect{fill:var(--enig)}
.outline{fill:none;stroke:var(--silk);stroke-width:2}
.pin1{fill:var(--silk)}
#body{opacity:0;transform:translateY(-10px);transition:opacity .35s,transform .35s cubic-bezier(.2,.8,.3,1)}
.on #body{opacity:1;transform:none}
#mark{fill:var(--laser);font:600 30px "Barlow Semi Condensed","Arial Narrow",sans-serif;letter-spacing:.04em}
.det{font-size:1.7rem;font-weight:600;line-height:1.15;margin-bottom:8px}
.jig p:not(.det){color:var(--silk-dim)}
.counts b{color:var(--silk);font-weight:600}

.up{border-top:1.5px solid rgba(242,241,236,.28);padding:26px 0 30px}
.up>p{color:var(--silk-dim);max-width:34em;margin-bottom:16px}
.row{display:flex;gap:12px;flex-wrap:wrap}
.drop{flex:1 1 260px;display:flex;align-items:center;min-height:52px;padding:10px 16px;
  border:1.5px solid var(--enig);border-radius:7px;color:var(--enig);cursor:pointer;
  overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.drop.over,.drop:hover{background:rgba(214,178,94,.12)}
.drop.has{color:var(--silk)}
.drop input{position:absolute;opacity:0;width:1px;height:1px}
.drop:focus-within,button:focus-visible{outline:2px solid var(--silk);outline-offset:3px}
button{flex:0 0 auto;min-height:52px;padding:0 22px;border:0;border-radius:7px;
  background:var(--enig);color:var(--mask);font:600 1.05rem "Barlow Semi Condensed","Arial Narrow",sans-serif;cursor:pointer}
button:disabled{opacity:.35;cursor:default}
.trace{height:3px;margin-top:16px;background:var(--mask-hi);border-radius:2px;overflow:hidden}
.trace i{display:block;height:100%;width:0;background:var(--enig);transition:width .15s}
.msg{min-height:1.5em;margin-top:8px}
.msg.good{color:var(--good)} .msg.bad{color:var(--bad)}

footer{color:var(--silk-dim);font-size:.95rem;border-top:1.5px solid rgba(242,241,236,.28);padding-top:18px}
@media (max-width:560px){.jig{grid-template-columns:1fr;gap:18px}}
@media (prefers-reduced-motion:reduce){#body,.trace i{transition:none}}
</style>
</head>
<body>
<main>
<h1>STM8Flasher</h1>

<section class="jig" aria-live="polite">
  <svg viewBox="0 0 300 150" aria-hidden="true">
    <g id="pads"></g>
    <rect class="outline" x="20" y="44" width="260" height="62" rx="3"/>
    <circle class="pin1" cx="12" cy="136" r="4"/>
    <g id="body">
      <rect x="24" y="30" width="252" height="90" rx="4" fill="var(--epoxy)"/>
      <circle cx="42" cy="102" r="6" fill="#383A3E"/>
      <text id="mark" x="150" y="86" text-anchor="middle"></text>
    </g>
  </svg>
  <div>
    <p class="det" id="det">Looking for a target</p>
    <p class="counts"><b id="pass">0</b> passed, <b id="fail">0</b> failed since restart</p>
    <p id="mode"></p>
    <p id="image"></p>
  </div>
</section>

<section class="up" id="ihx">
  <h2>Target firmware</h2>
  <p>The Intel HEX file (.ihx) for the STM8. The flasher keeps it and writes it to
  every target from now on. A target already in the jig gets flashed as soon as the upload finishes.</p>
  <div class="row">
    <label class="drop"><input type="file" accept=".ihx,.hex"><span>Choose or drop an .ihx file</span></label>
    <button disabled>Upload and flash</button>
  </div>
  <div class="trace"><i></i></div>
  <p class="msg" role="status"></p>
</section>

<section class="up" id="fw">
  <h2>Flasher firmware</h2>
  <p>Updates this T-Display S3. Use TDisplayS3-STM8S-Flasher.ino.bin from the build
  folder, not the merged or bootloader .bin. The flasher restarts when the update is written.</p>
  <div class="row">
    <label class="drop"><input type="file" accept=".bin"><span>Choose or drop the .ino.bin file</span></label>
    <button disabled>Update flasher</button>
  </div>
  <div class="trace"><i></i></div>
  <p class="msg" role="status"></p>
</section>

<footer id="build">&nbsp;</footer>
</main>

<script>
const $ = (s, el = document) => el.querySelector(s);
const sleep = ms => new Promise(r => setTimeout(r, ms));

// TSSOP-20 footprint: 10 pads per side, 25-unit pitch
let pads = '';
for (let i = 0; i < 10; i++) {
  const x = 31.5 + i * 25;
  pads += `<rect x="${x}" y="4" width="12" height="32" rx="2"/><rect x="${x}" y="114" width="12" height="32" rx="2"/>`;
}
$('#pads').innerHTML = pads;

let md5 = '';
async function status() {
  const r = await fetch('/status', {cache: 'no-store', signal: AbortSignal.timeout(2500)});
  const s = await r.json();
  const on = s.detected !== 'no target';
  $('.jig').classList.toggle('on', on);
  if (on) $('#mark').textContent = s.detected;
  $('#det').textContent = on ? `${s.detected} in the jig` : 'No target in the jig';
  $('#pass').textContent = s.pass;
  $('#fail').textContent = s.fail;
  $('#mode').textContent = s.auto ? 'Auto mode: each new target is flashed once.'
                                  : 'Manual mode: press the flash button to flash.';
  $('#image').textContent = s.image ? `Target image: ${s.image.toLocaleString()} bytes.`
                                    : 'No target image yet. Upload one below.';
  $('#build').textContent = `Flasher firmware built ${s.build}`;
  md5 = s.md5;
  return s;
}
status().catch(() => {});
setInterval(() => status().catch(() => {}), 2000);

function wire(id, url, field, done) {
  const sec = $(id), inp = $('input', sec), btn = $('button', sec), drop = $('.drop', sec),
        name = $('.drop span', sec), bar = $('.trace i', sec), msg = $('.msg', sec);
  const hint = name.textContent;
  const say = (t, c = '') => { msg.textContent = t; msg.className = 'msg ' + c; };

  inp.onchange = () => {
    const f = inp.files[0];
    name.textContent = f ? f.name : hint;
    drop.classList.toggle('has', !!f);
    btn.disabled = !f;
    bar.style.width = 0;
    say('');
  };
  drop.ondragover = e => { e.preventDefault(); drop.classList.add('over'); };
  drop.ondragleave = () => drop.classList.remove('over');
  drop.ondrop = e => {
    e.preventDefault();
    drop.classList.remove('over');
    inp.files = e.dataTransfer.files;
    inp.onchange();
  };

  btn.onclick = () => {
    const f = inp.files[0];
    if (!f) return;
    const fd = new FormData();
    fd.append(field, f);
    const x = new XMLHttpRequest();
    x.open('POST', url);
    btn.disabled = true;
    x.upload.onprogress = e => {
      const p = Math.round(e.loaded / e.total * 100);
      bar.style.width = p + '%';
      say(`Uploading, ${p}%`);
    };
    x.onload = () => {
      if (x.status === 200) return done(say, btn);
      bar.style.width = 0;
      btn.disabled = false;
      say(x.responseText.trim() || `The flasher rejected the upload (HTTP ${x.status}).`, 'bad');
    };
    x.onerror = () => {
      btn.disabled = false;
      say('Lost the connection to the flasher during the upload. Try again.', 'bad');
    };
    x.send(fd);
  };
}

wire('#ihx', '/upload', 'ihx', (say, btn) => {
  btn.disabled = false;
  say('Target image saved. Any target in the jig is being flashed now.', 'good');
  status().catch(() => {});
});

wire('#fw', '/update', 'firmware', async (say, btn) => {
  const old = md5;
  say('Update written. Restarting the flasher.');
  await sleep(3000);
  for (const t0 = Date.now(); Date.now() - t0 < 40000; await sleep(1000)) {
    try {
      const s = await status();
      btn.disabled = false;
      if (s.md5 === old) say('The flasher restarted but is still running the old firmware.', 'bad');
      else say('Updated. The flasher is running the new firmware.', 'good');
      return;
    } catch (e) {}
  }
  btn.disabled = false;
  say("The flasher hasn't come back after 40 seconds. Check its screen.", 'bad');
});
</script>
</body>
</html>
)rawliteral";
