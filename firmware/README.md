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
   are fitted, and pick the FFT size and sample rate from that.
2. **Battery** (`battery.c`): take a first battery reading while the LEDs
   are still dark.
3. **Settle** (`main.c`): for 3 seconds a white "scanning" animation runs
   on the wings while the mic sensitivity and the background floor adjust to
   the room.
4. **Light show** (`main.c`, `leds.c`): forever, read a frame of mic samples,
   apply a Blackman window and FFT, adjust the mic sensitivity, learn each
   bin's background floor and keep only the sound well above it, apply a
   peak-and-decay filter, and send the bins to the LEDs.  Every 10 seconds,
   check the battery.

The ADC samples the mic in continuous (DMA) mode, at a rate set by the LED
count (see [Frequency range](#frequency-range)), so the sample timing is set
by hardware, not software delays, and other tasks such Bluetooth can't disturb
it.  The driver keeps only the newest frame: if the FFT and LED update fall
behind, older frames are dropped, so the lights never lag the sound by more
than one frame (6.4 ms with 10 LEDs, up to 15.4 ms with 24).

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

## Frequency range

Every board shows the same range, from 0 Hz up to `CONFIG_LEDS_TOP_FREQ_HZ`
(2344 Hz) in [`main/config.h`](main/config.h).  It's shared equally between
the LED pairs, so more LEDs show it in finer steps.

Each pair shows three FFT bins, one per colour channel, so the LEDs show the
lowest LEDs * 3 / 2 bins.  A bin is the sample rate divided by the FFT size
wide, so `config_init()` picks the sample rate that makes those bins end at the
top frequency:

    sample rate = FFT size * top frequency / (LEDs * 3 / 2)

| LEDs | FFT size | Sample rate | Each pair shows | Frame   |
|------|----------|-------------|-----------------|---------|
| 10   | 64       | 10.0 kHz    | 469 Hz          | 6.4 ms  |
| 12   | 128      | 16.7 kHz    | 391 Hz          | 7.7 ms  |
| 14   | 128      | 14.3 kHz    | 335 Hz          | 9.0 ms  |
| 16   | 128      | 12.5 kHz    | 293 Hz          | 10.2 ms |
| 18   | 256      | 22.2 kHz    | 260 Hz          | 11.5 ms |
| 20   | 256      | 20.0 kHz    | 234 Hz          | 12.8 ms |
| 22   | 256      | 18.2 kHz    | 213 Hz          | 14.1 ms |
| 24   | 256      | 16.7 kHz    | 195 Hz          | 15.4 ms |

The sample rate is always more than four times the top frequency.  Sound
above half the sample rate folds back onto lower bins, and the mic's
anti-aliasing filters (R17 and C41 at 4.8 kHz, R9 and C38 at 10.6 kHz) cut
cymbals and other treble near 10 kHz by only about 10 dB.  A faster rate
folds back only higher sound, which the filters cut more.

Raise the top frequency to show more of the treble, or lower it to show more
detail in the bass.  Much above 4 kHz, the anti-aliasing filter dims the
highest LEDs.  The ADC can't sample faster than 83.3 kHz, which an 18-LED
board reaches at a top frequency of about 8.8 kHz.  The unit tests fail if any
board would need a rate outside the ADC's range.

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
| `config.c/h`          | Pin assignments, jumper-based LED count, FFT size, sample rate and top frequency |
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
whatever the frame size.  Bins under `FFT_NOISE_FLOOR_MV` in `main.c` are
always dark, and bins at `FFT_FULL_SCALE_MV` light at full brightness.
Raising full scale makes the sensitivity settle higher, using more of the
ADC's range, as long as the signal doesn't clip.  The clipping limits and hold
times are `#define`s at the top of `mic.c`.

## Background sound

To show music over a noisy room, such as a crowd talking, `main.c` learns the
level of each FFT bin's background sound, its floor, and lights a bin only
once it's 12 dB (4 times) over that.  The floor is learned all the time,
and follows a crowd as it grows or thins.

- While a bin is louder than its floor, the floor rises by 1 dB a second.
  While it's quieter, the floor falls by 3 dB a second.  So the floor settles
  where the bin is quieter than it a quarter of the time.
- A crowd never goes quiet, so the floor sits on it.  Beats are loud for much
  less than three quarters of the time, so the floor stays under them and
  they stand out.
- Noise in a bin jumps around from frame to frame, and gets 12 dB over its
  quietest quarter in 1 to 2% of frames.  So a steady crowd still lights an
  LED dimly now and then.
- Anything that doesn't change is learned too, so a note held 20 dB over the
  threshold fades out in about 20 seconds.  The light show follows beats and
  changes more than steady sound.
- A bin's brightness runs from its threshold up to full scale, so music over
  a noisy room can still reach full brightness.  Beats 12 dB over a crowd's
  average level show at about a quarter of full brightness.
- In a quiet room the floor falls until the threshold is `FFT_NOISE_FLOOR_MV`,
  and stops there, so hiss stays dark and a new crowd is learned from there.
- During the 3-second settling period the floor learns 15 times faster, so it
  can climb 45 dB and the light show starts with the room already learned.
- When the mic sensitivity changes, `mic_sensitivity_update()` returns the
  change in gain, and the floor is scaled by it to stay in step with the
  sound.

Music has to be louder than the crowd, at least on its beats, to show: with
one microphone, sound no louder than the crowd can't be told apart from it.
Someone talking right next to the pegasus isn't learned either, because
speech has pauses.

The rates, margin and speed-up are the `FLOOR_` `#define`s at the top of
`main.c`.

## Battery level

R12 and R13 (100k each) halve the battery voltage at GPIO 0, so a full 4.2 V
battery reads 2.1 V.  `battery.c` averages 16 oneshot ADC readings and
doubles the result.

The mic and the battery are both on ADC1, which can sample continuously or
take oneshot readings but not both at once.  So `main.c` takes the first
reading at power-on, before the mic starts.  After that, every 10 seconds it
pauses the mic (`mic_pause()`), reads the battery and resumes the mic
(`mic_resume()`).  This delays the next frame by at most a frame (15.4 ms).

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
- `main`: start and finish of the 3-second settling period, and each second,
  how many of the shown bins' background floors went up or down by more than
  0.5 dB, e.g. `adjusted the floor up on 3 bins and down on 1, of 15`.  Gain
  changes aren't counted, since `mic` logs those.  In a steady room most bins
  stay put; when a crowd grows or a note is held, they go up.
- `mic`: each sensitivity change, e.g. `too quiet, sensitivity 128 -> 144`
