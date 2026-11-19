/* Fault.h - flowchart step 2: fault detector (irradiance vs AC power) */
#ifndef FAULT_H
#define FAULT_H

#include "../common.h"

/* Physical upper limit for plane-of-array irradiance on Earth (W/m2). */
#define FAULT_MAX_IRRADIANCE 1400.0

typedef struct {
    int    rows_checked;        /* rows examined                                  */
    int    faulty_rows;         /* rows rejected (unusable or implausible)        */
    int    unusable_rows;       /* blank / NaN / non-numeric (valid == 0 already) */
    int    implausible_rows;    /* irradiance < 0 or > FAULT_MAX_IRRADIANCE       */
    double loss_kwh_per_kwp;    /* estimated energy lost from the faulty rows     */
    double loss_pct;            /* that loss as % of (usable yield + loss)        */
} FaultSummary;

/* AC power (kW) the irradiance implies for a kWp array: kWp * G/1000 * PERF_RATIO. */
double modelled_ac_kw(const HourRecord *r, double kwp);

/* Validates every row's irradiance against the AC power it implies. A row whose
 * implied AC power is negative or above what the array can physically deliver is
 * marked valid = 0 so every later module skips it. The energy those rows would
 * have produced (mean of healthy rows at the same month and hour) is the loss. */
void fault_scan(Dataset *d, FaultSummary *out);

#endif
