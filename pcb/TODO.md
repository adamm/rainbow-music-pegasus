# PCB TODO (v1.3)

Ideas from a design review of v1.2 (October 2026).  v1.2 is already ordered,
so these are for the next revision.  Items are roughly in priority order.

## Power and LEDs

- [ ] **Move all LEDs (onboard and external) to a 5 V boost rail.**  Today the
      24 WS2812Bs, the ESP32, the op-amp and the mic bias all share U3 (500 mA).
  - LED current pulses on 3.3 V reach the mic through R8 at about 1:1, so the
    FFT partly "hears" the LEDs.
  - Worst case (all LEDs full white) is well over 1 A.
  - On USB, U3 drops about 1.3 V; at 400 mA that's about 0.5 W in a SOT-23,
    close to thermal shutdown.
  - WS2812Bs are rated for 3.5 V minimum, so blue and green are dim at 3.3 V.
- [ ] **The LED rail must turn fully off with SW1.**  An idle WS2812B draws
      roughly 0.5–1 mA, so 24 of them would draw 12–24 mA with the switch off
      (a 1200 mAh cell is flat in a few days).  SW1 only switches U3, so the
      battery/USB rail (D25's cathode) is always live.
  - Pick a boost with *true load disconnect* in shutdown.  Many plain boost
    chips still pass the battery voltage through when disabled.
  - Drive its enable from SW_COM or from a GPIO (a GPIO also lets firmware
    cut LED power in sleep).  Otherwise add a P-FET or load switch ahead of it.
- [ ] **Keep U3 fed from the battery/USB rail, not the 5 V boost output**, so
      the ESP32 and audio stay away from the boost's switching noise.
- [ ] **Size the boost for the total LED load.**  From a 3.0 V battery, 2 A at
      5 V draws close to 4 A from the cell.  That's above the JST-PH's roughly
      2 A rating and beyond many small LiPos.  Add a total brightness cap in
      firmware.
- [ ] Move the 220 µF bulk caps (C9, C10) to the LED rail.  With the LEDs gone,
      3.3 V can use ceramics.

## Audio front end

- [ ] **Filter the mic bias:** add an RC filter (e.g. 1k + 10 µF) in front of R8.
- [ ] **Quieter mid-rail reference:** R5/R6/C6 corner at about 14 Hz.  Use
      100k/100k with 4.7–10 µF.
- [ ] **Re-centre the bias at about 1.25 V** (e.g. R5 = 16k, R6 = 10k).  At
      12 dB attenuation the ADC reads up to about 2.5 V, so 1.65 V leaves
      +0.85 V against −1.55 V of headroom, and positive peaks clip first (see
      `firmware/main/mic.c`).  1.25 V gives symmetric swing and roughly 2.5×
      the range before clipping.  Update `MIC_CLIP_*` in firmware to match.
- [ ] Optional: lower the anti-alias corner (R17/C41 at about 4.8 kHz, R9/C38
      at about 10.6 kHz).  The display tops out at 2.3 kHz, so cymbals and
      hi-hats can alias into the top bins.

## MCU

- [ ] **Program over USB-C.**  Use a 16-pin USB-C receptacle and wire D+/D−
      to GPIO19/18 (the C3's built-in USB serial/JTAG).  Move the digipot's CS
      and CLK to GPIO20/21 and drop the J2 UART header.  Flashing and logs then
      go over the charging cable.
- [ ] **EN reset delay:** C1 0.1 µF → 1 µF (Espressif recommends 10k + 1 µF).

## Battery and charging

- [ ] **Measure off-state current** with a meter in series with the battery.
      With SW1 off, Q1 stays on, and D25's reverse leakage flows into VBUS and
      through R14.  D25 is a 5 A Schottky, which leaks more than needed.  If
      it's tens of µA, use a small low-leakage diode or an ideal diode
      (e.g. LM66100).

## Layout

- [ ] **Ground plane:** the back copper is the only ground plane in the logic
      area, and about 43% of the non-ground tracks there are routed on it,
      which cuts it into strips.  There's also about 420 mm of thin 0.25 mm
      ground tracks.  Either pour ground on the front with plenty of stitching
      vias and keep the back mostly unbroken, or go to 4 layers (only a few
      dollars more at this size).
- [ ] **Load the fab's design rules into Board Setup.**  Minimum clearance,
      track width and copper-to-edge are all 0, so DRC passes trivially.  With
      typical fab limits there are 137 violations, almost all LED-strip tracks
      within 0.003 mm of the snap-off holes.
- [ ] **Snap-off holes:** make them drilled NPTH holes instead of 0.7 mm circles
      on Edge.Cuts (smaller than most router bits), with at least 0.25 mm
      clearance to copper.
- [ ] **Thermal reliefs:** every zone connects pads solidly.  Use thermal
      reliefs for pads (easier rework, fewer parts standing up on end during
      reflow) and keep solid connections for vias.
- [ ] Keep the boost converter, its inductor and its input-cap loop away from
      MK1 and U2.

## Housekeeping

- [ ] Get ERC to zero (3 errors and about 425 warnings, mostly off-grid wires
      and outdated library symbols) so new problems stand out.  Add a PWR_FLAG
      on the D25-cathode net, remove the one on +BATT, and fix D26's pin types.
- [ ] `Makefile`: the PCB PDF target lists `In2.Cu`, which doesn't exist on
      this 2-layer board.

## Bigger changes to consider

- [ ] **Digital MEMS mic** (PDM or I²S, e.g. ICS-43434) instead of the electret,
      OPA2322, MCP41050 and about a dozen passives.  It avoids the ADC's noise
      and non-linearity, and the C3 supports PDM input.  The downside is
      rewriting `mic.c` and the auto-gain code.
- [ ] **Newer charger** with built-in power sharing, a near-zero-drain "ship
      mode" and a thermistor input for safe charge temperatures (e.g. BQ25180).
      Worth it for a LiPo inside a toy.
