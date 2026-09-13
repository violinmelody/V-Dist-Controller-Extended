![V-Dist Controller](https://dl.thorinair.net/MLP/vdistcontroller_b.png "V-Dist Controller")

# V-Dist Controller - clock-synced multi-mode firmware

V-Dist Controller is a single-knob USB MIDI controller originally created as a hardware counterpart to Violin Melody's [V-Dist Classic](https://violinmelody.net/plugins/vdist/) single-knob VST distortion effect.

Much like the original VST and controller, the hardware uses a single knob and sends MIDI data back to the DAW or VST host. It can be mapped to V-Dist itself or to virtually any MIDI-mappable parameter in a DAW, plugin, instrument, or effect.

This version expands the original firmware with multiple operating modes. In addition to using the knob as a normal MIDI CC controller, it can generate tempo-synchronized LFOs, random modulation, rhythmic patterns, gates, sequences, and ramps. Clocked modes follow MIDI Clock from the DAW so modulation remains tied to the project tempo.

## Requirements

- [MIDIUSB library](https://github.com/arduino-libraries/MIDIUSB)
- [Arduino Micro](https://store.arduino.cc/arduino-micro) (ATmega32U4)

## Modes

Send **CC119** to the controller on MIDI channels 1-12 to select the mode. The controller then sends **CC69 on that same MIDI channel**.

| MIDI channel | Mode | Knob function |
|---|---|---|
| 1 | Direct | CC value 0-127 |
| 2 | Sine LFO | rhythmic division |
| 3 | Triangle LFO | rhythmic division |
| 4 | Saw up | rhythmic division |
| 5 | Saw down | rhythmic division |
| 6 | Square LFO | rhythmic division |
| 7 | Sample & Hold | interval between random values |
| 8 | Smooth Random | interval between random targets |
| 9 | Rhythmic Pattern | selects one of 8 patterns |
| 10 | Euclidean Gate | density: 1-16 hits per 16 steps |
| 11 | Step Sequencer | selects one of 8 value sequences |
| 12 | Ramp / Envelope | rhythmic cycle length |

Channels above use the normal human-readable MIDI numbering of 1-16. Internally, MIDIUSB represents channels as 0-15.

## DAW clock sync

Modes 2-12 require the DAW to send USB MIDI realtime/transport messages to the controller:

- MIDI Clock (`F8`) - 24 pulses per quarter note
- Start (`FA`) - resets phase and starts modulation
- Continue (`FB`) - resumes modulation
- Stop (`FC`) - freezes modulation
- Song Position Pointer (`F2`) - follows song-position jumps/locates in DAWs that send SPP

If MIDI Clock disappears for longer than `CLOCK_TIMEOUT_MS`, modulation freezes rather than silently switching to an unrelated internal tempo.

For the LFO, Sample & Hold, random and envelope modes, the knob selects cycle lengths from:

`8 bars, 4 bars, 2 bars, 1 bar, dotted half, half, dotted quarter, quarter, quarter-triplet, eighth, eighth-triplet, sixteenth, sixteenth-triplet, thirty-second`

The bar divisions assume 4/4. MIDI Clock provides timing pulses but does not carry time-signature metadata.

## DAW setup

1. Enable V-Dist Controller as a MIDI input and MIDI output device in the DAW.
2. Enable MIDI Clock / Sync output from the DAW to V-Dist Controller.
3. To choose a mode, send CC119 to V-Dist Controller on the corresponding MIDI channel. The CC119 value itself is ignored.
4. Map CC69 from that channel to the parameter you want to control.
5. Start the DAW transport when using a clocked mode.

### Ableton Live

Enable **Sync** for V-Dist Controller's MIDI **Output** port.

- In **Pattern** MIDI Clock mode, Live sends Start at a bar boundary and the firmware resets its modulation phase on Start.
- In **Song** MIDI Clock mode, Live can also send Song Position Pointer/Continue when the play position changes. The firmware handles SPP so clocked modulation can follow song-position changes rather than always restarting from zero.

Enable **Remote** for the controller's MIDI **Input** port when mapping its outgoing CC69 to Live parameters.

### FL Studio

In MIDI Settings, select V-Dist Controller under **Output**, enable **Send master sync**, select **MIDI clock**, and enable **Options > Enable MIDI master sync**. Enable V-Dist Controller as an **Input** as well so its outgoing CC69 can be linked to FL Studio/plugin parameters.

FL Studio's Pattern/Song playback selector does not require a different protocol in the firmware. As long as FL Studio is sending MIDI Clock and transport messages to the controller, the clocked modes use those messages as their timing source.

### Transport and locating

Start resets the modulation phase to zero. Continue resumes the held position. Song Position Pointer relocates the internal clock to the DAW-provided song position. DAWs can differ in the exact transport/location messages they emit, so absolute song-position following is available when the host supplies SPP; tempo synchronization itself only requires MIDI Clock (`F8`).

## Configuration

Edit `Configuration.h` to change the MIDI CC numbers, ADC averaging, clock timeout, output update interval, or default mode.

## Changing the USB controller name

By default, an Arduino Micro normally appears in the operating system and DAW under its standard Arduino USB product name. The original V-Dist Controller project renamed this so the device appears as **V-Dist Controller**.

On a typical Windows Arduino IDE installation using the Arduino AVR core:

1. Navigate to:
   `C:\Users\<Your User>\AppData\Local\Arduino15\packages\arduino\hardware\avr\<Version Number>\`
2. Open `boards.txt` for editing.
3. Find:
   `micro.name=Arduino/Genuino Micro`
4. Below it, find:
   `micro.build.usb_product="Arduino Micro"`
5. Change the text inside the quotes to whatever custom name you want. For example:
   `micro.build.usb_product="V-Dist Controller"`
6. Save `boards.txt` and restart the Arduino IDE.
7. Compile and flash the firmware to the Arduino Micro.

You can replace `V-Dist Controller` with another custom name if desired.

**Important:** this modifies the Arduino Micro definition in that installed AVR core. You may want to restore the original `"Arduino Micro"` name after flashing V-Dist Controller; otherwise other Arduino Micro projects compiled with the same core can inherit the custom USB product name. Updating/reinstalling the Arduino AVR core may also overwrite the change, in which case it needs to be applied again before rebuilding the firmware.

## About the original project

The original controller was intentionally minimal: one physical knob controlling one MIDI parameter. This firmware keeps that simple hardware concept while allowing the same knob to act as either a direct controller or a DAW-synchronized modulation source without adding switches, displays, or additional controls.
