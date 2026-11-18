/* tnb.c - TNB Domestic General Tariff and bill-based savings (see tnb.h) */
#include "tnb.h"
#include "Yield.h"

/* ---- tariff ----------------------------------------------------------- */

/* Energy Efficiency Incentive: RM/kWh credit applied to ALL kWh, chosen by the
 * band the month's total falls in (1 Jul 2025 table on the TNB tariff page). */
static const struct
{
    double upto_kwh;
    double rm_kwh;
} incentive[] = {
    {200, 0.250}, {250, 0.245}, {300, 0.225}, {350, 0.210}, {400, 0.170}, {450, 0.145},
    {500, 0.120}, {550, 0.105}, {600, 0.090}, {650, 0.075}, {700, 0.055}, {750, 0.045},
    {800, 0.040}, {850, 0.025}, {900, 0.010}, {1000, 0.005}
};

static double incentive_rate(double kwh)
{
    size_t i;

    if (kwh > TNB_INCENTIVE_MAX_KWH)
        return (0.0);
    for (i = 0; i < sizeof incentive / sizeof incentive[0]; ++i)
        if (kwh <= incentive[i].upto_kwh)
            return (incentive[i].rm_kwh);
    return (0.0);
}

double tnb_energy_charge(double kwh)
{
    double per_kwh;
    double bill;

    if (!isfinite(kwh) || kwh <= 0.0)
        return (0.0);
    per_kwh = TNB_ENERGY_RM_KWH + TNB_CAPACITY_RM_KWH + TNB_NETWORK_RM_KWH;
    if (kwh > TNB_HIGH_USE_KWH)
        per_kwh += TNB_HIGH_USE_EXTRA;
    per_kwh -= incentive_rate(kwh);
    bill = kwh * per_kwh;
    if (kwh > TNB_PROTECT_KWH)
    {
        bill += TNB_RETAIL_RM + kwh * TNB_AFA_RM_KWH;
        /* simplified: 8% service tax on the share of the bill that belongs to the kWh above 800 */
        bill += (kwh - TNB_PROTECT_KWH) * (per_kwh + TNB_AFA_RM_KWH) * TNB_SERVICE_TAX;
    }
    if (kwh > TNB_KWTBB_FREE_KWH)
        bill *= 1.0 + TNB_KWTBB_RATE;
    return (bill);
}

double tnb_monthly_bill(double kwh)
{
    double rm = tnb_energy_charge(kwh);

    if (rm < TNB_MIN_CHARGE_RM)
        return (TNB_MIN_CHARGE_RM);
    return (rm);
}

/* Inverse of tnb_energy_charge. The bill rises with kWh, so bisection works. */
double tnb_kwh_from_bill(double bill_rm)
{
    double lo = 0.0;
    double hi = 1000.0;
    double mid;
    int i;

    if (!isfinite(bill_rm) || bill_rm <= 0.0)
        return (0.0);
    while (tnb_energy_charge(hi) < bill_rm && hi < 1e300)
        hi *= 2.0;
    for (i = 0; i < 200; ++i)
    {
        mid = 0.5 * (lo + hi);
        if (tnb_energy_charge(mid) < bill_rm)
            lo = mid;
        else
            hi = mid;
    }
    return (0.5 * (lo + hi));
}

/* ---- savings ---------------------------------------------------------- */

double tnb_annual_saving(const Dataset *d, double kwp, double monthly_kwh, double bill_rm)
{
    double gen[12];
    double before;
    double bought;
    double after;
    double gap;
    double total = 0.0;
    int m;

    if (!d || kwp <= 0.0 || monthly_kwh <= 0.0)
        return (0.0);
    monthly_generation_kwh(d, kwp, gen);
    before = tnb_monthly_bill(monthly_kwh);
    /* The bill the user really pays can sit above tnb_monthly_bill(monthly_kwh): the tariff jumps
     * upward at some kWh thresholds, so a bill inside a jump converts to the threshold kWh. Use the
     * real bill as "before" and carry the difference down in proportion to the kWh still bought,
     * so a zero-size array saves nothing and a full offset saves bill minus the RM 3 minimum. */
    gap = 0.0;
    if (isfinite(bill_rm) && bill_rm > before)
        gap = bill_rm - before;
    before += gap;
    for (m = 0; m < 12; ++m)
    {
        bought = monthly_kwh - gen[m];
        if (bought < 0.0)
            bought = 0.0;
        after = tnb_monthly_bill(bought) + gap * (bought / monthly_kwh);
        total += before - after;
    }
    if (total > 0.0)
        return (total);
    return (0.0);
}

void tnb_apply_savings(const Dataset *d, const SystemConfig *cfg, SimResult out[], int n)
{
    int i;

    for (i = 0; i < n; ++i)
    {
        out[i].saving_rm_per_year = tnb_annual_saving(d, out[i].kwp, cfg->monthly_kwh, cfg->monthly_bill_rm);
        out[i].payback_years = payback_years(out[i].capex_rm, out[i].saving_rm_per_year);
        out[i].verdict = decide(out[i].offset_pct, out[i].payback_years);
    }
}
