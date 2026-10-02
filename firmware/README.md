# Rainbow Music Pegasus Firmware

ESP-IDF firmware for the Rainbow Music Pegasus board.  It samples the
microphone, runs an FFT, and drives the WS2812B LEDs in the wings with the
result.

## Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/)
  v5.x (the project was created with v5.5.1)
- Target: ESP32-C3 only.  Other targets fail to build: the original ESP32
  can't read the mic's ADC in DMA mode, and other models are untested.
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

## Unit tests

The unit tests in [`test/`](test/) run on your computer, not the board, so
they need only a C compiler and CMake 3.20 or later, not ESP-IDF or hardware.
They use the [Unity](https://github.com/ThrowTheSwitch/Unity) test framework,
which CMake downloads the first time.

```sh
cd firmware
cmake -S test -B test/build
cmake --build test/build
ctest --test-dir test/build --output-on-failure
```

Each `test/test_<module>.c` tests `main/<module>.c`.  The ESP-IDF calls the
firmware makes (GPIO, SPI, ADC, RMT, timer) go to the fakes in `test/fakes/`
instead.  A test uses them to set what the hardware reports, such as jumper
levels, ADC samples or the time, and to check what the firmware sent, such as
digipot wiper codes or LED bytes.  Run one test program, e.g.
`test/build/test_mic`, to see each of its tests by name.

The tests are built with AddressSanitizer and UndefinedBehaviorSanitizer, so
an out-of-bounds access fails a test instead of silently corrupting memory as
it would on the ESP32-C3.  Pass `-DSANITIZE=OFF` to CMake if your compiler
doesn't support them.

## How it works

1. **Configure** (`config.c`): read jumpers JP5–JP7 to decide how many LEDs
   are fitted, and pick the FFT size from that.
2. **Battery** (`battery.c`): take a first battery reading while the LEDs
   are still dark.
3. **Settle** (`main.c`): for 3 seconds a white "scanning" animation runs
   on the wings while the mic sensitivity adjusts to the room.
4. **Light show** (`main.c`, `leds.c`): forever, read a frame of mic samples,
   apply a Blackman window and FFT, adjust the mic sensitivity, subtract a
   fixed noise floor, apply a peak-and-decay filter, and send the bins to the
   LEDs.  Every 10 seconds, check the battery.

The ADC samples the mic at 10 kHz in continuous (DMA) mode, so the sample
timing is set by hardware, not software delays, and other tasks such Bluetooth
can't disturb it.  The driver keeps only the newest frame: if the FFT and LED
update fall behind, older frames are dropped, so the lights never lag the sound
by more than one frame (6.4 ms for 64 samples, 25.6 ms for 256).

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

| Function                    | GPIO | Direction |
|-----------------------------|------|-----------|
| Battery (ADC1 channel 0)    | 0    | Input     |
| Button                      | 1    | Input     |
| WS2812B data                | 3    | Output    |
| Microphone (ADC1 channel 4) | 4    | Input     |
| LED-count jumpers           | 5, 6, 7 | Input  |
| ESP32 Bootloader mode       | 9    | Input     |
| Digipot MOSI                | 10   | Output    |
| Digipot CS                  | 18   | Output    |
| Digipot CLK                 | 19   | Output    |
| Serial RXD / Code Upload    | 20   | Input     |
| Serial TXD / Activity Log   | 21   | Output    |    

## Source layout

| File                  | Purpose |
|-----------------------|---------|
| `main.c`              | Startup and the main sample → FFT → LED loop |
| `config.c/h`          | Pin assignments and jumper-based LED count |
| `mic.c/h`             | Continuous (DMA) sampling of the microphone, with ADC calibration, and automatic mic sensitivity |
| `battery.c/h`         | Battery voltage readings, smoothing, and the low-battery decision |
| `fft.c/h`             | FFT, windowing and magnitude (C port of arduinoFFT, GPL-3.0) |
| `leds.c/h`            | WS2812B output over RMT, startup scanning animation, low-battery blink |
| `led_strip_encoder.c/h` | RMT encoder for WS2812B (from the ESP-IDF examples) |
| `digipot.c/h`         | SPI driver for the MCP41050 digital potentiometer |

## Mic sensitivity

The MCP41050's B–W resistance is the feedback resistor of the second op-amp
stage (U2B), so the preamp gain is proportional to it.  Wiper code 00h is at
terminal B, so a higher wiper code means higher gain.  `mic.c` uses the
wiper code (0 to 255) directly as the sensitivity.

After every frame's FFT, `main.c` looks at the loudest bin the LEDs show and
tells `mic_sensitivity_update()` whether the frame was too loud or too quiet:

- If the brightest LED stays at full brightness, or the samples keep clipping
  near the ADC rails, for 50 ms, the sensitivity drops by a quarter (about
  -2.5 dB).
- If the loudest bin stays under a quarter of full brightness for 200 ms, the
  sensitivity rises by an eighth (about +1 dB).

The steps repeat while the condition lasts, so after moving to a louder or
quieter room the lights recover within a few seconds.

Each FFT bin is converted to millivolts at the ADC, so the LEDs behave the same
whatever the frame size.  The brightness range is set by `FFT_NOISE_FLOOR_MV`
and `FFT_FULL_SCALE_MV` in `main.c`.  Raising full scale makes the sensitivity
settle higher, using more of the ADC's range, as long as the signal doesn't
clip.  The clipping limits and hold times are `#define`s at the top of `mic.c`.

## Battery level

R12 and R13 (100k each) halve the battery voltage at GPIO 0, so a full 4.2 V
battery reads 2.1 V.  `battery.c` averages 16 oneshot ADC readings and
doubles the result.

The mic and the battery are both on ADC1, which can sample continuously or
take oneshot readings but not both at once.  So `main.c` takes the first
reading at power-on, before the mic starts.  After that, every 10 seconds it
pauses the mic (`mic_pause()`), reads the battery and resumes the mic
(`mic_resume()`).  This delays the next frame by at most a frame (25.6 ms).

The voltage dips with the music as the LEDs draw current, so readings are
smoothed.  The smoothed voltage moves about two thirds of the way to a new
reading over 30 seconds.  The first reading is taken as is, because the LEDs
are still dark.  The battery is low once the smoothed voltage is under
3.6 V.  It stays low until the voltage is back over 3.7 V, so the warning
doesn't flicker on and off around 3.6 V.  The LEDs and ESP32-C3 run from a
3.3 V regulator, which starts to drop out with the battery under about 3.4 V.

While the battery is low, the first LED in each wing (the first pair on the
strip, nearest the ESP32) blinks red for a quarter of a second every 3
seconds.  It shows the music the rest of the time, and the other LEDs aren't
affected.  The light
show can't make both LEDs of a pair pure red, so the blink can't be mistaken
for music.

What GPIO 0 measures depends on the board:

- **v1.1**: the divider is wired straight to the battery, so it always reads
  the battery, even while USB charges it.  The warning stops once charging
  brings the battery back over 3.7 V.
- **v1.2**: the divider is after the power switch, so it doesn't drain the
  battery while the switch is off.  While USB is plugged in it reads the USB
  supply, about 4.6 V after D25, instead of the battery.  The warning stops at
  the next check after USB is plugged in.

The thresholds, smoothing time and number of samples are `#define`s at the
top of `battery.c`.  The check interval is in `main.c`, and the blink's
timing and brightness are in `leds.c`.

## Logs

Useful messages appear in the serial monitor:

- `config`: which jumpers are closed, the LED count and the FFT size
- `battery`: each reading, e.g. `3712 mV, smoothed 3698 mV`, and when the
  battery becomes low or stops being low
- `main`: start and finish of the 3-second settling period
- `mic`: each sensitivity change, e.g. `too quiet, sensitivity 128 -> 144`
