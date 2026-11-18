/* sweep.h - Module 4 (M4) scenario sweep
 *
 * EEEB1014 Solar Planner, Theme E
 *
 * sweep_panels() builds one SimResult per candidate array size (1 panel up to
 * the number that physically fits on the site) so the user can compare
 * options instead of being given a single answer.
 */

#ifndef SWEEP_H
#define SWEEP_H

#include "common.h"

/* Selection rule used by best_index(): an option must cover at least this
 * share of the user's annual demand to be worth recommending ("shortest
 * payback that reaches the offset target"). 80 % means the array should
 * supply most of the household's yearly electricity. Change the value here,
 * keep python/recalc_m4.py in step, and justify the change in the report.   */
#define BEST_MIN_OFFSET_PCT 80.0

/* Fill out[0..count-1] with one scenario per panel count from 1 to the number
 * that fits, never writing more than `max` entries.
 *   - uses M2 (panels_that_fit, array_kwp, annual_generation_kwh)
 *   - uses M3 (offset_pct, payback_years, decide)
 *   - uses M4 (saving_rm, co2_avoided_kg)
 * Returns the number of scenarios written (0 on bad input or if no panel
 * fits). Callers must not use more than the returned count.                */
int sweep_panels(const Dataset *d, const SystemConfig *cfg,
                 SimResult out[], int max);

/* Index of the recommended row in out[0..n-1], or -1 if no row is acceptable.
 * Criterion (see BEST_MIN_OFFSET_PCT above): among rows M3 did not
 * reject with VERDICT_DONT and that cover at least the offset target, the
 * shortest finite payback wins; on a tie, the higher offset wins. If no row
 * reaches the target, the row covering the most demand is returned.        */
int best_index(const SimResult out[], int n);

#endif /* SWEEP_H */
