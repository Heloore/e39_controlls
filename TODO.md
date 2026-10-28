# E39 controls: TODO

Car: 2003 E39, BM54 nav (GT), GM5, LCM IV (diagnostic index 0x14), seat memory not on the bus, no mirror memory.
Hardware: ESP32 + PCM5102, I-Bus and K-Bus reader/writer (discrete), Wi-Fi web page, Bluetooth audio.

Legend: **[ready]** we know enough to build it, **[scan]** needs lamp/job discovery first, **[research]** unknown, **[hardware]** needs wiring/parts.

## Rules (do not break)
- Never touch the sunroof, trunk or windows jobs from scans; the blocklist in `GmJobScanner.h` stays.
- Seat, lamps and doors from the web page stay locked while the car is moving.
- Test new hardware receive-only first; ask before tapping wires.

## Done
- Bluetooth audio, MFL buttons, volume ducking in reverse (frame `13 02 10/B0`, byte 4 is ignored)
- Track name on the nav screen (AUX) with scrolling; gauges in the nav header zones
- Auto-unlock when the engine stops and the car locked the doors (job `0C 00 14 01`, verified in the car)
- Follow-me-home: low beams for a set time after ignition off (bits 5/4 and 5/5 on the LCM)
- Seat: 10 moves (job `0C 15 <00-09>`), `seat <move> [pulses]`
- Lamp map for the LCM (13 lamps), manual lamps with auto-off
- Gauges: speed, RPM, coolant, outside temp, odometer (`BF 17`), OBC texts (range, consumption, average speed)
- Wi-Fi web page (Control, Monitor, Settings); mock server in `tools/`

## Before anything else
- [hardware] **Power hold**: ESP must stay powered after ignition off (self-hold relay design, or the TH3122/ELMOS 10026B transceiver that wakes on bus activity). Needed for auto-unlock and follow-me-home to work in the car.
- Flash the web interface: Partition Scheme "Huge APP (3MB No OTA)", then `wifi pass <8-31 chars>`; test on the car (audio stutter with Wi-Fi? "bip"?).
  - Heap is the limit: only ~20 KB free with Bluetooth + Wi-Fi (lowest 10 KB), the page body could not be sent (errno 11). So the web page runs in its own **Wi-Fi mode with Bluetooth off**: hold R/T on the wheel (or serial `wifi on`) to restart into it, hold R/T again, the page button or `wifi off` to return. A power cycle always returns to Bluetooth audio.
  - The page is sent gzipped (3.9 KB); after editing `tools/web_page.html` run `python3 tools/build_web_page.py`.
  - Verified in the car: page loads fully in Wi-Fi mode (3933 of 3933 bytes, ~105 KB heap left), heap before Wi-Fi 178 KB / after 122 KB, page button returns to audio.
  - R/T (the button next to the cruise control) sends `50 04 C8 3B 40` on press and `3B 00` on release, no hold frame, so the hold is timed (1.5 s in `WebControl.ino`). The voice button (`80` press, `90` hold, `A0` release) keeps play/pause only.
  - Still to check: switching with the real R/T hold in both directions, the nav screen text "WIFI MODE - HOLD R/T TO EXIT", seat buttons on the phone (tap = one step, hold = repeat, scrolling must not move the seat).
  - Later: automatic Wi-Fi mode at ignition off (needs power hold first).
- Drive-test auto-unlock again with the final build.
- [hardware] Decoupling and grounding to remove the "bip" from TX bursts.

## Your list
- **Unlock trunk and hazards from the web page** [scan]
  - Hazards: the turn-signal lamp bits (byte 2, 0x80 + 0x40) already flash both sides; the real hazard function with the cluster indicator may be a different job.
  - Trunk: BlueBus has no E39 trunk job, and the E46 ids (`02`, `05`, `46`, `95`) are blocked. Needs a careful, single-step search with the trunk area clear.
- **Comfort turn signals** (one tap = 3 flashes) [research]
  - First check whether the LCM can be coded for it; that needs no ESP.
  - Otherwise: detect the stalk tap from the LCM/IKE frames and send the turn-signal bit 3 times.
- **Mirrors: check again** [research]
  - Job `15` params `0A`-`12` only clicked the headrest; job `16` did nothing; idents `51`, `72`, `73`, `9B` silent. Try `DA`, other GM jobs, and the mirror switch wiring.
- **Comfort exit: move the seat back when the key is taken out** [ready]
  - Trigger: `44 05 BF 74 00 FF` (key out) or ignition off. Pulse `back` N times.
  - Move forward again on key in. Need to measure how many pulses give a good position (no position feedback).
- **Welcome, goodbye, follow-me-home lights, by key fob** [scan]
  - Triggers: fob unlock = `00 04 BF 72 22`, fob lock = `00 04 BF 72 10`.
  - Welcome: all front lights in sequence. Goodbye: angel eyes flash twice. Follow-me-home: low beams for 1 minute (already built on ignition off; add the fob variant).
  - Missing lamps: angel eyes / driver side marker, high beams, rear driver lamps, brake lights.
  - Lamp scan so far (2026-10-28): one test at a time with `python3 tools/lamp_test.py <data byte> <mask hex> [sec]`, Igor says what lit, then the next. Byte 1 latches the hazards, switch them off by button.
    - Nothing: byte 0, byte 11, byte 2 bits 1-2, byte 7 bits 2/3/5, byte 3 bits 0-2 and 5-7.
    - Byte 3 bits 3 and 4: low beams + interior buttons + angel eyes (one "lights on" function).
    - Byte 1 (all bits): high beams ~5 s + hazards. Byte 4 (all): high beams + something red at the rear. Byte 6 bits 0-5: passenger angel eye. Byte 8 (all): low beams + buttons, flashing.
    - Next: single bits of byte 6 (0-5), byte 1, byte 4 (look at the rear to name the red lamp), byte 8.
- **BlueBus-like interface** [research]
  - Own menu on the nav screen (BlueBus `ui/bmbt.c`). The fork session started on this: text zones `A5 62`, `21 60/61/62` index writes, GT menu detection.
- **Custom 2.1" gauge integration** [hardware]
  - Separate round display fed with our bus data (second ESP32 over Wi-Fi/ESP-NOW/UART). Decide the data: speed, RPM, coolant, boost?, gear.
- **Cameras** [research]
  - Original goal: reverse and front camera switching and the nav video input. Need the video module (VID, `ED`) frames and the video input wiring.
- **Custom image on the navigation unit** [research]
  - Likely only possible through the video input, not via bus frames.
- **DDE readings and writing** [hardware]
  - The DDE is not on the I/K-Bus. It uses K-line/DS2 on the OBD connector. Needs a separate K-line transceiver. Reading first; writing is risky.
- **Custom gear gauge for the 8HP transmission** [hardware]
  - Not on the I/K-Bus either. Needs the transmission controller's CAN or its own data. Define what the swap provides.
- **Custom ECU flashes** [research]
  - Out of scope for this hardware and a high-risk job on its own. Needs the right tools, bench power and backups. Research only.

## Ideas to add
- Battery voltage and oil temperature: the LCM does not answer `3F 03 D0 0B`. Try other requests or the K-Bus.
- Custom gauge modes on the nav screen (switch with an MFL button).
- Fuel level as range only; no raw fuel value found.
- Re-use the E39 job map for unlock-only job (`43`/`42`) instead of the toggle (`14`).
- Seat "easy entry" memory slots (stored pulse counts).
- Passenger seat: electric, no memory. Job `15` params `0A`-`12` only click the driver headrest (`0D`); the GM job scan `00-77` only moved the driver seat. Probably not on the bus (motors on direct switches), so it would need relays/motor drivers: not planned.
