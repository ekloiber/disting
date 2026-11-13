// Sub voice DSP core. See subBassDSP.h for jacks and units.

#include "subBassDSP.h"
#include "subBassTables.h"

typedef long long i64;

#define VOLT            ( 1 << 19 )
#define SUB_PEAK        ( 5 * VOLT )            // Level 99, full envelope and velocity
#define ENV_OUT_PEAK    ( 5 * VOLT )            // B at full envelope
#define GATE_ON         ( VOLT )                // X above 1 V opens the gate...
#define GATE_OFF        ( VOLT / 2 )            // ...below 0.5 V closes it
#define Z_SILENT        ( 8 )                   // Z at or below this many counts = silent...
#define Z_FULL          ( 1015 )                // ...at or above this = full velocity
#define PITCH_SMOOTH_SHIFT ( 6 )                // ~0.85 ms smoothing on X
#define VEL_SLEW_SHIFT  ( 7 )                   // ~1.7 ms velocity smoothing (Z is read at control rate)
#define MAX_PHASE_INC   ( 0x10000000u )         // fs/16, about 4.7 kHz
#define QUANT_HYST      ( 0x9000 )              // 0.5625 semitone, Q16 (X is accurate; just avoid chatter at the midpoint)
#define Z_SMOOTH_SHIFT  ( 8 )                   // ~3.4 ms smoothing at 75 kHz
#define ENV_FULL        ( 1 << 30 )
#define ENV_ATTACK_AIM  ( ENV_FULL + ( ENV_FULL >> 2 ) )   // attack aims past full so it arrives
#define ENV_SILENT      ( ENV_FULL / 1000 )     // -60 dB: below this a new gate restarts the waveform

// velocity (Q15) from the smoothed Z reading (counts, 16 fractional bits)
static int velocityFromZ( int zQ16 )
{
    if ( zQ16 <= ( Z_SILENT << 16 ) )
        return 0;
    if ( zQ16 >= ( Z_FULL << 16 ) )
        return 32767;
    return (int)( ( (i64)( zQ16 - ( Z_SILENT << 16 ) ) * 32767 ) / ( ( Z_FULL - Z_SILENT ) << 16 ) );
}

static int paramClamp( const signed char* p, int i, int lo, int hi )
{
    int v = p[i];
    if ( v < lo ) v = lo;
    if ( v > hi ) v = hi;
    return v;
}

void sbInit( SubBassState* s, int pitchIn, int zCounts )
{
    s->phase = 0;
    s->pitchSmooth = pitchIn;
    s->zSmooth = zCounts << 16;
    s->gate = 0;
    s->env = 0;
    s->velTarget = velocityFromZ( s->zSmooth );
    s->vel = s->velTarget;                      // start at the knob's level, no ramp
    s->phaseInc = 0;
    s->curSemi = 0;
    s->quantPrimed = 0;
    s->attack = kAttackQ31[0];
    s->release = kReleaseQ31[0];
    s->shapePos = 0;
    s->driveGain = 4096;
    s->driveMix = 0;
    s->levelVolts = 0;
}

// 2^(octQ16 / 65536), returned as a phase increment relative to C4
static unsigned int phaseIncForOctaves( int octQ16 )
{
    int oct = octQ16 >> 16;                     // floor
    int frac = octQ16 & 0xffff;
    int idx = frac >> 8;
    int f8 = frac & 0xff;
    int m = kExp2Q30[idx] + (int)( ( (i64)( kExp2Q30[idx+1] - kExp2Q30[idx] ) * f8 ) >> 8 );
    i64 inc = ( (i64)kC4PhaseInc * m ) >> 30;
    if ( oct >= 0 )
    {
        if ( oct > 8 )
            return MAX_PHASE_INC;
        inc <<= oct;
    }
    else
    {
        if ( oct < -31 )
            return 0;
        inc >>= -oct;
    }
    if ( inc > MAX_PHASE_INC )
        inc = MAX_PHASE_INC;
    return (unsigned int)inc;
}

void sbControl( SubBassState* s, const signed char* p )
{
    // pitch: semitones relative to C4, Q16. 1 V (2^19) = 12 semitones (12 * 2^16), so * 1.5
    int semis = s->pitchSmooth + ( s->pitchSmooth >> 1 );
    if ( paramClamp( p, kSbQuant, 0, 1 ) )
    {
        int nearest = ( semis + 0x8000 ) >> 16;
        int diff = semis - s->curSemi * 65536;
        if ( !s->quantPrimed || diff > QUANT_HYST || diff < -QUANT_HYST )
            s->curSemi = nearest;
        s->quantPrimed = 1;
        semis = s->curSemi * 65536;
    }
    else
        s->quantPrimed = 0;
    semis += paramClamp( p, kSbOctave, -4, 0 ) * ( 12 << 16 );
    semis += paramClamp( p, kSbFine, -50, 50 ) * 65536 / 100;
    // octaves = semitones / 12   (357913941 = 2^32 / 12)
    int octQ16 = (int)( ( (i64)semis * 357913941 ) >> 32 );
    s->phaseInc = phaseIncForOctaves( octQ16 );

    s->levelVolts = paramClamp( p, kSbLevel, 0, 99 ) * ( SUB_PEAK / 99 );
    s->shapePos = paramClamp( p, kSbShape, 0, 99 ) * ( 3 * 65536 ) / 99;

    int drive = paramClamp( p, kSbDrive, 0, 99 );
    s->driveGain = 4096 + drive * ( 4 * 4096 ) / 99;    // 1x .. 5x
    s->driveMix = drive * 32767 / 99;

    s->attack = kAttackQ31[ paramClamp( p, kSbAttack, 0, 99 ) ];
    s->release = kReleaseQ31[ paramClamp( p, kSbRelease, 0, 99 ) ];

    // velocity from Z (knob + jack), Q15; per-sample slewing smooths these steps
    s->velTarget = velocityFromZ( s->zSmooth );
}

static int sineAt( unsigned int ph )
{
    int idx = ph >> 24;
    int frac = ( ph >> 8 ) & 0xffff;
    int a = kSineQ15[idx];
    return a + ( ( ( kSineQ15[idx+1] - a ) * frac ) >> 16 );
}

// waveforms, Q15, all start at zero and rise so the morph has no phase jump
static int waveAt( int which, unsigned int ph )
{
    switch ( which )
    {
        default:
        case 0:
            return sineAt( ph );
        case 1:
        {
            int u = (int)( ( ph + 0x40000000u ) >> 16 );    // 0..65535
            int d = u - 32768;
            if ( d < 0 ) d = -d;
            int t = 32768 - 2 * d;
            return t > 32767 ? 32767 : t;
        }
        case 2:
            return (int)ph >> 16;
        case 3:
            return ph < 0x80000000u ? 32767 : -32767;
    }
}

// cubic soft saturation, input Q15 (may exceed 1.0), output Q15 in [-1, 1]
static int saturate( int u )
{
    if ( u >= 32768 ) return 32767;
    if ( u <= -32768 ) return -32767;
    // 1.5u - 0.5u^3
    return ( ( 3 * u ) >> 1 ) - (int)( ( (i64)u * u * u ) >> 31 );
}

void sbProcess( SubBassState* s, int pitchIn, int gateIn, int zCounts, int* outA, int* outB )
{
    // input smoothing: X pitch lightly, Z (velocity) more
    s->pitchSmooth += ( pitchIn - s->pitchSmooth ) >> PITCH_SMOOTH_SHIFT;
    s->zSmooth += ( ( zCounts << 16 ) - s->zSmooth ) >> Z_SMOOTH_SHIFT;

    // gate with hysteresis
    if ( !s->gate && gateIn > GATE_ON )
    {
        s->gate = 1;
        if ( s->env < ENV_SILENT )
        {
            // previous note has died away: start the waveform at phase 0 (a zero
            // crossing for sine, triangle and saw) so every note begins the same way.
            // If it is still sounding, keep the waveform running: a jump would click.
            s->phase = 0;
        }
    }
    else if ( s->gate && gateIn < GATE_OFF )
        s->gate = 0;
    {
        int d = s->velTarget - s->vel;
        if ( d > -( 1 << VEL_SLEW_SHIFT ) && d < ( 1 << VEL_SLEW_SHIFT ) )
            s->vel = s->velTarget;              // last step: the shift would stall short of it
        else
            s->vel += d / ( 1 << VEL_SLEW_SHIFT );
    }

    // attack / sustain at full / release; a new gate attacks from the current level
    if ( s->gate )
    {
        if ( s->env < ENV_FULL )
        {
            s->env += (int)( ( (i64)( ENV_ATTACK_AIM - s->env ) * s->attack ) >> 31 );
            if ( s->env > ENV_FULL )
                s->env = ENV_FULL;
        }
    }
    else if ( s->env > 0 )
    {
        // exponential, plus 1 so it reaches zero instead of stalling
        s->env -= (int)( ( (i64)s->env * s->release ) >> 31 ) + 1;
        if ( s->env < 0 )
            s->env = 0;
    }
    int envQ15 = s->env >> 15;
    if ( envQ15 > 32767 ) envQ15 = 32767;

    // oscillator with shape morph (phase advances after use, so a reset note starts exactly at 0)
    int seg = s->shapePos >> 16;
    int w;
    if ( seg >= 3 )
        w = waveAt( 3, s->phase );
    else
    {
        int frac = s->shapePos & 0xffff;
        int a = waveAt( seg, s->phase );
        int b = waveAt( seg + 1, s->phase );
        w = a + (int)( ( (i64)( b - a ) * frac ) >> 16 );
    }

    // drive: crossfade from clean to saturated so drive = 1 is not a jump
    if ( s->driveMix )
    {
        int sat = saturate( ( w * s->driveGain ) >> 12 );
        w += (int)( ( (i64)( sat - w ) * s->driveMix ) >> 15 );
    }

    int amp = ( envQ15 * s->vel ) >> 15;
    *outA = (int)( ( (i64)( ( w * amp ) >> 15 ) * s->levelVolts ) >> 15 );
    *outB = (int)( ( (i64)s->env * ENV_OUT_PEAK ) >> 30 );
    s->phase += s->phaseInc;
}
