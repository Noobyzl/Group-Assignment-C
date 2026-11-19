/* report_csv.c - Module 5 (M5) optional CSV export (split out of report.c).
 * export_csvs_with_config() is kept for callers that want CSV files; the main
 * program does not call it.
 */
#include "report.h"
#include "../Module_2/Yield.h"

/* Build "<dir>/<name>" into path. This is string building, not output, so it
 * uses bounded strlen/memcpy. Returns 0 if it does not fit. */
static int make_path(char *path, size_t size, const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);

    if (dir_len + 1 + name_len + 1 > size)
        return (0);
    memcpy(path, dir, dir_len);
    path[dir_len] = '/';
    memcpy(path + dir_len + 1, name, name_len + 1);
    return (1);
}

static FILE *open_csv(const char *dir, const char *name)
{
    char path[512];

    if (!make_path(path, sizeof path, dir, name))
        return (NULL);
    return (fopen(path, "w"));
}

static int write_sweep_csv(const char *dir, const SimResult sweep[], int n)
{
    FILE *f = open_csv(dir, "sweep.csv");
    int i;

    if (!f)
        return (0);
    fprintf(f, "panels,kwp,generation_kwh,offset_pct,capex_rm,saving_rm,payback_years,co2_kg,verdict\n");
    for (i = 0; i < n; ++i)
        fprintf(f, "%d,%.6f,%.6f,%.6f,%.2f,%.2f,%.6f,%.6f,%d\n", sweep[i].n_panels, sweep[i].kwp,
                sweep[i].annual_gen_kwh, sweep[i].offset_pct, sweep[i].capex_rm,
                sweep[i].saving_rm_per_year, sweep[i].payback_years, sweep[i].co2_avoided_kg,
                (int)sweep[i].verdict);
    fclose(f);
    return (1);
}

static int write_results_csv(const char *dir, const SystemConfig *cfg, const SimResult *best)
{
    FILE *f = open_csv(dir, "results.csv");

    if (!f)
        return (0);
    fprintf(f, "panels,kwp,generation_kwh,offset_pct,capex_rm,saving_rm,payback_years,co2_kg,verdict,annual_use_kwh,tariff_rm_kwh,perf_ratio,panel_price_rm\n");
    fprintf(f, "%d,%.6f,%.6f,%.6f,%.2f,%.2f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%.6f\n", best->n_panels, best->kwp,
            best->annual_gen_kwh, best->offset_pct, best->capex_rm, best->saving_rm_per_year,
            best->payback_years, best->co2_avoided_kg, (int)best->verdict, cfg->annual_kwh,
            cfg->tariff_rm_kwh, PERF_RATIO, cfg->panel.price_rm);
    fclose(f);
    return (1);
}

static int write_monthly_csv(const char *dir, const Dataset *d, const SystemConfig *cfg, const SimResult *best)
{
    FILE *f;
    double monthly_gen[12];
    int counts[13] = {0};
    double irr;
    int i;
    int m;

    /* M2 owns the generation maths; reuse it so the CSVs agree with the sweep. */
    monthly_generation_kwh(d, best->kwp, monthly_gen);
    for (i = 0; i < d->n; ++i)
    {
        const HourRecord *r = &d->rec[i];

        if (!r->valid || r->month < 1 || r->month > 12 || r->hour < 0 || r->hour > 23)
            continue;
        counts[r->month]++;
    }
    f = open_csv(dir, "monthly.csv");
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
    return (1);
}

static int write_daily_csv(const char *dir, const Dataset *d, const SimResult *best)
{
    FILE *f;
    double hour_irr[24] = {0};
    int hour_count[24] = {0};
    double dt = time_step_hours(d);
    double mean;
    int i;
    int h;

    for (i = 0; i < d->n; ++i)
    {
        const HourRecord *r = &d->rec[i];

        if (!r->valid || r->month < 1 || r->month > 12 || r->hour < 0 || r->hour > 23)
            continue;
        hour_irr[r->hour] += r->irradiance;
        hour_count[r->hour]++;
    }
    f = open_csv(dir, "daily_profile.csv");
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

int export_csvs_with_config(const char *dir, const Dataset *d, const SystemConfig *cfg,
                            const SimResult sweep[], int n)
{
    const SimResult *best = NULL;
    int best_i;

    if (!dir || !d || !cfg || !sweep || n <= 0)
        return (0);
    best_i = report_best_index(sweep, n);
    if (best_i >= 0 && best_i < n)
        best = &sweep[best_i];
    if (!best)
        return (0);
    return (write_sweep_csv(dir, sweep, n) && write_results_csv(dir, cfg, best) &&
            write_monthly_csv(dir, d, cfg, best) && write_daily_csv(dir, d, best));
}
