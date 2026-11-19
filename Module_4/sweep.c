/* sweep.c - Module 4 (M4) scenario sweep
 *
 * EEEB1014 Solar Planner, Theme E
 *
 * Flow for every candidate panel count n = 1, 2, 3, ... (up to the number
 * that fits on the roof, and never past the caller's array capacity):
 *
 *     n  -->  panels_that_fit / array_kwp      (M2)
 *          -->  annual_generation_kwh          (M2, uses the weather dataset)
 *          -->  offset_pct                     (M3)
 *          -->  capex, saving_rm, payback      (M4 / M3)
 *          -->  co2_avoided_kg                 (M4)
 *          -->  decide                         (M3, BUILD / SMALLER / DONT)
 *
 * One SimResult record is stored per scenario, and the caller (M5) prints the
 * table in the report.
 */
#include "common.h"
#include "sweep.h"
#include "impact.h"


/* BEST_MIN_OFFSET_PCT (the offset target used by best_index) is defined in
 * sweep.h so the demo/report can print it and the Python check can mirror it. */

/* ---- sweep ------------------------------------------------------------ */

/* 1 = the configuration can produce a useful scenario. Written in the
 * positive form so that NaN fails every test; isfinite() rejects +/-infinity. */
static int config_is_valid(const SystemConfig *cfg)
{
    if (!isfinite(cfg->area_m2) || !(cfg->area_m2 > 0.0) ||
        !isfinite(cfg->annual_kwh) || !(cfg->annual_kwh > 0.0))
        return (0);
    if (!isfinite(cfg->tariff_rm_kwh) || cfg->tariff_rm_kwh < 0.0 ||
        !isfinite(cfg->panel.price_rm) || cfg->panel.price_rm < 0.0)
        return (0);
    /* A zero or negative rating would give a zero/negative kWp. */
    if (!isfinite(cfg->panel.rated_w) || !(cfg->panel.rated_w > 0.0))
        return (0);
    return (1);
}

int sweep_panels(const Dataset *d, const SystemConfig *cfg, SimResult out[], int max)
{
    int fit;
    int count;
    int n;
    double displaced_kwh;
    SimResult *r;

    if (d == NULL || cfg == NULL || out == NULL || max <= 0)
        return (0);
    if (!config_is_valid(cfg))
        return (0);
    /* How many panels physically fit? (M2: floor(usable area / panel area)) */
    fit = panels_that_fit(cfg->area_m2, &cfg->panel);
    if (fit <= 0)
        return (0);                 /* roof too small for even one panel */

    /* One record per panel count. The loop stops at the roof limit or the
     * caller's array size, which protects the output array from overrun. */
    count = 0;
    for (n = 1; n <= fit && count < max; ++n)
    {
        r = &out[count];
        r->n_panels = n;
        r->kwp = array_kwp(n, &cfg->panel);
        r->annual_gen_kwh = annual_generation_kwh(d, r->kwp);
        /* A negative or NaN yield means the dataset or M2 is broken: stop
         * rather than exporting nonsense numbers. */
        if (!isfinite(r->annual_gen_kwh) || r->annual_gen_kwh < 0.0)
            return (0);
        r->offset_pct = offset_pct(r->annual_gen_kwh, cfg->annual_kwh);
        /* M2 sums hour by hour, so a nominal 80.000 % can arrive as
         * 79.999999999999844. Snap to 1e-6 % so that the stored value, the
         * M3 verdict (strict >= 80 / >= 40) and best_index all agree. */
        r->offset_pct = round(r->offset_pct * 1e6) / 1e6;
        /* Capital cost uses panel price only. */
        r->capex_rm = (double)n * cfg->panel.price_rm;
        r->saving_rm_per_year = saving_rm(r->annual_gen_kwh, cfg->annual_kwh, cfg->tariff_rm_kwh);
        r->payback_years = payback_years(r->capex_rm, r->saving_rm_per_year);
        /* CO2 uses the same cap as the money: only grid-displacing kWh count. */
        if (r->annual_gen_kwh < cfg->annual_kwh)
            displaced_kwh = r->annual_gen_kwh;
        else
            displaced_kwh = cfg->annual_kwh;
        r->co2_avoided_kg = co2_avoided_kg(displaced_kwh);
        r->verdict = decide(r->offset_pct, r->payback_years);
        ++count;
    }
    return (count);
}

/* ---- best option ------------------------------------------------------ */

/* 1 = this row may be recommended: not rejected by M3, finite offset and a
 * finite, non-negative payback. */
static int is_candidate(const SimResult *r)
{
    if (r->verdict == VERDICT_DONT)
        return (0);
    if (!isfinite(r->payback_years) || r->payback_years < 0.0)
        return (0);
    if (!isfinite(r->offset_pct))
        return (0);
    return (1);
}

int best_index(const SimResult out[], int n)
{
    const double EPS = 1e-9;        /* tolerance used when comparing two paybacks */
    double diff;
    int best;
    int i;

    if (out == NULL || n <= 0)
        return (-1);
    best = -1;

    /* Pass 1: options that reach the offset target. Below the point where
     * generation covers the demand, generation and capital cost both grow
     * with the panel count, so several sizes have an identical payback.
     * Comparing payback alone would then be blind, and a tie rule such as
     * "fewest panels" would recommend a tiny array. The offset target removes
     * that blind spot: only arrays that supply most of the demand compete. */
    for (i = 0; i < n; ++i)
    {
        if (!is_candidate(&out[i]))
            continue;
        if (!(out[i].offset_pct + EPS >= BEST_MIN_OFFSET_PCT))
            continue;               /* not enough of the demand is covered */
        if (best < 0)
        {
            best = i;
            continue;
        }
        diff = out[i].payback_years - out[best].payback_years;
        if (diff < -EPS)
            best = i;               /* clearly faster payback: new best */
        else if (fabs(diff) <= EPS && out[i].offset_pct > out[best].offset_pct + EPS)
            best = i;               /* same payback: prefer more grid energy displaced */
    }
    if (best >= 0)
        return (best);

    /* Pass 2: nothing reaches the target (small or shaded site), so recommend
     * the array that covers the most demand among the options M3 accepts. */
    for (i = 0; i < n; ++i)
    {
        if (!is_candidate(&out[i]))
            continue;
        if (best < 0 || out[i].offset_pct > out[best].offset_pct + EPS)
            best = i;
    }
    return (best);
}
