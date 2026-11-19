/* coverage.c - Module 5 (M5) dataset coverage check (split out of report.c).
 *
 * M2's "annual" generation is simply the sum over the rows in the file. That
 * is only a yearly figure when the file covers about one year without holes,
 * so the program warns when it does not.
 */
#include "report.h"
#include "report_internal.h"
#include "../Module_2/Yield.h"

/* Days since 1970-01-01 for a calendar date (proleptic Gregorian). */
static long days_from_civil(int y, int m, int d)
{
    long era;
    long yoe;
    long doy;
    long doe;
    int mp;

    y -= m <= 2;
    if (y >= 0)
        era = y / 400;
    else
        era = (y - 399) / 400;
    yoe = y - era * 400;
    if (m > 2)
        mp = m - 3;
    else
        mp = m + 9;
    doy = (153L * mp + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + doe - 719468);
}

static double minutes_of(const HourRecord *r)
{
    return ((double)days_from_civil(r->year, r->month, r->day) * 1440.0 + r->hour * 60.0 + r->minute);
}

/* Returns COVERAGE_OK, or the kind of warning with its number in *value
 * (days covered, or the percentage of hours missing). */
int dataset_coverage_warning(const Dataset *d, double *value)
{
    double step;
    double covered_h;
    double span_h;

    if (!d || !d->rec || d->n < 2 || !value)
        return (COVERAGE_OK);
    step = time_step_hours(d);
    covered_h = d->n * step;
    span_h = (minutes_of(&d->rec[d->n - 1]) - minutes_of(&d->rec[0])) / 60.0 + step;
    if (covered_h < 0.9 * 8760.0 || covered_h > 1.1 * 8784.0)
    {
        *value = covered_h / 24.0;
        return (COVERAGE_NOT_ONE_YEAR);
    }
    if (span_h > 0.0 && covered_h < 0.97 * span_h)
    {
        *value = 100.0 * (1.0 - covered_h / span_h);
        return (COVERAGE_HOURS_MISSING);
    }
    return (COVERAGE_OK);
}

/* Print the warning line for a kind returned by dataset_coverage_warning(). */
void print_coverage_warning(FILE *out, int kind, double value)
{
    if (kind == COVERAGE_NOT_ONE_YEAR)
        fprintf(out, "WARNING: the dataset covers about %.0f days of data, not one year, so generation, offset, savings and payback are totals over the file, not annual figures.\n", value);
    else if (kind == COVERAGE_HOURS_MISSING)
        fprintf(out, "WARNING: about %.0f%% of the hours between the first and last timestamp are missing from the file, so generation is under-counted.\n", value);
}
