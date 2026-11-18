#ifndef COMMON_H
#define COMMON_H

#define CO2_KG_PER_KWH     0.758     /* grid factor from the brief */
#define MIN_RECORDS        50
#define PERF_RATIO         0.80      /* assumption: cite or justify */
#define USABLE_FRACTION    0.70      /* assumption: share of site area that can hold panels */
#define MAX_PANELS_CATALOGUE 20

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>

typedef struct {
    int    year, month, day, hour;
    double irradiance;       /* W/m2, say whether horizontal or tilted */
    double temp_c;           /* deg C */
    int    valid;            /* 1 = numbers usable, 0 = blank/NaN/non-numeric */
    int    minute;           /* 0..59; kept last so older positional initialisers still compile */
} HourRecord;

typedef struct {
    HourRecord *rec;
    int         n;           /* rows kept (valid or not) */
    int         n_dropped;   /* rows dropped: unparseable timestamp */
} Dataset;

typedef struct {
    char   name[32];
    double rated_w, length_m, width_m, price_rm;
} Panel;

typedef struct {
    double area_m2;          /* site area the user can build on */
    double annual_kwh;       /* yearly energy use */
    double tariff_rm_kwh;
    Panel  panel;            /* chosen panel */
} SystemConfig;

typedef enum { VERDICT_DONT, VERDICT_SMALLER, VERDICT_BUILD } Verdict;

typedef struct {
    int     n_panels;
    double  kwp, annual_gen_kwh, offset_pct;
    double  capex_rm, saving_rm_per_year, payback_years;
    double  co2_avoided_kg;
    Verdict verdict;
} SimResult;

/* M1 loader.h (load_csv etc. in src/loader.c; load_panels in src/panels.c) */
int  load_csv(const char *path, Dataset *ds);               /* returns 0 ok, -1 on any error */
int  load_panels(const char *path, Panel out[], int max);   /* returns count loaded */
void free_dataset(Dataset *ds);

/* M2 yield.h */
double panel_area_m2(const Panel *p);
int    panels_that_fit(double site_area_m2, const Panel *p);       /* floor(usable area / panel area) */
double array_kwp(int n_panels, const Panel *p);
double annual_generation_kwh(const Dataset *d, double kwp);        /* skips valid == 0 rows */
double specific_yield(const Dataset *d);                           /* kWh per kWp per year */

/* M3 decision.h */
double  offset_pct(double gen_kwh, double use_kwh);                /* capped at 100 */
double  payback_years(double capex_rm, double saving_rm_per_year);
Verdict decide(double offset_pct, double payback_years);
const char *verdict_text(Verdict v);
const char *decide_reason(double offset_pct, double payback_years);  /* why decide() gave its verdict */

/* M4 impact.h / sweep.h */
double co2_avoided_kg(double kwh);
double saving_rm(double gen_kwh, double use_kwh, double tariff);   /* capped at use */
int    sweep_panels(const Dataset *d, const SystemConfig *cfg, SimResult out[], int max);  /* loops 1..max panels */
int    best_index(const SimResult out[], int n);

/* M5 report.h / search.h */
int  write_report(const char *path, const SystemConfig *cfg, const SimResult *res);
int  export_csvs(const char *dir, const Dataset *d, const SimResult sweep[], int n);
void search_menu(const Dataset *d, const SimResult sweep[], int n);

#endif