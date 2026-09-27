# Rainbow Music Pegasus Firmware

ESP-IDF firmware for the Rainbow Music Pegasus board.  It samples the
microphone, runs an FFT, and drives the WS2812B LEDs in the wings with the
result.

## Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/)
  v5.x (the project was created with v5.5.1)
- Target: ESP32-C3 (an ESP32 pin mapping also exists in `main/config.h`)
- [esp-dsp](https://components.espressif.com/components/espressif/esp-dsp),
  fetched automatically by the IDF Component Manager

## Building and flashing

```sh
. $IDF_PATH/export.sh          # load the ESP-IDF environment
cd firmware
idf.py set-target esp32c3      # only needed the first time
idf.py build
idf.py -p <PORT> flash monitor
```

`<PORT>` is the board's USB serial device, e.g. `/dev/cu.usbmodem*` on macOS
or `/dev/ttyACM0` on Linux.  Exit the monitor with `Ctrl-]`.

## How it works

1. **Configure** (`config.c`): read jumpers JP5–JP7 to decide how many LEDs
   are fitted, and pick the FFT size from that.
2. **Settle** (`main.c`): for 3 seconds a white "scanning" animation runs
   on the wings while the mic sensitivity adjusts to the room.
3. **Light show** (`main.c`, `leds.c`): forever, sample the mic at a nominal
   10 kHz, adjust the mic sensitivity, apply a Blackman window and FFT,
   subtract a fixed noise floor, apply a peak-and-decay filter, and send the
   bins to the LEDs.

LEDs are driven in left/right pairs.  Each group of three FFT bins becomes
one colour; the right wing gets it as GRB and the left wing with the channels
rotated, so the two wings are equally bright but differently coloured.

## LED count jumpers

The board supports 10 to 24 LEDs.  Closing a jumper pulls its GPIO low and
adds LEDs on top of the base 10:

| Jumper | GPIO | Adds |
|--------|------|------|
| JP7    | 7    | +2   |
| JP6    | 6    | +4   |
| JP5    | 5    | +8   |

The total is capped at 24.  The FFT size follows the LED count: 64 samples
for up to 10 LEDs, 128 for up to 16, and 256 above that.

## Pin map (ESP32-C3)

All pins are defined in [`main/config.h`](main/config.h).

| Function                    | GPIO |
|-----------------------------|------|
| Button                      | 1    |
| WS2812B data                | 3    |
| Microphone (ADC1 channel 4) | 4    |
| LED-count jumpers           | 5, 6, 7 |
| Digipot MOSI                | 10   |
| Digipot CS                  | 18   |
| Digipot CLK                 | 19   |

## Source layout

| File                  | Purpose |
|-----------------------|---------|
| `main.c`              | Startup and the main sample → FFT → LED loop |
| `config.c/h`          | Pin assignments and jumper-based LED count |
| `mic.c/h`             | ADC one-shot reads of the microphone, with ADC calibration, and automatic mic sensitivity |
| `fft.c/h`             | FFT, windowing and magnitude (C port of arduinoFFT, GPL-3.0) |
| `leds.c/h`            | WS2812B output over RMT, startup scanning animation |
| `led_strip_encoder.c/h` | RMT encoder for WS2812B (from the ESP-IDF examples) |
| `digipot.c/h`         | SPI driver for the MCP41050 digital potentiometer |

## Mic sensitivity

The MCP41050's B–W resistance is the feedback resistor of the second op-amp
stage (U2B), so the preamp gain is proportional to it.  Wiper code 00h is at
terminal B, so a **higher wiper code means higher gain**.  `mic.c` uses the
wiper code (0 to 255) directly as the sensitivity.

After every frame, `mic_sensitivity_update()` checks the lowest and highest
voltage read:

- If frames keep clipping near the ADC rails for 50 ms, the sensitivity
  drops by a quarter (about -2.5 dB).
- If frames stay under 50 mV peak-to-peak for 200 ms, the sensitivity rises
  by an eighth (about +1 dB).

The steps repeat while the condition lasts, so after moving to a louder or
quieter room the lights recover within a few seconds.  The thresholds and
hold times are `#define`s at the top of `mic.c`.

## Logs

Useful messages appear in the serial monitor:

- `config`: which jumpers are closed, the LED count and the FFT size
- `main`: start and finish of the 3-second settling period
- `mic`: each sensitivity change, e.g. `too quiet, sensitivity 128 -> 144`
