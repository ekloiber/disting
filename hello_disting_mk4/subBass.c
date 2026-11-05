// A1: Sub bass
//
//   X, Y : stereo audio in          A, B : X, Y (high-passed) + sub
//   Z    : V/oct for the sub (knob + jack), 0 V = C4 after 'Cal Z'
//
// Mode 0: constant tone at Level, no envelope (ignores the input level).
// Mode 1: the sub's level follows the input envelope (Attack, Release, Sens).
// Mode 2: the input crossing a threshold triggers attack -> hold -> release
//         (Attack, Hold, Release; Sens becomes Thresh).
// Mode 3: gate: fixed level while the input is above the threshold
//         (Attack, Release, Thresh; Hold unused).
//
// The DSP itself is in subBassDSP.c; this file connects it to the framework
// (calibrated I/O, parameters, display, menu).

#include <stdio.h>
#include <stdlib.h>
#include <xc.h>
#include <plib.h>

#include "constants.h"
#include "subBassDSP.h"
#include "zcal.h"

#define CONTROL_INTERVAL    16      // samples between control-rate updates (~4.7 kHz)

void subBass()
{
    // Display order is set by the enum in subBassDSP.h; entries here are
    // placed by name, so reordering the enum needs no change below.
    // Not const: Sens is renamed Thresh in trigger and gate modes.
    static char const * paramNames[kSbNumParams] = {
        [kSbMode]    = "Mode",
        [kSbShape]   = "Shape",
        [kSbDrive]   = "Drive",
        [kSbLevel]   = "Level",
        [kSbOctave]  = "Octave",
        [kSbFine]    = "Fine",
        [kSbQuant]   = "Quant",
        [kSbHpf]     = "HPF",
        [kSbEnvSrc]  = "Env src",
        [kSbSens]    = "Sens",
        [kSbAttack]  = "Attack",
        [kSbHold]    = "Hold",
        [kSbRelease] = "Release",
    };

    // min, max, default
    static const char ranges[ 3 * kSbNumParams ] = {
        [3*kSbMode]    =   0,  3,  0,   // 0 constant, 1 follow, 2 trigger (attack/hold/release), 3 gate
        [3*kSbShape]   =   0, 99,  0,
        [3*kSbDrive]   =   0, 99,  0,
        [3*kSbLevel]   =   0, 99, 50,
        [3*kSbOctave]  =  -4,  0, -2,
        [3*kSbFine]    = -50, 50,  0,
        [3*kSbQuant]   =   0,  1,  1,
        [3*kSbHpf]     =   0, 99,  0,
        [3*kSbEnvSrc]  =   0,  1,  0,
        [3*kSbSens]    =   0, 99, 50,
        [3*kSbAttack]  =   0, 99, 10,
        [3*kSbHold]    =   0, 99, 50,   // trigger mode, 50 = 63 ms
        [3*kSbRelease] =   0, 99, 30,
    };
    setParameterRanges( kSbNumParams, ranges );

    readZCal();
    if ( !zcalValid )
        scrollMessageOnce( "Z not calibrated, use Cal Z" );     // the LED font has no ':'

    static SubBassState st;
    sbInit( &st, pot );
    sbControl( &st, (const signed char*)parameters, zcal.m_zeroQ16, zcal.m_semisPerCountQ16 );

    int controlCountdown = CONTROL_INTERVAL;

    DECLARATIONS();

    while ( 1 )
    {
        IDLE();

        // calibrated input, volts .19
        int vL = ( ( inL - A[0] ) * Br[0] ) >> 24;
        int vR = ( ( inR - A[1] ) * Br[1] ) >> 24;

        int oL, oR;
        sbProcess( &st, vL, vR, pot, &oL, &oR );

        int cL = ( ( oL - D[0] ) * Er[0] ) >> 24;
        int cR = ( ( oR - D[1] ) * Er[1] ) >> 24;
        CLAMP( cL );
        CLAMP( cR );
        outL = cL;
        outR = cR;

        if ( --controlCountdown == 0 )
        {
            controlCountdown = CONTROL_INTERVAL;
            sbControl( &st, (const signed char*)parameters, zcal.m_zeroQ16, zcal.m_semisPerCountQ16 );
            paramNames[kSbSens] = parameters[kSbMode] >= kSbModeTrigger ? "Thresh" : "Sens";
        }

        SHOW_POT_HANDLING_PARAM_NAME()

        LOOP_END();
    }
}
