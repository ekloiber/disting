// Z input V/oct calibration.
//
// The Z knob and Z jack are summed in hardware into one 10-bit ADC reading,
// and the module's factory calibration only covers X/Y/A/B. 'Cal Z' measures
// the reading at 1 V and 3 V (knob fully counter-clockwise) and stores the
// offset and scale in the settings flash page, after the Settings struct.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xc.h>
#include <plib.h>

#include "constants.h"
#include "zcal.h"

#define ZCAL_MAGIC          0x5A43414C          // 'ZCAL'
#define ZCAL_FLASH_OFFSET   0x100               // Settings uses the first 0x20 bytes
#define ZCAL_FLASH_ADDR     ( SETTINGS_FLASH_ADDR + ZCAL_FLASH_OFFSET )

// Used until 'Cal Z' has been run: 0 V at mid-scale, 128 counts per volt
// (the scaling the framework's example algorithm assumes). Expect it to be off.
#define NOMINAL_ZERO_Q16    ( 512 << 16 )
#define NOMINAL_SPC_Q16     ( ( 12 << 16 ) / 128 )

#define DEBOUNCE_SAMPLES    1500                // 20 ms at 75 kHz
#define SETTLE_SAMPLES      7500                // 100 ms
#define AVERAGE_SHIFT       13                  // average 8192 samples (~110 ms)
#define MIN_COUNTS_PER_VOLT ( 32 << 16 )

ZCal zcal;
int zcalValid = 0;

void readZCal()
{
    const ZCal* stored = (const ZCal*)ZCAL_FLASH_ADDR;
    if ( stored->m_magic == ZCAL_MAGIC && stored->m_semisPerCountQ16 != 0 )
    {
        zcal = *stored;
        zcalValid = 1;
    }
    else
    {
        zcal.m_magic = 0;
        zcal.m_zeroQ16 = NOMINAL_ZERO_Q16;
        zcal.m_semisPerCountQ16 = NOMINAL_SPC_Q16;
        zcalValid = 0;
    }
}

// wait one sample, keep the display running, return the latest Z reading
static int zcalTick()
{
    static unsigned int lastTime = 0;
    unsigned int thisTime;
    while ( ( thisTime = time ) == lastTime )
        ;
    lastTime = thisTime;
    updateDisplay( 1 );
    int v = ReadADC10( 0 );
    ConvertADC10();
    return v;
}

// wait for S to be released, then pressed. Returns 0 if Z is pressed (cancel).
static int waitForS()
{
    int stable = 0;
    while ( stable < DEBOUNCE_SAMPLES )
    {
        zcalTick();
        stable = PORTCbits.RC2 ? stable + 1 : 0;
    }
    stable = 0;
    while ( stable < DEBOUNCE_SAMPLES )
    {
        zcalTick();
        if ( !PORTCbits.RC4 )
            return 0;
        stable = !PORTCbits.RC2 ? stable + 1 : 0;
    }
    return 1;
}

// 1 = measured, 0 = cancelled, -1 = reading at the ADC limit
static int measure( const char* prompt, int* averageQ16 )
{
    scrollMessageContinuously( prompt );
    if ( !waitForS() )
        return 0;
    scrollMessageContinuously( "Reading" );
    int i;
    for ( i=0; i<SETTLE_SAMPLES; ++i )
        zcalTick();
    int sum = 0, lo = 1023, hi = 0;
    for ( i=0; i<(1<<AVERAGE_SHIFT); ++i )
    {
        int v = zcalTick();
        sum += v;
        if ( v < lo ) lo = v;
        if ( v > hi ) hi = v;
    }
    if ( lo < 4 || hi > 1019 )
        return -1;
    *averageQ16 = (int)( ( (int64_t)sum << 16 ) >> AVERAGE_SHIFT );
    return 1;
}

static void saveZCal()
{
    memcpy( pageBuffer, (const void*)SETTINGS_FLASH_ADDR, BYTE_PAGE_SIZE );
    memcpy( pageBuffer + ZCAL_FLASH_OFFSET, &zcal, sizeof zcal );
    WriteSettingsPage( pageBuffer );
}

void calibrateZ()
{
    STATIC_ASSERT( ZCAL_FLASH_OFFSET + sizeof(ZCal) <= BYTE_PAGE_SIZE );

    // the algorithm loop is paused while we are in here: output silence
    outL = 0;
    outR = 0;

    int at1V = 0, at3V = 0;
    int r = measure( "Knob CCW, 1V into Z, press S", &at1V );
    if ( r > 0 )
        r = measure( "3V into Z, press S", &at3V );
    if ( r == 0 )
    {
        scrollMessageOnceAndWait( "Cancelled" );
        return;
    }

    int perVoltQ16 = ( at3V - at1V ) / 2;
    if ( r < 0 || ( perVoltQ16 < MIN_COUNTS_PER_VOLT && perVoltQ16 > -MIN_COUNTS_PER_VOLT ) )
    {
        scrollMessageOnceAndWait( "Cal failed" );
        return;
    }

    zcal.m_magic = ZCAL_MAGIC;
    zcal.m_zeroQ16 = at1V - perVoltQ16;
    zcal.m_semisPerCountQ16 = (int)( ( (int64_t)12 << 32 ) / perVoltQ16 );
    saveZCal();
    zcalValid = 1;
    scrollMessageOnceAndWait( "Z calibrated" );
}
