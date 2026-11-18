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
 * table and exports sweep.csv.
 */

#include "common.h"
#include "sweep.h"
#include "impact.h"

#include <math.h>

/* BEST_MIN_OFFSET_PCT (the offset target used by best_index) is defined in
 * sweep.h so the demo/report can print it and the Python check can mirror it. */

int sweep_panels(const Dataset *d, const SystemConfig *cfg,
                 SimResult out[], int max)
{
    /* 1. Validate the pointers and the caller's array capacity.            */
    if (d == NULL || cfg == NULL || out == NULL || max <= 0) {
        return 0;
    }

    /* 2. Validate the user's numbers before any calculation. A zero site
     *    area or a zero annual demand cannot produce a useful scenario.    */
    /* Written in the positive form so that NaN fails every test, and with
     * isfinite() so that +/-infinity is rejected too.                      */
    if (!isfinite(cfg->area_m2) || !(cfg->area_m2 > 0.0) ||
        !isfinite(cfg->annual_kwh) || !(cfg->annual_kwh > 0.0)) {
        return 0;
    }
    if (!isfinite(cfg->tariff_rm_kwh) || cfg->tariff_rm_kwh < 0.0 ||
        !isfinite(cfg->panel.price_rm) || cfg->panel.price_rm < 0.0) {
        return 0;
    }
    /* A zero or negative rating would give a zero/negative kWp.            */
    if (!isfinite(cfg->panel.rated_w) || !(cfg->panel.rated_w > 0.0)) {
        return 0;
    }

    /* 3. How many panels physically fit? (M2: floor(usable area / panel area)) */
    int fit = panels_that_fit(cfg->area_m2, &cfg->panel);
    if (fit <= 0) {
        /* Roof too small for even one panel: no scenario to show.          */
        return 0;
    }

    /* 4. Build one record per panel count. The loop stops at either limit:
     *    the roof, or the caller's array size. This is what protects the
     *    output array from being overrun.                                  */
    int count = 0;
    for (int n = 1; n <= fit && count < max; ++n) {
        SimResult *r = &out[count];

        r->n_panels       = n;
        r->kwp            = array_kwp(n, &cfg->panel);
        r->annual_gen_kwh = annual_generation_kwh(d, r->kwp);

        /* A negative or NaN yield means the dataset or M2 is broken: stop
         * rather than exporting nonsense numbers.                        */
        if (!isfinite(r->annual_gen_kwh) || r->annual_gen_kwh < 0.0) {
            return 0;
        }

        r->offset_pct = offset_pct(r->annual_gen_kwh, cfg->annual_kwh);
        /* M2 sums hour by hour, so a nominal 80.000 % can arrive as
         * 79.999999999999844. Snap to 1e-6 % so that the stored value, the
         * M3 verdict (strict >= 80 / >= 40) and best_index all agree.     */
        r->offset_pct = round(r->offset_pct * 1e6) / 1e6;

        /* Capital cost uses panel price only (see the limitation in README). */
        r->capex_rm = (double)n * cfg->panel.price_rm;

        r->saving_rm_per_year =
            saving_rm(r->annual_gen_kwh, cfg->annual_kwh, cfg->tariff_rm_kwh);

        r->payback_years =
            payback_years(r->capex_rm, r->saving_rm_per_year);

        /* CO2 uses the same cap as the money: only grid-displacing kWh count. */
        double displaced_kwh = (r->annual_gen_kwh < cfg->annual_kwh)
                             ? r->annual_gen_kwh
                             : cfg->annual_kwh;
        r->co2_avoided_kg = co2_avoided_kg(displaced_kwh);

        r->verdict = decide(r->offset_pct, r->payback_years);

        ++count;
    }

    return count;
}

int best_index(const SimResult out[], int n)
{
    const double EPS = 1e-9;   /* tolerance used when comparing two paybacks */

    if (out == NULL || n <= 0) {
        return -1;
    }

    int best = -1;

    /* ---- Pass 1: options that reach the offset target ------------------
     * Below the point where generation covers the demand, generation and
     * capital cost both grow with the panel count, so several sizes have an
     * identical payback. Comparing payback alone would then be blind, and a
     * tie rule such as "fewest panels" would recommend a tiny array that
     * covers a third of the demand. The offset target removes that blind
     * spot: only arrays that actually supply most of the demand compete.  */
    for (int i = 0; i < n; ++i) {
        /* An option M3 rejects as "don't build" can never be recommended.  */
        if (out[i].verdict == VERDICT_DONT) {
            continue;
        }
        /* Never pays back (infinite/NaN payback) is not a recommendation.  */
        if (!isfinite(out[i].payback_years) || out[i].payback_years < 0.0) {
            continue;
        }
        if (!isfinite(out[i].offset_pct)) {
            continue;
        }
        /* Not enough of the demand is covered, so not a candidate yet.     */
        if (!(out[i].offset_pct + EPS >= BEST_MIN_OFFSET_PCT)) {
            continue;
        }

        if (best < 0) {
            best = i;
            continue;
        }

        double diff = out[i].payback_years - out[best].payback_years;

        if (diff < -EPS) {
            /* Clearly faster payback: new best.                            */
            best = i;
        } else if (fabs(diff) <= EPS &&
                   out[i].offset_pct > out[best].offset_pct + EPS) {
            /* Same payback: prefer the option that displaces more grid
             * energy, because it avoids more CO2 for the same return.     */
            best = i;
        }
    }

    if (best >= 0) {
        return best;
    }

    /* ---- Pass 2: nothing reaches the target ---------------------------
     * A small or shaded site may never reach it, so recommend the array that
     * covers the most demand among the options M3 still accepts.          */
    for (int i = 0; i < n; ++i) {
        if (out[i].verdict == VERDICT_DONT) {
            continue;
        }
        if (!isfinite(out[i].payback_years) || out[i].payback_years < 0.0) {
            continue;
        }
        if (!isfinite(out[i].offset_pct)) {
            continue;
        }
        if (best < 0 || out[i].offset_pct > out[best].offset_pct + EPS) {
            best = i;
        }
    }

    return best;
}
