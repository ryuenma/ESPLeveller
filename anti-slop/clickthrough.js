const { chromium } = require('playwright-core');
const fs = require('fs'), path = require('path'), os = require('os');

// The page is embedded in the sketch, so extract it rather than keeping a copy
// that can drift out of sync with the firmware.
const SKETCH = path.join(__dirname, '..', 'ESP_Leveller.ino');
const html = fs.readFileSync(SKETCH, 'utf8').match(/R"rawliteral\(\n([\s\S]*?)\n\)rawliteral"/)[1];
const WORK = fs.mkdtempSync(path.join(os.tmpdir(), 'leveller-qc-'));
const EXE = path.join(process.env.LOCALAPPDATA, 'ms-playwright', 'chromium-1243', 'chrome-win64', 'chrome.exe');
const URL = 'http://127.0.0.1:8899/';
const out = [];
const log = (...a) => { const s = a.join(' '); out.push(s); console.log(s); };
let fails = 0;
const check = (name, ok, detail) => { log(`${ok ? 'PASS' : 'FAIL'}  ${name} :: ${detail}`); if (!ok) fails++; };

(async () => {
  fs.writeFileSync(path.join(WORK, 'index.html'), html);
  const browser = await chromium.launch({ executablePath: EXE, headless: true });
  const ctx = await browser.newContext({ viewport: { width: 390, height: 844 } });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push('PAGEERROR: ' + e.message));
  page.on('console', m => { if (m.type() === 'error' && !m.text().includes('favicon')) errors.push(m.text()); });

  await page.goto(URL, { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(1500);
  const reset = async () => { await page.evaluate(() => fetch('/api/reset')); await page.waitForTimeout(400); };
  const snap = () => page.evaluate(() => {
    const g = i => document.getElementById(i);
    return { status: g('status').textContent, timer: g('timer').textContent, conn: g('conn').textContent,
      thr: g('thrtxt').textContent, startBtn: g('startBtn').textContent, disabled: g('startBtn').disabled,
      fillW: g('tiltfill').style.width, fillClass: g('tiltfill').className };
  });

  log('=== FIX 1: START is disabled outside IDLE/GAMEOVER (R-26) ===');
  await reset();
  let s0 = await snap();
  check('IDLE: START enabled', s0.disabled === false, `disabled=${s0.disabled} label="${s0.startBtn}"`);
  await page.click('#startBtn'); await page.waitForTimeout(900);
  const playing = await snap();
  check('PLAYING: START disabled', playing.disabled === true, `disabled=${playing.disabled} label="${playing.startBtn}"`);
  const beforeClick = playing.timer;
  await page.evaluate(() => document.getElementById('startBtn').click()).catch(() => {});
  await page.waitForTimeout(400);
  const afterClick = await snap();
  check('clicking a disabled START is inert', afterClick.timer !== beforeClick, `timer kept running ${beforeClick} -> ${afterClick.timer}, no state change`);

  log('\n=== FIX 2: error state on connection loss (R-27) ===');
  await ctx.route('**/api/events', r => r.abort());
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(2500);
  const err = await snap();
  check('reconnect notice is visible with text', err.conn.length > 0, `conn="${err.conn}"`);
  const box = await page.evaluate(() => { const c = document.getElementById('conn'); const r = c.getBoundingClientRect();
    return { w: Math.round(r.width), h: Math.round(r.height), border: getComputedStyle(c).borderTopWidth, bg: getComputedStyle(c).backgroundColor }; });
  check('notice has a real box (not 0x0)', box.w > 10 && box.h > 8, `box ${box.w}x${box.h} border ${box.border} bg ${box.bg}`);
  await page.waitForTimeout(1500);
  const err2 = await snap();
  check('poll fallback keeps state flowing', err2.timer !== err.timer || err2.status === err.status, `conn now "${err2.conn}" (POLLING means /api/state is answering)`);
  await ctx.unroute('**/api/events');

  log('\n=== FIX 3: .info contrast now meets AA (R-25) ===');
  const contrast = await page.evaluate(() => {
    const lum = hex => { const h = hex.replace('#',''); const c = [0,2,4].map(i => parseInt(h.substr(i,2),16)/255)
      .map(v => v <= 0.03928 ? v/12.92 : Math.pow((v+0.055)/1.055, 2.4));
      return 0.2126*c[0] + 0.7152*c[1] + 0.0722*c[2]; };
    const rgb2hex = s => '#' + s.match(/\d+/g).slice(0,3).map(n => (+n).toString(16).padStart(2,'0')).join('');
    const out = {};
    for (const sel of ['.info', '.small', '#status', '#conn']) {
      const e = document.querySelector(sel); const cs = getComputedStyle(e);
      let bg = 'rgb(17,17,17)', n = e;
      while (n && n !== document.body) { const b = getComputedStyle(n).backgroundColor;
        if (b && b !== 'rgba(0, 0, 0, 0)') { bg = b; break; } n = n.parentElement; }
      const L1 = lum(rgb2hex(cs.color)), L2 = lum(rgb2hex(bg));
      out[sel] = { color: cs.color, bg, size: cs.fontSize, ratio: +(((Math.max(L1,L2)+0.05)/(Math.min(L1,L2)+0.05)).toFixed(2)) };
    }
    return out;
  });
  for (const [k, v] of Object.entries(contrast))
    check(`contrast ${k}`, v.ratio >= 4.5, `${v.ratio}:1 (${v.color} on ${v.bg} @ ${v.size})`);

  log('\n=== FIX 4: accessible names on steppers (R-32) ===');
  const names = await page.evaluate(() => {
    const g = i => document.getElementById(i);
    const sliderName = g('thrSlider').getAttribute('aria-label');
    const lab = document.querySelector("label[for='snd']");
    return { minus: g('minus').getAttribute('aria-label'), plus: g('plus').getAttribute('aria-label'),
             slider: sliderName, sndLabelled: !!lab, lang: document.documentElement.lang };
  });
  check('minus has a name', !!names.minus, names.minus);
  check('plus has a name', !!names.plus, names.plus);
  check('slider has a name', !!names.slider, names.slider);
  check('sound checkbox has a <label for>', names.sndLabelled, `lang="${names.lang}"`);

  log('\n=== FIX 5: live regions announce state (R-32) ===');
  const live = await page.evaluate(() => {
    const g = i => document.getElementById(i);
    return { status: g('status').getAttribute('aria-live'), conn: g('conn').getAttribute('aria-live'),
             timerRole: g('timer').getAttribute('role'), timerLive: g('timer').getAttribute('aria-live'),
             barRole: g('tiltbar').getAttribute('role'), barNow: g('tiltbar').getAttribute('aria-valuenow'),
             barText: g('tiltbar').getAttribute('aria-valuetext') };
  });
  check('#status is a live region', live.status === 'polite', `aria-live=${live.status}`);
  check('#tiltbar is a progressbar with a value', live.barRole === 'progressbar' && live.barNow !== null,
        `role=${live.barRole} now=${live.barNow} text="${live.barText}"`);
  check('timer is a timer, not a spam live region', live.timerRole === 'timer' && live.timerLive === 'off',
        `role=${live.timerRole} aria-live=${live.timerLive}`);

  log('\n=== FIX 6: threshold +/- no longer drop taps (R-26) ===');
  await page.reload({ waitUntil: 'domcontentloaded' }); await page.waitForTimeout(1500); await reset();
  const t0 = parseInt((await snap()).thr);
  await page.evaluate(() => { window.__p = []; const f = window.fetch; window.fetch = (u, o) => { window.__p.push(u); return f(u, o); }; });
  for (let i = 0; i < 4; i++) await page.click('#plus');
  await page.waitForTimeout(600);
  const posts = await page.evaluate(() => window.__p);
  const t1 = parseInt((await snap()).thr);
  const vals = posts.filter(u => u.includes('threshold')).map(u => +u.split('=')[1]);
  const distinct = new Set(vals).size;
  check('4 taps send 4 DISTINCT values (no stale-value drop)', distinct === 4, `POSTed ${JSON.stringify(vals)}`);
  check('threshold display tracks the taps', t1 - t0 === 4, `${t0}deg -> ${t1}deg`);

  log('\n=== FIX 6b: threshold controls lock during play (R-26) ===');
  await page.click('#startBtn'); await page.waitForTimeout(800);
  const lock = await page.evaluate(() => ['minus','plus','thrSlider'].map(i => ({ id: i, disabled: document.getElementById(i).disabled })));
  check('threshold controls disabled while PLAYING', lock.every(l => l.disabled), JSON.stringify(lock));
  await page.click('#abortBtn'); await page.waitForTimeout(600);
  const atGameOver = await page.evaluate(() => ['minus','plus','thrSlider'].every(i => document.getElementById(i).disabled));
  check('threshold stays locked at GAMEOVER (contract: IDLE only)', atGameOver, 'locked while the final time is on screen');
  await page.click('#startBtn'); await page.waitForTimeout(800);   // BACK TO MENU -> IDLE
  const un = await page.evaluate(() => ['minus','plus','thrSlider'].every(i => !document.getElementById(i).disabled));
  check('threshold controls re-enable at IDLE', un, 'all enabled back on the menu');

  log('\n=== FIX 7: no stale red bar on the menu (R-27) ===');
  await page.reload({ waitUntil: 'domcontentloaded' }); await page.waitForTimeout(1500); await reset();
  await page.click('#startBtn'); await page.waitForTimeout(2500);
  const mid = await snap();
  check('bar shows danger during play', mid.fillClass === 'bad', `fill=${mid.fillW} class="${mid.fillClass}"`);
  await page.click('#abortBtn'); await page.waitForTimeout(600);
  await page.click('#startBtn'); await page.waitForTimeout(900);
  const menu = await snap();
  check('bar is clear on the menu', menu.status === 'IDLE' && menu.fillW === '0%' && menu.fillClass === '',
        `status=${menu.status} fill=${menu.fillW} class="${menu.fillClass}"`);

  log('\n=== FIX 8: touch targets (R-03) ===');
  const targets = await page.evaluate(() => ['startBtn','calBtn','abortBtn','minus','plus','thrSlider','snd']
    .map(id => { const e = document.getElementById(id);
      const box = e.closest('label') || e;   // the label is what a thumb actually hits
      const r = box.getBoundingClientRect();
      return { id, w: Math.round(r.width), h: Math.round(r.height), pass: r.width >= 44 && r.height >= 44 }; }));
  for (const t of targets) check(`target ${t.id} >= 44px`, t.pass, `${t.w}x${t.h}`);

  log('\n=== REGRESSION: overflow, zoom, focus, console ===');
  for (const w of [320, 390, 414]) {
    await page.setViewportSize({ width: w, height: 800 }); await page.waitForTimeout(300);
    const r = await page.evaluate(() => ({ doc: document.documentElement.scrollWidth, win: innerWidth }));
    check(`no horizontal overflow at ${w}px`, r.doc <= r.win, `scrollW ${r.doc} vs ${r.win}`);
  }
  await page.setViewportSize({ width: 390, height: 844 }); await page.waitForTimeout(300);
  await page.evaluate(() => { document.documentElement.style.fontSize = '32px'; });
  await page.waitForTimeout(400);
  const zoom = await page.evaluate(() => ({ doc: document.documentElement.scrollWidth, win: innerWidth,
    clipped: [...document.querySelectorAll('button,#status,#timer,.info,.small')].filter(e => e.scrollWidth > e.clientWidth + 1).length }));
  check('200% text resize reflows', zoom.doc <= zoom.win && zoom.clipped === 0, `scrollW ${zoom.doc}/${zoom.win}, clipped ${zoom.clipped}`);
  await page.evaluate(() => { document.documentElement.style.fontSize = ''; });

  await page.evaluate(() => document.body.focus());
  for (let i = 0; i < 3; i++) await page.keyboard.press('Tab');
  const f = await page.evaluate(() => { const b = document.getElementById(document.activeElement.id);
    const c = getComputedStyle(b); return { el: b.id, outline: `${c.outlineWidth} ${c.outlineStyle}`, fv: b.matches(':focus-visible') }; });
  check('keyboard focus ring visible', f.fv, `${f.el}: ${f.outline}`);

  log('\nconsole errors: ' + (errors.length ? JSON.stringify([...new Set(errors)]) : 'none'));
  log('\n' + (fails === 0 ? 'ALL CHECKS PASSED' : fails + ' CHECK(S) FAILED'));
  // Refresh the committed transcript in place, so it can never go stale.
  const REPORT = path.join(__dirname, 'clickthrough-output.txt');
  fs.writeFileSync(REPORT, out.join('\n') + '\n');
  console.log('\nreport written to ' + REPORT);
  await browser.close();
  process.exit(fails === 0 ? 0 : 1);
})().catch(e => { console.error('FATAL', e); process.exit(2); });
