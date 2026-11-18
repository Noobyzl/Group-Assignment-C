/* Logic_Decision.c - Module 3 (M3) offset, payback and build decision.
 *
 * Verdict rules: payback above MAX_PAYBACK_YEARS is DON'T BUILD, an offset of
 * MIN_OFFSET_BUILD or more is BUILD, MIN_OFFSET_SMALLER or more is BUILD SMALLER.
 */
#include "common.h"

#define MAX_PAYBACK_YEARS    10.0   /* maximum acceptable payback period */
#define MIN_OFFSET_BUILD     80.0   /* minimum % for a full BUILD recommendation */
#define MIN_OFFSET_SMALLER   40.0   /* minimum % to at least BUILD SMALLER */

/* ---- offset and payback ----------------------------------------------- */

/* Offset percentage. Bad or non-positive inputs give 0 (never NaN or a
 * negative value) and the result is capped at 100%. */
double offset_pct(double gen_kwh, double use_kwh)
{
    double pct;

    if (isnan(gen_kwh) || isnan(use_kwh) || use_kwh <= 0.0 || gen_kwh <= 0.0)
        return (0.0);
    pct = (gen_kwh / use_kwh) * 100.0;
    if (pct > 100.0)
        return (100.0);
    return (pct);
}

/* Payback years. Zero or negative savings return -1.0 ("never pays back").
 * Invalid data (NaN, negative capex, NaN saving) returns NAN so it cannot be
 * confused with the -1.0 sentinel; decide() treats NAN as DON'T BUILD. */
double payback_years(double capex_rm, double saving_rm_per_year)
{
    if (isnan(capex_rm) || isnan(saving_rm_per_year) || capex_rm < 0.0)
        return (NAN);
    if (saving_rm_per_year <= 0.0)
        return (-1.0);
    return (capex_rm / saving_rm_per_year);
}

/* ---- the decision ----------------------------------------------------- */

/* The single place that holds the decision rules, so the verdict and its
 * reason can never disagree. 'reason' may be NULL when only the verdict is needed. */
static Verdict evaluate(double offset_pct, double payback_years, const char **reason)
{
    const char *r;
    Verdict v;

    if (isnan(payback_years) || isnan(offset_pct) || offset_pct < 0.0 || offset_pct > 100.0)
    {
        v = VERDICT_DONT;
        r = "Invalid input data (NaN, negative capex or offset outside 0-100%); cannot recommend building.";
    }
    else if (payback_years < 0.0)
    {
        v = VERDICT_DONT;
        r = "No annual saving, so the system never pays back.";
    }
    else if (payback_years > MAX_PAYBACK_YEARS)
    {
        v = VERDICT_DONT;
        r = "Payback period is longer than the maximum acceptable limit.";
    }
    else if (offset_pct >= MIN_OFFSET_BUILD)
    {
        v = VERDICT_BUILD;
        r = "Offset meets the build threshold and payback is within the limit.";
    }
    else if (offset_pct >= MIN_OFFSET_SMALLER)
    {
        v = VERDICT_SMALLER;
        r = "Payback is acceptable but offset is below the full-build threshold; a smaller array is advised.";
    }
    else
    {
        v = VERDICT_DONT;
        r = "Offset is too low to be worth building.";
    }
    if (reason != NULL)
        *reason = r;
    return (v);
}

/* Verdict from offset percentage and payback years. */
Verdict decide(double offset_pct, double payback_years)
{
    return (evaluate(offset_pct, payback_years, NULL));
}

/* Plain-words reason for the verdict decide() gives for the same inputs. */
const char *decide_reason(double offset_pct, double payback_years)
{
    const char *reason;

    evaluate(offset_pct, payback_years, &reason);
    return (reason);
}

/* Descriptive text for a verdict. */
const char *verdict_text(Verdict v)
{
    switch (v)
    {
    case VERDICT_BUILD:
        return ("APPROVED (FEASIBLE WITHIN BUDGET & ROOF LIMITS)");
    case VERDICT_SMALLER:
        return ("BUILD SMALLER (PARTIAL OFFSET ACHIEVED)");
    case VERDICT_DONT:
        return ("DON'T BUILD (POOR OFFSET OR PAYBACK TOO LONG)");
    default:
        return ("UNKNOWN VERDICT");
    }
}
