# antislop audit 001, follow-up

Date: 2026-10-01
Fixes applied: all 16 findings from `audit-001-2026-10-01.md`, approved by the owner (including em dashes in
the sketch's header comment, which the owner explicitly chose over the narrower reading).

## Delivery Gate

**Block 1, Hard Gate**

- R-02 PASS: zero em dashes and zero en dashes in the sketch, the README, and the rendered page. Verified by
  counting the characters, not by eye. The header block was rewritten on the owner's instruction.
- R-03 PASS: no horizontal overflow at 320px, 390px, or 414px. Every touch target is now at least 44px in
  both axes, including the threshold slider (16px -> 44px) and the sound checkbox via its 44px label.
- R-17 PASS: no unsourced statistics. The one decorative shields.io badge was removed.
- R-18 PASS: no testimonials exist.
- R-23 PASS: no invented assets. The decorative shield emoji in the page heading was removed rather than
  replaced with a generated logo.
- R-24 PASS: there is no navbar, and the page has nothing to navigate to.
- R-25 PASS: `.info` moved from 3.29:1 to 6.63:1. Recomputed in the browser against the real rendered
  colours: `.info` 6.63, `.small` 6.63, `#status` 6.14, `#conn` 7.34, all above the 4.5:1 floor.
- R-26 PASS: no dead controls remain. START is disabled outside IDLE/GAMEOVER, and the threshold controls
  are disabled outside IDLE, which is exactly the rule the firmware enforces. A disabled button cannot be
  pressed into a no-op.
- R-27 PASS: the connection indicator now has a real bordered pill with text, appears the moment the
  stream drops, and the poll fallback starts from the error event instead of an unreachable 3s timer. The
  tilt bar no longer holds a red state across the transition back to the menu.
- R-28 PASS: no FAQ exists, by decision, recorded in `DESIGN.md`.
- R-32 PASS: `−` and `+` have `aria-label`s, the slider has an accessible name, the sound checkbox has a
  real `<label for>`, `#status` is `aria-live="polite"`, `#tiltbar` is a `progressbar` with a live
  `aria-valuenow` and `aria-valuetext`, `#timer` is `role="timer"` with `aria-live="off"` so it does not
  spam a screen reader ten times a second, and `<html lang="en">` is set. The focus ring was verified with
  real keyboard `Tab` presses, not a programmatic `focus()`.
- R-33 PASS: all changes are in the source. No patch script was left in the repo; the one-off cleanup
  script ran from a temp directory and was never committed.
- R-34 PASS: only one theme ships, so there is no second mode to break.
- R-35 PASS: the page was served and every control exercised. Full transcript in
  `clickthrough-output.txt`, harness in `clickthrough.js`, firmware mock in `mock-firmware-server.py`.
- R-36 PASS: the "no polling lag" claim and the "leveller.local on most phones" claim were both removed.
  The README now says plainly that most Android builds dropped `.local` resolution.
- R-37 PASS: `DESIGN.md` records the dials and the reason behind every visual decision.
- R-38 PASS: the stale `BARRETT` title, left behind by the rebrand in `a7a786d`, is gone.

**Block 2, Purpose-Gate**: no gradients, glass, glow, bento grid, or icon set. The only techniques in play
are a flat dark surface, one accent, and a three-state colour vocabulary, all justified in `DESIGN.md`.

**Block 3, Liveliness**: dials declared (`ENERGY 1 / RHYTHM 1 / MOTION 1`) and held, which is the correct
reading for a reflex game with one screen. One focal point (the timer, 3.5rem against 0.85rem hints). One
deliberate accent. Motif is the tilt bar, the one thing that moves.

**Block 4, Craftsmanship**: C-1 to C-5 all hold, and the Quality Locks hold. R-11 radius varies by
hierarchy (8px primary, 4px status pill). R-15 CTAs are `START`, `CALIBRATE`, `STOP`, `BACK TO MENU`.
R-16 no buzzwords. R-29 justified in `DESIGN.md` with the reason written down.

## Two things I could not verify, stated plainly

1. **The sketch was not compiled.** There is no `arduino-cli` on this machine. Instead of claiming a
   build passed, I proved the change surface: stripping comments, string literals, and the raw HTML literal
   from both `HEAD` and the working tree leaves 313 identical lines of executable C++ and a diff of zero.
   The firmware changes in this pass are comments and two `SLOG` strings, nothing else. The page JS was
   syntax-checked with `node --check` and then exercised in a real browser.
2. **The firmware was not run on hardware.** The click-through ran against a mock of the HTTP surface
   (`mock-firmware-server.py`), which reproduces the endpoints, the SSE stream, the state machine, and the
   IDLE-only threshold rule. It does not reproduce real accelerometer noise, the AP, or the I2C bus.

## Corrections to my own first pass

Two results in the first audit were wrong and are corrected above: the focus ring read as missing (an
artifact of programmatic `focus()` in headless) and the 195px overflow read as a reflow failure (it is a
200% browser-zoom case; WCAG 1.4.10 is a 320px requirement and passes). Two later failures were my test
harness leaking mock state between blocks, not app defects, and the final re-run confirms it.

## Follow-on defect found while fixing

The threshold ± buttons and the slider stayed live during a round even though the firmware only accepts a
change while IDLE, so a tap was swallowed with no feedback. This was not in the original 16; it surfaced
when the click-through showed a 4-tap burst that the device rejected. Fixed alongside finding 1, and now
covered by the `FIX 6b` checks.
