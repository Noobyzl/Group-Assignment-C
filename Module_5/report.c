/* report.c - Module 5 (M5) report writer
 *
 * write_report(), append_sweep_table() and append_planner_section() write
 * output/report.txt in full and also print each part to the console. When a
 * pause hook is set (set_report_pause), the console waits for the user between
 * parts; declining stops the console output only, the file is still complete.
 * export_csvs_with_config() is kept for callers that want CSV files, but the
 * main program no longer calls it.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "report.h"
#include "sweep.h"
#include "Yield.h"
#include "tnb.h"

#define INSTALL_COST_RM_PER_W_REPORT   INSTALL_COST_RM_PER_W

static const char DEFAULT_SOURCE[] = "Not provided - replace with the actual data provider and location";
static const char DEFAULT_IRRADIANCE[] = "Not confirmed - state horizontal or tilted and tilt angle if applicable";

static SystemConfig last_report_config;
static int have_report_config = 0;
static char report_data_source[256] = "Not provided - replace with the actual data provider and location";
static char report_irradiance_type[256] = "Not confirmed - state horizontal or tilted and tilt angle if applicable";

/* Dataset facts that M2 already computes, remembered so the report can state them. */
static int ds_records = -1;
static int ds_dropped = 0;
static int ds_invalid = 0;
static double ds_step_h = 0.0;
static double ds_specific_yield = 0.0;
static char ds_warning[512] = "";

static int selected_index = -1;     /* scenario the report is about, -1 = use the best-index rule */
static int console_on = 1;          /* cleared when the user declines to continue */

/* Print to the file, and to the console while console_on is set. */
#define REPORT_LINE(...) \
    do \
    { \
        fprintf(f, __VA_ARGS__); \
        if (console_on) \
            fprintf(stdout, __VA_ARGS__); \
    } while (0)

/* ---- dataset coverage ------------------------------------------------- */

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

/* M2's "annual" generation is simply the sum over the rows in the file. That
 * is only a yearly figure when the file covers about one year without holes,
 * so say so when it does not. Returns buf, or NULL when the coverage looks fine. */
const char *dataset_coverage_warning(const Dataset *d, char *buf, size_t size)
{
    double step;
    double covered_h;
    double span_h;

    if (!d || !d->rec || d->n < 2 || !buf || size == 0)
        return (NULL);
    step = time_step_hours(d);
    covered_h = d->n * step;
    span_h = (minutes_of(&d->rec[d->n - 1]) - minutes_of(&d->rec[0])) / 60.0 + step;
    buf[0] = '\0';
    if (covered_h < 0.9 * 8760.0 || covered_h > 1.1 * 8784.0)
        snprintf(buf, size, "the dataset covers about %.0f days of data, not one year, so generation, offset, savings and payback are totals over the file, not annual figures.", covered_h / 24.0);
    else if (span_h > 0.0 && covered_h < 0.97 * span_h)
        snprintf(buf, size, "about %.0f%% of the hours between the first and last timestamp are missing from the file, so generation is under-counted.", 100.0 * (1.0 - covered_h / span_h));
    if (buf[0])
        return (buf);
    return (NULL);
}

void set_report_dataset(const Dataset *d)
{
    ds_warning[0] = '\0';
    if (!d || !d->rec || d->n <= 0)
    {
        ds_records = -1;
        return;
    }
    dataset_coverage_warning(d, ds_warning, sizeof ds_warning);
    ds_records = d->n;
    ds_dropped = d->n_dropped;
    ds_invalid = count_invalid_rows(d);
    ds_step_h = time_step_hours(d);
    ds_specific_yield = specific_yield(d);
}

/* An empty or NULL answer resets the field to its "not provided" placeholder,
 * so a newly loaded dataset never inherits the previous one's description. */
void set_report_metadata(const char *source, const char *irradiance_description)
{
    if (source && *source)
        snprintf(report_data_source, sizeof report_data_source, "%s", source);
    else
        snprintf(report_data_source, sizeof report_data_source, "%s", DEFAULT_SOURCE);
    if (irradiance_description && *irradiance_description)
        snprintf(report_irradiance_type, sizeof report_irradiance_type, "%s", irradiance_description);
    else
        snprintf(report_irradiance_type, sizeof report_irradiance_type, "%s", DEFAULT_IRRADIANCE);
}

/* ---- text helpers ----------------------------------------------------- */

/* The "never pays back" sentinel (-1), NaN and inf are shown as words, not numbers. */
static const char *payback_text(double years, char *buf, size_t size)
{
    if (years < 0.0 || !isfinite(years))
        snprintf(buf, size, "never pays back");
    else
        snprintf(buf, size, "%.2f years", years);
    return (buf);
}

static const char *short_verdict(Verdict v)
{
    switch (v)
    {
    case VERDICT_BUILD:
        return ("BUILD");
    case VERDICT_SMALLER:
        return ("BUILD SMALLER");
    default:
        return ("DON'T BUILD");
    }
}

/* ---- selected scenario ------------------------------------------------ */

void set_report_selected(int index)
{
    selected_index = index;
}

/* Index of the scenario the report is about: the pinned one, else M4's
 * best_index(), or when every option is rejected (best_index == -1) the
 * option with the highest offset, so a DON'T BUILD answer still gets a
 * report. Returns -1 only for an empty sweep. */
int report_best_index(const SimResult sweep[], int n)
{
    int i;
    int best;

    if (!sweep || n <= 0)
        return (-1);
    if (selected_index >= 0 && selected_index < n)
        return (selected_index);
    best = best_index(sweep, n);
    if (best >= 0)
        return (best);
    best = 0;
    for (i = 1; i < n; ++i)
        if (sweep[i].offset_pct > sweep[best].offset_pct)
            best = i;
    return (best);
}

/* ---- console pacing --------------------------------------------------- */

/* Optional hook: when set, the console asks the user before each later report
 * part. The file is always written in full; only the console output is paced. */
static int (*pause_hook)(const char *next_section) = NULL;

void set_report_pause(int (*hook)(const char *next_section))
{
    pause_hook = hook;
}

int report_stopped(void)
{
    return (!console_on);
}

static void next_section(const char *name)
{
    if (pause_hook && console_on && !pause_hook(name))
        console_on = 0;
}

/* ---- report parts ----------------------------------------------------- */

/* Parts 1 to 3: data and inputs, selected scenario, verdict. */
int write_report(const char *path, const SystemConfig *cfg, const SimResult *res)
{
    FILE *f;
    char pbbuf[40];

    if (!path || !cfg || !res)
        return (0);
    f = fopen(path, "w");
    if (!f)
        return (0);
    last_report_config = *cfg;
    have_report_config = 1;
    console_on = 1;
    REPORT_LINE("SOLAR BUILD DECISION PLANNER - SIMULATION REPORT\n");
    REPORT_LINE("================================================\n\n");

    REPORT_LINE("1. DATA AND INPUTS\n------------------\n");
    REPORT_LINE("Available site area: %.2f m2\n", cfg->area_m2);
    REPORT_LINE("Annual electricity use: %.2f kWh\n", cfg->annual_kwh);
    if (cfg->monthly_bill_rm > 0.0)
        REPORT_LINE("Electricity tariff: TNB Domestic General Tariff (average RM %.4f/kWh at this use)\n", cfg->tariff_rm_kwh);
    else
        REPORT_LINE("Electricity tariff: RM %.4f/kWh\n", cfg->tariff_rm_kwh);
    REPORT_LINE("Panel: %s\nRated power: %.1f W\nPanel dimensions: %.3f m x %.3f m\nPanel price: RM %.2f\n",
                cfg->panel.name, cfg->panel.rated_w, cfg->panel.length_m, cfg->panel.width_m, cfg->panel.price_rm);
    if (ds_records >= 0)
    {
        REPORT_LINE("Dataset: %d records kept, %d rows dropped (bad timestamp), %d rows skipped (unusable values)\n",
                    ds_records, ds_dropped, ds_invalid);
        REPORT_LINE("Time step: %.2f h (from the file's timestamps); specific yield: %.1f kWh/kWp/year\n",
                    ds_step_h, ds_specific_yield);
        if (ds_warning[0])
            REPORT_LINE("WARNING: %s\n", ds_warning);
    }
    REPORT_LINE("Data source: %s\n", report_data_source);
    REPORT_LINE("Irradiance description: %s\n\n", report_irradiance_type);

    next_section("2. SELECTED SCENARIO RESULTS");
    REPORT_LINE("2. SELECTED SCENARIO RESULTS\n----------------------------\n");
    REPORT_LINE("Panel count: %d\nSystem capacity: %.3f kWp\nAnnual generation: %.2f kWh\nEnergy offset: %.2f%%\n",
                res->n_panels, res->kwp, res->annual_gen_kwh, res->offset_pct);
    REPORT_LINE("Initial cost: RM %.2f\nAnnual bill savings: RM %.2f\nSimple payback: %s\nCO2 avoided: %.2f kg/year\n\n",
                res->capex_rm, res->saving_rm_per_year,
                payback_text(res->payback_years, pbbuf, sizeof pbbuf), res->co2_avoided_kg);

    next_section("3. VERDICT AND REASON");
    REPORT_LINE("3. VERDICT AND REASON\n---------------------\n");
    REPORT_LINE("Recommendation: %s\n", verdict_text(res->verdict));
    REPORT_LINE("Reason: %s\n", decide_reason(res->offset_pct, res->payback_years));
    REPORT_LINE("This is a screening estimate, not an engineering quotation. Confirm roof condition, shading, permits, wiring, and current installed cost before making a real purchase.\n");
    if (fclose(f) != 0)
        return (0);
    return (1);
}

/* Part 5: fault detection and sizing. */
int append_planner_section(const char *path, const SystemConfig *cfg, const FaultSummary *fs,
                           int target, int roof, int budget, int chosen)
{
    FILE *f;

    if (!path || !cfg || !fs)
        return (0);
    f = fopen(path, "a");
    if (!f)
        return (0);
    next_section("5. FAULT DETECTION AND SIZING");
    REPORT_LINE("\n5. FAULT DETECTION AND SIZING\n-----------------------------\n");
    REPORT_LINE("Fault detector: %d of %d rows rejected (%d unusable, %d implausible irradiance); estimated energy loss %.2f kWh/kWp (%.2f%%).\n",
                fs->faulty_rows, fs->rows_checked, fs->unusable_rows, fs->implausible_rows,
                fs->loss_kwh_per_kwp, fs->loss_pct);
    REPORT_LINE("AC power is modelled from irradiance (no AC column in the dataset): kWp x G/1000 x %.2f.\n", PERF_RATIO);
    REPORT_LINE("Monthly bill: RM %.2f -> %.1f kWh/month (inverse of the TNB tariff); budget: RM %.2f.\n",
                cfg->monthly_bill_rm, cfg->monthly_kwh, cfg->budget_rm);
    REPORT_LINE("Panels needed to cover the demand: %d | fit on roof: %d | affordable at RM %.2f/W: %d\n",
                target, roof, INSTALL_COST_RM_PER_W_REPORT, budget);
    {
        int least = target;

        if (roof < least)
            least = roof;
        if (budget < least)
            least = budget;
        if (chosen < least)
            REPORT_LINE("Chosen panel count = %d (limited to %d panels: the program caps a sweep at that size; the minimum of the three is %d)\n",
                        chosen, chosen, least);
        else
            REPORT_LINE("Chosen panel count = min of the three = %d\n", chosen);
    }
    return (fclose(f) == 0);
}

/* Part 4: the scenario sweep table. */
int append_sweep_table(const char *path, const SimResult sweep[], int n)
{
    FILE *f;
    int i;
    char pbbuf[40];

    if (!path || !sweep || n <= 0)
        return (0);
    f = fopen(path, "a");
    if (!f)
        return (0);
    next_section("4. SCENARIO SWEEP TABLE");
    REPORT_LINE("\n4. SCENARIO SWEEP TABLE\n-----------------------\n");
    REPORT_LINE("Panels | kWp | Generation kWh | Offset %% | Capex RM | Savings RM/year | Payback years | CO2 kg | Verdict\n");
    for (i = 0; i < n; ++i)
        REPORT_LINE("%d | %.3f | %.2f | %.2f | %.2f | %.2f | %s | %.2f | %s\n",
                    sweep[i].n_panels, sweep[i].kwp, sweep[i].annual_gen_kwh, sweep[i].offset_pct,
                    sweep[i].capex_rm, sweep[i].saving_rm_per_year,
                    payback_text(sweep[i].payback_years, pbbuf, sizeof pbbuf),
                    sweep[i].co2_avoided_kg, short_verdict(sweep[i].verdict));
    return (fclose(f) == 0);
}

/* ---- CSV export (optional, not called by main) ------------------------ */

int export_csvs_with_config(const char *dir, const Dataset *d, const SystemConfig *cfg,
                            const SimResult sweep[], int n)
{
    char path[512];
    FILE *f;
    int i;
    int m;
    int h;
    double monthly_gen[12];
    double dt;
    double irr;
    double mean;
    int counts[13] = {0};
    double hour_irr[24] = {0};
    int hour_count[24] = {0};
    const SimResult *best = NULL;
    int best_i;

    if (!dir || !d || !cfg || !sweep || n <= 0)
        return (0);
    best_i = report_best_index(sweep, n);
    if (best_i >= 0 && best_i < n)
        best = &sweep[best_i];
    if (!best)
        return (0);

    snprintf(path, sizeof(path), "%s/sweep.csv", dir);
    f = fopen(path, "w");
    if (!f)
        return (0);
    fprintf(f, "panels,kwp,generation_kwh,offset_pct,capex_rm,saving_rm,payback_years,co2_kg,verdict\n");
    for (i = 0; i < n; ++i)
        fprintf(f, "%d,%.6f,%.6f,%.6f,%.2f,%.2f,%.6f,%.6f,%d\n", sweep[i].n_panels, sweep[i].kwp,
                sweep[i].annual_gen_kwh, sweep[i].offset_pct, sweep[i].capex_rm,
                sweep[i].saving_rm_per_year, sweep[i].payback_years, sweep[i].co2_avoided_kg,
                (int)sweep[i].verdict);
    fclose(f);

    snprintf(path, sizeof(path), "%s/results.csv", dir);
    f = fopen(path, "w");
    if (!f)
        return (0);
    fprintf(f, "panels,kwp,generation_kwh,offset_pct,capex_rm,saving_rm,payback_years,co2_kg,verdict,annual_use_kwh,tariff_rm_kwh,perf_ratio,panel_price_rm\n");
    fprintf(f, "%d,%.6f,%.6f,%.6f,%.2f,%.2f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%.6f\n", best->n_panels, best->kwp,
            best->annual_gen_kwh, best->offset_pct, best->capex_rm, best->saving_rm_per_year,
            best->payback_years, best->co2_avoided_kg, (int)best->verdict, cfg->annual_kwh,
            cfg->tariff_rm_kwh, PERF_RATIO, cfg->panel.price_rm);
    fclose(f);

    /* M2 owns the time-step and generation maths; reuse it so the CSVs agree with the sweep. */
    dt = time_step_hours(d);
    monthly_generation_kwh(d, best->kwp, monthly_gen);
    for (i = 0; i < d->n; ++i)
    {
        const HourRecord *r = &d->rec[i];

        if (!r->valid || r->month < 1 || r->month > 12 || r->hour < 0 || r->hour > 23)
            continue;
        counts[r->month]++;
        hour_irr[r->hour] += r->irradiance;
        hour_count[r->hour]++;
    }
    snprintf(path, sizeof(path), "%s/monthly.csv", dir);
    f = fopen(path, "w");
    if (!f)
        return (0);
    fprintf(f, "month,valid_records,irradiation_kwh_m2,generation_kwh,estimated_usage_kwh\n");
    for (m = 1; m <= 12; ++m)
    {
        if (best->kwp > 0.0 && PERF_RATIO > 0.0)
            irr = monthly_gen[m - 1] / (best->kwp * PERF_RATIO);
        else
            irr = 0.0;
        fprintf(f, "%d,%d,%.8f,%.6f,%.6f\n", m, counts[m], irr, monthly_gen[m - 1], cfg->annual_kwh / 12.0);
    }
    fclose(f);

    snprintf(path, sizeof(path), "%s/daily_profile.csv", dir);
    f = fopen(path, "w");
    if (!f)
        return (0);
    fprintf(f, "hour,mean_irradiance_w_m2,mean_generation_kwh_per_record,record_count\n");
    for (h = 0; h < 24; ++h)
    {
        if (hour_count[h] > 0)
            mean = hour_irr[h] / hour_count[h];
        else
            mean = 0.0;
        fprintf(f, "%d,%.8f,%.8f,%d\n", h, mean, best->kwp * mean / 1000.0 * PERF_RATIO * dt, hour_count[h]);
    }
    fclose(f);
    return (1);
}

/* Compatibility wrapper for callers that only have the original shared
 * prototype. The integrated M5 main program would call export_csvs_with_config(). */
int export_csvs(const char *dir, const Dataset *d, const SimResult sweep[], int n)
{
    if (!have_report_config)
        return (0);
    return (export_csvs_with_config(dir, d, &last_report_config, sweep, n));
}
