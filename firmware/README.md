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
2. **Calibrate** (`main.c`): for 3 seconds a white "scanning" animation runs
   on the wings while the firmware records the ambient noise spectrum.  Keep
   the room quiet during this step.
3. **Light show** (`main.c`, `leds.c`): forever, sample the mic at a nominal
   10 kHz, apply a Blackman window and FFT, subtract the calibration
   spectrum, apply a peak-and-decay filter, and send the bins to the LEDs.

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
| Microphone (ADC1 channel 4) | 4    |
| WS2812B data                | 3    |
| Digipot CS                  | 18   |
| Digipot MOSI                | 10   |
| Digipot CLK                 | 19   |
| LED-count jumpers           | 5, 6, 7 |

## Source layout

| File                  | Purpose |
|-----------------------|---------|
| `main.c`              | Startup, calibration and the main sample → FFT → LED loop |
| `config.c/h`          | Pin assignments and jumper-based LED count |
| `mic.c/h`             | ADC one-shot reads of the microphone, with ADC calibration |
| `fft.c/h`             | FFT, windowing and magnitude (C port of arduinoFFT, GPL-3.0) |
| `leds.c/h`            | WS2812B output over RMT, startup scanning animation |
| `led_strip_encoder.c/h` | RMT encoder for WS2812B (from the ESP-IDF examples) |
| `digipot.c/h`         | SPI driver for the MCP41050 digital potentiometer |

The MCP41050 driver is initialised at boot, but `main.c` does not set a
wiper value yet.

## Logs

Useful messages appear in the serial monitor at boot:

- `config`: which jumpers are closed, the LED count and the FFT size
- `main`: calibration start and finish, followed by an ASCII plot of the
  calibration spectrum
