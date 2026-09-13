#include <MIDIUSB.h>
#include <math.h>
#include "Configuration.h"

#define PIN_POT A5
#define MODE_COUNT 12
#define CLOCK_PPQN 24.0f

/*
  Modes are selected by sending CC119 (MODE_SELECT_CC) TO the controller on:
    Ch 1  Direct knob
    Ch 2  Clock-synced sine LFO
    Ch 3  Clock-synced triangle LFO
    Ch 4  Clock-synced saw up
    Ch 5  Clock-synced saw down
    Ch 6  Clock-synced square
    Ch 7  Clock-synced sample & hold
    Ch 8  Clock-synced smooth random
    Ch 9  Rhythmic pattern
    Ch 10 Euclidean gate
    Ch 11 Step sequencer
    Ch 12 Ramp / envelope

  MIDI Clock (F8) is 24 pulses per quarter note. Start (FA) resets phase,
  Continue (FB) resumes it, and Stop (FC) freezes clocked modulation.
*/

enum Mode {
  DIRECT = 0, SINE_LFO, TRIANGLE_LFO, SAW_UP, SAW_DOWN, SQUARE_LFO,
  SAMPLE_HOLD, SMOOTH_RANDOM, RHYTHMIC_PATTERN, EUCLIDEAN_GATE,
  STEP_SEQUENCER, RAMP_ENVELOPE
};

/* Musical cycle lengths selected by the knob, expressed in quarter notes. */
const float divisions[] = {
  32.0f, 16.0f, 8.0f, 4.0f, 3.0f, 2.0f, 1.5f, 1.0f,
  2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f
};
const uint8_t DIVISION_COUNT = sizeof(divisions) / sizeof(divisions[0]);

Mode currentMode = (Mode)DEFAULT_MODE;
uint8_t outputChannel = DEFAULT_MODE; // zero-based MIDI channel

float accumulator = 0.0f;
uint16_t averageCount = 0;
float knobRaw = 0.0f;
uint8_t knobCC = 0;

bool transportRunning = false;
bool haveClock = false;
uint32_t clockTicks = 0;
bool anchorNextClock = true; // first F8 after Start/Continue/SPP represents the current position
unsigned long lastClockMicros = 0;
unsigned long clockIntervalMicros = 20833; // roughly 120 BPM
unsigned long lastOutputMs = 0;
uint8_t lastSentValue = 255;

uint32_t randomStep = 0xFFFFFFFFUL;
uint8_t randomValue = 64;
uint8_t previousRandomValue = 64;
uint8_t nextRandomValue = 64;

void controlChange(byte channel, byte control, byte value) {
  midiEventPacket_t event = {0x0B, (byte)(0xB0 | (channel & 0x0F)), control, value};
  MidiUSB.sendMIDI(event);
  MidiUSB.flush();
}

void selectMode(uint8_t zeroBasedChannel) {
  if (zeroBasedChannel >= MODE_COUNT) return;
  currentMode = (Mode)zeroBasedChannel;
  outputChannel = zeroBasedChannel;
  lastSentValue = 255;
  randomStep = 0xFFFFFFFFUL;
}

void resetClockPhase() {
  clockTicks = 0;
  anchorNextClock = true;
  lastClockMicros = 0;
  haveClock = false;
  randomStep = 0xFFFFFFFFUL;
}

void setSongPosition(uint16_t midiBeats) {
  // MIDI Song Position Pointer is counted in 16th notes (6 MIDI clocks each).
  clockTicks = (uint32_t)midiBeats * 6UL;
  anchorNextClock = true;
  lastClockMicros = 0;
  haveClock = false;
  randomStep = 0xFFFFFFFFUL;
}

void readMidi() {
  midiEventPacket_t rx;
  do {
    rx = MidiUSB.read();
    if (!rx.header) break;

    byte status = rx.byte1;

    if (status == 0xF8) { // Timing Clock
      unsigned long now = micros();
      if (lastClockMicros != 0) {
        unsigned long measured = now - lastClockMicros;
        // Reject obviously invalid gaps and lightly smooth clock jitter.
        if (measured > 1000UL && measured < 250000UL)
          clockIntervalMicros = (clockIntervalMicros * 7UL + measured) / 8UL;
      }
      lastClockMicros = now;
      haveClock = true;
      if (transportRunning) {
        if (anchorNextClock) anchorNextClock = false;
        else clockTicks++;
      }
    } else if (status == 0xFA) { // Start: absolute position zero
      resetClockPhase();
      transportRunning = true;
    } else if (status == 0xFB) { // Continue: resume current/SPP position
      transportRunning = true;
      anchorNextClock = true;
      lastClockMicros = 0;
      haveClock = false;
    } else if (status == 0xFC) { // Stop
      transportRunning = false;
      anchorNextClock = true;
    } else if (status == 0xF2) { // Song Position Pointer (14-bit, in 16th notes)
      uint16_t spp = (uint16_t)(rx.byte2 & 0x7F) | ((uint16_t)(rx.byte3 & 0x7F) << 7);
      setSongPosition(spp);
    } else if ((status & 0xF0) == 0xB0) { // Control Change
      uint8_t channel = status & 0x0F;
      if (rx.byte2 == MODE_SELECT_CC && channel < MODE_COUNT)
        selectMode(channel);
    }
  } while (rx.header);

  if (haveClock && (micros() - lastClockMicros) > (CLOCK_TIMEOUT_MS * 1000UL))
    haveClock = false;
}

void readKnob() {
  accumulator += analogRead(PIN_POT);
  averageCount++;

  if (averageCount < AVERAGE_COUNT) return;

  float newValue = accumulator / averageCount;
  accumulator = 0.0f;
  averageCount = 0;

  if (fabs(newValue - knobRaw) >= MIN_CHANGE) {
    knobRaw = newValue;
    int mapped = (int)(newValue * 128.0f / 1024.0f);
    knobCC = constrain(mapped, 0, 127);
  }
}

uint8_t divisionIndex() {
  return min((uint8_t)(knobCC * DIVISION_COUNT / 128), (uint8_t)(DIVISION_COUNT - 1));
}

/* Smooth phase between MIDI Clock pulses while remaining locked to the clock. */
float quarterPosition() {
  float ticks = (float)clockTicks;
  if (haveClock && transportRunning && lastClockMicros != 0 && clockIntervalMicros > 0) {
    float fraction = (float)(micros() - lastClockMicros) / (float)clockIntervalMicros;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    ticks += fraction;
  }
  return ticks / CLOCK_PPQN;
}

float cyclePhase(float quarterNotes) {
  float p = fmod(quarterPosition(), quarterNotes) / quarterNotes;
  if (p < 0.0f) p += 1.0f;
  return p;
}

uint8_t unitToCC(float x) {
  x = constrain(x, 0.0f, 1.0f);
  return (uint8_t)(x * 127.0f + 0.5f);
}

uint8_t clockedWave(Mode mode) {
  float phase = cyclePhase(divisions[divisionIndex()]);
  float value = 0.0f;

  switch (mode) {
    case SINE_LFO:     value = 0.5f - 0.5f * cos(2.0f * PI * phase); break;
    case TRIANGLE_LFO: value = (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f; break;
    case SAW_UP:       value = phase; break;
    case SAW_DOWN:     value = 1.0f - phase; break;
    case SQUARE_LFO:   value = (phase < 0.5f) ? 0.0f : 1.0f; break;
    default: break;
  }
  return unitToCC(value);
}

uint32_t currentCycleNumber(float lengthQn) {
  return (uint32_t)floor(quarterPosition() / lengthQn);
}

uint8_t sampleHold() {
  float lengthQn = divisions[divisionIndex()];
  uint32_t step = currentCycleNumber(lengthQn);
  if (step != randomStep) {
    randomStep = step;
    randomValue = random(0, 128);
  }
  return randomValue;
}

uint8_t smoothRandom() {
  float lengthQn = divisions[divisionIndex()];
  uint32_t step = currentCycleNumber(lengthQn);
  if (step != randomStep) {
    randomStep = step;
    previousRandomValue = nextRandomValue;
    nextRandomValue = random(0, 128);
  }
  float phase = cyclePhase(lengthQn);
  // Smoothstep interpolation avoids hard corners.
  float t = phase * phase * (3.0f - 2.0f * phase);
  return (uint8_t)(previousRandomValue + (nextRandomValue - previousRandomValue) * t);
}

uint8_t rhythmicPattern() {
  // Knob selects one of eight 16-step patterns; sequence advances on 16th notes.
  static const uint16_t patterns[8] = {
    0x8888, 0xAAAA, 0xF0F0, 0xE4E4, 0xD2D2, 0xC936, 0xB6D9, 0xFFFF
  };
  uint8_t pattern = min((uint8_t)(knobCC * 8 / 128), (uint8_t)7);
  uint8_t step = ((uint32_t)floor(quarterPosition() * 4.0f)) & 0x0F;
  return (patterns[pattern] & (1U << (15 - step))) ? 127 : 0;
}

uint8_t euclideanGate() {
  // One 16th-note cycle of 16 steps. Knob sets 1..16 evenly distributed hits.
  uint8_t hits = 1 + (uint16_t)knobCC * 15 / 127;
  uint8_t step = ((uint32_t)floor(quarterPosition() * 4.0f)) & 0x0F;
  return (((step * hits) % 16) < hits) ? 127 : 0;
}

uint8_t stepSequencer() {
  // Eight fixed musical shapes; knob chooses the sequence.
  static const uint8_t sequences[8][8] = {
    {0,18,36,54,72,90,108,127}, {127,108,90,72,54,36,18,0},
    {0,127,0,127,0,127,0,127}, {0,32,64,96,127,96,64,32},
    {32,96,48,112,16,80,64,127}, {0,64,127,64,0,64,127,64},
    {127,64,32,96,16,112,48,80}, {16,48,80,112,96,64,32,0}
  };
  uint8_t sequence = min((uint8_t)(knobCC * 8 / 128), (uint8_t)7);
  uint8_t step = ((uint32_t)floor(quarterPosition() * 2.0f)) & 0x07; // eighth notes
  return sequences[sequence][step];
}

uint8_t rampEnvelope() {
  return unitToCC(cyclePhase(divisions[divisionIndex()]));
}

void sendIfChanged(uint8_t value) {
  if (value == lastSentValue) return;
  lastSentValue = value;
  controlChange(outputChannel, MIDI_CC, value);
}

void updateController() {
  if (currentMode == DIRECT) {
    sendIfChanged(knobCC);
    return;
  }

  // Clocked modes intentionally freeze if no usable DAW clock is present.
  if (!haveClock || !transportRunning) return;

  unsigned long now = millis();
  if (now - lastOutputMs < OUTPUT_INTERVAL_MS) return;
  lastOutputMs = now;

  uint8_t value = 0;
  switch (currentMode) {
    case SINE_LFO:
    case TRIANGLE_LFO:
    case SAW_UP:
    case SAW_DOWN:
    case SQUARE_LFO:      value = clockedWave(currentMode); break;
    case SAMPLE_HOLD:     value = sampleHold(); break;
    case SMOOTH_RANDOM:   value = smoothRandom(); break;
    case RHYTHMIC_PATTERN:value = rhythmicPattern(); break;
    case EUCLIDEAN_GATE:  value = euclideanGate(); break;
    case STEP_SEQUENCER:  value = stepSequencer(); break;
    case RAMP_ENVELOPE:   value = rampEnvelope(); break;
    default: return;
  }
  sendIfChanged(value);
}

void setup() {
  randomSeed(analogRead(PIN_POT) ^ micros());
}

void loop() {
  readMidi();
  readKnob();
  updateController();
}
