/* search.c - Module 5 (M5) search menu
 *
 * Options 1 and 5 read the hourly records (by month, and by exact timestamp).
 * Options 2, 3 and 4 look up rows of the scenario sweep table.
 */
#include "search.h"
#include "common.h"
#include "Yield.h"
#include "Fault.h"
#include "sweep.h"
#include "report.h"

/* ---- input ------------------------------------------------------------ */

/* Read one line, dropping the rest of an over-long line. 0 = input ended. */
static int read_text(const char *prompt, char *line, size_t size)
{
    int c;

    printf("%s", prompt);
    if (!fgets(line, (int)size, stdin))
        return (0);
    if (!strchr(line, '\n'))
    {
        c = getchar();
        if (c != '\n' && c != EOF)
        {
            while (c != '\n' && c != EOF)
                c = getchar();
            line[0] = '\0';        /* over-long line: answer is empty, so it is rejected */
        }
    }
    return (1);
}

static int read_int(const char *prompt, int *out)
{
    char line[128];
    char *end;
    long v;

    if (!read_text(prompt, line, sizeof line))
        return (0);
    v = strtol(line, &end, 10);
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        ++end;
    if (end == line || *end || v < -2147483647L || v > 2147483647L)
        return (0);
    *out = (int)v;
    return (1);
}

static int read_double(const char *prompt, double *out)
{
    char line[128];
    char *end;
    double v;

    if (!read_text(prompt, line, sizeof line))
        return (0);
    v = strtod(line, &end);
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        ++end;
    if (end == line || *end || !isfinite(v))
        return (0);
    *out = v;
    return (1);
}

/* ---- option 1: month -------------------------------------------------- */

static void search_by_month(const Dataset *d, const SimResult sweep[], int n)
{
    int month;
    int records = 0;
    int shown = 0;
    int i;
    double monthly[12];
    double unit_gen;
    const HourRecord *r;

    if (!d || !d->rec || !read_int("Month (1-12): ", &month) || month < 1 || month > 12)
    {
        puts("Invalid month or dataset.");
        return;
    }
    for (i = 0; i < d->n; ++i)
        if (d->rec[i].valid && d->rec[i].month == month)
            records++;
    if (!records)
    {
        puts("No valid records found for that month.");
        return;
    }
    monthly_generation_kwh(d, 1.0, monthly);   /* M2: kWh per kWp for each month */
    unit_gen = monthly[month - 1];
    printf("Month %d: %d valid records; %.2f kWh per kWp.\n", month, records, unit_gen);
    puts("Estimated generation for each available panel count:");
    for (i = 0; i < n; ++i)
        printf("%2d panels (%6.3f kWp): %.2f kWh\n", sweep[i].n_panels, sweep[i].kwp, sweep[i].kwp * unit_gen);

    /* the first 24 hourly records of that month, with the AC power of the sized system */
    printf("Hourly records for month %d (AC power for %d panels, %.3f kWp):\n",
           month, sweep[n - 1].n_panels, sweep[n - 1].kwp);
    for (i = 0; i < d->n && shown < 24; ++i)
    {
        r = &d->rec[i];
        if (!r->valid || r->month != month)
            continue;
        printf("  %04d-%02d-%02d %02d:%02d | G %.2f W/m2 | T %.2f C | AC %.3f kW\n",
               r->year, r->month, r->day, r->hour, r->minute, r->irradiance, r->temp_c,
               modelled_ac_kw(r, sweep[n - 1].kwp));
        shown++;
    }
    if (records > shown)
        printf("  ... %d more records in this month\n", records - shown);
}

/* ---- option 5: timestamp ---------------------------------------------- */

static void search_by_timestamp(const Dataset *d, const SimResult sweep[], int n)
{
    int y;
    int mo;
    int dd;
    int hh;
    int i;
    int found = 0;
    char line[128];
    const HourRecord *r;

    if (!d || !d->rec)
    {
        puts("No dataset loaded.");
        return;
    }
    if (!read_text("Timestamp (YYYYMMDD:HH, e.g. 20190615:12): ", line, sizeof line))
    {
        puts("Invalid input.");
        return;
    }
    if (sscanf(line, "%4d%2d%2d:%2d", &y, &mo, &dd, &hh) != 4)
    {
        puts("Invalid timestamp format.");
        return;
    }
    for (i = 0; i < d->n; ++i)
    {
        r = &d->rec[i];
        if (r->year != y || r->month != mo || r->day != dd || r->hour != hh)
            continue;
        if (!r->valid)
            printf("%04d-%02d-%02d %02d:%02d | record rejected by the fault detector or unusable\n",
                   r->year, r->month, r->day, r->hour, r->minute);
        else
            printf("%04d-%02d-%02d %02d:%02d | G %.2f W/m2 | T %.2f C | AC %.3f kW (%d panels, %.3f kWp)\n",
                   r->year, r->month, r->day, r->hour, r->minute, r->irradiance, r->temp_c,
                   modelled_ac_kw(r, sweep[n - 1].kwp), sweep[n - 1].n_panels, sweep[n - 1].kwp);
        found = 1;
    }
    if (!found)
        puts("No hourly record found for that timestamp.");
}

/* ---- options 2 to 4: sweep table -------------------------------------- */

static void search_by_panel_count(const SimResult sweep[], int n)
{
    int panels;
    int i;
    int found = 0;
    char pb[40];

    if (!read_int("Panel count: ", &panels))
    {
        puts("Invalid input.");
        return;
    }
    for (i = 0; i < n; ++i)
    {
        if (sweep[i].n_panels != panels)
            continue;
        if (sweep[i].payback_years < 0.0 || !isfinite(sweep[i].payback_years))
            snprintf(pb, sizeof pb, "never pays back");
        else
            snprintf(pb, sizeof pb, "%.2f years", sweep[i].payback_years);
        printf("Panels: %d | Capacity: %.3f kWp | Generation: %.2f kWh | Payback: %s | Verdict: %s\n",
               sweep[i].n_panels, sweep[i].kwp, sweep[i].annual_gen_kwh, pb, verdict_text(sweep[i].verdict));
        found = 1;
    }
    if (!found)
        puts("No result found for that panel count.");
}

static void search_best(const SimResult sweep[], int n)
{
    int best = report_best_index(sweep, n);   /* the report's own choice (the sized system) */

    if (best < 0)
        puts("No acceptable configuration found.");
    else
        printf("Best option: %d panels | RM %.2f cost | %.2f years | %.2f%% offset\n",
               sweep[best].n_panels, sweep[best].capex_rm, sweep[best].payback_years, sweep[best].offset_pct);
}

static void search_by_payback(const SimResult sweep[], int n)
{
    double limit;
    int i;
    int found = 0;

    if (!read_double("Maximum payback (years): ", &limit) || limit < 0.0)
    {
        puts("Enter a valid non-negative number.");
        return;
    }
    for (i = 0; i < n; ++i)
    {
        if (!(sweep[i].payback_years >= 0.0 && sweep[i].payback_years <= limit))
            continue;                   /* also skips NaN */
        printf("%d panels | RM %.2f | %.2f years | %.2f%% offset\n",
               sweep[i].n_panels, sweep[i].capex_rm, sweep[i].payback_years, sweep[i].offset_pct);
        found = 1;
    }
    if (!found)
        puts("No configurations meet that payback limit.");
}

/* ---- menu ------------------------------------------------------------- */

void search_menu(const Dataset *d, const SimResult sweep[], int n)
{
    int choice;

    if (!sweep || n <= 0)
    {
        puts("No sweep results. Run the scenario sweep first.");
        return;
    }
    puts("\n--- SEARCH RESULTS ---");
    puts("1. Search by month (estimated generation)");
    puts("2. Search by panel count");
    puts("3. Find best option (same rule as the report)");
    puts("4. Search by maximum payback");
    puts("5. Search hourly record by timestamp");
    if (!read_int("Select: ", &choice))
    {
        puts("Invalid input.");
        return;
    }
    if (choice == 1)
        search_by_month(d, sweep, n);
    else if (choice == 2)
        search_by_panel_count(sweep, n);
    else if (choice == 3)
        search_best(sweep, n);
    else if (choice == 4)
        search_by_payback(sweep, n);
    else if (choice == 5)
        search_by_timestamp(d, sweep, n);
    else
        puts("Unknown search option.");
}
