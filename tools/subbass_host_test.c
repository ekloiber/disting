/*
 * Host-side test for the Sub voice DSP core (hello_disting_mk4/subBassDSP.c).
 * Build and run from the repository root:
 *
 *   cc -O1 -fsigned-char -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover
 *      -Ihello_disting_mk4 tools/subbass_host_test.c hello_disting_mk4/subBassDSP.c -lm
 *      -o /tmp/subbass_test && /tmp/subbass_test
 *
 * It drives sbProcess/sbControl exactly as the firmware does (75 kHz, control
 * update every 16 samples) with synthetic gate, velocity and Z signals.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "subBassDSP.h"

#define FS 75000
#define VOLT (1 << 19)
#define CONTROL_INTERVAL 16

// firmware defaults
static const signed char kDefaults[kSbNumParams] = {
    [kSbShape] = 0, [kSbDrive] = 0, [kSbLevel] = 50, [kSbOctave] = -2,
    [kSbFine] = 0, [kSbQuant] = 1, [kSbAttack] = 10, [kSbRelease] = 30,
};

// calibration used by the tests: 0 V = 300 counts, 128 counts per volt
#define Z_ZERO_Q16  ( 300 << 16 )
#define Z_SPC_Q16   ( ( 12 << 16 ) / 128 )

static int failures = 0;

static void check( int ok, const char* what, double got, double lo, double hi )
{
    printf( "%-4s %-56s %10.3f   (expect %.3f .. %.3f)\n", ok ? "ok" : "FAIL", what, got, lo, hi );
    if ( !ok ) failures++;
}
#define CHECK_RANGE( what, got, lo, hi ) check( (got) >= (lo) && (got) <= (hi), what, got, lo, hi )

// a run: up to 4 segments of (start ms, gate V, velocity V); the last segment lasts until the end
typedef struct {
    int n;
    double atMs[4], gateV[4], velV[4];
    int zCounts;
    signed char p[kSbNumParams];
} Run;

#define MAX_SAMPLES ( FS * 2 )
static int traceA[MAX_SAMPLES], traceB[MAX_SAMPLES], traceEnv[MAX_SAMPLES];

static int run( const Run* r, double seconds )
{
    SubBassState s;
    sbInit( &s, r->zCounts );
    sbControl( &s, r->p, Z_ZERO_Q16, Z_SPC_Q16 );
    int n = (int)( seconds * FS ), countdown = CONTROL_INTERVAL, i, k;
    for ( i=0; i<n; ++i )
    {
        double ms = i * 1000.0 / FS, g = 0, v = 0;
        for ( k=0; k<r->n; ++k )
            if ( ms >= r->atMs[k] ) { g = r->gateV[k]; v = r->velV[k]; }
        int a, b;
        sbProcess( &s, (int)lrint( g * VOLT ), (int)lrint( v * VOLT ), r->zCounts, &a, &b );
        if ( --countdown == 0 ) { countdown = CONTROL_INTERVAL; sbControl( &s, r->p, Z_ZERO_Q16, Z_SPC_Q16 ); }
        traceA[i] = a; traceB[i] = b; traceEnv[i] = s.env;
    }
    return n;
}

static int idx( double ms ) { return (int)( ms * FS / 1000 ); }

// peak |A| in volts over [fromMs, toMs)
static double peakA( double fromMs, double toMs )
{
    int i; double m = 0;
    for ( i=idx( fromMs ); i<idx( toMs ); ++i ) if ( fabs( (double)traceA[i] ) > m ) m = fabs( (double)traceA[i] );
    return m / VOLT;
}

// frequency of A from rising zero crossings over [fromMs, toMs)
static double freqA( double fromMs, double toMs )
{
    int i, c = 0, first = -1, last = -1;
    for ( i=idx( fromMs ) + 1; i<idx( toMs ); ++i )
        if ( traceA[i-1] < 0 && traceA[i] >= 0 ) { c++; if ( first < 0 ) first = i; last = i; }
    return c > 1 ? ( c - 1 ) * (double)FS / ( last - first ) : 0;
}

static double envAt( double ms ) { return traceEnv[idx( ms )] / (double)( 1 << 30 ); }
static double bAt( double ms ) { return traceB[idx( ms )] / (double)VOLT; }

// first time >= fromMs where the envelope falls below frac
static double envBelow( double fromMs, double frac, int n )
{
    int i, lim = (int)( frac * ( 1 << 30 ) );
    for ( i=idx( fromMs ); i<n; ++i ) if ( traceEnv[i] < lim ) return i * 1000.0 / FS;
    return -1;
}

static Run gateRun( double gateV, double velV )
{
    Run r;
    r.n = 2;
    r.atMs[0] = 0;  r.gateV[0] = 0;     r.velV[0] = velV;
    r.atMs[1] = 10; r.gateV[1] = gateV; r.velV[1] = velV;
    r.zCounts = 300;
    memcpy( r.p, kDefaults, sizeof r.p );
    r.p[kSbLevel] = 99;
    return r;
}

static double noteHz( double semisFromC4 ) { return 261.6255653 * pow( 2, semisFromC4 / 12 ); }

int main( void )
{
    Run r; int n;

    printf( "-- pitch (gate high, 5 V velocity) --\n" );
    r = gateRun( 5, 5 );
    n = run( &r, 1.0 );
    CHECK_RANGE( "0 V, Octave -2 -> C2 (65.41 Hz)", freqA( 200, 1000 ), 65.2, 65.6 );
    r.zCounts = 300 + 128;
    n = run( &r, 1.0 );
    CHECK_RANGE( "1 V -> C3 (130.81 Hz)", freqA( 200, 1000 ), 130.4, 131.2 );
    r.zCounts = 300 + 11; r.p[kSbQuant] = 1;
    n = run( &r, 1.0 );
    CHECK_RANGE( "Quant on: +1.03 st snaps to C#2", freqA( 200, 1000 ), noteHz( -23 ) - 0.2, noteHz( -23 ) + 0.2 );
    r.zCounts = 300 + 5; r.p[kSbQuant] = 0;
    n = run( &r, 1.0 );
    CHECK_RANGE( "Quant off: +0.469 st", freqA( 200, 1000 ), noteHz( -23.531 ) - 0.2, noteHz( -23.531 ) + 0.2 );
    r = gateRun( 5, 5 ); r.p[kSbFine] = 50;
    n = run( &r, 1.0 );
    CHECK_RANGE( "Fine +50 cents", freqA( 200, 1000 ), noteHz( -23.5 ) - 0.2, noteHz( -23.5 ) + 0.2 );

    printf( "-- gate --\n" );
    r = gateRun( 0, 5 );
    n = run( &r, 0.5 );
    CHECK_RANGE( "No gate: A silent", peakA( 0, 500 ), 0, 0 );
    CHECK_RANGE( "No gate: B at 0 V", bAt( 400 ), 0, 0 );
    r = gateRun( 0.9, 5 );
    n = run( &r, 0.5 );
    CHECK_RANGE( "0.9 V gate (below 1 V): stays closed", peakA( 0, 500 ), 0, 0 );
    r = gateRun( 1.2, 5 );
    n = run( &r, 0.5 );
    CHECK_RANGE( "1.2 V gate: opens, B reaches 5 V", bAt( 400 ), 4.99, 5.0 );

    // gate opens at 5 V, drops to 0.7 V (between thresholds): must stay open
    r = gateRun( 5, 5 ); r.n = 3; r.atMs[2] = 100; r.gateV[2] = 0.7; r.velV[2] = 5;
    n = run( &r, 0.5 );
    CHECK_RANGE( "Hysteresis: 0.7 V after opening keeps it open", envAt( 450 ), 0.999, 1.0 );
    r.gateV[2] = 0.4;
    n = run( &r, 0.5 );
    CHECK_RANGE( "0.4 V after opening closes it", envAt( 450 ), 0, 0.001 );

    printf( "-- envelope (Attack 0 = 0.5 ms, Release 0 = 10 ms) --\n" );
    r = gateRun( 5, 5 ); r.p[kSbAttack] = 0; r.p[kSbRelease] = 0;
    r.n = 3; r.atMs[2] = 200; r.gateV[2] = 0; r.velV[2] = 5;
    n = run( &r, 0.5 );
    CHECK_RANGE( "Full within 2 ms of the gate", envAt( 12 ), 0.999, 1.0 );
    CHECK_RANGE( "Sustains at full while the gate is high", envAt( 199 ), 0.999, 1.0 );
    CHECK_RANGE( "Released to -40 dB within 60 ms of gate off", envBelow( 200, 0.01, n ) - 200, 1, 60 );
    CHECK_RANGE( "B follows: 5 V during the note", bAt( 150 ), 4.99, 5.0 );
    CHECK_RANGE( "B follows: 0 V after release", bAt( 450 ), 0, 0.001 );

    r = gateRun( 5, 5 ); r.p[kSbRelease] = 60;   // ~250 ms release
    r.n = 4; r.atMs[2] = 100; r.gateV[2] = 0; r.velV[2] = 5; r.atMs[3] = 250; r.gateV[3] = 5; r.velV[3] = 5;
    n = run( &r, 0.6 );
    {
        double before = envAt( 249.99 );
        int i, lowest = traceEnv[idx( 250 ) - 1];
        for ( i=idx( 250 ); i<idx( 260 ); ++i ) if ( traceEnv[i] < lowest ) lowest = traceEnv[i];
        CHECK_RANGE( "Retrigger: level before (mid-release)", before, 0.05, 0.95 );
        CHECK_RANGE( "Retrigger: attacks from there (lowest after / before)", lowest / (double)traceEnv[idx( 250 ) - 1], 0.99, 1.0 );
        CHECK_RANGE( "Retrigger: back to full", envAt( 500 ), 0.999, 1.0 );
    }

    printf( "-- velocity (Level 99) --\n" );
    r = gateRun( 5, 5 );
    n = run( &r, 0.5 );
    CHECK_RANGE( "5 V velocity: A peak 5 V", peakA( 200, 500 ), 4.95, 5.05 );
    r = gateRun( 5, 2.5 );
    n = run( &r, 0.5 );
    CHECK_RANGE( "2.5 V velocity: A peak 2.5 V", peakA( 200, 500 ), 2.45, 2.55 );
    CHECK_RANGE( "2.5 V velocity: B still 5 V (envelope only)", bAt( 400 ), 4.99, 5.0 );
    r = gateRun( 5, 0 );
    n = run( &r, 0.5 );
    CHECK_RANGE( "0 V velocity: A silent", peakA( 200, 500 ), 0, 0.001 );
    r = gateRun( 5, -3 );
    n = run( &r, 0.5 );
    CHECK_RANGE( "-3 V velocity: A silent", peakA( 200, 500 ), 0, 0.001 );
    r = gateRun( 5, 8 );
    n = run( &r, 0.5 );
    CHECK_RANGE( "8 V velocity: capped at 5 V peak", peakA( 200, 500 ), 4.95, 5.05 );

    // velocity changes mid-note are ignored until the next gate
    r = gateRun( 5, 1 ); r.n = 3; r.atMs[2] = 200; r.gateV[2] = 5; r.velV[2] = 5;
    n = run( &r, 0.5 );
    CHECK_RANGE( "Velocity sampled at gate: Y 1 V -> 5 V mid-note, A stays 1 V", peakA( 300, 500 ), 0.95, 1.05 );

    printf( "-- shape, drive, level --\n" );
    int shapes[4] = { 0, 33, 66, 99 }, k;
    for ( k=0; k<4; ++k )
    {
        char label[64];
        r = gateRun( 5, 5 ); r.p[kSbShape] = shapes[k];
        n = run( &r, 1.0 );
        snprintf( label, sizeof label, "Shape %d: pitch unchanged", shapes[k] );
        CHECK_RANGE( label, freqA( 200, 1000 ), 65.0, 65.8 );
    }
    r = gateRun( 5, 5 ); r.p[kSbDrive] = 99;
    n = run( &r, 0.5 );
    CHECK_RANGE( "Drive 99: peak stays <= 5 V", peakA( 200, 500 ), 4.5, 5.05 );
    r = gateRun( 5, 5 ); r.p[kSbLevel] = 50;
    n = run( &r, 0.5 );
    CHECK_RANGE( "Level 50: about 2.5 V", peakA( 200, 500 ), 2.45, 2.55 );
    r.p[kSbLevel] = 0;
    n = run( &r, 0.5 );
    CHECK_RANGE( "Level 0: silent", peakA( 200, 500 ), 0, 0 );
    (void)n;

    printf( "%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed" );
    return failures ? 1 : 0;
}
