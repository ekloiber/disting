// Sub bass DSP core. See subBassDSP.h for units.

#include "subBassDSP.h"
#include "subBassTables.h"

typedef long long i64;

#define VOLT            ( 1 << 19 )
#define SUB_PEAK        ( 5 * VOLT )            // Level 99, full envelope
#define CLIP_KNEE       ( 7 * VOLT )            // soft clip starts here...
#define CLIP_SPAN       ( 2 * VOLT )            // ...and approaches knee + span (9 V)
#define MAX_PHASE_INC   ( 0x10000000u )         // fs/16, about 4.7 kHz
#define QUANT_HYST      ( 0xA000 )              // 0.625 semitone, Q16
#define Z_SMOOTH_SHIFT  ( 8 )                   // ~3.4 ms smoothing at 75 kHz
#define AHR_FULL        ( 1 << 30 )
#define AHR_ATTACK_AIM  ( AHR_FULL + ( AHR_FULL >> 2 ) )   // attack aims past full so it arrives

enum { kSbStageIdle, kSbStageAttack, kSbStageHold, kSbStageRelease };

static int clampInt( i64 v, int lo, int hi )
{
    if ( v < lo ) return lo;
    if ( v > hi ) return hi;
    return (int)v;
}

static int paramClamp( const signed char* p, int i, int lo, int hi )
{
    int v = p[i];
    if ( v < lo ) v = lo;
    if ( v > hi ) v = hi;
    return v;
}

void sbInit( SubBassState* s, int zCounts )
{
    int i;
    for ( i=0; i<2; ++i )
    {
        s->hpfLow[i] = 0;
        s->hpfBand[i] = 0;
    }
    s->env = 0;
    s->phase = 0;
    s->zSmooth = zCounts << 16;
    s->phaseInc = 0;
    s->curSemi = 0;
    s->quantPrimed = 0;
    s->hpfF = 0;
    s->hpfGain = 1 << 30;
    s->attack = kAttackQ31[0];
    s->release = kReleaseQ31[0];
    s->sens = kSensQ12[50];
    s->shapePos = 0;
    s->driveGain = 4096;
    s->driveMix = 0;
    s->levelVolts = 0;
    s->envSrc = 0;
    s->mode = kSbModeFollow;
    s->threshold = kThresholdQ19[50];
    s->rearm = kThresholdQ19[50] >> 1;
    s->holdSamples = 0;
    s->det = 0;
    s->armed = 1;
    s->gateOpen = 0;
    s->stage = kSbStageIdle;
    s->holdCount = 0;
    s->ahr = 0;
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

void sbControl( SubBassState* s, const signed char* p, int zZeroQ16, int zSemisPerCountQ16 )
{
    // pitch: semitones relative to C4, Q16
    int semis = (int)( ( (i64)( s->zSmooth - zZeroQ16 ) * zSemisPerCountQ16 ) >> 16 );
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

    int level = paramClamp( p, kSbLevel, 0, 99 );
    s->levelVolts = level * ( SUB_PEAK / 99 );

    s->shapePos = paramClamp( p, kSbShape, 0, 99 ) * ( 3 * 65536 ) / 99;

    int drive = paramClamp( p, kSbDrive, 0, 99 );
    s->driveGain = 4096 + drive * ( 4 * 4096 ) / 99;    // 1x .. 5x
    s->driveMix = drive * 32767 / 99;

    s->attack = kAttackQ31[ paramClamp( p, kSbAttack, 0, 99 ) ];
    s->release = kReleaseQ31[ paramClamp( p, kSbRelease, 0, 99 ) ];
    s->sens = kSensQ12[ paramClamp( p, kSbSens, 0, 99 ) ];
    s->envSrc = paramClamp( p, kSbEnvSrc, 0, 1 );
    s->mode = paramClamp( p, kSbMode, 0, kSbNumModes - 1 );
    s->threshold = kThresholdQ19[ paramClamp( p, kSbSens, 0, 99 ) ];
    s->rearm = s->threshold >> 1;               // re-arm 6 dB below the threshold
    s->holdSamples = kHoldSamples[ paramClamp( p, kSbHold, 0, 99 ) ];
    s->hpfF = kHpfFQ30[ paramClamp( p, kSbHpf, 0, 99 ) ];
    // a Chamberlin SVF's high-pass passband gain is 1 / ( 1 - f*damp/2 ): undo it
    s->hpfGain = ( 1 << 30 ) - (int)( ( (i64)s->hpfF * kHpfDampQ30 ) >> 31 );
}

// Chamberlin state-variable high-pass, 12 dB/oct, Butterworth damping
static int highPass( SubBassState* s, int ch, int x19 )
{
    int x = x19 * 256;                          // .27 (multiply: x19 may be negative)
    int f = s->hpfF;
    s->hpfLow[ch] += (int)( ( (i64)f * s->hpfBand[ch] ) >> 30 );
    int high = clampInt( (i64)x - s->hpfLow[ch] - ( ( (i64)kHpfDampQ30 * s->hpfBand[ch] ) >> 30 ),
                         -0x7fffffff, 0x7fffffff );
    s->hpfBand[ch] += (int)( ( (i64)f * high ) >> 30 );
    return (int)( ( (i64)high * s->hpfGain ) >> 38 );     // .27 * Q30 -> .19
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

static int softClip( int v )
{
    int neg = v < 0;
    int a = neg ? -v : v;
    if ( a <= CLIP_KNEE )
        return v;
    // knee + span * t / ( t + span ), computed in 2^-8 V steps to stay in 32 bits
    int t = ( a - CLIP_KNEE ) >> 8;
    int span = CLIP_SPAN >> 8;
    if ( t > 0x3fffff ) t = 0x3fffff;
    int y = CLIP_KNEE + ( ( ( span * t ) / ( t + span ) ) << 8 );
    return neg ? -y : y;
}

// Trigger mode: a peak detector on the input; crossing the threshold (after
// falling 6 dB below it) triggers attack -> hold -> release. Returns Q15.
static int triggeredEnvelope( SubBassState* s, int level19 )
{
    if ( level19 > s->det )
        s->det = level19;
    else
        s->det += (int)( ( (i64)( level19 - s->det ) * kDetReleaseQ31 ) >> 31 );

    if ( s->armed && s->det >= s->threshold )
    {
        s->armed = 0;
        if ( s->stage == kSbStageHold )
            s->holdCount = 0;                   // another hit: hold again from now
        else if ( s->stage != kSbStageAttack )
            s->stage = kSbStageAttack;          // idle or releasing: attack from the current level
    }
    else if ( !s->armed && s->det < s->rearm )
        s->armed = 1;

    switch ( s->stage )
    {
        case kSbStageAttack:
            s->ahr += (int)( ( (i64)( AHR_ATTACK_AIM - s->ahr ) * s->attack ) >> 31 );
            if ( s->ahr >= AHR_FULL )
            {
                s->ahr = AHR_FULL;
                s->stage = kSbStageHold;
                s->holdCount = 0;
            }
            break;
        case kSbStageHold:
            if ( ++s->holdCount >= s->holdSamples )
                s->stage = kSbStageRelease;
            break;
        case kSbStageRelease:
            // exponential, plus 1 so it reaches zero instead of stalling
            s->ahr -= (int)( ( (i64)s->ahr * s->release ) >> 31 ) + 1;
            if ( s->ahr <= 0 )
            {
                s->ahr = 0;
                s->stage = kSbStageIdle;
            }
            break;
        default:
            break;
    }
    int amp = s->ahr >> 15;
    return amp > 32767 ? 32767 : amp;
}

// Gate mode: full level while a peak detector (50 ms decay, steady down to
// ~40 Hz input) is above the threshold; closes 6 dB below it. Attack and
// Release shape the edges. Returns Q15.
static int gatedEnvelope( SubBassState* s, int level19 )
{
    if ( level19 > s->det )
        s->det = level19;
    else
        s->det += (int)( ( (i64)( level19 - s->det ) * kDetGateReleaseQ31 ) >> 31 );

    if ( !s->gateOpen && s->det >= s->threshold )
        s->gateOpen = 1;
    else if ( s->gateOpen && s->det < s->rearm )
        s->gateOpen = 0;

    if ( s->gateOpen )
    {
        if ( s->ahr < AHR_FULL )
        {
            s->ahr += (int)( ( (i64)( AHR_ATTACK_AIM - s->ahr ) * s->attack ) >> 31 );
            if ( s->ahr > AHR_FULL )
                s->ahr = AHR_FULL;
        }
    }
    else if ( s->ahr > 0 )
    {
        s->ahr -= (int)( ( (i64)s->ahr * s->release ) >> 31 ) + 1;
        if ( s->ahr < 0 )
            s->ahr = 0;
    }
    int amp = s->ahr >> 15;
    return amp > 32767 ? 32767 : amp;
}

void sbProcess( SubBassState* s, int inL, int inR, int zCounts, int* outL, int* outR )
{
    // Z smoothing
    s->zSmooth += ( ( zCounts << 16 ) - s->zSmooth ) >> Z_SMOOTH_SHIFT;

    // input high-pass
    int hL = inL, hR = inR;
    if ( s->hpfF )
    {
        hL = highPass( s, 0, inL );
        hR = highPass( s, 1, inR );
    }

    // envelope follower on |L| + |R|
    int eL = s->envSrc ? hL : inL;
    int eR = s->envSrc ? hR : inR;
    if ( eL < 0 ) eL = -eL;
    if ( eR < 0 ) eR = -eR;
    int amp;
    if ( s->mode == kSbModeFollow )
    {
        int target = clampInt( ( (i64)eL + eR ) << 7, 0, 0x7fffffff );    // (|L|+|R|)/2 at .27
        int coef = target > s->env ? s->attack : s->release;
        s->env += (int)( ( (i64)( target - s->env ) * coef ) >> 31 );

        // amplitude Q15: env / 5 V * sens   (13107 = 2^32 / (80 * 4096))
        amp = clampInt( ( (i64)( s->env >> 8 ) * s->sens * 13107 ) >> 32, 0, 32767 );
    }
    else if ( s->mode == kSbModeConstant )
        amp = 32767;
    else if ( s->mode == kSbModeTrigger )
        amp = triggeredEnvelope( s, ( eL + eR ) >> 1 );
    else
        amp = gatedEnvelope( s, ( eL + eR ) >> 1 );

    // oscillator with shape morph
    s->phase += s->phaseInc;
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

    int sub = (int)( ( (i64)( ( w * amp ) >> 15 ) * s->levelVolts ) >> 15 );

    *outL = softClip( hL + sub );
    *outR = softClip( hR + sub );
}
