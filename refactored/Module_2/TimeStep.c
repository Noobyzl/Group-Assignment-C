/* TimeStep.c - Module 2 (M2) time step between rows (split out of Yield.c).
 *
 * time_step_hours() and record_hours() are declared in Yield.h.
 */
#include "Yield.h"

/* ---- time step -------------------------------------------------------- */

/* Days in a month, with the Gregorian leap-year rule. */
static int days_in_month(int y, int m)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;

    if (m < 1 || m > 12)
        return (30);
    if (m == 2 && leap)
        return (29);
    return (days[m - 1]);
}

/* Days since 1970-01-01 (proleptic Gregorian calendar). */
static double day_number(int y, int m, int d)
{
    long yy = (long)y - (m <= 2);
    long era;
    long yoe;
    long doy;
    long doe;
    int mp;

    if (yy >= 0)
        era = yy / 400;
    else
        era = (yy - 399) / 400;
    yoe = yy - era * 400;
    if (m > 2)
        mp = m - 3;
    else
        mp = m + 9;
    doy = (153L * mp + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return ((double)(era * 146097 + doe - 719468));
}

static double minute_number(const HourRecord *r)
{
    return (day_number(r->year, r->month, r->day) * 1440.0 + r->hour * 60.0 + r->minute);
}

/* Distinct gap sizes seen between neighbouring rows, and how often each occurs. */
#define MAX_GAPS 16

typedef struct {
    double val[MAX_GAPS];           /* distinct gap sizes (minutes) */
    int    cnt[MAX_GAPS];           /* how often each one occurs */
    int    kinds;                   /* entries in use */
} GapTable;

/* Count one gap. Returns nothing; a full table lets the rarest stored size make room, so
 * a steady step seen only after many odd gaps still wins. */
static void tally_gap(GapTable *t, double gap)
{
    int k;
    int rare;

    for (k = 0; k < t->kinds; k++)
        if (fabs(t->val[k] - gap) < 0.5)
            break;
    if (k == t->kinds)
    {
        if (t->kinds == MAX_GAPS)
        {
            rare = 0;
            for (k = 1; k < MAX_GAPS; k++)
                if (t->cnt[k] < t->cnt[rare])
                    rare = k;
            t->val[rare] = gap;
            t->cnt[rare] = 0;
            k = rare;
        }
        else
        {
            t->val[t->kinds] = gap;
            t->cnt[t->kinds] = 0;
            t->kinds++;
        }
    }
    t->cnt[k]++;
}

/* The most common gap wins; on a tie the smaller gap wins. Falls back to 60 minutes. */
static double most_common_gap(const GapTable *t)
{
    double step = 60.0;             /* fallback: 1 hour */
    int best = 0;
    int k;

    for (k = 0; k < t->kinds; k++)
    {
        if (t->cnt[k] > best || (t->cnt[k] == best && t->val[k] < step))
        {
            best = t->cnt[k];
            step = t->val[k];
        }
    }
    return (step);
}

/* The time between rows, in hours. We must NOT assume 1 hour, because the
 * lecturer's second file may be different, so the timestamps (minutes
 * included) are used. Each row gets a minute number on the real calendar
 * (leap years included). The step is the MOST COMMON size of the gap between
 * neighbouring rows, so one stray row cannot change it. The gap is an
 * absolute value, so a file written newest-first works too. Gaps of 0
 * (duplicate timestamps) are ignored. If no gap can be found, it falls back
 * to 1 hour. */
double time_step_hours(const Dataset *d)
{
    GapTable t;
    double gap;
    int i;

    t.kinds = 0;
    for (i = 1; i < d->n; i++)
    {
        gap = fabs(minute_number(&d->rec[i]) - minute_number(&d->rec[i - 1]));
        if (gap < 0.5)
            continue;               /* duplicate timestamp */
        tally_gap(&t, gap);
    }
    return (most_common_gap(&t) / 60.0);
}

/* How many hours one row stands for. Normally that is the time step. Monthly
 * data (step of 28 to 31 days) is the exception: months are not all the same
 * length, so each row gets the real length of its own month. */
double record_hours(const HourRecord *r, double dt)
{
    if (dt >= 28.0 * 24.0 && dt <= 31.0 * 24.0)
        return (days_in_month(r->year, r->month) * 24.0);
    return (dt);
}
