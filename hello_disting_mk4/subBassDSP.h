// Sub bass DSP core: hardware-independent, fixed point, no libm.
// Compiled into the firmware and into tools/subbass_host_test.c.
//
// Units
//   audio   : volts with 19 fractional bits (1 V = 0x80000), as used by the framework
//   Z       : raw 10-bit ADC counts (0..1023) from the Z knob + jack
//   params  : the algorithm's parameter values, indexed by the enum below

#ifndef SUBBASS_DSP_H
#define SUBBASS_DSP_H

enum {
    kSbLevel = 0,   // 0..99   sub level, 99 = 5 V peak
    kSbShape,       // 0..99   sine -> triangle -> saw -> square
    kSbOctave,      // -4..0   octaves relative to Z
    kSbFine,        // -50..50 cents
    kSbAttack,      // 0..99   0.5 ms .. 200 ms
    kSbRelease,     // 0..99   10 ms .. 2 s
    kSbSens,        // 0..99   follow mode: envelope gain, 50 = unity
                    //         trigger mode: threshold, -60 dB .. 0 dB re 5 V
    kSbEnvSrc,      // 0..1    0 = full input, 1 = after the high-pass
    kSbHpf,         // 0..99   0 = off, 1..99 = 20 Hz .. 300 Hz
    kSbQuant,       // 0..1    snap pitch to semitones
    kSbDrive,       // 0..99   saturation on the sub
    kSbMode,        // 0..1    0 = follow the input envelope, 1 = threshold-triggered attack/hold/release
    kSbHold,        // 0..99   trigger mode hold: 0 none, 1..99 = 2 ms .. 2 s
    kSbNumParams
};

typedef struct {
    // per-sample state
    int hpfLow[2], hpfBand[2];      // volts, 27 fractional bits
    int env;                        // volts, 27 fractional bits
    unsigned int phase;
    int zSmooth;                    // ADC counts, 16 fractional bits

    // control-rate state
    unsigned int phaseInc;
    int curSemi, quantPrimed;
    int hpfF, hpfGain, attack, release, sens;   // hpfGain Q30 corrects the SVF passband
    int shapePos;                   // 0 .. 3<<16
    int driveGain, driveMix;        // Q12 gain, Q15 dry/wet
    int levelVolts;                 // .19 volts at full envelope
    int envSrc;

    // trigger mode (attack / hold / release)
    int mode;
    int threshold, rearm;           // .19 volts on the peak detector
    int holdSamples;
    int det;                        // peak detector, .19 volts
    int armed;                      // detector has fallen below rearm since the last trigger
    int stage;                      // see kSbStage* in subBassDSP.c
    int holdCount;
    int ahr;                        // envelope, Q30 (1 << 30 = full)
} SubBassState;

// zCounts: current Z reading, used to start the smoother without a glide
void sbInit( SubBassState* s, int zCounts );

// Call at control rate (every few samples). zZeroQ16 = ADC counts at 0 V
// (16 fractional bits), zSemisPerCountQ16 = semitones per ADC count (Q16).
void sbControl( SubBassState* s, const signed char* params, int zZeroQ16, int zSemisPerCountQ16 );

// Call once per sample.
void sbProcess( SubBassState* s, int inL, int inR, int zCounts, int* outL, int* outR );

#endif // SUBBASS_DSP_H
