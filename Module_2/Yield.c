/* ============================================================
   yield.c  --  M2 CAPACITY AND YIELD

   Answers two questions:
     1. How many panels fit on the site, and how big is the system?
     2. How much electricity will that system make in a year?

   Units used everywhere:
     irradiance  W/m2        panel size  m / m2
     power       kW          energy      kWh
   ============================================================ */

#include "Yield.h"


/* ------------------------------------------------------------
   1. AREA OF ONE PANEL (m2)
   Formula: area = length x width
   ------------------------------------------------------------ */
double panel_area_m2(const Panel *p)
{
    return p->length_m * p->width_m;
}


/* ------------------------------------------------------------
   2. HOW MANY PANELS FIT (whole number)
   Formula: n = floor( site area x USABLE_FRACTION / panel area )
   floor() rounds DOWN because you cannot install half a panel.
   Edge cases: no area, NaN/inf area or a broken panel size gives 0
   panels; a site too big for an int gives INT_MAX.
   ------------------------------------------------------------ */
int panels_that_fit(double site_area_m2, const Panel *p)
{
    double one = panel_area_m2(p);
    double q;

    if (!isfinite(site_area_m2) || site_area_m2 <= 0.0 ||
        !isfinite(one) || one <= 0.0) {
        return 0;                           /* NaN, inf, zero or negative */
    }
    /* the tiny factor keeps an exact fit (3 m2 x 0.7 / 2.1 m2) from
       rounding down to one panel too few */
    q = floor(site_area_m2 * USABLE_FRACTION / one * (1.0 + 1e-12));
    if (!isfinite(q) || q >= (double)INT_MAX) {
        return INT_MAX;                     /* cannot count past int */
    }
    return (int)q;
}


/* ------------------------------------------------------------
   3. SYSTEM SIZE (kWp)
   Formula: kWp = number of panels x panel watts / 1000
   ------------------------------------------------------------ */
double array_kwp(int n_panels, const Panel *p)
{
    if (n_panels <= 0) {
        return 0.0;
    }
    return n_panels * p->rated_w / 1000.0;
}


/* ------------------------------------------------------------
   HELPER: the time between rows, in hours.
   We must NOT assume 1 hour, because the lecturer's second file
   may be different. So we look at the timestamps (minutes included).

   Each row gets a minute number on the real calendar (leap years
   included). The step is the MOST COMMON size of the gap between
   neighbouring rows, so one stray row cannot change it. The gap is
   taken as an absolute value, so a file written newest-first works
   too. Gaps of 0 (duplicate timestamps) are ignored. If no gap can
   be found, we fall back to 1 hour.
   ------------------------------------------------------------ */
static int days_in_month(int y, int m)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;

    if (m < 1 || m > 12) {
        return 30;
    }
    return (m == 2 && leap) ? 29 : days[m - 1];
}

/* days since 1970-01-01 (proleptic Gregorian calendar) */
static double day_number(int y, int m, int d)
{
    long yy  = (long)y - (m <= 2);
    long era = (yy >= 0 ? yy : yy - 399) / 400;
    long yoe = yy - era * 400;
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return (double)(era * 146097 + doe - 719468);
}

static double minute_number(const HourRecord *r)
{
    return day_number(r->year, r->month, r->day) * 1440.0
           + r->hour * 60.0 + r->minute;
}

double time_step_hours(const Dataset *d)
{
    enum { MAX_GAPS = 16 };
    double gap_val[MAX_GAPS];               /* distinct gap sizes (minutes) */
    int    gap_cnt[MAX_GAPS];               /* how often each one occurs    */
    int    kinds = 0;

    for (int i = 1; i < d->n; i++) {
        double gap = fabs(minute_number(&d->rec[i]) - minute_number(&d->rec[i - 1]));
        int k;

        if (gap < 0.5) {
            continue;                       /* duplicate timestamp */
        }
        for (k = 0; k < kinds; k++) {
            if (fabs(gap_val[k] - gap) < 0.5) {
                break;
            }
        }
        if (k == kinds) {
            if (kinds == MAX_GAPS) {
                continue;                   /* too many odd gaps: ignore */
            }
            gap_val[kinds] = gap;
            gap_cnt[kinds] = 0;
            kinds++;
        }
        gap_cnt[k]++;
    }

    /* the most common gap wins; on a tie the smaller gap wins */
    double step = 60.0;                     /* fallback: 1 hour */
    int    best = 0;
    for (int k = 0; k < kinds; k++) {
        if (gap_cnt[k] > best || (gap_cnt[k] == best && gap_val[k] < step)) {
            best = gap_cnt[k];
            step = gap_val[k];
        }
    }
    return step / 60.0;
}


/* ------------------------------------------------------------
   HELPER: how many hours one row stands for.
   Normally that is the time step. Monthly data (step of 28 to 31
   days) is the exception: months are not all the same length, so
   each row gets the real length of its own month.
   ------------------------------------------------------------ */
static double row_hours(const HourRecord *r, double dt)
{
    if (dt >= 28.0 * 24.0 && dt <= 31.0 * 24.0) {
        return days_in_month(r->year, r->month) * 24.0;
    }
    return dt;
}


/* ------------------------------------------------------------
   4. ANNUAL ENERGY (kWh)
   For each valid row:
       power (kW)  = kWp x (irradiance / 1000) x PERF_RATIO
       energy(kWh) = power x time step (h)
   Then add them all up.
   Rows with valid == 0 are skipped (they have no usable number).
   ------------------------------------------------------------ */
double annual_generation_kwh(const Dataset *d, double kwp)
{
    double sum = 0.0;
    double dt  = time_step_hours(d);

    if (kwp <= 0.0 || d->n <= 0) {
        return 0.0;
    }
    for (int i = 0; i < d->n; i++) {
        if (d->rec[i].valid == 0) {
            continue;                       /* skip bad rows */
        }
        double power_kw = kwp * (d->rec[i].irradiance / 1000.0) * PERF_RATIO;
        sum += power_kw * row_hours(&d->rec[i], dt);
    }
    return sum;
}


/* ------------------------------------------------------------
   5. SPECIFIC YIELD (kWh per kWp per year)
   Formula: annual kWh / kWp
   Trick: if kWp = 1, then annual kWh and specific yield are the
   same number, so we reuse annual_generation_kwh with 1 kWp.
   This also avoids dividing by zero.
   ------------------------------------------------------------ */
double specific_yield(const Dataset *d)
{
    return annual_generation_kwh(d, 1.0);
}


/* ------------------------------------------------------------
   EXTRA A: count the rows that were skipped (valid == 0)
   M5 can print this in the report.
   ------------------------------------------------------------ */
int count_invalid_rows(const Dataset *d)
{
    int bad = 0;
    for (int i = 0; i < d->n; i++) {
        if (d->rec[i].valid == 0) {
            bad++;
        }
    }
    return bad;
}


/* ------------------------------------------------------------
   EXTRA B: energy for each month (kWh)
   out[0] = January ... out[11] = December
   Same formula as annual_generation_kwh, but the sum is
   split by month. M5 uses this for monthly.csv and the plots.
   ------------------------------------------------------------ */
void monthly_generation_kwh(const Dataset *d, double kwp, double out[12])
{
    double dt = time_step_hours(d);

    for (int m = 0; m < 12; m++) {
        out[m] = 0.0;
    }
    if (kwp <= 0.0) {
        return;
    }
    for (int i = 0; i < d->n; i++) {
        int m = d->rec[i].month - 1;        /* month 1..12 -> index 0..11 */

        if (d->rec[i].valid == 0 || m < 0 || m > 11) {
            continue;
        }
        out[m] += kwp * (d->rec[i].irradiance / 1000.0) * PERF_RATIO
                  * row_hours(&d->rec[i], dt);
    }
}


/* ------------------------------------------------------------
   EXTRA C: PRINT M2 SUMMARY
   One function M5 can call for the report.
   Shows the results AND how many rows were skipped.
   ------------------------------------------------------------ */
void print_m2_summary(const Dataset *d, const Panel *p, double site_area_m2)
{
    int    n     = panels_that_fit(site_area_m2, p);
    double kwp   = array_kwp(n, p);
    int    bad   = count_invalid_rows(d);
    double month[12];

    monthly_generation_kwh(d, kwp, month);

    printf("\n===== M2 CAPACITY AND YIELD =====\n");
    printf("Site area            : %.1f m2\n", site_area_m2);
    printf("Panel                : %s (%.0f W, %.3f m2)\n",
           p->name, p->rated_w, panel_area_m2(p));
    printf("Panels that fit      : %d\n", n);
    printf("System size          : %.2f kWp\n", kwp);

    if (d->n <= 0) {
        printf("No data rows loaded, so no yield can be calculated.\n");
        return;
    }

    printf("Rows used            : %d\n", d->n - bad);
    printf("Rows skipped (bad)   : %d\n", bad);
    printf("Time step            : %.1f h\n", time_step_hours(d));
    printf("Annual generation    : %.2f kWh\n", annual_generation_kwh(d, kwp));
    printf("Specific yield       : %.2f kWh/kWp\n", specific_yield(d));
    printf("Monthly kWh          :");
    for (int m = 0; m < 12; m++) {
        printf(" %.1f", month[m]);
    }
    printf("\n");
}
