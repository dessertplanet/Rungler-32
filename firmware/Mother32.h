/*
 * Mother32.h - single-header framework for custom Mother-32 firmware.
 * Unofficial: not made, endorsed or supported by Moog Music. Part of Rungler-32.
 * Copyright (C) 2026 Dune Desormeaux (@Dessertplanet). SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The API is based on Chris Johnson's ComputerCard.h (MIT) for the Music Thing Modular Workshop System
 * Computer: https://github.com/TomWhitwell/Workshop_Computer
 *
 * Usage (same shape as ComputerCard: a fixed-rate Process() callback):
 *
 *     #include "Mother32.h"
 *     class MyFirmware : public Mother32 {
 *         void Process() {                    // called kTickHz times a second
 *             if (KeyPressed(0)) { PitchMIDINote(48); Gate(true); }
 *             if (KeyReleased(0)) Gate(false);
 *         }
 *     };
 *     int main() { static MyFirmware fw; fw.Run(); }
 *
 * Pin assignments and default constants were measured on a real unit.
 * Target: TMS320F28023 (C28x, no FPU), 40 MHz, TI C2000 compiler, COFF ABI, large model.
 * In this compiler `int` and `char` are 16 bits: use the <stdint.h> types.
 * The TI compiler supports C++03 only: no in-class member initialisers, no `override`.
 *
 * Define MOTHER32_NOIMPL before including in all but one translation unit.
 */
#ifndef MOTHER32_H
#define MOTHER32_H

#include <stdint.h>

// Code on the per-sample (audio) path runs from RAM: flash adds wait states. The framework marks its own
// per-sample code with this (fast ADC interrupt, tick interrupt and handler, CV outputs, the accessors a
// Process() typically calls every sample); the panel scan and MIDI stay in flash. Use it in firmware on
// Process() and what it calls. It costs L0 RAM.
#define M32_RAMFUNC __attribute__((ramfunc))

// Build-time settings (-DM32_TICK_HZ=20000 etc.). Defaults are the tested configuration.
#ifndef M32_TICK_HZ
#define M32_TICK_HZ 10000      // Process() rate; a multiple of 2500
#endif
#ifndef M32_OS
#define M32_OS 8               // ADC oversampling factor of the TEMPO CV input: 4, 8 or 16
#endif
// Timer periods in SYSCLK cycles: the tick period is a whole multiple of the fast-timer period, so the two
// timers never drift against each other. The real tick rate is 40 MHz / M32_TICK_CYCLES.
#define M32_TICK_CYCLES ((40000000UL / M32_TICK_HZ / M32_OS) * M32_OS)
#if M32_OS == 4
#define M32_OS_SHIFT 6         // CIC gain M32_OS^3 = 2^M32_OS_SHIFT
#elif M32_OS == 8
#define M32_OS_SHIFT 9
#elif M32_OS == 16
#define M32_OS_SHIFT 12
#else
#error "M32_OS must be 4, 8 or 16"
#endif

class Mother32 {
public:
    static const uint16_t kTickHz = M32_TICK_HZ;                 // Process() rate (default 10000)
    static const uint16_t kScanDivider = (uint16_t)(((uint32_t)M32_TICK_HZ + 1250UL) / 2500UL);   // panel scanned at about 2.5 kHz (32-bit: int is 16 bits)

    enum Button { Right, Left, Pattern, RunButton, Shift, Hold, Reset };
    enum JackIn { GateJack, RunJack, ResetJack, HoldJack };
    enum Colour { Off = 0, Green = 1, Red = 2, Amber = 3 };

    struct MIDIMessage {
        uint16_t status;   // 0x80-0xFF; channel messages include the channel in the low nibble
        uint16_t data1;    // 0-127 (0 if unused)
        uint16_t data2;    // 0-127 (0 if unused)
    };

    Mother32();
    virtual ~Mother32() {}

    // Sets up the hardware and starts calling Process() at kTickHz. Never returns.
    void Run();

protected:
    // ---- to implement ---------------------------------------------------------------
    virtual void Process() = 0;      // timer interrupt, kTickHz; keep it under ~60 us
    virtual void Idle() {}           // called repeatedly from the main loop (slow work)

    // ---- keyboard: k = 0 (low C) ... 12 (high C) ---------------------------------------
    bool Key(int16_t k) const;           // currently held (debounced)
    bool KeyPressed(int16_t k) const;    // went down since the previous Process() call
    bool KeyReleased(int16_t k) const;
    bool AnyKeyHeld() const;

    // ---- buttons ------------------------------------------------------------------
    bool ButtonHeld(Button b) const;
    bool ButtonPressed(Button b) const;
    bool ButtonReleased(Button b) const;

    // ---- gate/clock jacks (logic level, high = gate present) ---------------------------
    bool JackHigh(JackIn j) const;
    bool JackRising(JackIn j) const;
    // exact timing of the jacks' rising edges, for clock measurement: the tick at which the panel inputs that
    // showed the edge were latched (resolution: one scan, about 0.4 ms), and a count of rising edges
    uint32_t JackRiseTick(JackIn j) const { return jackRiseTick_[j]; }
    uint16_t JackRiseCount(JackIn j) const { return jackRiseCount_[j]; }
    bool JackFalling(JackIn j) const;

    // ---- analogue inputs, smoothed, 0..4095 ---------------------------------------------
    int16_t Tempo() const;       // TEMPO knob
    int16_t Glide() const;       // GLIDE knob
    bool Sustain() const;        // SUSTAIN switch on
    int16_t TempoCVIn() const;   // TEMPO CV jack, about -2048..+2047, positive = positive volts (uncalibrated)
    // Same, but not smoothed, for signals above ~100 Hz. The ADC is read at M32_OS (8) x kTickHz and low-pass
    // filtered (CIC, order 3) down to kTickHz, so frequencies above kTickHz / 2 are attenuated before
    // sampling instead of aliasing: about 130 us group delay, -3 dB at 2.5 kHz, -12 dB at 5 kHz.
    // The filter costs roughly 20% of the CPU. Latched at the start of the tick, so evenly spaced.
    int16_t TempoCVInRaw() const;

    // ---- CV outputs ---------------------------------------------------------------
    // Raw: 1/256ths of a PWM count, 0 .. 400*256 (about -5.4 V .. +5.1 V).
    void PitchRaw(int32_t counts256);
    void AssignRaw(int32_t counts256);
    // Calibrated: the unit's factory calibration from the EEPROM when present, typical constants otherwise.
    void PitchMillivolts(int32_t mv);
    void AssignMillivolts(int32_t mv);
    void PitchMIDINote(int16_t note);    // 1 V/oct, MIDI 60 = 0 V, 48 = -1 V
    static int32_t PitchMIDINoteToRaw(int16_t note);
    static int32_t PitchMillivoltsToRaw(int32_t mv);
    static int32_t AssignMillivoltsToRaw(int32_t mv);

    // ---- gates, envelope, accent -------------------------------------------------------
    // Two gate lines: GPIO3 (the GATE output jack, held for the note) and GPIO0 (the envelope
    // generator's gate). With SUSTAIN off the envelope gate is shortened to a trigger pulse, which is
    // how the SUSTAIN switch works. NoteGateOn/Off do both.
    void Gate(bool on);            // GPIO3: note gate (held while the note sounds)
    void Envelope(bool on);        // GPIO0: envelope gate, held
    void EnvelopeTrigger();        // GPIO0: 10 ms pulse (attack/decay without sustain)
    void NoteGateOn();             // Gate on + Envelope on (SUSTAIN on) or trigger (SUSTAIN off)
    void NoteGateOff();            // Gate off + Envelope off
    void Accent();                 // GPIO5: 15 ms pulse

    // ---- LEDs ---------------------------------------------------------------------
    void KeyLED(int16_t i, bool on);        // i = 0..7: the LEDs under silkscreen keys 1..8
    void StepLED(int16_t i, Colour c);      // i = 0..7: the bicolour octave/location LEDs
    void TempoLED(Colour c);                // Red = GPIO7, Green = GPIO6
    void AllLEDsOff();

    // ---- MIDI IN ------------------------------------------------------------------
    bool MIDIRead(MIDIMessage &m);          // next received message, false if none

    // ---- EEPROM (24LC256 on I2C, 32 KB) ----------------------------------------------
    // Blocking, polled, 100 kHz: for start-up or Idle(), never Process(). False on a bus error or timeout.
    // Each byte goes into one word of dst (the C28x has no 8-bit type). addr is a byte address.
    static bool EepromRead(uint16_t addr, uint16_t *dst, uint16_t n);

    // ---- firmware update ----------------------------------------------------------
    // Marks this firmware invalid and resets, so the synth's bootloader waits for a SysEx firmware
    // update. Called automatically when the ERASE message F0 04 14 11 F7 arrives on MIDI IN, so the
    // official update procedure (ERASE twice, then the firmware file) works with this firmware installed.
    // Returns false (and does nothing) if the bootloader's flash routines cannot be verified in RAM.
    static bool EnterUpdateMode();

    // ---- time ---------------------------------------------------------------------
    uint32_t Ticks() const { return ticks_; }   // Process() calls since Run()
    static uint16_t Overruns();                  // ticks that ran past the next tick's start (0 = hard real time held)
    // True on the tick whose panel scan has just been collected (about 2.5 kHz). Keys, buttons and the
    // gate jacks only change, and their ...Pressed / ...Rising edges are only true, on these ticks, so
    // panel handling can skip the other ticks.
    bool PanelUpdated() const { return scanPhase_ == 0; }

    // ---- panel events for Idle() (main loop) ------------------------------------------------------------
    // Panel handling (keys, buttons, gate jacks) need not run in the audio tick: the tick collects every
    // panel edge, and code in Idle() takes them here. Bit layout as KeyBit / ButtonBit / JackBit.
    struct PanelEvents { uint32_t pressed, released; };    // keys/buttons pressed, jacks gone high / the reverse
    PanelEvents TakePanelEvents();                          // edges since the last call; clears them
    static uint32_t KeyBit(int16_t k) { return 1UL << (12 - k); }   // k = 0 (low C) .. 12 (high C)
    static uint32_t ButtonBit(Button b);
    static uint32_t JackBit(JackIn j);

private:
    friend void Mother32_Tick();
    static Mother32 *instance_;

    void InitSystem();
    void InitGPIO();
    void InitPWM();
    void InitSPI();
    void InitADC();
    void InitSCI();
    void InitI2C();
    void LoadCalibration();
    void InitTimerAndInterrupts();
    void Tick();
    void ScanStart();                // pulse the latch and start the 24-bit SPI burst (does not wait)
    void ScanFinish();               // collect the previous burst and update the inputs
    void UpdateInputs(uint32_t raw);
    void PollMIDI();
    void ParseMIDI(uint16_t b);

    volatile uint32_t ticks_;
    uint32_t leds_;              // 24 output bits
    uint16_t tempoLED_;
    uint32_t inRaw_;             // last raw input word (active-low buttons, active-high jacks)
    uint32_t inStable_;          // debounced, normalised: 1 = pressed / high
    uint32_t inCandidate_;
    uint32_t deb0_, deb1_, deb2_;  // debounce counters, one bit per input (3 bit-planes)
    uint16_t scanPhase_;
    bool scanPending_;
    volatile uint32_t pendUp_, pendDown_;   // panel edges waiting for TakePanelEvents()
    uint32_t edgesUp_, edgesDown_;
    uint32_t scanLatchTick_;                // ticks_ when the current scan latched the inputs
    volatile uint32_t jackRiseTick_[4];
    volatile uint16_t jackRiseCount_[4];
    uint16_t resetStable_, resetCandidate_, resetCount_;
    bool resetEdgeUp_, resetEdgeDown_;
    int32_t adcAcc_[5];
    uint16_t envPulseLeft_, accentLeft_;
    volatile bool envRetrigger_;   // envelope gate pulled low for a retrigger; the next tick raises it
    int16_t cvRaw_;              // TEMPO CV ADC result latched at the very start of the tick
    // MIDI
    MIDIMessage midiQ_[16];
    volatile uint16_t midiHead_, midiTail_;
    uint16_t runStatus_, midiData_[2], midiCount_, midiNeeded_;
    bool inSysEx_;
    uint16_t sysPos_, sysBuf_[3];   // first SysEx data bytes, for the ERASE message
};

// ===================================================================================
#ifndef MOTHER32_NOIMPL

#define M32_R16(a) (*(volatile uint16_t *)(a))
#define M32_R32(a) (*(volatile uint32_t *)(a))
#define M32_EALLOW() asm(" EALLOW")
#define M32_EDIS()   asm(" EDIS")

// --- register addresses (word addresses, TMS320F2802x) ------------------------------
// System control
#define M32_XCLK      0x7010
#define M32_PLLSTS    0x7011
#define M32_CLKCTL    0x7012
#define M32_LOSPCP    0x701B
#define M32_PCLKCR0   0x701C
#define M32_PCLKCR1   0x701D
#define M32_PCLKCR3   0x7020
#define M32_PLLCR     0x7021
#define M32_SCSR      0x7022
#define M32_WDCR      0x7029
// Flash
#define M32_FOPT      0x0A80
#define M32_FSTDBY    0x0A84
#define M32_FACTIVE   0x0A85
#define M32_FBANKWAIT 0x0A86
#define M32_FOTPWAIT  0x0A87
// GPIO
#define M32_GPAQSEL2  0x6F84
#define M32_GPAMUX1   0x6F86
#define M32_GPAMUX2   0x6F88
#define M32_GPADIR    0x6F8A
#define M32_GPAPUD    0x6F8C
#define M32_GPBQSEL1  0x6F92
#define M32_GPBMUX1   0x6F96
#define M32_GPBPUD    0x6F9C
// I2C-A (EEPROM 24LC256 at 0x50 on GPIO32/33)
#define M32_I2COAR    0x7900
#define M32_I2CIER    0x7901
#define M32_I2CSTR    0x7902
#define M32_I2CCLKL   0x7903
#define M32_I2CCLKH   0x7904
#define M32_I2CCNT    0x7905
#define M32_I2CDRR    0x7906
#define M32_I2CSAR    0x7907
#define M32_I2CDXR    0x7908
#define M32_I2CMDR    0x7909
#define M32_I2CPSC    0x790C
#define M32_I2CFFTX   0x7920
#define M32_I2CFFRX   0x7921
#define M32_GPADAT    0x6FC0
#define M32_GPASET    0x6FC2
#define M32_GPACLEAR  0x6FC4
// ePWM (base + offset)
#define M32_EPWM1     0x6800
#define M32_EPWM2     0x6840
#define M32_EPWM3     0x6880
#define M32_TBCTL     0x00
#define M32_TBPHS     0x03
#define M32_TBCTR     0x04
#define M32_TBPRD     0x05
#define M32_CMPCTL    0x07
#define M32_CMPAHR    0x08      // 32-bit pair CMPAHR:CMPA starts here
#define M32_AQCTLA    0x0B
#define M32_HRCNFG    0x20
#define M32_HRMSTEP   0x26      // ePWM1 only
// SPI-A
#define M32_SPICCR    0x7040
#define M32_SPICTL    0x7041
#define M32_SPISTS    0x7042
#define M32_SPIBRR    0x7044
#define M32_SPIRXBUF  0x7047
#define M32_SPITXBUF  0x7048
#define M32_SPIFFTX   0x704A
#define M32_SPIFFRX   0x704B
#define M32_SPIFFCT   0x704C
#define M32_SPIPRI    0x704F
// SCI-A
#define M32_SCICCR    0x7050
#define M32_SCICTL1   0x7051
#define M32_SCIHBAUD  0x7052
#define M32_SCILBAUD  0x7053
#define M32_SCIRXST   0x7055
#define M32_SCIRXBUF  0x7057
#define M32_SCIFFTX   0x705A
#define M32_SCIFFRX   0x705B
#define M32_SCIPRI    0x705F
// ADC
#define M32_ADCCTL1   0x7100
#define M32_ADCCTL2   0x7101
#define M32_INTSEL1N2 0x7108
#define M32_ADCSOCSEL 0x7114     // ADCINTSOCSEL1
#define M32_ADCSOCFRC 0x711A
#define M32_ADCSOC0   0x7120
#define M32_ADCRESULT 0x0B00
// CPU timer 0, PIE
#define M32_T0PRD     0x0C02     // 32-bit
#define M32_T0TCR     0x0C04
#define M32_T0TPR     0x0C06
#define M32_T1TIM     0x0C08     // 32-bit down-counter
#define M32_T1PRD     0x0C0A     // 32-bit
#define M32_T1TCR     0x0C0C
#define M32_T1TPR     0x0C0E
#define M32_T2TIM     0x0C10     // CPU timer 2: free-running cycle counter in profiling builds
#define M32_T2PRD     0x0C12
#define M32_T2TCR     0x0C14
#define M32_T2TPR     0x0C16
#define M32_PIECTRL   0x0CE0
#define M32_PIEACK    0x0CE1
#define M32_PIEIER1   0x0CE2
#define M32_PIEIFR1   0x0CE3
#define M32_PIEVECT   0x0D00

// --- default constants (measured on one unit; outputs use the EEPROM calibration when present) ---
#define M32_PWM_PERIOD      400
#define M32_HRMSTEP_VALUE   188        // HRPWM MEP scale factor for this device at 40 MHz (fixed)
#define M32_PITCH_OFFSET_MV 5405       // V = 0.02626 * count - 5.405
#define M32_PITCH_K4096     39930      // counts256 per mV * 4096 (256 / 26.26 * 4096)
#define M32_ASSIGN_OFFSET_MV 5365      // V = 0.02615 * count - 5.365
#define M32_ASSIGN_K4096    40099
#define M32_DEBOUNCE_SCANS  8          // 8 scans at 2.5 kHz = 3.2 ms

// Input word bit positions
static const uint16_t kButtonBit[] = { 13, 14, 15, 20, 21, 22 };     // Right, Left, Pattern, Run, Shift, Hold
static const uint16_t kJackBit[]   = { 19, 18, 16, 17 };              // Gate, Run, Reset, Hold jacks
static const uint32_t kActiveLowMask = 0x0070FFFFUL;                 // keys + buttons (jacks are active high)

Mother32 *Mother32::instance_ = 0;

extern "C" __interrupt void Mother32_TickIsr(void);
extern "C" __interrupt void Mother32_UnusedIsr(void) { asm(" ESTOP0"); for (;;) {} }
M32_RAMFUNC void Mother32_Tick() { Mother32::instance_->Tick(); }
#ifdef M32_PROFILE
// Profiling build (-DM32_PROFILE): CPU timer 2 counts SYSCLK cycles; each interrupt and each part of the
// tick is timed and summed here (read them with a debugger). Times in the segments are net
// of the fast interrupts that nested inside them. Hardware interrupt entry/exit (about 24 cycles) is
// not included. m32_idleCount counts main-loop passes; m32_idleBase is the same count over 50 ms
// measured before any interrupt was enabled, so load = 1 - idle rate / base rate.
extern "C" {
volatile uint32_t m32_pfFastTotal = 0;                       // running sum of fast-ISR cycles
volatile uint32_t m32_pfFast[3] = { 0, 0, 0 };               // sum, max, count
volatile uint32_t m32_pfScan[3] = { 0, 0, 0 };               // panel scan
volatile uint32_t m32_pfRest[3] = { 0, 0, 0 };               // MIDI poll, ADC smoothing, pulse timers
volatile uint32_t m32_pfProc[3] = { 0, 0, 0 };               // Process()
volatile uint32_t m32_pfTick[3] = { 0, 0, 0 };               // whole tick interrupt, entry to exit, nesting included
volatile uint32_t m32_idleCount = 0, m32_idleBase = 0;
}
#define PF_NOW() M32_R32(M32_T2TIM)
#define PF_BEGIN(id) uint32_t pf_t##id = PF_NOW(), pf_f##id = m32_pfFastTotal
#define PF_ADD(arr, cyc) { uint32_t pf_d = (cyc); arr[0] += pf_d; if (pf_d > arr[1]) arr[1] = pf_d; arr[2]++; }
#define PF_END(id, arr) PF_ADD(arr, pf_t##id - PF_NOW() - (m32_pfFastTotal - pf_f##id))
#else
#define PF_BEGIN(id)
#define PF_END(id, arr)
#endif

// --- oversampled TEMPO CV input (always on) -------------------------------------------------
// Timer 1 (CPU interrupt INT13) runs at M32_OS x the tick rate. Each interrupt feeds the 12-bit ADC
// result through three cascaded 8-point moving sums (a CIC filter, gain 512) and publishes the
// latest filtered value, so Tick() can read it at any moment. Three moving sums are used instead of
// integrator/comb pairs so the result is valid at every fast sample, not only every 8th.
extern cregister volatile unsigned int IER;
// State is exported (extern "C") so a debugger can find it in the link map.
extern "C" {
uint16_t m32_cicN = 0;
uint16_t m32_cicX[M32_OS];                     // history of inputs, stage 1 and stage 2 sums
uint16_t m32_cicS1[M32_OS];
uint32_t m32_cicS2[M32_OS];
uint32_t m32_cicA1 = 0, m32_cicA2 = 0, m32_cicA3 = 0;   // running sums of the three stages
volatile uint32_t m32_fastCount = 0;           // interrupts handled, to check the 80 kHz rate
volatile int16_t m32_cvOver = 0;               // filtered input, 2048 - ADC counts (same scale as TempoCVInRaw)
}
extern "C" M32_RAMFUNC __interrupt void Mother32_FastIsr(void)
{
#ifdef M32_PROFILE
    uint32_t pf_start = PF_NOW();
#endif
    uint16_t x = M32_R16(M32_ADCRESULT + 2) & 0x0FFF;      // the conversion started by the previous interrupt
    M32_R16(M32_ADCSOCFRC) = 0x0004;                       // start the next one now: fixed sampling instant
    uint16_t i = m32_cicN;
    m32_cicA1 += (uint32_t)x - m32_cicX[i];  m32_cicX[i] = x;               // moving sum of 8 inputs (32-bit maths: int is 16 bits)
    uint16_t s1 = (uint16_t)m32_cicA1;
    m32_cicA2 += (uint32_t)s1 - m32_cicS1[i]; m32_cicS1[i] = s1;            // moving sum of 8 sums
    m32_cicA3 += m32_cicA2 - m32_cicS2[i];      m32_cicS2[i] = m32_cicA2;       // and again
    m32_cicN = (i + 1) & (M32_OS - 1);
    m32_fastCount++;
#ifdef M32_PROFILE
    { uint32_t pf_c = pf_start - PF_NOW(); m32_pfFastTotal += pf_c; PF_ADD(m32_pfFast, pf_c); }
#endif
    m32_cvOver = (int16_t)(2048 - (int32_t)((m32_cicA3 + (1UL << (M32_OS_SHIFT - 1))) >> M32_OS_SHIFT));   // / gain, rounded
}

// tick overruns: a tick still running when the next one is due (seen as INT1.7 pending again at its end)
extern "C" { volatile uint16_t m32_overruns = 0; }

extern "C" M32_RAMFUNC __interrupt void Mother32_TickIsr(void)
{
    // The tick takes up to ~100 us (the panel scan alone ~38 us), so let the fast timer interrupt it
#ifdef M32_PROFILE
    uint32_t pf_tick0 = PF_NOW();
#endif
    uint16_t ier = IER;
    IER = 0x1000;                                    // only INT13 (the fast timer) may interrupt us
    M32_R16(M32_PIEACK) = 0x0001;                    // acknowledge PIE group 1
    asm(" CLRC INTM");
    Mother32_Tick();
    asm(" SETC INTM");
    if (M32_R16(M32_PIEIFR1) & 0x0040) m32_overruns++;   // the next tick is already pending: this one overran
    IER = ier;
#ifdef M32_PROFILE
    PF_ADD(m32_pfTick, pf_tick0 - PF_NOW());
#endif
}

// Runs before C/C++ initialisation (TI runtime hook): stop the watchdog early.
extern "C" int _system_pre_init(void)
{
    M32_EALLOW();
    M32_R16(M32_WDCR) = 0x0068;
    M32_EDIS();
    return 1;
}

#pragma CODE_SECTION(".TI.ramfunc")   /* C++ form: applies to the next function */
static void Mother32_InitFlash(void)
{
    M32_EALLOW();
    M32_R16(M32_FOPT) = 0x0001;                      // pipeline on
    M32_R16(M32_FBANKWAIT) = 0x0101;                 // wait states for 40 MHz
    M32_R16(M32_FOTPWAIT) = 0x0001;
    M32_R16(M32_FSTDBY) = 0x01FF;
    M32_R16(M32_FACTIVE) = 0x01FF;
    M32_EDIS();
    asm(" RPT #7 || NOP");
}

// RAM-resident code (.TI.ramfunc): linked to load in flash and run in L0 for flash builds,
// so it must be copied before use. In RAM builds load and run addresses are the same.
extern "C" { extern uint16_t RamfuncsLoadStart, RamfuncsLoadSize, RamfuncsRunStart; }
static void Mother32_CopyRamfuncs(void)
{
    uint16_t *src = &RamfuncsLoadStart, *dst = &RamfuncsRunStart;
    uint32_t n = (uint32_t)&RamfuncsLoadSize;
    if (src != dst) for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

// Entering update mode. After a normal boot the bootloader's flash routines (TI Flash API) sit in RAM
// 0x0040-0x056F, copied from 0x3F6000-0x3F652F. Firmware may reuse that RAM as scratch (.bootram), so the
// routines are copied back and verified before use. Everything here runs from RAM, because sector D holds
// the start of this program and may be erased underneath it (fallback).
#define M32_FAPI_RAM      0x0040
#define M32_FAPI_FLASH    0x3F6000UL
#define M32_FAPI_WORDS    0x0530
#define M32_FAPI_VERSION  0x056D        // Flash_APIVersionHex(), must return 0x0200
#define M32_FAPI_ERASE    0x0110        // Flash_Erase(sector mask, status*)
#define M32_FAPI_PROGRAM  0x0054        // Flash_Program(flash*, buffer*, length, status*)
#define M32_APP_MARKER    0x3F5FBCUL    // u32 app-valid marker
#define M32_FAPI_SCALE    0x0D04        // Flash_CPUScaleFactor (u32, PIE reserved slot)
#define M32_FAPI_CALLBACK 0x0D02        // Flash_CallbackPtr (u32)
#define M32_SECTOR_D      0x0008        // the only sector this code ever erases

typedef uint16_t (*Mother32_FapiVersionFn)(void);
typedef uint16_t (*Mother32_FapiEraseFn)(uint16_t mask, uint16_t *status);
typedef uint16_t (*Mother32_FapiProgramFn)(uint16_t *flash, uint16_t *buf, uint32_t len, uint16_t *status);

#pragma CODE_SECTION(".TI.ramfunc")
static bool Mother32_InvalidateAppAndReset(void)
{
    asm(" SETC INTM");
    // Firmware may have used this RAM as scratch (.bootram), so copy the flash routines back first
    // (their master copy is only read). Not when the stack itself lives in this area.
    volatile uint16_t here = 0;
    if ((uint32_t)&here < 0x0570UL) { asm(" CLRC INTM"); return false; }
    for (uint16_t i = 0; i < M32_FAPI_WORDS; i++)
        M32_R16(M32_FAPI_RAM + i) = *(volatile uint16_t *)(M32_FAPI_FLASH + i);
    // the RAM copy must match its master copy exactly, or we must not call it
    for (uint16_t i = 0; i < M32_FAPI_WORDS; i++)
        if (M32_R16(M32_FAPI_RAM + i) != *(volatile uint16_t *)(M32_FAPI_FLASH + i)) {
            asm(" CLRC INTM");
            return false;
        }
    if (((Mother32_FapiVersionFn)M32_FAPI_VERSION)() != 0x0200) { asm(" CLRC INTM"); return false; }

    M32_EALLOW();
    M32_R32(M32_FAPI_SCALE) = 0x00800000UL;      // CPU scale factor for 40 MHz
    M32_R32(M32_FAPI_CALLBACK) = 0;
    M32_EDIS();
    uint16_t status[8];
    // invalidate the app: program the marker to 0 (bits 1 -> 0 only,
    // no erase, microseconds). If it does not read back as 0, erase sector D instead.
    uint16_t zeros[2] = { 0, 0 };
    volatile uint16_t *marker = (volatile uint16_t *)M32_APP_MARKER;
    ((Mother32_FapiProgramFn)M32_FAPI_PROGRAM)((uint16_t *)M32_APP_MARKER, zeros, 2UL, status);
    if (marker[0] != 0 || marker[1] != 0)
        ((Mother32_FapiEraseFn)M32_FAPI_ERASE)(M32_SECTOR_D, status);

    // reset: enable the watchdog, then write a bad WDCHK value (immediate reset); if that
    // does not reset, the unserviced watchdog does within about 13 ms
    M32_EALLOW();
    M32_R16(M32_WDCR) = 0x0028;
    M32_R16(M32_WDCR) = 0x0000;
    for (;;) {}
}

bool Mother32::EnterUpdateMode() { return Mother32_InvalidateAppAndReset(); }

static void Mother32_DelayLoops(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) {} }

Mother32::Mother32()
    : ticks_(0), leds_(0), tempoLED_(0), inRaw_(kActiveLowMask), inStable_(0), inCandidate_(0),
      edgesUp_(0), edgesDown_(0), resetStable_(0), resetCandidate_(0), resetCount_(0),
      resetEdgeUp_(false), resetEdgeDown_(false), envPulseLeft_(0), accentLeft_(0), envRetrigger_(false),
      midiHead_(0), midiTail_(0), runStatus_(0), midiCount_(0), midiNeeded_(0), inSysEx_(false),
      sysPos_(0), cvRaw_(0)
{
    sysBuf_[0] = sysBuf_[1] = sysBuf_[2] = 0;
    deb0_ = deb1_ = deb2_ = 0;
    scanPhase_ = kScanDivider - 1;               // the first tick is a scan tick
    scanPending_ = false;
    pendUp_ = pendDown_ = 0;
    scanLatchTick_ = 0;
    for (int16_t j = 0; j < 4; j++) { jackRiseTick_[j] = 0; jackRiseCount_[j] = 0; }
    for (int16_t i = 0; i < 5; i++) adcAcc_[i] = 0;
    midiData_[0] = midiData_[1] = 0;
}

void Mother32::Run()
{
    asm(" SETC INTM");
    instance_ = this;
    Mother32_CopyRamfuncs();
    InitSystem();
    InitGPIO();
    InitPWM();
    InitSPI();
    InitADC();
    InitSCI();
    InitI2C();
    LoadCalibration();
    PitchMillivolts(0);
    AssignMillivolts(0);
#ifdef M32_PROFILE
    M32_R16(M32_T2TCR) = 0x0010;
    M32_R32(M32_T2PRD) = 0xFFFFFFFFUL;
    M32_R16(M32_T2TPR) = 0;
    M32_R16(M32_T2TCR) = 0x0C20;                                 // reload, free run, start; no interrupt
    {   // idle-loop speed with no interrupts: passes in 50 ms (2,000,000 cycles)
        uint32_t t0 = PF_NOW(), n = 0;
        while (t0 - PF_NOW() < 2000000UL) { Idle(); n++; }
        m32_idleBase = n;
    }
#endif
    InitTimerAndInterrupts();
    for (;;) {
        Idle();
#ifdef M32_PROFILE
        m32_idleCount++;
#endif
    }
}

// --- system: watchdog, calibration, clocks, flash, peripheral clocks ------------------
void Mother32::InitSystem()
{
    M32_EALLOW();
    M32_R16(M32_WDCR) = 0x0068;                                  // watchdog off
    M32_R16(M32_PCLKCR0) |= 0x0008;                              // ADC clock on for Device_cal
    ((void (*)(void))0x3D7C80)();                                // TI factory calibration (OTP)
    M32_R16(M32_PCLKCR0) &= ~0x0008;
    // Clock source: internal oscillator 1, crystal/XCLKIN off
    M32_R16(M32_CLKCTL) &= ~0x0100;
    M32_R16(M32_CLKCTL) &= ~0x0001;
    M32_R16(M32_CLKCTL) |= 0x2000;
    M32_R16(M32_CLKCTL) |= 0x4000;
    M32_R16(M32_CLKCTL) |= 0x0400;
    // PLL: 10 MHz x 8 / 2 = 40 MHz (TI InitPll sequence)
    if (M32_R16(M32_PLLSTS) & 0x0008) asm(" ESTOP0");            // missing-clock flag
    M32_R16(M32_PLLSTS) &= ~0x0180;                              // DIVSEL = /4 while changing
    if ((M32_R16(M32_PLLCR) & 0x000F) != 8) {
        M32_R16(M32_PLLSTS) |= 0x0040;                           // MCLKOFF
        M32_R16(M32_PLLCR) = 8;
        M32_EDIS();
        while (!(M32_R16(M32_PLLSTS) & 0x0001)) {}               // PLLLOCKS
        M32_EALLOW();
        M32_R16(M32_PLLSTS) &= ~0x0040;
    }
    M32_R16(M32_PLLSTS) = (M32_R16(M32_PLLSTS) & ~0x0180) | (2 << 7);   // DIVSEL = /2
    M32_R16(M32_XCLK) |= 0x0003;                                 // XCLKOUT off
    M32_R16(M32_LOSPCP) = 2;                                     // LSPCLK = 10 MHz
    M32_R16(M32_PCLKCR0) = 0x0001 | 0x0008 | 0x0010 | 0x0100 | 0x0400;  // HRPWM ADC I2C SPI SCI
    M32_R16(M32_PCLKCR1) = 0x000F;                               // ePWM1-4
    M32_R16(M32_PCLKCR3) = 0x0700 | 0x2000;                      // CPU timers 0-2, GPIO input
    M32_EDIS();
    Mother32_InitFlash();
}

// --- pins -----------------------------------------------------------------------------
void Mother32::InitGPIO()
{
    M32_EALLOW();
    M32_R32(M32_GPACLEAR) = 0x000810E9UL;      // outputs start low: GPIO0,3,5,6,7,12,19
    M32_R32(M32_GPAMUX1) = 0x00000110UL;       // GPIO2 = EPWM2A (ASSIGN), GPIO4 = EPWM3A (pitch)
    M32_R32(M32_GPAMUX2) = 0x05000015UL;       // GPIO16-18 SPI-A, GPIO28/29 SCI-A (MIDI)
    M32_R32(M32_GPAQSEL2) |= 0x03000000UL;     // GPIO28 (MIDI RX) asynchronous
    M32_R32(M32_GPAPUD) = 0x000D00FDUL;        // pull-ups only on inputs (GPIO1 = RESET button)
    M32_R32(M32_GPADIR) = 0x000810FDUL;        // outputs: 0,2-7,12,19; GPIO1 input
    M32_EDIS();
}

// --- CV outputs: ePWM2 (ASSIGN) and ePWM3 (pitch), 100 kHz with high-resolution edge ---
void Mother32::InitPWM()
{
    M32_EALLOW();
    M32_R16(M32_PCLKCR0) &= ~0x0004;                             // TBCLKSYNC off while configuring
    M32_R16(M32_EPWM1 + M32_HRMSTEP) = M32_HRMSTEP_VALUE;
    const uint16_t bases[2] = { M32_EPWM2, M32_EPWM3 };
    for (int16_t i = 0; i < 2; i++) {
        uint16_t b = bases[i];
        M32_R16(b + M32_TBPRD) = M32_PWM_PERIOD;
        M32_R16(b + M32_TBPHS) = 0;
        M32_R16(b + M32_TBCTR) = 0;
        M32_R16(b + M32_TBCTL) = 0xC030;       // up-count, free run under debug, no sync out
        M32_R16(b + M32_CMPCTL) = 0x0000;      // CMPA shadowed, loaded at counter zero
        M32_R16(b + M32_AQCTLA) = 0x0012;      // high at zero, low at CMPA
        M32_R16(b + M32_HRCNFG) = 0x0042;      // MEP on falling edge, CMPAHR, auto-scaling
    }
    M32_R16(M32_PCLKCR0) |= 0x0004;                              // start time bases together
    M32_EDIS();
}

// --- panel shift registers on SPI-A --------------------------------------------------
void Mother32::InitSPI()
{
    M32_R16(M32_SPICCR) = 0x0047;              // reset, clock polarity 1, 8-bit
    M32_R16(M32_SPICTL) = 0x000E;              // clock phase, master, talk
    M32_R16(M32_SPIBRR) = 3;                   // 10 MHz / 4 = 2.5 MHz
    M32_R16(M32_SPIPRI) = 0x0010;              // keep running under debug halts
    M32_R16(M32_SPICCR) = 0x00C7;              // release reset
    M32_R16(M32_SPIFFTX) = 0xE040;             // FIFOs on: a scan writes 3 words and collects 3 later
    M32_R16(M32_SPIFFRX) = 0x2044;
    M32_R16(M32_SPIFFCT) = 0;
}

// The panel is a 24-bit SPI burst (3 x 74HC595 out, 3 x 74HC165 in). It is started on one tick and
// collected on the next scan tick through the SPI FIFO, so the CPU never waits for the 9.6 us transfer.
// Pulse GPIO19 (latch), then three bytes, bits 0-7 first.
void Mother32::ScanStart()
{
    scanLatchTick_ = ticks_;
    M32_R32(M32_GPASET) = 1UL << 19;
    asm(" RPT #2 || NOP");
    M32_R32(M32_GPACLEAR) = 1UL << 19;
    asm(" RPT #2 || NOP");
    uint32_t out = leds_;
    M32_R16(M32_SPITXBUF) = (uint16_t)(out << 8);
    M32_R16(M32_SPITXBUF) = (uint16_t)out & 0xFF00;
    M32_R16(M32_SPITXBUF) = (uint16_t)(out >> 8) & 0xFF00;
    scanPending_ = true;
}

void Mother32::ScanFinish()
{
    if (!scanPending_) return;
    scanPending_ = false;
    if (((M32_R16(M32_SPIFFRX) >> 8) & 0x1F) < 3) return;       // burst not complete: keep the old inputs
    uint32_t r = M32_R16(M32_SPIRXBUF) & 0xFF;
    r |= (uint32_t)(M32_R16(M32_SPIRXBUF) & 0xFF) << 8;
    r |= (uint32_t)(M32_R16(M32_SPIRXBUF) & 0xFF) << 16;
    UpdateInputs(r);
}

void Mother32::UpdateInputs(uint32_t raw)
{
    inRaw_ = raw;
    uint32_t now = (inRaw_ ^ kActiveLowMask) & 0x00FFFFFFUL;    // 1 = pressed / gate high
    uint32_t prev = inStable_;
    uint32_t jackMask = 0x000F0000UL;                           // jacks (bits 16-19): no debounce
    inStable_ = (inStable_ & ~jackMask) | (now & jackMask);
    // keys and buttons: an input flips after M32_DEBOUNCE_SCANS (8) scans that disagree with its stable
    // value. Counted for all 24 inputs at once in three bit-planes (a vertical 3-bit counter).
    uint32_t d = (now ^ inStable_) & 0x00FFFFFFUL & ~jackMask;  // inputs that disagree
    deb0_ &= d; deb1_ &= d; deb2_ &= d;                         // agreeing inputs start again from 0
    uint32_t k0 = d & deb0_;  deb0_ ^= d;
    uint32_t k1 = k0 & deb1_; deb1_ ^= k0;
    uint32_t k2 = k1 & deb2_; deb2_ ^= k1;                      // k2: count rolled over from 7 to 0
    inStable_ ^= k2;
    uint32_t up = inStable_ & ~prev;
    edgesUp_ |= up;
    if (up & jackMask)
        for (uint16_t j = 0; j < 4; j++)
            if (up & (1UL << kJackBit[j])) { jackRiseTick_[j] = scanLatchTick_; jackRiseCount_[j]++; }
    edgesDown_ |= prev & ~inStable_;
    // RESET button on GPIO1 (active low)
    uint16_t r = (M32_R32(M32_GPADAT) & 0x2UL) ? 0 : 1;
    if (r != resetStable_) {
        if (++resetCount_ >= M32_DEBOUNCE_SCANS) {
            resetStable_ = r; resetCount_ = 0;
            if (r) resetEdgeUp_ = true; else resetEdgeDown_ = true;
        }
    } else {
        resetCount_ = 0;
    }
}

// --- ADC: five channels --------------------------------------------------------------
void Mother32::InitADC()
{
    M32_EALLOW();
    M32_R16(M32_ADCCTL1) |= 0x00E0;            // bandgap, reference, analog power up
    M32_R16(M32_ADCCTL1) &= ~0x0008;           // internal reference
    M32_R16(M32_ADCCTL1) |= 0x4000;            // ADC enable
    M32_EDIS();
    Mother32_DelayLoops(10000);                // > 1 ms power-up
    M32_EALLOW();
    M32_R16(M32_ADCCTL1) |= 0x0004;            // interrupt pulse at end of conversion
    M32_R16(M32_ADCCTL2) = 0x0001;             // ADC clock = SYSCLK / 2
    // SOCx: CHSEL << 6 | ACQPS 12, software/ADCINT1 triggered
    M32_R16(M32_ADCSOC0 + 0) = (9 << 6) | 12;  // ADCINB1 TEMPO knob
    M32_R16(M32_ADCSOC0 + 1) = (11 << 6) | 12; // ADCINB3 GLIDE knob
    M32_R16(M32_ADCSOC0 + 2) = (14 << 6) | 12; // ADCINB6 TEMPO CV jack
    M32_R16(M32_ADCSOC0 + 3) = (4 << 6) | 12;  // ADCINA4 gate readback
    M32_R16(M32_ADCSOC0 + 4) = (6 << 6) | 12;  // ADCINA6 SUSTAIN switch
    // No automatic triggering: every conversion is started by software. The fast interrupt starts the CV
    // channel (SOC2) at a fixed point in each of its periods, so the sampling times are as regular as the
    // interrupt itself; the other four channels are converted once per scan tick.
    M32_R16(M32_INTSEL1N2) = 0;                // no ADC interrupts
    M32_R16(M32_ADCSOCSEL) = 0;                // no SOC is triggered by an ADC interrupt
    M32_R16(M32_ADCSOCFRC) = 0x001F;           // first conversions, so the first reads hold data
    M32_EDIS();
}

// --- MIDI IN on SCI-A, polled from the tick ------------------------------------------
// KB and ASSIGN output coefficients (see the output calibration section below); defaults = typical values
static int32_t g_pitchK = M32_PITCH_K4096, g_pitchB = ((int32_t)M32_PITCH_OFFSET_MV * M32_PITCH_K4096) >> 12, g_pitchQ = 0;
static int32_t g_assignK = M32_ASSIGN_K4096, g_assignB = ((int32_t)M32_ASSIGN_OFFSET_MV * M32_ASSIGN_K4096) >> 12, g_assignQ = 0;

// --- factory calibration in the EEPROM ---------------------------------------------------------------------
// 16-bit words, little-endian bytes, word w at byte 2w. 0x80 = 0xDEAF when calibrated. 0x81-0x89: three ADC
// input ranges (not used here). 0x8A-0x8F (KB) and 0x90-0x95 (ASSIGN): output calibrations, each gain
// (1/100000), offset (uV) and bow (uV) as 32-bit values (low word first), meaning
// y = x * g / 100000 + o + q * (1 - t^2 / 10^6), t = (x - 5,000,000) / 5000, x = 5,000,000 + output in uV,
// PWM counts = y * 400 / 10,000,000. Applied by default; -DM32_NO_EEPROM_CAL uses the default constants.
extern "C" {
volatile int16_t m32_calValid = -2;            // -2 not read, -1 I2C error, 0 no calibration, 1 calibration found
volatile int16_t m32_calApplied = 0;           // 1 if the EEPROM values are in use
uint16_t m32_calRaw[22];                       // EEPROM words 0x80..0x95
}

static int32_t Mother32_Cal32(uint16_t i) { return (int32_t)((uint32_t)m32_calRaw[i] | ((uint32_t)m32_calRaw[i + 1] << 16)); }

// output coefficients from a factory (g, o, q) triple: counts256 = y * 256 / 25000 = 0.01024 * y
static void Mother32_CalToCoeffs(uint16_t i, int32_t &k, int32_t &b, int32_t &q)
{
    int64_t g = Mother32_Cal32(i), o = Mother32_Cal32(i + 2), qq = Mother32_Cal32(i + 4);
    k = (int32_t)((g * 4194304LL) / 10000000LL);             // 0.01024 * g / 100 per mV, x 4096
    b = (int32_t)(((50LL * g + o) * 1024LL) / 100000LL);
    q = (int32_t)((qq * 1024LL) / 100000LL);
}

void Mother32::LoadCalibration()
{
    uint16_t bytes[44];
    if (!EepromRead(0x100, bytes, 44)) { m32_calValid = -1; return; }
    for (uint16_t i = 0; i < 22; i++) m32_calRaw[i] = bytes[2 * i] | (bytes[2 * i + 1] << 8);
    m32_calValid = (m32_calRaw[0] == 0xDEAF) ? 1 : 0;
#ifndef M32_NO_EEPROM_CAL
    if (m32_calValid == 1) {
        Mother32_CalToCoeffs(10, g_pitchK, g_pitchB, g_pitchQ);     // words 0x8A-0x8F: KB
        Mother32_CalToCoeffs(16, g_assignK, g_assignB, g_assignQ);  // words 0x90-0x95: ASSIGN
        m32_calApplied = 1;
    }
#endif
}

// --- I2C EEPROM -------------------------------------------------------------------------
void Mother32::InitI2C()
{
    M32_EALLOW();
    M32_R32(M32_GPBMUX1) = (M32_R32(M32_GPBMUX1) & ~0xFUL) | 0x5UL;   // GPIO32 = SDAA, GPIO33 = SCLA
    M32_R32(M32_GPBQSEL1) |= 0xFUL;                                     // asynchronous inputs
    M32_R32(M32_GPBPUD) &= ~0x3UL;                                      // pull-ups on
    M32_EDIS();
    M32_R16(M32_I2CMDR) = 0;                   // module in reset while configuring
    M32_R16(M32_I2CPSC) = 4;                   // 40 MHz / 5 = 8 MHz module clock
    M32_R16(M32_I2CCLKL) = 35;                 // 8 MHz / ((35 + 5) + (35 + 5)) = 100 kHz
    M32_R16(M32_I2CCLKH) = 35;
    M32_R16(M32_I2COAR) = 0x0001;
    M32_R16(M32_I2CIER) = 0;
    M32_R16(M32_I2CFFTX) = 0;                  // no FIFOs: one byte at a time, polled
    M32_R16(M32_I2CFFRX) = 0;
    M32_R16(M32_I2CMDR) = 0x0020;              // out of reset
}

// wait for a status bit, about 5 ms at most; a NACK stops the transfer
static bool Mother32_I2CWait(uint16_t bit)
{
    for (uint32_t t = 0; t < 20000UL; t++) {
        uint16_t st = M32_R16(M32_I2CSTR);
        if (st & 0x0002) {                                   // NACK
            M32_R16(M32_I2CMDR) |= 0x0800;                   // STP
            M32_R16(M32_I2CSTR) = 0x0002;
            return false;
        }
        if (st & bit) return true;
    }
    return false;
}

bool Mother32::EepromRead(uint16_t addr, uint16_t *dst, uint16_t n)
{
    for (uint32_t t = 0; (M32_R16(M32_I2CMDR) & 0x0800) || (M32_R16(M32_I2CSTR) & 0x1000); t++)
        if (t > 20000UL) return false;                       // previous stop pending or bus busy
    M32_R16(M32_I2CSAR) = 0x50;
    M32_R16(M32_I2CCNT) = 2;
    M32_R16(M32_I2CDXR) = addr >> 8;                         // address high byte
    M32_R16(M32_I2CMDR) = 0x2620;                            // STT, master, transmit, no stop
    if (!Mother32_I2CWait(0x0010)) return false;             // XRDY
    M32_R16(M32_I2CDXR) = addr & 0xFF;                       // address low byte
    if (!Mother32_I2CWait(0x0004)) return false;             // ARDY: both sent
    M32_R16(M32_I2CSTR) = 0x0004;
    M32_R16(M32_I2CCNT) = n;
    M32_R16(M32_I2CMDR) = 0x2C20;                            // repeated start, master, receive, stop after n
    for (uint16_t i = 0; i < n; i++) {
        if (!Mother32_I2CWait(0x0008)) return false;         // RRDY
        dst[i] = M32_R16(M32_I2CDRR) & 0xFF;
    }
    return true;
}

void Mother32::InitSCI()
{
    M32_R16(M32_SCICCR) = 0x0007;              // 8N1
    M32_R16(M32_SCICTL1) = 0x0003;             // RX + TX enabled, in reset
    M32_R16(M32_SCIHBAUD) = 0;
    M32_R16(M32_SCILBAUD) = 0x27;              // 10 MHz / (8 * 40) = 31250 baud
    M32_R16(M32_SCIFFTX) = 0xE040;             // FIFO enhancements on
    M32_R16(M32_SCIFFRX) = 0x2040;             // RX FIFO on, cleared
    M32_R16(M32_SCIPRI) = 0x0010;              // keep running under debug halts
    M32_R16(M32_SCICTL1) = 0x0023;             // release reset
}

void Mother32::PollMIDI()
{
    if (M32_R16(M32_SCIRXST) & 0x0080) {       // receive error: reset the receiver
        M32_R16(M32_SCICTL1) &= ~0x0020;
        M32_R16(M32_SCICTL1) |= 0x0020;
    }
    while (M32_R16(M32_SCIFFRX) & 0x1F00) ParseMIDI(M32_R16(M32_SCIRXBUF) & 0xFF);
}

void Mother32::ParseMIDI(uint16_t b)
{
    MIDIMessage m;
    if (b >= 0xF8) {                                   // real-time: deliver immediately
        m.status = b; m.data1 = 0; m.data2 = 0;
    } else if (b >= 0x80) {
        if (b == 0xF7 && inSysEx_ && sysPos_ == 3 &&
            sysBuf_[0] == 0x04 && sysBuf_[1] == 0x14 && sysBuf_[2] == 0x11)
            EnterUpdateMode();                          // ERASE: does not return if it can run
        inSysEx_ = (b == 0xF0);
        sysPos_ = 0;
        midiCount_ = 0;
        if (b >= 0xF0) { runStatus_ = 0; return; }     // system common/SysEx: not delivered
        runStatus_ = b;
        uint16_t t = b & 0xF0;
        midiNeeded_ = (t == 0xC0 || t == 0xD0) ? 1 : 2;
        return;
    } else {
        if (inSysEx_) {
            if (sysPos_ < 3) sysBuf_[sysPos_] = b;
            if (sysPos_ < 4) sysPos_++;
            return;
        }
        if (!runStatus_) return;
        midiData_[midiCount_++] = b;
        if (midiCount_ < midiNeeded_) return;
        midiCount_ = 0;
        m.status = runStatus_; m.data1 = midiData_[0]; m.data2 = midiNeeded_ == 2 ? midiData_[1] : 0;
    }
    uint16_t next = (midiHead_ + 1) & 15;
    if (next != midiTail_) { midiQ_[midiHead_] = m; midiHead_ = next; }   // drop if full
}

bool Mother32::MIDIRead(MIDIMessage &m)
{
    if (midiTail_ == midiHead_) return false;
    m = midiQ_[midiTail_];
    midiTail_ = (midiTail_ + 1) & 15;
    return true;
}

// --- timer 0 interrupt at kTickHz -------------------------------------------------
void Mother32::InitTimerAndInterrupts()
{
    M32_EALLOW();
    M32_R16(M32_PIECTRL) = 0;                                          // PIE off while filling
    for (uint16_t v = 0; v < 128; v++)
        M32_R32(M32_PIEVECT + 2 * v) = (uint32_t)&Mother32_UnusedIsr;
    M32_R32(M32_PIEVECT + 0x4C) = (uint32_t)&Mother32_TickIsr;         // INT1.7 = TINT0
    M32_R32(M32_PIEVECT + 0x1A) = (uint32_t)&Mother32_FastIsr;         // INT13 = TINT1 (needs EALLOW, like the rest)
    M32_R16(M32_PIECTRL) = 0x0001;                                     // PIE on
    M32_EDIS();
    M32_R16(M32_T0TCR) = 0x0010;                                       // stop
    M32_R32(M32_T0PRD) = M32_TICK_CYCLES - 1;
    M32_R16(M32_T0TPR) = 0;
    {
        // the filter state lives in uninitialised RAM, which the runtime does not zero: a stale
        // history value would be subtracted without ever having been added and stay in the running
        // sums for good
        for (uint16_t i = 0; i < M32_OS; i++) { m32_cicX[i] = 0; m32_cicS1[i] = 0; m32_cicS2[i] = 0; }
        m32_cicA1 = m32_cicA2 = m32_cicA3 = 0;
        m32_cicN = 0;
        m32_cvOver = 0;
        M32_R16(M32_T1TCR) = 0x0010;
        M32_R32(M32_T1PRD) = M32_TICK_CYCLES / M32_OS - 1;
        M32_R16(M32_T1TPR) = 0;
        M32_R16(M32_T1TCR) = 0x4C20;                                   // start the fast timer
        // start the tick timer half-way between two fast samples, so the tick never falls right next
        // to one (interrupts are still masked, so polling the counter is exact to a few cycles)
        uint32_t t;
        do { t = M32_R32(M32_T1TIM); } while (t > M32_TICK_CYCLES / M32_OS / 2 + 30 || t < M32_TICK_CYCLES / M32_OS / 2 - 30);
    }
    M32_R16(M32_T0TCR) = 0x4C20;                                       // reload, interrupt, free run, start
    M32_R16(M32_PIEIER1) |= 0x0040;
    asm(" OR IER, #0x0001");
    asm(" OR IER, #0x1000");                                          // INT13 (fast timer)
    asm(" CLRC DBGM");                                                 // allow real-time debug access
    asm(" CLRC INTM");
}

M32_RAMFUNC void Mother32::Tick()
{
    // Latch the filtered input first, before the panel work, so it is always read at the same moment
    // (reading it after the scan on some ticks gives a timing error that sounds like noise).
    cvRaw_ = m32_cvOver;
    // The periodic work is spread over different ticks of the scan period (about 2.5 kHz) so no single
    // tick carries all of it: phase 0 collects the panel inputs, phase 1 starts the next SPI burst (it
    // completes long before phase 0 comes round again), half-way the knobs and switch are smoothed.
    if (++scanPhase_ >= kScanDivider) scanPhase_ = 0;
    if (scanPhase_ <= 1) {
        PF_BEGIN(a);
        if (scanPhase_ == 0) { ScanFinish(); pendUp_ |= edgesUp_; pendDown_ |= edgesDown_; }
        else ScanStart();
        PF_END(a, m32_pfScan);
    }
    PF_BEGIN(b);
    PollMIDI();
    if (scanPhase_ == kScanDivider / 2 || ticks_ == 0) {               // slow inputs: one-pole smoothing at the scan rate
        for (int16_t i = 0; i < 5; i++) {
            int32_t raw = (int32_t)(M32_R16(M32_ADCRESULT + i) & 0x0FFF) << 4;
            if (ticks_ == 0) adcAcc_[i] = raw;
            adcAcc_[i] += (raw - adcAcc_[i]) >> 2;                     // tau about 1.7 ms
        }
        M32_R16(M32_ADCSOCFRC) = 0x001B;                               // convert the knobs and switch (SOC0, 1, 3, 4)
    }
    if (envRetrigger_) {                                               // second half of EnvelopeTrigger()
        envRetrigger_ = false;
        M32_R32(M32_GPASET) = 1UL << 0;
        envPulseLeft_ = (uint16_t)(kTickHz / 100);
    } else if (envPulseLeft_ && --envPulseLeft_ == 0) M32_R32(M32_GPACLEAR) = 1UL << 0;
    if (accentLeft_ && --accentLeft_ == 0) M32_R32(M32_GPACLEAR) = 1UL << 5;
    PF_END(b, m32_pfRest);

    PF_BEGIN(c);
    Process();
    PF_END(c, m32_pfProc);

    edgesUp_ = edgesDown_ = 0;
    resetEdgeUp_ = resetEdgeDown_ = false;
    ticks_++;
}

// --- accessors ----------------------------------------------------------------------
Mother32::PanelEvents Mother32::TakePanelEvents()
{
    PanelEvents e;
    e.pressed = e.released = 0;
    if (!(pendUp_ | pendDown_)) return e;    // usual case: nothing pending, no need to hold interrupts
    asm(" SETC INTM");                       // the tick must not add edges between the read and the clear
    e.pressed = pendUp_;  pendUp_ = 0;
    e.released = pendDown_; pendDown_ = 0;
    asm(" CLRC INTM");
    return e;
}
uint32_t Mother32::ButtonBit(Button b) { return 1UL << kButtonBit[b]; }
uint32_t Mother32::JackBit(JackIn j) { return 1UL << kJackBit[j]; }

static inline uint32_t Mother32_KeyMask(int16_t k) { return 1UL << (12 - k); }

bool Mother32::Key(int16_t k) const { return k >= 0 && k <= 12 && (inStable_ & Mother32_KeyMask(k)); }
bool Mother32::KeyPressed(int16_t k) const { return k >= 0 && k <= 12 && (edgesUp_ & Mother32_KeyMask(k)); }
bool Mother32::KeyReleased(int16_t k) const { return k >= 0 && k <= 12 && (edgesDown_ & Mother32_KeyMask(k)); }
bool Mother32::AnyKeyHeld() const { return (inStable_ & 0x1FFFUL) != 0; }

M32_RAMFUNC bool Mother32::ButtonHeld(Button b) const
{ return b == Reset ? resetStable_ != 0 : (inStable_ & (1UL << kButtonBit[b])) != 0; }
M32_RAMFUNC bool Mother32::ButtonPressed(Button b) const
{ return b == Reset ? resetEdgeUp_ : (edgesUp_ & (1UL << kButtonBit[b])) != 0; }
bool Mother32::ButtonReleased(Button b) const
{ return b == Reset ? resetEdgeDown_ : (edgesDown_ & (1UL << kButtonBit[b])) != 0; }

bool Mother32::JackHigh(JackIn j) const { return (inStable_ & (1UL << kJackBit[j])) != 0; }
bool Mother32::JackRising(JackIn j) const { return (edgesUp_ & (1UL << kJackBit[j])) != 0; }
bool Mother32::JackFalling(JackIn j) const { return (edgesDown_ & (1UL << kJackBit[j])) != 0; }

int16_t Mother32::Tempo() const { return (int16_t)(adcAcc_[0] >> 4); }
M32_RAMFUNC int16_t Mother32::Glide() const { return (int16_t)(adcAcc_[1] >> 4); }
bool Mother32::Sustain() const { return (adcAcc_[4] >> 4) > 2048; }
int16_t Mother32::TempoCVIn() const { return (int16_t)(2048 - (adcAcc_[2] >> 4)); }
M32_RAMFUNC int16_t Mother32::TempoCVInRaw() const { return cvRaw_; }

static inline int32_t Mother32_Clamp(int32_t v)
{
    if (v < 0) return 0;
    if (v > (int32_t)M32_PWM_PERIOD * 256) return (int32_t)M32_PWM_PERIOD * 256;
    return v;
}

M32_RAMFUNC static inline void Mother32_SetPWM(uint16_t base, int32_t counts256)
{
    counts256 = Mother32_Clamp(counts256);
    // CMPAHR (fraction in the high byte) and CMPA written together
    M32_R32(base + M32_CMPAHR) = ((uint32_t)(counts256 >> 8) << 16) | ((uint32_t)(counts256 & 0xFF) << 8);
}

M32_RAMFUNC void Mother32::PitchRaw(int32_t c) { Mother32_SetPWM(M32_EPWM3, c); }
M32_RAMFUNC void Mother32::AssignRaw(int32_t c) { Mother32_SetPWM(M32_EPWM2, c); }

static inline int32_t Mother32_ClampMv(int32_t mv)
{ return mv < -6000 ? -6000 : (mv > 6000 ? 6000 : mv); }

// --- output calibration -------------------------------------------------------------------
// counts256 = (tenths of mV * K / 10) / 4096 + B + bow, bow = Q * (1 - mv^2 / 25,000,000): a parabola that is Q
// at 0 V and 0 at +-5 V. When the factory calibration is present in the EEPROM (see LoadCalibration) the unit's own
// values are used; otherwise, or with -DM32_NO_EEPROM_CAL, the default constants.
// (coefficients g_pitchK/B/Q and g_assignK/B/Q are declared with the EEPROM calibration loader above)

static int32_t Mother32_TenthsToCounts(int32_t tenths, int32_t k, int32_t b, int32_t q)
{
    if (tenths < -60000L) tenths = -60000L; else if (tenths > 60000L) tenths = 60000L;   // +-6 V
    int32_t c = ((tenths * (k / 10)) >> 12) + b;
    if (q) {
        int32_t mv = tenths / 10;
        c += q - (q * ((mv * mv) / 1000)) / 25000;
    }
    return c;
}

int32_t Mother32::PitchMillivoltsToRaw(int32_t mv) { return Mother32_TenthsToCounts(mv * 10, g_pitchK, g_pitchB, g_pitchQ); }
int32_t Mother32::AssignMillivoltsToRaw(int32_t mv) { return Mother32_TenthsToCounts(mv * 10, g_assignK, g_assignB, g_assignQ); }

void Mother32::PitchMillivolts(int32_t mv) { PitchRaw(PitchMillivoltsToRaw(mv)); }

M32_RAMFUNC void Mother32::AssignMillivolts(int32_t mv) { AssignRaw(AssignMillivoltsToRaw(mv)); }

int32_t Mother32::PitchMIDINoteToRaw(int16_t note)
{
    // 1000/12 mV per semitone, computed in tenths of a millivolt to keep precision
    // x is at most about 110,000; x * (K/10) stays well inside 32 bits
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    int32_t tenths = ((int32_t)(note - 60) * 10000L) / 12;
    return Mother32_TenthsToCounts(tenths, g_pitchK, g_pitchB, g_pitchQ);
}

void Mother32::PitchMIDINote(int16_t note) { PitchRaw(PitchMIDINoteToRaw(note)); }

uint16_t Mother32::Overruns() { return m32_overruns; }

void Mother32::Gate(bool on)
{ if (on) M32_R32(M32_GPASET) = 1UL << 3; else M32_R32(M32_GPACLEAR) = 1UL << 3; }

void Mother32::Envelope(bool on)
{
    envRetrigger_ = false;
    envPulseLeft_ = 0;
    if (on) M32_R32(M32_GPASET) = 1UL << 0; else M32_R32(M32_GPACLEAR) = 1UL << 0;
}

void Mother32::EnvelopeTrigger()
{
    // A trigger while the envelope gate is already high needs a low gap to be seen as an edge: pull it low
    // now and let the next tick raise it (a gap of one tick), instead of waiting here with interrupts held
    if (M32_R32(M32_GPADAT) & 1UL) {
        envPulseLeft_ = 0;
        M32_R32(M32_GPACLEAR) = 1UL << 0;
        envRetrigger_ = true;
        return;
    }
    M32_R32(M32_GPASET) = 1UL << 0;
    envPulseLeft_ = (uint16_t)(kTickHz / 100);                                          // 10 ms
}

void Mother32::NoteGateOn()
{
    Gate(true);
    if (Sustain()) Envelope(true); else EnvelopeTrigger();
}

void Mother32::NoteGateOff() { Gate(false); Envelope(false); }

void Mother32::Accent()
{ M32_R32(M32_GPASET) = 1UL << 5; accentLeft_ = ((uint32_t)kTickHz * 3) / 200; } // 15 ms

void Mother32::KeyLED(int16_t i, bool on)
{
    if (i < 0 || i > 7) return;
    uint32_t m = 1UL << (7 - i);                 // bits 0-7 = key LEDs 8..1
    leds_ = on ? (leds_ | m) : (leds_ & ~m);
}

void Mother32::StepLED(int16_t i, Colour c)
{
    if (i < 0 || i > 7) return;
    uint32_t g = 1UL << (8 + i), r = 1UL << (16 + i);
    leds_ &= ~(g | r);
    if (c & Green) leds_ |= g;
    if (c & Red) leds_ |= r;
}

void Mother32::TempoLED(Colour c)
{
    // GPIO7 is red, GPIO6 green
    if (c & Red) M32_R32(M32_GPASET) = 1UL << 7; else M32_R32(M32_GPACLEAR) = 1UL << 7;
    if (c & Green) M32_R32(M32_GPASET) = 1UL << 6; else M32_R32(M32_GPACLEAR) = 1UL << 6;
}

void Mother32::AllLEDsOff() { leds_ = 0; TempoLED(Off); }

#endif // MOTHER32_NOIMPL
#endif // MOTHER32_H
