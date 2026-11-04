/*
 * Host-side test for the Sub bass DSP core (hello_disting_mk4/subBassDSP.c).
 * Build and run from the repository root:
 *
 *   cc -O1 -fsigned-char -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover
 *      -Ihello_disting_mk4 tools/subbass_host_test.c hello_disting_mk4/subBassDSP.c -lm
 *      -o /tmp/subbass_test && /tmp/subbass_test
 *
 * It feeds synthetic signals through sbProcess/sbControl exactly as the
 * firmware does (75 kHz, control update every 16 samples) and checks pitch,
 * levels, filter response and clipping against the design.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "subBassDSP.h"

#define FS 75000
#define VOLT (1 << 19)
#define CONTROL_INTERVAL 16

static const signed char kDefaults[kSbNumParams] = { 50, 0, -2, 0, 10, 30, 50, 0, 0, 1, 0 };

// calibration used by the tests: 0 V = 300 counts, 128 counts per volt
#define Z_ZERO_Q16  ( 300 << 16 )
#define Z_SPC_Q16   ( ( 12 << 16 ) / 128 )

static int failures = 0;

static void check( int ok, const char* what, double got, double lo, double hi )
{
    printf( "%-4s %-52s %10.3f   (expect %.3f .. %.3f)\n", ok ? "ok" : "FAIL", what, got, lo, hi );
    if ( !ok ) failures++;
}
#define CHECK_RANGE( what, got, lo, hi ) check( (got) >= (lo) && (got) <= (hi), what, got, lo, hi )

typedef struct {
    double inFreq, inPeakV;     // input sine on both channels
    int zCounts;
    signed char p[kSbNumParams];
} Run;

typedef struct {
    double subFreq, subPeakV, outPeakV, outRmsLeft;
} Result;

// run for `seconds`, measure over the last `measure` seconds
static Result run( const Run* r, double seconds, double measure )
{
    SubBassState s;
    sbInit( &s, r->zCounts );
    sbControl( &s, r->p, Z_ZERO_Q16, Z_SPC_Q16 );
    int n = (int)( seconds * FS ), m0 = n - (int)( measure * FS );
    int countdown = CONTROL_INTERVAL;
    int prevSub = 0, crossings = 0, firstX = -1, lastX = -1;
    double subPeak = 0, outPeak = 0, sumSq = 0;
    int i;
    for ( i=0; i<n; ++i )
    {
        int x = (int)lrint( r->inPeakV * VOLT * sin( 2 * M_PI * r->inFreq * i / FS ) );
        int oL, oR;
        sbProcess( &s, x, x, r->zCounts, &oL, &oR );
        if ( --countdown == 0 ) { countdown = CONTROL_INTERVAL; sbControl( &s, r->p, Z_ZERO_Q16, Z_SPC_Q16 ); }
        if ( i >= m0 )
        {
            int sub = oL - x;       // valid when the HPF is off and nothing clips
            if ( prevSub < 0 && sub >= 0 ) { crossings++; if ( firstX < 0 ) firstX = i; lastX = i; }
            prevSub = sub;
            if ( fabs( (double)sub ) > subPeak ) subPeak = fabs( (double)sub );
            if ( fabs( (double)oL ) > outPeak ) outPeak = fabs( (double)oL );
            sumSq += (double)oL * oL;
        }
    }
    Result res;
    res.subFreq = crossings > 1 ? ( crossings - 1 ) * (double)FS / ( lastX - firstX ) : 0;
    res.subPeakV = subPeak / VOLT;
    res.outPeakV = outPeak / VOLT;
    res.outRmsLeft = sqrt( sumSq / ( n - m0 ) ) / VOLT;
    return res;
}

static Run base( void )
{
    Run r;
    r.inFreq = 1000; r.inPeakV = 5; r.zCounts = 300;
    memcpy( r.p, kDefaults, sizeof r.p );
    return r;
}

static double noteHz( double semisFromC4 ) { return 261.6255653 * pow( 2, semisFromC4 / 12 ); }

int main( void )
{
    Run r; Result res;

    printf( "-- pitch --\n" );
    r = base();
    res = run( &r, 1.0, 0.8 );
    CHECK_RANGE( "0 V, Octave -2 -> C2 (65.41 Hz)", res.subFreq, 65.2, 65.6 );

    r = base(); r.zCounts = 300 + 128;
    res = run( &r, 1.0, 0.8 );
    CHECK_RANGE( "1 V, Octave -2 -> C3 (130.81 Hz)", res.subFreq, 130.4, 131.2 );

    r = base(); r.zCounts = 300 + 128 * 2; r.p[kSbOctave] = -4;
    res = run( &r, 1.0, 0.8 );
    CHECK_RANGE( "2 V, Octave -4 -> C2 (65.41 Hz)", res.subFreq, 65.2, 65.6 );

    r = base(); r.zCounts = 300 + 11;     // 11 counts = 1.03 semitone
    res = run( &r, 1.0, 0.8 );
    CHECK_RANGE( "Quant on: +1.03 st snaps to C#2 (69.30 Hz)", res.subFreq, noteHz( -23 ) - 0.2, noteHz( -23 ) + 0.2 );

    r = base(); r.zCounts = 300 + 5; r.p[kSbQuant] = 0;  // 5 counts = 0.469 st
    res = run( &r, 1.0, 0.8 );
    CHECK_RANGE( "Quant off: +0.469 st (67.19 Hz)", res.subFreq, noteHz( -23.531 ) - 0.2, noteHz( -23.531 ) + 0.2 );

    r = base(); r.p[kSbFine] = 50;
    res = run( &r, 1.0, 0.8 );
    CHECK_RANGE( "Fine +50 cents (67.32 Hz)", res.subFreq, noteHz( -23.5 ) - 0.2, noteHz( -23.5 ) + 0.2 );

    printf( "-- level & envelope --\n" );
    r = base(); r.p[kSbLevel] = 99; r.p[kSbSens] = 99; r.inPeakV = 3;
    res = run( &r, 1.0, 0.5 );
    CHECK_RANGE( "Level 99, Sens 99: sub peak ~5 V", res.subPeakV, 4.8, 5.05 );

    r = base(); r.p[kSbLevel] = 0;
    res = run( &r, 0.5, 0.3 );
    CHECK_RANGE( "Level 0: no sub", res.subPeakV, 0, 0.0001 );

    r = base(); r.inPeakV = 0;
    res = run( &r, 0.5, 0.3 );
    CHECK_RANGE( "Silent input: no sub", res.subPeakV, 0, 0.0001 );

    r = base(); r.inPeakV = 2; r.p[kSbLevel] = 99;
    res = run( &r, 1.0, 0.5 );
    double at2V = res.subPeakV;
    r.inPeakV = 4;
    res = run( &r, 1.0, 0.5 );
    CHECK_RANGE( "Envelope tracks input: 4 V / 2 V input -> sub ratio", res.subPeakV / at2V, 1.9, 2.1 );

    printf( "-- shapes --\n" );
    int shapes[4] = { 0, 33, 66, 99 };
    const char* names[4] = { "sine", "triangle", "saw", "square" };
    int k;
    for ( k=0; k<4; ++k )
    {
        r = base(); r.p[kSbShape] = shapes[k]; r.p[kSbLevel] = 99; r.p[kSbSens] = 99; r.inPeakV = 3;
        res = run( &r, 1.0, 0.5 );
        char label[64];
        snprintf( label, sizeof label, "Shape %d (%s): pitch unchanged", shapes[k], names[k] );
        CHECK_RANGE( label, res.subFreq, 65.0, 65.8 );
    }
    r = base(); r.p[kSbDrive] = 99; r.p[kSbLevel] = 99; r.p[kSbSens] = 99; r.inPeakV = 3;
    res = run( &r, 1.0, 0.5 );
    CHECK_RANGE( "Drive 99: peak stays <= 5 V", res.subPeakV, 4.5, 5.05 );

    printf( "-- high-pass (Level 0, measure output RMS / input RMS) --\n" );
    struct { int p; double f, lo, hi; const char* what; } hp[] = {
        { 1, 20, 0.66, 0.75, "HPF 1 at 20 Hz: -3 dB" },
        { 99, 300, 0.66, 0.75, "HPF 99 at 300 Hz: -3 dB" },
        { 99, 75, 0.05, 0.08, "HPF 99 at 75 Hz: about -24 dB" },
        { 99, 3000, 0.99, 1.01, "HPF 99 at 3 kHz: passes" },
        { 0, 10, 0.999, 1.001, "HPF 0 at 10 Hz: bypass" },
    };
    for ( k=0; k<(int)( sizeof hp / sizeof hp[0] ); ++k )
    {
        r = base(); r.p[kSbLevel] = 0; r.p[kSbHpf] = hp[k].p; r.inFreq = hp[k].f; r.inPeakV = 1;
        res = run( &r, 2.0, 1.0 );
        CHECK_RANGE( hp[k].what, res.outRmsLeft / ( 1 / sqrt( 2 ) ), hp[k].lo, hp[k].hi );
    }

    printf( "-- output limiting --\n" );
    r = base(); r.inPeakV = 9.5; r.p[kSbLevel] = 99; r.p[kSbSens] = 99;
    res = run( &r, 1.0, 0.5 );
    CHECK_RANGE( "9.5 V input + 5 V sub: output below 9 V", res.outPeakV, 7.0, 9.0 );

    printf( "%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed" );
    return failures ? 1 : 0;
}
