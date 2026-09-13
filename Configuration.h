/* Output MIDI CC. */
#define MIDI_CC 69

/* Incoming CC used to select a mode. Send this CC on MIDI channels 1-12.
   The controller will then transmit MIDI_CC on that same channel. */
#define MODE_SELECT_CC 119

/* Number of ADC samples used for knob smoothing. */
#define AVERAGE_COUNT 20

/* Minimum averaged ADC movement before accepting a new knob position. */
#define MIN_CHANGE 0.5f

/* CC update rate for continuously changing modes. */
#define OUTPUT_INTERVAL_MS 10

/* If MIDI Clock disappears for this long, clocked modulation freezes. */
#define CLOCK_TIMEOUT_MS 750

/* Default mode after power-up: 0 = MIDI channel 1 / Direct. */
#define DEFAULT_MODE 0
