/* Yield.c - Module 2 (M2) capacity and yield
 *
 * Answers two questions:
 *   1. How many panels fit on the site, and how big is the system?
 *   2. How much electricity will that system make in a year?
 *
 * Units used everywhere:
 *   irradiance  W/m2        panel size  m / m2
 *   power       kW          energy      kWh
 */
#include "Yield.h"

/* ---- panel and system size -------------------------------------------- */

/* Area of one panel (m2) = length x width. */
double panel_area_m2(const Panel *p)
{
    return (p->length_m * p->width_m);
}

/* How many panels fit: n = floor(site area x USABLE_FRACTION / panel area).
 * floor() rounds DOWN because you cannot install half a panel.
 * No area, NaN/inf area or a broken panel size gives 0 panels; a site too big
 * for an int gives INT_MAX. */
int panels_that_fit(double site_area_m2, const Panel *p)
{
    double one;
    double q;

    one = panel_area_m2(p);
    if (!isfinite(site_area_m2) || site_area_m2 <= 0.0 || !isfinite(one) || one <= 0.0)
        return (0);                 /* NaN, inf, zero or negative */
    /* The tiny factor keeps an exact fit (3 m2 x 0.7 / 2.1 m2) from rounding
     * down to one panel too few. */
    q = floor(site_area_m2 * USABLE_FRACTION / one * (1.0 + 1e-12));
    if (!isfinite(q) || q >= (double)INT_MAX)
        return (INT_MAX);           /* cannot count past int */
    return ((int)q);
}

/* System size (kWp) = number of panels x panel watts / 1000. */
double array_kwp(int n_panels, const Panel *p)
{
    if (n_panels <= 0)
        return (0.0);
    return (n_panels * p->rated_w / 1000.0);
}

/* ---- energy ----------------------------------------------------------- */

/* Annual energy (kWh). For each valid row:
 *     power (kW)  = kWp x (irradiance / 1000) x PERF_RATIO
 *     energy(kWh) = power x time step (h)
 * then add them all up. Rows with valid == 0 have no usable number and are skipped. */
double annual_generation_kwh(const Dataset *d, double kwp)
{
    double sum = 0.0;
    double dt = time_step_hours(d);
    double power_kw;
    int i;

    if (kwp <= 0.0 || d->n <= 0)
        return (0.0);
    for (i = 0; i < d->n; i++)
    {
        if (d->rec[i].valid == 0)
            continue;               /* skip bad rows */
        power_kw = kwp * (d->rec[i].irradiance / 1000.0) * PERF_RATIO;
        sum += power_kw * record_hours(&d->rec[i], dt);
    }
    return (sum);
}

/* Specific yield (kWh per kWp per year) = annual kWh / kWp. With kWp = 1 the
 * two numbers are the same, so annual_generation_kwh is reused. This also
 * avoids dividing by zero. */
double specific_yield(const Dataset *d)
{
    return (annual_generation_kwh(d, 1.0));
}

/* Count the rows that were skipped (valid == 0). */
int count_invalid_rows(const Dataset *d)
{
    int bad = 0;
    int i;

    for (i = 0; i < d->n; i++)
        if (d->rec[i].valid == 0)
            bad++;
    return (bad);
}

/* Energy for each month (kWh): out[0] = January ... out[11] = December.
 * Same formula as annual_generation_kwh, but the sum is split by month. */
void monthly_generation_kwh(const Dataset *d, double kwp, double out[12])
{
    double dt = time_step_hours(d);
    int m;
    int i;

    for (m = 0; m < 12; m++)
        out[m] = 0.0;
    if (kwp <= 0.0)
        return;
    for (i = 0; i < d->n; i++)
    {
        m = d->rec[i].month - 1;    /* month 1..12 -> index 0..11 */
        if (d->rec[i].valid == 0 || m < 0 || m > 11)
            continue;
        out[m] += kwp * (d->rec[i].irradiance / 1000.0) * PERF_RATIO * record_hours(&d->rec[i], dt);
    }
}

/* ---- summary ---------------------------------------------------------- */

/* Print the M2 results and how many rows were skipped. */
void print_m2_summary(const Dataset *d, const Panel *p, double site_area_m2)
{
    int n = panels_that_fit(site_area_m2, p);
    double kwp = array_kwp(n, p);
    int bad = count_invalid_rows(d);
    double month[12];
    int m;

    monthly_generation_kwh(d, kwp, month);
    printf("\n===== M2 CAPACITY AND YIELD =====\n");
    printf("Site area            : %.1f m2\n", site_area_m2);
    printf("Panel                : %s (%.0f W, %.3f m2)\n", p->name, p->rated_w, panel_area_m2(p));
    printf("Panels that fit      : %d\n", n);
    printf("System size          : %.2f kWp\n", kwp);
    if (d->n <= 0)
    {
        printf("No data rows loaded, so no yield can be calculated.\n");
        return;
    }
    printf("Rows used            : %d\n", d->n - bad);
    printf("Rows skipped (bad)   : %d\n", bad);
    printf("Time step            : %.1f h\n", time_step_hours(d));
    printf("Annual generation    : %.2f kWh\n", annual_generation_kwh(d, kwp));
    printf("Specific yield       : %.2f kWh/kWp\n", specific_yield(d));
    printf("Monthly kWh          :");
    for (m = 0; m < 12; m++)
        printf(" %.1f", month[m]);
    printf("\n");
}
