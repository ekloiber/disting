// Z input V/oct calibration (knob + jack are summed into one 10-bit ADC reading).
#ifndef ZCAL_H
#define ZCAL_H

typedef struct {
    unsigned int    m_magic;
    int             m_zeroQ16;          // ADC counts at 0 V on the jack, knob fully CCW (Q16)
    int             m_semisPerCountQ16; // semitones per ADC count (Q16)
} ZCal;

extern ZCal zcal;
extern int zcalValid;

// load from flash, or fall back to nominal values (zcalValid = 0)
extern void readZCal(void);

// interactive calibration; called from the 'Cal Z' menu item
extern void calibrateZ(void);

#endif // ZCAL_H
