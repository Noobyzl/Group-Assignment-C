#include <stdio.h>
#include "common.h"

#define MAX_PAYBACK_YEARS 10.0  /* Maximum acceptable payback period */
#define MIN_OFFSET_BUILD 80.0   /* Minimum % for a full BUILD recommendation */
#define MIN_OFFSET_SMALLER 40.0 /* Minimum % to at least BUILD SMALLER */

/*
 * Calculates the offset percentage.
 * Guards against zero/negative use and caps at 100%.
 */
double offset_pct(double gen_kwh, double use_kwh)
{
    if (use_kwh <= 0.0)
    {
        return 0.0;
    }

    double pct = (gen_kwh / use_kwh) * 100.0;

    if (pct > 100.0)
    {
        return 100.0;
    }

    return pct;
}

/*
 * Calculates payback years.
 * Guards against zero savings by returning -1.0 (meaning "never pays back").
 */
double payback_years(double capex_rm, double saving_rm_per_year)
{
    if (saving_rm_per_year <= 0.0)
    {
        return -1.0;
    }

    return capex_rm / saving_rm_per_year;
}

/*
 * Decides the verdict based on offset percentage and payback years.
 */
Verdict decide(double offset_pct, double payback_years)
{
    /* Reject if payback is negative (zero savings) or exceeds our maximum limit */
    if (payback_years < 0.0 || payback_years > MAX_PAYBACK_YEARS)
    {
        return VERDICT_DONT;
    }

    /* Decide based on offset thresholds */
    if (offset_pct >= MIN_OFFSET_BUILD)
    {
        return VERDICT_BUILD;
    }
    else if (offset_pct >= MIN_OFFSET_SMALLER)
    {
        return VERDICT_SMALLER;
    }
    else
    {
        return VERDICT_DONT; /* Offset is too low to be worth it */
    }
}

/*
 * Returns a descriptive string for the verdict using a switch statement.
 */
const char *verdict_text(Verdict v)
{
    switch (v)
    {
    case VERDICT_BUILD:
        return "APPROVED (FEASIBLE WITHIN BUDGET & ROOF LIMITS)";
    case VERDICT_SMALLER:
        return "BUILD SMALLER (PARTIAL OFFSET ACHIEVED)";
    case VERDICT_DONT:
        return "DON'T BUILD (POOR OFFSET OR PAYBACK TOO LONG)";
    default:
        return "UNKNOWN VERDICT";
    }
}