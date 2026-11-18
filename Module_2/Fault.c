/* Fault.c - flowchart step 2: fault detector (irradiance vs AC power)
 *
 * The PVGIS file has no measured AC power column, so AC power is modelled from
 * irradiance (P = kWp * G/1000 * PERF_RATIO). A reading is a fault when the
 * irradiance is missing, negative, or higher than anything the sun can deliver,
 * because the implied AC power would then be impossible.
 */
#include "Fault.h"
#include "Yield.h"

double modelled_ac_kw(const HourRecord *r, double kwp)
{
    return (kwp * (r->irradiance / 1000.0) * PERF_RATIO);
}

/* Is this reading impossible (NaN, negative or above the sun's maximum)? */
static int implausible(const HourRecord *r)
{
    return (!isfinite(r->irradiance) || r->irradiance < 0.0 ||
            r->irradiance > FAULT_MAX_IRRADIANCE);
}

void fault_scan(Dataset *d, FaultSummary *out)
{
    static double sum[12][24];
    static int cnt[12][24];
    double dt;
    double healthy_kwh;
    int i;
    int m;
    int h;

    if (!out)
        return;
    memset(out, 0, sizeof *out);
    if (!d || !d->rec || d->n <= 0)
        return;
    memset(sum, 0, sizeof sum);
    memset(cnt, 0, sizeof cnt);
    out->rows_checked = d->n;

    /* ---- pass 1: flag bad rows, average the healthy irradiance per month/hour ---- */
    for (i = 0; i < d->n; ++i)
    {
        HourRecord *r = &d->rec[i];

        if (r->valid && implausible(r))
        {
            r->valid = 0;
            out->implausible_rows++;
        }
        else if (!r->valid)
            out->unusable_rows++;
        if (r->valid && r->month >= 1 && r->month <= 12 && r->hour >= 0 && r->hour <= 23)
        {
            sum[r->month - 1][r->hour] += r->irradiance;
            cnt[r->month - 1][r->hour]++;
        }
    }
    out->faulty_rows = out->unusable_rows + out->implausible_rows;

    /* ---- pass 2: estimate the energy lost in the rejected rows ---- */
    dt = time_step_hours(d);
    for (i = 0; i < d->n; ++i)
    {
        const HourRecord *r = &d->rec[i];

        if (r->valid)
            continue;
        m = r->month - 1;
        h = r->hour;
        if (m < 0 || m > 11 || h < 0 || h > 23 || cnt[m][h] == 0)
            continue;
        out->loss_kwh_per_kwp += (sum[m][h] / cnt[m][h]) / 1000.0 * PERF_RATIO * record_hours(r, dt);
    }
    healthy_kwh = specific_yield(d);
    if (healthy_kwh + out->loss_kwh_per_kwp > 0.0)
        out->loss_pct = 100.0 * out->loss_kwh_per_kwp / (healthy_kwh + out->loss_kwh_per_kwp);
}
