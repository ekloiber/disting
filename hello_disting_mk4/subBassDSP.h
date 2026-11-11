// Sub voice DSP core: hardware-independent, fixed point, no libm.
// Compiled into the firmware and into tools/subbass_host_test.c.
//
//   X : gate (high above 1 V, low below 0.5 V)
//   Y : velocity, 0..5 V, sampled when the gate opens
//   Z : V/oct pitch (knob + jack)
//   A : sub, Level 99 = 5 V peak at full envelope and velocity
//   B : envelope, 0..5 V (not scaled by velocity or Level)
//
// A gate that arrives after the previous note has released (below -60 dB)
// restarts the waveform at phase 0; during a release it keeps running.
//
// Units
//   voltages : volts with 19 fractional bits (1 V = 0x80000), as used by the framework
//   Z        : raw 10-bit ADC counts (0..1023)
//   params   : the algorithm's parameter values, indexed by the enum below

#ifndef SUBBASS_DSP_H
#define SUBBASS_DSP_H

enum {
    kSbShape = 0,   // 0..99   sine -> triangle -> saw -> square
    kSbDrive,       // 0..99   saturation on the sub
    kSbLevel,       // 0..99   sub level, 99 = 5 V peak
    kSbOctave,      // -4..0   octaves relative to Z
    kSbFine,        // -50..50 cents
    kSbQuant,       // 0..1    snap pitch to semitones
    kSbAttack,      // 0..99   0.5 ms .. 200 ms (time constant)
    kSbRelease,     // 0..99   10 ms .. 2 s (time constant)
    kSbNumParams
};

typedef struct {
    // per-sample state
    unsigned int phase;
    int zSmooth;                    // ADC counts, 16 fractional bits
    int gate;                       // X is high
    int env;                        // Q30, 1 << 30 = full
    int velTarget, vel;             // Q15; vel slews to velTarget to avoid clicks

    // control-rate state
    unsigned int phaseInc;
    int curSemi, quantPrimed;
    int attack, release;            // Q31 one-pole coefficients
    int shapePos;                   // 0 .. 3<<16
    int driveGain, driveMix;        // Q12 gain, Q15 dry/wet
    int levelVolts;                 // .19 volts at full envelope and velocity
} SubBassState;

// zCounts: current Z reading, used to start the smoother without a glide
void sbInit( SubBassState* s, int zCounts );

// Call at control rate (every few samples). zZeroQ16 = ADC counts at 0 V
// (16 fractional bits), zSemisPerCountQ16 = semitones per ADC count (Q16).
void sbControl( SubBassState* s, const signed char* params, int zZeroQ16, int zSemisPerCountQ16 );

// Call once per sample. gateIn, velIn: X and Y in volts .19.
void sbProcess( SubBassState* s, int gateIn, int velIn, int zCounts, int* outA, int* outB );

#endif // SUBBASS_DSP_H
