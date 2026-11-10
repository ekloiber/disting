// A1: Sub voice
//
//   X : gate (high above 1 V, low below 0.5 V)
//   Y : velocity, 0..5 V, sampled when the gate opens
//   Z : V/oct pitch (knob + jack), 0 V = C4 after 'Cal Z'
//   A : sub (Level 99 = 5 V peak at full velocity)
//   B : envelope, 0..5 V
//
// The envelope rises (Attack) while the gate is high, holds at full, and
// falls (Release) when it goes low. A new gate during the release attacks
// again from the current level.
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
    static char const * const paramNames[kSbNumParams] = {
        [kSbShape]   = "Shape",
        [kSbDrive]   = "Drive",
        [kSbLevel]   = "Level",
        [kSbOctave]  = "Octave",
        [kSbFine]    = "Fine",
        [kSbQuant]   = "Quant",
        [kSbAttack]  = "Attack",
        [kSbRelease] = "Release",
    };

    // min, max, default
    static const char ranges[ 3 * kSbNumParams ] = {
        [3*kSbShape]   =   0, 99,  0,
        [3*kSbDrive]   =   0, 99,  0,
        [3*kSbLevel]   =   0, 99, 50,
        [3*kSbOctave]  =  -4,  0, -2,
        [3*kSbFine]    = -50, 50,  0,
        [3*kSbQuant]   =   0,  1,  1,
        [3*kSbAttack]  =   0, 99, 10,
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

        // calibrated inputs, volts .19: X = gate, Y = velocity
        int vX = ( ( inL - A[0] ) * Br[0] ) >> 24;
        int vY = ( ( inR - A[1] ) * Br[1] ) >> 24;

        int oA, oB;
        sbProcess( &st, vX, vY, pot, &oA, &oB );

        int cA = ( ( oA - D[0] ) * Er[0] ) >> 24;
        int cB = ( ( oB - D[1] ) * Er[1] ) >> 24;
        CLAMP( cA );
        CLAMP( cB );
        outL = cA;
        outR = cB;

        if ( --controlCountdown == 0 )
        {
            controlCountdown = CONTROL_INTERVAL;
            sbControl( &st, (const signed char*)parameters, zcal.m_zeroQ16, zcal.m_semisPerCountQ16 );
        }

        SHOW_POT_HANDLING_PARAM_NAME()

        LOOP_END();
    }
}
