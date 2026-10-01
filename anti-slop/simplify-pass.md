# simplify-code pass

Date: 2026-10-01
Method: four parallel reviewers (reuse, quality, efficiency, altitude) over the full working-tree diff,
read-only. Every finding below was re-verified in this session against the source before acting. The
reviewers' summaries were treated as self-reports, not facts.

## Applied

| # | Finding | Source | Verdict |
|---|---|---|---|
| 1 | Dead `thr=15` global: the diff moved every read to `thrLocal` and removed the writer, leaving a binding that looks authoritative and updates nothing | all four | SAFE, removed |
| 2 | `applyState` re-implemented `paintThr`'s three writes inline instead of calling the helper the same change introduced | reuse, quality | CAREFUL, now calls `paintThr(s.thr)` |
| 3 | `aria-valuetext` and `#tilttxt` read raw `s.tilt` while the bar was gated by `inPlay`, so at IDLE with a tilted device the bar read 0% and the text read "Tilt 90.0 of 15 deg" | efficiency, altitude | CAREFUL, one gated `tilt` now feeds pct, class, aria text, and the caption |
| 4 | `f.className=(pct===0)?...` conflated "not in play" with "device level" | quality | SAFE, now keyed off `inPlay` |
| 5 | `#status` is `aria-live="polite"` and was rewritten 10x a second; an identical `textContent` assignment still counts as a change to assistive tech | efficiency, altitude | CAREFUL, write is now guarded. Measured 0 DOM mutations over 1.2s of steady PLAYING, where the old code produced about 12 |
| 6 | Stale-poll race defeated the new badge: `onopen` cleared the timer, but a poll already in flight resolved afterwards and left the badge reading `POLLING` for the rest of a healthy session | quality, efficiency, altitude | CAREFUL, in-flight polls now bail if the timer was cleared, and an `inFlight` guard stops request stacking |
| 7 | `setConn` wrote an identical string every second into an `aria-live` region | altitude | CAREFUL, now write-on-transition only |
| 8 | `CALIBRATE` and `STOP` were not gated, the same defect class as the two already fixed. `/api/calibrate` has no state gate and `calibrateDevice()` forces `currentState = IDLE`, so calibrating mid-round destroyed a live run with no `GAMEOVER` and no final time. `/api/abort` at IDLE hit neither branch: a dead control | altitude | CAREFUL, both now follow the device's real admission rules |
| 9 | `slider.oninput` updated the caption but not `thrLocal`, so a drag followed by a tap on `+` posted the pre-drag value | efficiency | CAREFUL, `oninput` now updates `thrLocal` |
| 10 | `paintThr` closed over `slider`, declared eight lines below it: safe only because nothing called it earlier, a latent TDZ trap | quality, efficiency | SAFE, declaration hoisted |
| 11 | `flex:1` declared twice for the slider (stylesheet plus leftover inline style) | reuse, quality | SAFE, inline copy removed |
| 12 | The 5 to 90 bound was written three times in the page; the JS literal was new and could drift from the element it mirrors | reuse, quality | CAREFUL, derived from `slider.min` / `slider.max` |
| 13 | `mock-firmware-server.py` split its own filename on `"\\"`, so it could not start on POSIX, and it required an `index.html` that is not in the repo | efficiency | CAREFUL, `os.path.dirname` and it now extracts the page from the sketch itself |

The click-through gained twelve checks to cover 1 through 12. All 38 pass.

## Rejected, with reasons

- **A comment claiming the 10 Hz push was the cause of dropped taps.** Reviewer 4 showed the premise was
  wrong: `/api/threshold` calls `pushStateEvent(true)` on an accepted change, so the device echoes at once.
  The real cause was the stale `thr` global. The comment was rewritten to say that, and the fix is
  unchanged; only my explanation of it was wrong.
- **Restoring the `// ---- SECTION ----` banners.** Reviewer 2 worried a 650-line sketch is harder to
  navigate without them. The plain `// GAME STATES` headers remain, which is the same navigation aid
  without the decoration. Not a defect.
- **Collapsing `beep()` and `alarm()`.** Two of the reviewers' overlap, and the shared block is idiomatic
  WebAudio. Not worth the indirection.
- **Treating `thrLocal` as removable redundant state.** Reviewer 2 flagged it and then correctly withdrew
  it: `paintThr` deliberately skips `slider.value` while the slider holds focus, so during a drag
  `thrLocal` is the only current copy. Left alone.

## Flagged, not applied (all need a decision)

These are the real ones. Each changes a contract, so none was applied in a cleanup pass.

1. **`/api/threshold` answers `200 "ok"` whether or not it applied the value.** It silently drops
   non-IDLE and out-of-range requests. This is the root cause of the whole optimistic-state
   (`thrLocal` plus `NUDGE_LOCK_MS`) arrangement: the page cannot know what the device accepted, so it
   guesses with a wall-clock window. Returning `409` on rejection, or echoing the applied value, would
   delete the guess. RISKY: changes the HTTP contract that the mock and the click-through assert against.

2. **The client owns the firmware's admission rules.** `startBtn`, `minus`, `plus`, `slider`, `calBtn`
   and `abortBtn` are now gated by rules that also live in the firmware. They agree today and will drift
   the first time the state machine changes. The device should publish its accepted-action set
   (`canStart`, `canSetThreshold`, `canCalibrate`, `canAbort`) from `buildStateJson` and the page should
   render purely from that, so the rules have one owner. RISKY: changes the JSON contract, the README API
   table, and the mock. This is the correct fix for item 8 above and should be its own task.

3. **A scripted click that bypasses a disabled button still reaches the device**, which the harness prints
   as a live note. The UI gate is a usability fix, not a security or correctness boundary. Item 2 is the
   real fix.

4. **Pre-existing firmware bug, confirmed by all four reviewers and re-verified here.**
   `calibrateDevice()` averages over the *attempted* count, not the successful one. If all ten
   `readAccelG()` calls fail, `sumX / N` is `0 / 10`, and `saveSettings()` writes `baseX = baseY = 0` to
   NVS, where it is read back on every boot. Partial failure is the nastier case: four of ten samples
   yields a plausible but wrong baseline biased toward zero. `git blame` shows the skip-guard and the
   divisor arrived in different commits and were never reconciled. BMI160-only reachable, since the
   MPU6050 path returns `true` unconditionally. Not fixed: it needs a firmware change and a decision on
   what calibration should do when the sensor will not answer.

5. **`thrDirty` and `lastThrTouch` are not `volatile`**, though they are written from the async web task
   and read in `loop()`, unlike `calRequested` on the same handler. A cacheable-load race on an ESP32-C3.
   Pre-existing, one-word fix, but firmware.

6. **The accel-to-roll/pitch math is duplicated** between `loop()` and `calibrateDevice()`, byte-identical
   modulo whitespace. Calibration builds the baseline through one copy and the round compares through the
   other, so a change to the scaling or the `57.2958f` constant makes them disagree silently. Worth
   extracting, but it touches the sensing path, so it is not a cleanup change.

## Notes

- Firmware logic is still untouched: stripping comments, strings, and the raw HTML literal from `HEAD`
  and the working tree leaves 313 identical lines of executable C++ and a diff of zero.
- Two of the four reviewers' headline claims about my own work were wrong in my favour and one was wrong
  against me. Each was checked against the source rather than taken at face value; the correction to my
  comment about the 10 Hz push, and the correction to the audit's unreachability claim, both came from
  this pass.
