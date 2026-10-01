# ESP Leveller web UI: design direction

Written for the release audit (`anti-slop/audit-001-2026-10-01.md`). Every decision below carries the
one-line reason R-31 requires. If a decision here has no reason, the decision is invalid.

## Design Read

> Reading this as: a handheld steadiness game for a phone held one-handed at a convention booth, in a
> utilitarian instrument language, dial `ENERGY 1 / RHYTHM 1 / MOTION 1`.

## Dials

| Dial | Value | Why |
|---|---|---|
| ENERGY | 1 | A reflex game has no time for decoration. The player glances down for under a second between rounds. |
| RHYTHM | 1 | One screen, one job. There is nothing to vary between, so a uniform layout is the honest composition rather than a template. |
| MOTION | 1 | Motion costs reaction time in a game about not moving. The only transitions are the 100 ms tilt bar and button `:active` feedback. |

## Decisions

- **Dark theme, no light mode.** The player is watching a dark booth and a bright phone screen at night
  would blow out their night vision, which is the exact opposite of the steadiness the game is asking
  for. A light mode would serve no real use, so there is none to break.
- **The timer is the single focal point**, at 3.5rem against 1.1rem for the status and 0.85rem for hints.
  It is the number the round is about, and it is the only thing large enough to read mid-round.
- **Monospace for the timer, system sans everywhere else.** Monospace keeps the digits from shifting
  width as the value changes, so the number stays still while it counts. That matters because the
  whole game is about staying still. The rest of the UI has no such constraint and gets the system
  font for legibility at small sizes.
- **One column, max 480px, centred.** The player holds the phone in portrait, and the reading order
  (state, time, tilt, threshold, controls) is a single vertical scan. A two-column layout would force
  the eyes back and forth during a round.
- **The accent `#0af` is the device's own colour language.** It marks the interactive surfaces (the
  threshold slider, the START button, a safe tilt bar) so the player can tell at a glance what is
  tappable. It is the same cyan already used for the boot AP address in the docs.
- **Three saturated colours on one screen, and that is deliberate.** `#0af` safe, `#fa0` warning at
  75% of the threshold, `#f33` failure at the threshold. This is past the usual two-to-three plus
  accent line, and it stays that way on purpose: safe, warning, and fail is the entire vocabulary of
  the game, and collapsing them would remove the one piece of information the player needs to read
  mid-round. The warning state is a shade, not a hue, so it is distinguishable without relying on
  colour alone.
- **Sound is generated on the phone, not the device.** The ESP32 has no speaker, and the phone is
  already the display and the controller. Proximity ticking accelerates as the tilt approaches the
  limit, so the player can react to audio alone and keep their eyes on the device.
- **No FAQ, no feature grid, no pricing table.** This is a device UI reached by typing an IP address,
  not a landing page. The README carries the documentation instead.
- **Rounded corners at 8px, with 4px on the small status pill.** The variation is hierarchy: the
  primary action and the status block get the larger radius, the reconnect notice is a small label.

## Anti-patterns deliberately avoided

- No em dash in any user-visible text (R-02).
- No decorative emoji in the heading (R-36). The old `BARRETT` title was also a rebrand bug, fixed in
  the same pass.
- No AI-slop badge in the README, and no claims about uptime, user counts, or response times (R-17,
  R-36). The only numbers published are ones a reviewer can reproduce from the code.
- No navbar: a single-screen UI has nothing to navigate to (R-24).
