// Rungler-32: custom firmware for the Moog Mother-32 (unofficial; not affiliated with Moog Music).
// Copyright (C) 2026 Dune Desormeaux (@Dessertplanet). SPDX-License-Identifier: GPL-3.0-or-later
// The shift register is inspired by Rob Hordijk's Rungler (Benjolin / Blippoo Box).
//
// Shift register (digital ASR, Rungler-style), up to 8 bits:
//  - Clocked by the rising edge of the combined gate: GATE input jack OR the internal clock (RUN/STOP button
//    toggles it; 0.25 Hz .. 1 kHz, exponential, on the TEMPO knob). While it is on and MIDI clock is
//    arriving, MIDI clock in 16th notes replaces it (MIDI transport is ignored).
//  - Each clock rotates the active bits one place (the last wraps to the first) and XORs the first bit with
//    the RUN, RESET and HOLD input jacks and a comparator: comparator input above the GLIDE knob. The
//    comparator input is the TEMPO CV jack (its whole range mapped onto the knob's travel), or internal
//    noise while the jack has been silent (within about +-120 mV) for 2 s or has never had a signal. Dead
//    bands at both ends of GLIDE lock the pattern. All bits start set.
//  - HOLD button: loop lock on/off. Locked, each clock only rotates the bits (no XOR), so the current pattern
//    loops unchanged; the seven non-selected octave LEDs light yellow.
//  - TEMPO LED: quantisation colour (green on, red off), flashing with the clock while the sequencer is active.
//  - White keys set the length (low C = 1 bit ... high C = 8 bits); the white-key LEDs show the active bits.
//  - KB output: the active bits as an L-bit number (first bit = least significant) scaled to 0 .. +2 V,
//    C-major quantised by default (SHIFT tapped on its own toggles it; TEMPO LED green = on, red = off),
//    transposed by the arrows in octaves (-4 .. +3, shown as one red octave LED).
//  - GATE output and envelope (honouring SUSTAIN): high while the last active bit ASR(L) is set (whatever is
//    clocking the register), while a MIDI note is held (see below) or while a panel key is held. A clock that
//    leaves ASR(L) set, or a new key press, retriggers the envelope. The GATE input jack only clocks. The HOLD
//    input jack also opens and triggers the envelope (not the gate output), besides feeding the XOR.
//  - While the sequencer is active (RUN/STOP on, or GATE jack edges in the last 2 s), key presses (length and gate) are queued to the next
//    clock; a key released before its clock still gives a 10 ms gate. Releases act at once.
//
// MIDI IN (any channel), e.g. a KeyStep: held notes (last-note priority) own KB as played (no quantisation or
// transpose), open the envelope and gate output (honouring SUSTAIN: legato with it on, retrigger with it
// off), velocity > 95 fires the accent, pitch bend +-2 semitones. After the last note-off KB holds that note until
// the next shift-register clock hands it back to the shift register.
//
// Delay: TEMPO CV jack (oversampled 4x and low-pass filtered by the framework) -> 24 kHz tape-style delay ->
// ASSIGN output (wet only; for dry + wet patch ASSIGN into EXT AUDIO and blend with the MIX knob).
//  - GLIDE knob: delay time, 0.33 ms to about 0.52 s, gliding toward the knob like tape (read speed 0.1x ..
//    1.9x), so turning it bends the pitch of what is playing. GLIDE is also the comparator threshold.
//  - Clock sync: while a clock (2.5 ms .. 4 s period) arrives on the RESET input jack (an edge within twice its
//    period + 0.25 s), the period is measured from exact edge times, averaged over the last 150 ms, so a clock
//    whose rate is modulated (e.g. by a slow LFO) gives chorus/vibrato, frozen or not; the
//    delay is that period times a fraction picked by GLIDE (1/4, 1/3, 3/8, 1/2, 2/3, 3/4, 1, 3/2, 2), halved
//    until it fits in 0.52 s. RESET still feeds the shift register's XOR as well.
//  - SHIFT + arrows: feedback, 25 levels (0 = single repeat ... 100%), shown on the octave LEDs while SHIFT is
//    held and for 1.5 s after a change: a green bar (1-8), then yellow (9-16) and red (17-24) rolling in from
//    the right; all red = 100% (the line then holds forever and new input piles up into saturation).
//    Build with -DDELAY_NO_FEEDBACK for a version without internal feedback.
//  - HOLD input jack high: freeze. Writing stops and the last delay-time's worth of sound loops, unchanged (no
//    re-encoding). GLIDE and the RESET clock sync still set the delay time: the loop is replayed so that one pass
//    lasts that time, which repitches it (speed = delay time at the freeze / current delay time, 1/4x .. 4x,
//    gliding). The seven non-selected octave LEDs flash green. Releasing HOLD resumes the delay where it stopped.
//
//  Storage: 4-bit IMA ADPCM (as in my MLRws Workshop Computer card), four samples per word, in L0 and
//  in the 1,328 words the bootloader's flash routines occupy after boot (.bootram; the framework copies
//  the routines back before a firmware update). The read head moves forward only, so one decoder
//  follows it sample by sample and no keyframes are needed. Each repeat is re-encoded, so at high
//  feedback the repeats get grainier (generation loss).
//
//  Speed: the writer collects four nibbles and stores a whole word every fourth sample; the reader keeps
//  its current word and takes one nibble per sample; the codec state stays in members with the step size
//  cached. The reader stays at least one word behind the writer (minimum delay 8 samples).
//  The whole per-sample path and its tables run from RAM (M32_RAMFUNC); the line gives up some L0 for it.
//
//  Input scale (measured 2026-11-12): 3.02 mV per count, 0 V at 32 counts. Samples are offset-corrected
//  (0 V = 0) and scaled to 16 bits for the codec.
#ifndef M32_TICK_HZ
#define M32_TICK_HZ 24000
#endif
#ifndef M32_OS
#define M32_OS 4
#endif
#include "Mother32.h"

#ifdef M32_PROFILE
extern "C" { volatile uint32_t m32_pfU1[3] = {0, 0, 0}, m32_pfU2[3] = {0, 0, 0}; }   // user timing points: read, write
#endif

// ---- IMA ADPCM tables -----------------------------------------------------------------------------------
static int16_t kImaStep[89] = {             // tables in RAM (no flash wait states), like the code below
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
    1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767
};
static int16_t kImaIndex[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

// ---- delay line: 4-bit samples, four per word (lowest nibble first), in two RAM areas ---------------------
static const uint16_t kWordsA = 0x530;                          // bootloader RAM, 0x0040-0x056F
#ifdef M32_PROFILE
static const uint16_t kWordsB = 1545;                           // L0 (less: the profiling counters need room)
#else
static const uint16_t kWordsB = 1805;                           // L0
#endif
static const uint16_t kN = (kWordsA + kWordsB) * 4;             // 12,532 samples = 0.52 s at 24 kHz
#pragma DATA_SECTION(".bootram")
uint16_t g_lineA[kWordsA];
uint16_t g_lineB[kWordsB];

static inline uint16_t &LineWord(uint16_t w) { return w < kWordsA ? g_lineA[w] : g_lineB[w - kWordsA]; }
static inline uint16_t Wrap(int32_t i) { if (i < 0) i += kN; else if (i >= kN) i -= kN; return (uint16_t)i; }

// ---- internal clock: 0.25 Hz .. 1 kHz, 12 octaves on the TEMPO knob ---------------------------------------
static const int32_t kExp2Q15[65] = {   // 2^(i/64) in 1/32768, for the clock rate
    32768, 33125, 33486, 33850, 34219, 34591, 34968, 35349,
    35734, 36123, 36516, 36914, 37316, 37722, 38133, 38548,
    38968, 39392, 39821, 40255, 40693, 41136, 41584, 42037,
    42495, 42958, 43425, 43898, 44376, 44859, 45348, 45842,
    46341, 46846, 47356, 47871, 48393, 48920, 49452, 49991,
    50535, 51085, 51642, 52204, 52773, 53347, 53928, 54515,
    55109, 55709, 56316, 56929, 57549, 58176, 58809, 59449,
    60097, 60751, 61413, 62081, 62757, 63441, 64132, 64830,
    65536
};
static const uint32_t kClockBaseInc = 44667;   // 0.25 Hz at 24,038 Hz: 0.25 / 24038.46 * 2^32

// ---- delay clock sync on the RESET input: GLIDE picks the delay as a fraction of the clock period ------------
static const int16_t kSyncSteps = 9;
static const uint16_t kSyncNum[kSyncSteps] = { 1, 1, 3, 1, 2, 3, 1, 3, 2 };
static const uint16_t kSyncDen[kSyncSteps] = { 4, 3, 8, 2, 3, 4, 1, 2, 1 };

// ---- shift register --------------------------------------------------------------------------------------
static const int16_t kWhiteKeys[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };   // semitones of the white keys, low C .. high C
static const int32_t kAsrSpanMv = 2000;                             // KB output span: 0 .. +2 V (two octaves)
static const int16_t kMajor[12] = { 0, 0, 2, 2, 4, 5, 5, 7, 7, 9, 9, 11 };   // semitone -> C major degree (round down)

#ifndef DELAY_NO_FEEDBACK
// feedback per level, in 1/4096 (4096 = 100%). Steps get finer toward the top, where the echo decay time
// (about 1 / (1 - feedback) repeats) changes fastest.
static const int16_t kFbLevels = 24;
static int32_t kFeedbackQ12[kFbLevels + 1] = {
    0,
    410, 819, 1229, 1638, 2048, 2458, 2867, 3195,       // green  1-8:  10 .. 78%
    3359, 3482, 3604, 3686, 3768, 3850, 3891, 3932,     // yellow 9-16: 82 .. 96%
    3973, 3994, 4014, 4035, 4055, 4067, 4083, 4096      // red   17-24: 97 .. 100%
};
#endif

class Rungler32 : public Mother32 {
public:
    Rungler32()
        : fbLevel_(0), w_(0), wAcc_(0), encPred_(0), encIdx_(0), encStep_(kImaStep[0]),
          dec_(0), decWord_(0), decPred_(0), decIdx_(0), decStep_(kImaStep[0]), prev_(0), cur_(0), delay_(0), frozen_(false), wasFrozen_(false), sDec_(0), sWord_(0), sPred_(0), sIdx_(0), sStep_(kImaStep[0]), sPrev_(0), sCur_(0),
          loopLen_(4), loopRel_(0), loopPos_(0), loopRate_(65536L), loopTarget_(65536L), freezeDelay_(kMinDelay), targetDelay_(0), assignFoldK_(7570), assignFoldB_(52523),
          resetCnt_(0), resetSeen_(false), lastResetEdge_(0), syncPeriod_(0), periodQ4_(0), ringIdx_(0), histN_(0),
          clkInc_(kClockBaseInc), clkPhase_(0), clkOn_(false), clkRestart_(false), clkHigh_(false), gatePrev_(false),
          lastActive_(0), everActive_(false), rng_(0x2545F491UL),
          octave_(0), shiftUsed_(false), fbShowUntil_(0), fbView_(false), dispDirty_(true), blinkOn_(false), ovSeen_(0), ovShowUntil_(0), ovView_(false),
          noteCount_(0), lastNote_(60), midiOwnsKB_(false), bend_(0), envPrev_(false), gatePrevOut_(false), holdPrev_(false),
          pendKeys_(0), activeKeys_(0), keyPulse_(false), keyPulseUntil_(0), asrLive_(false),
          midiTick_(0), midiGate_(false), lastMidiClock_(0), midiClockSeen_(false),
          locked_(false), jackPrev_(false), lastJackEdge_(0), jackSeen_(false), tempoLed_(Off),
          asrBits_(0xFF), asrLen_(8), quant_(true)
    {
        // all zeros is encoded silence: from state (0, 0), a zero sample encodes to nibble 0 and the
        // state stays (0, 0), so encoder and decoder agree everywhere from the start
        for (uint16_t i = 0; i < kWordsA; i++) g_lineA[i] = 0;
        for (uint16_t i = 0; i < kWordsB; i++) g_lineB[i] = 0;
    }

private:
    static const int32_t kMinDelay = 8L << 16;                  // in samples, 16.16 fixed point
    static const int32_t kMaxDelay = (int32_t)(kN - 8) << 16;
    static const int32_t kMaxSlew = 58982;                      // 0.9 sample per tick: read speed 0.1x .. 1.9x
    static const int16_t kZero = 32;                            // input counts at 0 V

    int16_t fbLevel_;
    // writer
    uint16_t w_;                // next write index
    uint16_t wAcc_;             // nibbles of the word being filled
    int32_t encPred_; int16_t encIdx_; int32_t encStep_;
    // reader
    uint16_t dec_;              // next index the decoder will decode
    uint16_t decWord_;          // remaining nibbles of the word being decoded
    int32_t decPred_; int16_t decIdx_; int32_t decStep_;
    int32_t prev_, cur_;        // decoded samples at dec_ - 2 and dec_ - 1
    int32_t delay_;             // current delay, samples 16.16
    volatile bool frozen_;      // HOLD input jack high: the delay line loops what it holds
    bool wasFrozen_;
    // freeze loop: decoder snapshot at its start, length, position and speed (16.16; target set in the main loop)
    uint16_t sDec_, sWord_; int32_t sPred_; int16_t sIdx_; int32_t sStep_, sPrev_, sCur_;
    uint16_t loopLen_, loopRel_;
    int32_t loopPos_, loopRate_;
    volatile int32_t loopTarget_;
    volatile int32_t freezeDelay_;   // delay time when the freeze started (16.16)
    volatile int32_t targetDelay_;   // from the GLIDE knob, updated in the main loop
    int32_t assignFoldK_, assignFoldB_;   // ASSIGN counts256 = (y * K >> 12) + B, set at start-up
    // clock sync on the RESET input jack
    uint16_t resetCnt_;         // framework's RESET rising-edge count last seen
    bool resetSeen_;
    uint32_t lastResetEdge_;    // tick of the last rising edge
    uint32_t syncPeriod_;       // clock period in ticks (= delay-line samples), 0 = none yet
    uint32_t periodQ4_;         // the same in 1/16 ticks
    uint16_t ring_[16];         // recent edge ticks (low 16 bits)
    uint16_t ringIdx_, histN_;  // next slot; previous edges usable for the average (0 .. 15)
    // internal clock (phase accumulator in the tick; edges handled in the main loop)
    volatile uint32_t clkInc_;  // phase step per tick, from the TEMPO knob
    uint32_t clkPhase_;
    volatile bool clkOn_;       // RUN/STOP button toggles it
    volatile bool clkRestart_;  // the tick restarts the phase (so it starts with a pulse)
    volatile bool clkHigh_;     // square wave, 50% duty
    bool gatePrev_;             // combined gate (GATE jack OR internal clock) at the last main-loop pass
    // comparator input: the TEMPO CV jack, or internal noise while the jack is silent
    uint32_t lastActive_;       // Ticks() when the input last went beyond the threshold
    bool everActive_;
    uint32_t rng_;              // xorshift32 state
    // octave transpose and the octave-LED display
    int16_t octave_;            // -4 .. +3
    bool shiftUsed_;            // an arrow was pressed while SHIFT was held (then releasing SHIFT does nothing)
    uint32_t fbShowUntil_;      // show the feedback level on the octave LEDs until this tick
    bool fbView_, dispDirty_;
    bool blinkOn_;              // freeze flash phase (octave LEDs)
    uint16_t ovSeen_; uint32_t ovShowUntil_; bool ovView_;   // tick-overrun warning
    // MIDI (main loop): held notes, last-note priority; they own KB while held
    int16_t notes_[8];
    int16_t noteCount_;
    int16_t lastNote_;          // the note KB is playing
    bool midiOwnsKB_;           // KB plays MIDI: from a note-on until the next shift-register clock with no note held
    int32_t bend_;              // pitch bend, -8192 .. 8191 (+-2 semitones)
    bool envPrev_;              // envelope open (gate output OR the HOLD input jack)
    bool gatePrevOut_;          // gate output state (ASR(L) OR MIDI notes OR panel keys)
    bool holdPrev_;             // HOLD input jack at the last main-loop pass
    uint32_t pendKeys_;         // key presses waiting for the next clock (KeyBit layout)
    uint32_t activeKeys_;       // held keys that count toward the gate output
    bool keyPulse_;             // 10 ms gate pulse for a queued key released before its clock
    uint32_t keyPulseUntil_;
    bool asrLive_;              // ASR(L) drives the gate only after the first clock
    // MIDI clock (transport is ignored): while the internal clock is on and MIDI clock is arriving, a 16th-note
    // gate (6 of 24 clocks per quarter, 50% duty) replaces the internal clock
    int16_t midiTick_;          // MIDI clocks since the last step, 0 .. 5
    bool midiGate_;
    uint32_t lastMidiClock_;    // Ticks() of the last MIDI clock
    bool midiClockSeen_;
    // loop lock (HOLD button) and indicators
    bool locked_;               // the register recirculates unchanged (no XOR into the first bit)
    bool jackPrev_;             // GATE jack level at the last main-loop pass
    uint32_t lastJackEdge_;     // Ticks() of the last GATE jack change
    bool jackSeen_;
    Colour tempoLed_;           // colour last written to the TEMPO LED
    // shift register
    uint16_t asrBits_;          // bit i = register position i (bit 0 is the first bit)
    int16_t asrLen_;            // active length, 1 .. 8
    bool quant_;                // KB quantised to C major

    // KB output and white-key LEDs from the active bits
    __attribute__((noinline)) void AsrOutput()      // flash: called from Idle(), and once at start
    {
        uint16_t mask = (uint16_t)((1u << asrLen_) - 1);
        uint16_t v = asrBits_ & mask;
        int32_t mv = (int32_t)v * kAsrSpanMv / mask;
        if (midiOwnsKB_) {                                       // KB plays MIDI until the next clock
        } else if (quant_) {
            int16_t semis = (int16_t)((mv * 12 + 500) / 1000);       // nearest semitone (1 V/oct, 0 V = middle C)
            PitchMIDINote(60 + octave_ * 12 + (semis / 12) * 12 + kMajor[semis % 12]);
        } else {
            PitchMillivolts(mv + (int32_t)octave_ * 1000);
        }
        for (int16_t i = 0; i < 8; i++) KeyLED(i, i < asrLen_ && ((asrBits_ >> i) & 1));
    }

    // TEMPO knob position stretched over its measured travel (about 13..3780) to 0..4095
    int32_t TempoPos() const
    {
        int32_t k = (((int32_t)Tempo() - 13) * 1113) >> 10;        // x 4096 / 3767
        return k < 0 ? 0 : (k > 4095 ? 4095 : k);
    }

    // clock phase step: 0.25 Hz * 2^(12 * pos / 4096), exponential
    static uint32_t ClockInc(int32_t pos)
    {
        int32_t x = pos * 12;                                    // 1/4096 octave units, 0 .. 49140
        int16_t oct = (int16_t)(x >> 12);
        int32_t fr = x & 4095;
        int16_t i = (int16_t)(fr >> 6);
        int32_t m = kExp2Q15[i] + (((kExp2Q15[i + 1] - kExp2Q15[i]) * (fr & 63)) >> 6);   // 2^(fr/4096), Q15
        return ((kClockBaseInc * (uint32_t)m) >> 15) << oct;
    }

    // silent comparator input: never beyond the threshold, or not for 2 s
    bool InputSilent() const { return !everActive_ || Ticks() - lastActive_ > 2UL * kTickHz; }

    // one shift-register clock: rotate the active bits, XOR the first with RUN, RESET, HOLD and the comparator
    void ClockAsr()
    {
        uint16_t mask = (uint16_t)((1u << asrLen_) - 1);
        uint16_t last = (asrBits_ >> (asrLen_ - 1)) & 1;
        uint16_t active = (uint16_t)(((asrBits_ << 1) | last) & mask);         // rotate by one place
        // comparator: the comparator input above the GLIDE knob; the input's whole range (0..4095) maps onto the knob's
        // travel: the TEMPO CV input (ADC 0..4095, about -6..+6 V), or internal noise while that is silent
        int16_t in;
        if (InputSilent()) {
            rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;        // xorshift32
            in = (int16_t)(rng_ >> 20);
        } else {
            in = (int16_t)(TempoCVInRaw() + 2048);
        }
        // GLIDE threshold with dead bands at both ends so the extremes lock the pattern: at the left end the
        // input is always above (the first bit flips every clock), at the right end never (nothing flips).
        // In between, the remaining travel (150 .. 3600) is stretched to 0 .. 4095.
        int16_t g = Glide();
        int32_t thr;
        if (g <= 150) thr = -1;
        else if (g >= 3600) thr = 4096;
        else thr = ((int32_t)(g - 150) * 1216) >> 10;            // x 4096 / 3450
        bool cmp = in > thr;                                     // input above the GLIDE threshold
        // an odd number of RUN, RESET, HOLD and the comparator true flips the first bit
        if (!locked_ && (JackHigh(RunJack) ^ JackHigh(ResetJack) ^ JackHigh(HoldJack) ^ cmp)) active ^= 1;   // locked: loop unchanged
        asrBits_ = (asrBits_ & ~mask) | active;                                // inactive bits are kept
        if (noteCount_ == 0) midiOwnsKB_ = false;                              // KB back from MIDI on the first clock with no note held
        AsrOutput();
    }

    // levels 1-8: a green bar grows from the left; 9-16: yellow replaces it, rolling in from the right;
    // 17-24: red replaces the yellow, again from the right. All red = 100%.
    void ShowFeedback()
    {
        for (int16_t i = 0; i < 8; i++) {
            Colour c;
            if (fbLevel_ <= 8)       c = i < fbLevel_ ? Green : Off;
            else if (fbLevel_ <= 16) c = i >= 16 - fbLevel_ ? Amber : Green;
            else                     c = i >= 24 - fbLevel_ ? Red : Amber;
            StepLED(i, c);
        }
    }

    // TEMPO knob -> kMinDelay..kMaxDelay. The knob reads about 13..3780 end to end (measured), so that travel
    // is stretched to 0..4095 first; the range is pre-divided by 4096 so the product fits in 32 bits.
    // KB from the newest held MIDI note plus pitch bend (MIDI 60 = 0 V, 1 V/oct, bend +-2 semitones)
    void MidiKB()
    {
        int32_t n = lastNote_ - 60;
        PitchMillivolts(((n * 8192 + bend_ * 2) * 1000) / (12L * 8192));
    }

    // MIDI notes (any channel): last-note priority, velocity > 95 = accent, pitch bend, all notes off
    void MidiInput()
    {
        MIDIMessage m;
        while (MIDIRead(m)) {
            if (m.status >= 0xF8) {                              // real time: only the clock is used
                if (m.status == 0xF8) {                          // 24 per quarter note: a step every 6
                    lastMidiClock_ = Ticks(); midiClockSeen_ = true;
                    if (midiTick_ == 0) midiGate_ = true; else if (midiTick_ == 3) midiGate_ = false;
                    if (++midiTick_ >= 6) midiTick_ = 0;
                }
                continue;                                        // Start, Continue, Stop and the rest ignored
            }
            uint16_t type = m.status & 0xF0;
            int16_t note = (int16_t)m.data1;
            if (type == 0x90 && m.data2 > 0) {                   // note on
                for (int16_t i = 0; i < noteCount_; i++)         // a repeated note moves to the top
                    if (notes_[i] == note) { for (; i < noteCount_ - 1; i++) notes_[i] = notes_[i + 1]; noteCount_--; break; }
                if (noteCount_ == 8) { for (int16_t i = 0; i < 7; i++) notes_[i] = notes_[i + 1]; noteCount_--; }
                bool legato = noteCount_ > 0 || envPrev_;
                notes_[noteCount_++] = note;
                lastNote_ = note;
                midiOwnsKB_ = true;
                MidiKB();
                if (m.data2 > 95) Accent();
                if (legato && !Sustain()) EnvelopeTrigger();      // new note: retrigger without SUSTAIN, legato with it
            } else if (type == 0x80 || type == 0x90) {           // note off
                for (int16_t i = 0; i < noteCount_; i++)
                    if (notes_[i] == note) { for (; i < noteCount_ - 1; i++) notes_[i] = notes_[i + 1]; noteCount_--; break; }
                if (noteCount_ > 0) { lastNote_ = notes_[noteCount_ - 1]; MidiKB(); }
                // with none held KB keeps the last note; the next clock hands it back to the shift register
            } else if (type == 0xE0) {                           // pitch bend
                bend_ = (int32_t)((m.data2 << 7) | m.data1) - 8192;
                if (midiOwnsKB_) MidiKB();
            } else if (type == 0xB0 && (m.data1 == 123 || m.data1 == 120)) {   // all notes / sound off
                noteCount_ = 0;
            }
        }
    }

    // apply queued key presses: white keys set the length (the highest wins); keys still held hold the gate,
    // keys already released give a 10 ms gate pulse. Returns true (they retrigger the envelope).
    bool ApplyKeys(uint32_t held)
    {
        for (int16_t w = 0; w < 8; w++)
            if (pendKeys_ & KeyBit(kWhiteKeys[w])) asrLen_ = w + 1;
        activeKeys_ |= pendKeys_ & held;
        if (pendKeys_ & ~held) { keyPulse_ = true; keyPulseUntil_ = Ticks() + kTickHz / 100; }
        pendKeys_ = 0;
        return true;
    }

    // octave LEDs: one red LED for the current octave (-4 .. +3 on LEDs 1 .. 8)
    void ShowOctave()
    {
        // loop lock: the other seven LEDs are lit yellow
        // freeze: they flash green (alternating with the loop-lock yellow when that is on too)
        Colour other = (frozen_ && blinkOn_) ? Green : (locked_ ? Amber : Off);
        for (int16_t i = 0; i < 8; i++) StepLED(i, i == octave_ + 4 ? Red : other);
    }

    // Main loop (not the audio tick): knob-derived settings, input activity, panel, display, combined gate.
    //  arrows: octave; SHIFT + arrows: feedback (shown while SHIFT is held and 1.5 s after a change);
    //  SHIFT pressed and released on its own: quantisation on/off; RUN/STOP: internal clock on/off;
    //  white keys: shift-register length.
    void Idle()
    {
        int32_t pos = TempoPos();
        // delay time (the tick glides toward it): synced to a clock on the RESET input while one is arriving,
        // otherwise free on GLIDE
        // Clock period: edge times come from the framework (the tick at which the panel latched the edge, so no
        // main-loop jitter); the period is averaged over the edges of the last 150 ms (up to 15 periods), which
        // smooths the 0.4 ms scan steps for fast clocks while slow clocks still follow at once.
        uint16_t rc = JackRiseCount(ResetJack);
        if (rc != resetCnt_) {
            resetCnt_ = rc;
            uint32_t now = JackRiseTick(ResetJack);
            uint32_t p = now - lastResetEdge_;
            if (!resetSeen_ || p < kTickHz / 400 || p > 4UL * kTickHz) histN_ = 0;     // 2.5 ms .. 4 s, else start over
            else if (syncPeriod_ && (p > 2 * syncPeriod_ || 2 * p < syncPeriod_)) histN_ = 1;   // tempo jump: this period only
            uint16_t now16 = (uint16_t)now;
            if (histN_ > 0) {
                uint16_t n = 1;
                uint32_t span = p;
                while (n < histN_) {                                            // widen the window up to 150 ms
                    uint16_t s2 = now16 - ring_[(ringIdx_ - 1 - n) & 15];
                    if (s2 > (uint16_t)(kTickHz * 3 / 20)) break;
                    span = s2; n++;
                }
                periodQ4_ = (span << 4) / n;                                    // period in 1/16 ticks
                syncPeriod_ = periodQ4_ >> 4;
            }
            ring_[ringIdx_] = now16; ringIdx_ = (ringIdx_ + 1) & 15;
            if (histN_ < 15) histN_++;
            lastResetEdge_ = now;
            resetSeen_ = true;
        }
        bool sync = syncPeriod_ && Ticks() - lastResetEdge_ < 2 * syncPeriod_ + kTickHz / 4;
        if (sync) {
            int16_t i = (int16_t)(((int32_t)Glide() * kSyncSteps) >> 12);   // GLIDE: fraction of the period
            if (i >= kSyncSteps) i = kSyncSteps - 1;
            uint32_t d = periodQ4_ * kSyncNum[i] / kSyncDen[i];             // 1/16 samples
            while (d > (uint32_t)(kMaxDelay >> 12)) d >>= 1;               // too long: halve until it fits
            if (d < (uint32_t)(kMinDelay >> 12)) d = (uint32_t)(kMinDelay >> 12);
            targetDelay_ = (int32_t)d << 12;
        } else {
            targetDelay_ = kMinDelay + (int32_t)Glide() * ((kMaxDelay - kMinDelay) >> 12);   // GLIDE: free time
        }
        // frozen loop speed = delay time at the freeze / current delay time (exactly 1x with GLIDE unchanged, 2x at half
        // the time), 1/4x .. 4x
        {
            int32_t d = targetDelay_;
            int32_t rt = d > 0 ? (int32_t)(((int64_t)freezeDelay_ << 16) / d) : 262144L;
            loopTarget_ = rt < 16384 ? 16384 : (rt > 262144L ? 262144L : rt);
        }
        clkInc_ = ClockInc(pos);

        int16_t v = TempoCVInRaw() - kZero;                    // input activity (about +-120 mV)
        if (v > 40 || v < -40) { lastActive_ = Ticks(); everActive_ = true; }

        PanelEvents e = TakePanelEvents();
        bool shift = ButtonHeld(Shift);
        if (e.pressed & ButtonBit(Shift)) shiftUsed_ = false;
        if (e.pressed & (ButtonBit(Right) | ButtonBit(Left))) {
            int16_t d = (e.pressed & ButtonBit(Right)) ? 1 : -1;
            if (shift) {                                         // SHIFT + arrows: feedback
                fbLevel_ += d;
                if (fbLevel_ < 0) fbLevel_ = 0; else if (fbLevel_ > kFbLevels) fbLevel_ = kFbLevels;
                shiftUsed_ = true;
                fbShowUntil_ = Ticks() + 3UL * kTickHz / 2;
                dispDirty_ = true;
            } else {                                             // arrows: octave transpose of KB
                octave_ += d;
                if (octave_ < -4) octave_ = -4; else if (octave_ > 3) octave_ = 3;
                AsrOutput();
                dispDirty_ = true;
            }
        }
        if ((e.released & ButtonBit(Shift)) && !shiftUsed_) {   // SHIFT on its own: quantisation on/off
            quant_ = !quant_;                                    // the TEMPO LED follows below
            AsrOutput();
        }
        if (e.pressed & ButtonBit(Hold)) { locked_ = !locked_; dispDirty_ = true; }   // HOLD: loop lock on/off
        if (e.pressed & ButtonBit(RunButton)) {                  // RUN/STOP: internal clock on/off
            if (!clkOn_) { clkRestart_ = true; midiTick_ = 0; midiGate_ = false; }   // next MIDI clock is a step
            clkOn_ = !clkOn_;
        }
        // panel keys: presses are queued while the sequencer is active (internal clock on, or the GATE jack changed
        // in the last 2 s) and take effect on the next clock; otherwise at once. Releases always take effect at once.
        bool jg = JackHigh(GateJack);
        if (jg != jackPrev_) { jackPrev_ = jg; lastJackEdge_ = Ticks(); jackSeen_ = true; }
        bool active = clkOn_ || (jackSeen_ && Ticks() - lastJackEdge_ < 2UL * kTickHz);
        uint32_t held = 0;
        for (int16_t k = 0; k < 13; k++) if (Key(k)) held |= KeyBit(k);
        pendKeys_ |= e.pressed & 0x1FFFUL;
        activeKeys_ &= held;
        bool keysNew = false;
        if (!active && pendKeys_) { keysNew = ApplyKeys(held); AsrOutput(); }

        // octave LEDs: the feedback level while SHIFT is held or just after a change, otherwise the octave
        bool blink = frozen_ && ((Ticks() / (kTickHz / 4)) & 1) == 0;      // 2 Hz flash while frozen
        if (blink != blinkOn_) { blinkOn_ = blink; dispDirty_ = true; }
        bool fbView = shift || (int32_t)(fbShowUntil_ - Ticks()) > 0;
        // a tick overrun (hard real time broken) lights all eight octave LEDs red for 1 s
        uint16_t ov = Overruns();
        if (ov != ovSeen_) { ovSeen_ = ov; ovShowUntil_ = Ticks() + kTickHz; ovView_ = true; dispDirty_ = true; }
        if (ovView_ && (int32_t)(ovShowUntil_ - Ticks()) <= 0) { ovView_ = false; dispDirty_ = true; }
        if (fbView != fbView_ || dispDirty_) {
            fbView_ = fbView; dispDirty_ = false;
            if (ovView_) { for (int16_t i = 0; i < 8; i++) StepLED(i, Red); }
            else if (fbView) ShowFeedback(); else ShowOctave();
        }

        MidiInput();

        // combined gate = GATE jack OR the sequencer clock: while RUN/STOP is on, MIDI clock (if one arrived in
        // the last 0.5 s) or else the internal clock. Its rise clocks the shift register.
        bool midiSync = midiClockSeen_ && Ticks() - lastMidiClock_ < (uint32_t)(kTickHz / 2);
        bool seq = clkOn_ && (midiSync ? midiGate_ : clkHigh_);
        bool g = JackHigh(GateJack) || seq;
        bool clocked = g && !gatePrev_;
        if (clocked && pendKeys_) keysNew = ApplyKeys(held);   // queued presses land on this clock (length first)
        if (clocked) ClockAsr();
        // gate output and envelope: high while the last active bit ASR(L) is set, a MIDI note is held or a panel
        // key is held (envelope held with SUSTAIN on, a trigger with it off). While already high, a clock that
        // leaves ASR(L) set, or a newly pressed panel key, retriggers the envelope.
        // The HOLD input jack drives the envelope only (not the gate output): open while high, its rise retriggers.
        // the start-up bits do not count until the first clock (no gate or envelope trigger at power-up)
        if (clocked) asrLive_ = true;
        bool asrHigh = asrLive_ && ((asrBits_ >> (asrLen_ - 1)) & 1);
        if (keyPulse_ && (int32_t)(keyPulseUntil_ - Ticks()) <= 0) keyPulse_ = false;
        bool gateOut = asrHigh || noteCount_ > 0 || activeKeys_ || keyPulse_;
        bool hold = JackHigh(HoldJack);
        frozen_ = hold;                                          // HOLD jack also freezes the delay
        bool env = gateOut || hold;
        if (gateOut != gatePrevOut_) { Gate(gateOut); gatePrevOut_ = gateOut; }
        bool retrig = (clocked && asrHigh) || keysNew || (hold && !holdPrev_);
        if (!env) { if (envPrev_) Envelope(false); }
        else if (!envPrev_ || retrig) { if (Sustain()) Envelope(true); else EnvelopeTrigger(); }
        holdPrev_ = hold;
        envPrev_ = env;
        gatePrev_ = g;

        // TEMPO LED: quantisation colour (green on, red off); while the sequencer is active (internal clock
        // on, or the GATE jack changed in the last 2 s) it flashes with the clock
        Colour want = (!active || g) ? (quant_ ? Green : Red) : Off;
        if (want != tempoLed_) { tempoLed_ = want; TempoLED(want); }
    }

    M32_RAMFUNC void Process()
    {
        if (Ticks() == 0) {
            TempoLED(quant_ ? Green : Red);                     // quantisation on by default
            int32_t c0 = AssignMillivoltsToRaw(0), c1 = AssignMillivoltsToRaw(1000);
            assignFoldK_ = (3093L * (c1 - c0)) / 4000;          // y * 3093 / 2^14 mV, then per mV, x 2^12
            assignFoldB_ = c0;
            AsrOutput();
            delay_ = targetDelay_ = kMinDelay + (int32_t)Glide() * ((kMaxDelay - kMinDelay) >> 12);
            dec_ = Wrap((int32_t)w_ - (delay_ >> 16) + 1);      // decoder starts at the read position
            decWord_ = LineWord(dec_ >> 2) >> ((dec_ & 3) << 2);
        }

        // internal clock: phase accumulator, square wave (edges are handled in the main loop)
        if (clkRestart_) { clkRestart_ = false; clkPhase_ = 0x80000000UL; }
        clkPhase_ += clkInc_;
        clkHigh_ = clkOn_ && (clkPhase_ >= 0x80000000UL);

        // freeze (HOLD jack): writing stops and the last delay-time's worth of the line loops, its speed set so one
        // pass lasts the current delay time (GLIDE or the RESET clock sync), which repitches it
        bool fz = frozen_;
        if (fz != wasFrozen_) { wasFrozen_ = fz; if (fz) FreezeStart(); else DecoderRestore(); }
        int32_t y;
        if (fz) {
            int32_t r = loopRate_ + ((loopTarget_ - loopRate_) >> 9);   // speed glides (time constant 21 ms)
            loopRate_ = r;
            y = LoopStep(r);
        } else {
            // one delay-line step per tick: read the delayed sample, then store input + feedback
            PF_BEGIN(u1);
            y = ReadStep();
            PF_END(u1, m32_pfU1);
            PF_BEGIN(u2);
            WriteStep(((int32_t)TempoCVInRaw() - kZero) << 4, y);  // input at 16-bit scale
            PF_END(u2, m32_pfU2);
        }
        // ASSIGN output in PWM counts directly: (y * 3.02 mV / 16) -> calibrated counts256, folded into one
        // multiply-add whose constants come from the framework's calibration at start-up
        AssignRaw(((y * assignFoldK_) >> 12) + assignFoldB_);
    }

    // freeze: remember the decoder (it sits at the oldest sample of the loop) and the loop length up to the last
    // stored word; while frozen the line is not written, so the encoded data stays exact (no generation loss)
    __attribute__((noinline)) M32_RAMFUNC void FreezeStart()
    {
        sDec_ = dec_; sWord_ = decWord_; sPred_ = decPred_; sIdx_ = decIdx_; sStep_ = decStep_; sPrev_ = prev_; sCur_ = cur_;
        int32_t len = (int32_t)(w_ & ~3u) - (int32_t)dec_;
        if (len <= 0) len += kN;
        if (len < 4) len = 4;
        loopLen_ = (uint16_t)len;
        loopPos_ = 0; loopRel_ = 0;
        loopRate_ = 65536L;                                     // starts at the original speed, then glides
        freezeDelay_ = delay_;
    }

    // back to the decoder state at the freeze: memory and the writer are unchanged, so the delay carries on
    // exactly where it stopped (and at the end of a loop pass, the loop starts again)
    __attribute__((noinline)) M32_RAMFUNC void DecoderRestore()
    {
        dec_ = sDec_; decWord_ = sWord_; decPred_ = sPred_; decIdx_ = sIdx_; decStep_ = sStep_; prev_ = sPrev_; cur_ = sCur_;
    }

    // one output sample of the frozen loop at speed r (16.16), linear interpolation
    __attribute__((noinline)) M32_RAMFUNC int32_t LoopStep(int32_t r)
    {
        loopPos_ += r;
        if ((uint32_t)(loopPos_ >> 16) >= loopLen_) { loopPos_ -= (int32_t)loopLen_ << 16; DecoderRestore(); loopRel_ = 0; }
        uint16_t whole = (uint16_t)(loopPos_ >> 16);
        uint16_t need = whole + 2 > loopLen_ ? loopLen_ : whole + 2;     // never decode past the loop end
        for (uint16_t k = 0; loopRel_ < need && k < 6; k++) { DecodeOne(); loopRel_++; }
        if (whole + 2 > loopLen_) return cur_;                           // last sample: no neighbour to blend
        return prev_ + (((cur_ - prev_) * (int32_t)((loopPos_ & 0xFFFF) >> 4)) >> 12);
    }

    // decode the next sample of the read head
    M32_RAMFUNC void DecodeOne()
    {
        if ((dec_ & 3) == 0) decWord_ = LineWord(dec_ >> 2);
        uint16_t n = decWord_ & 0xF;
        decWord_ >>= 4;
        int32_t step = decStep_;
        int32_t delta = step >> 3;
        if (n & 4) delta += step;
        if (n & 2) delta += step >> 1;
        if (n & 1) delta += step >> 2;
        int32_t p = (n & 8) ? decPred_ - delta : decPred_ + delta;
        decPred_ = p > 32767 ? 32767 : (p < -32768 ? -32768 : p);
        int16_t i = decIdx_ + kImaIndex[n];
        decIdx_ = i < 0 ? 0 : (i > 88 ? 88 : i);
        decStep_ = kImaStep[decIdx_];
        prev_ = cur_; cur_ = decPred_;
        if (++dec_ >= kN) dec_ = 0;
    }

    // the delayed sample at the (gliding) delay time
    M32_RAMFUNC int32_t ReadStep()
    {
        // delay time glides toward the knob (time constant 512 ticks = 21 ms), at most 0.9 sample per tick
        int32_t step = (targetDelay_ - delay_) >> 9;
        if (step > kMaxSlew) step = kMaxSlew; else if (step < -kMaxSlew) step = -kMaxSlew;
        delay_ += step;

        // read position = w_ - delay_: samples a and a + 1, fraction f (16 bits)
        int32_t whole = delay_ >> 16;
        uint32_t frac = (uint32_t)delay_ & 0xFFFF;
        int32_t a = (int32_t)w_ - whole - (frac ? 1 : 0);
        uint32_t f = frac ? 65536UL - frac : 0;
        uint16_t need = Wrap(a + 2);                            // decode until dec_ == a + 2
        for (uint16_t k = 0; dec_ != need && k < 4; k++) DecodeOne();   // normally 0 to 2 steps
        return prev_ + (((cur_ - prev_) * (int32_t)(f >> 4)) >> 12);    // linear interpolation
    }

    // input plus feedback of the delayed sample (rounded toward zero), clamped, encoded and stored
    M32_RAMFUNC void WriteStep(int32_t in, int32_t y)
    {
#ifdef DELAY_NO_FEEDBACK
        (void)y;
        int32_t s = in;                                         // input only (already within 16 bits)
#else
        int32_t prod = y * kFeedbackQ12[fbLevel_];
        int32_t s = in + ((prod >= 0) ? (prod >> 12) : -((-prod) >> 12));
        if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
#endif

        // IMA ADPCM encode, and update the state exactly as the decoder will
        int32_t step = encStep_;
        int32_t diff = s - encPred_;
        uint16_t n = 0;
        if (diff < 0) { n = 8; diff = -diff; }
        int32_t delta = step >> 3;
        if (diff >= step) { n |= 4; diff -= step; delta += step; }
        if (diff >= (step >> 1)) { n |= 2; diff -= step >> 1; delta += step >> 1; }
        if (diff >= (step >> 2)) { n |= 1; delta += step >> 2; }
        int32_t p = (n & 8) ? encPred_ - delta : encPred_ + delta;
        encPred_ = p > 32767 ? 32767 : (p < -32768 ? -32768 : p);
        int16_t i = encIdx_ + kImaIndex[n];
        encIdx_ = i < 0 ? 0 : (i > 88 ? 88 : i);
        encStep_ = kImaStep[encIdx_];

        // collect four nibbles, store the word when it is full
        wAcc_ |= n << ((w_ & 3) << 2);
        if ((w_ & 3) == 3) { LineWord(w_ >> 2) = wAcc_; wAcc_ = 0; }
        if (++w_ >= kN) w_ = 0;
    }
};

int main()
{
    static Rungler32 fw;
    fw.Run();
}
