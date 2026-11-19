/* report.c - Module 5 (M5) report writer
 *
 * write_report(), append_sweep_table() and append_planner_section() write
 * output/report.txt in full and also print each part to the console. When a
 * pause hook is set (set_report_pause), the console waits for the user between
 * parts; declining stops the console output only, the file is still complete.
 *
 * File layout: this file holds the report state and the writing order;
 * report_print.c has the text of each part, coverage.c the dataset coverage
 * check, and report_csv.c the optional CSV export (not called by the main
 * program). Shared declarations are in report_internal.h.
 */
#include "report.h"
#include "report_internal.h"
#include "../Module_4/sweep.h"
#include "../Module_2/Yield.h"

static const char DEFAULT_SOURCE[] = "Not provided - replace with the actual data provider and location";
static const char DEFAULT_IRRADIANCE[] = "Not confirmed - state horizontal or tilted and tilt angle if applicable";

static SystemConfig last_report_config;
static int have_report_config = 0;

ReportFacts report_facts = {-1, 0, 0, 0.0, 0.0, COVERAGE_OK, 0.0};
const char *report_data_source = DEFAULT_SOURCE;
const char *report_source_file = NULL;
const char *report_irradiance_type = DEFAULT_IRRADIANCE;

static int selected_index = -1;     /* scenario the report is about, -1 = use the best-index rule */
static int console_on = 1;          /* cleared when the user declines to continue */

/* ---- dataset facts and metadata --------------------------------------- */

void set_report_dataset(const Dataset *d)
{
    report_facts.warn_kind = COVERAGE_OK;
    report_facts.warn_value = 0.0;
    if (!d || !d->rec || d->n <= 0)
    {
        report_facts.records = -1;
        return;
    }
    report_facts.warn_kind = dataset_coverage_warning(d, &report_facts.warn_value);
    report_facts.records = d->n;
    report_facts.dropped = d->n_dropped;
    report_facts.invalid = count_invalid_rows(d);
    report_facts.step_h = time_step_hours(d);
    report_facts.specific_yield = specific_yield(d);
}

/* An empty or NULL answer resets the field to its "not provided" placeholder,
 * so a newly loaded dataset never inherits the previous one's description.
 * The pointers are stored as given (see report.h). */
void set_report_metadata(const char *source, const char *irradiance_description)
{
    report_source_file = NULL;
    if (source && *source)
        report_data_source = source;
    else
        report_data_source = DEFAULT_SOURCE;
    if (irradiance_description && *irradiance_description)
        report_irradiance_type = irradiance_description;
    else
        report_irradiance_type = DEFAULT_IRRADIANCE;
}

void set_report_source_file(const char *path)
{
    if (path && *path)
        report_source_file = path;
    else
        report_source_file = NULL;
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

    if (!path || !cfg || !res)
        return (0);
    f = fopen(path, "w");
    if (!f)
        return (0);
    last_report_config = *cfg;
    have_report_config = 1;
    console_on = 1;
    print_inputs(f, cfg);
    if (console_on)
        print_inputs(stdout, cfg);

    next_section("2. SELECTED SCENARIO RESULTS");
    print_scenario(f, res);
    if (console_on)
        print_scenario(stdout, res);

    next_section("3. VERDICT AND REASON");
    print_verdict(f, res);
    if (console_on)
        print_verdict(stdout, res);
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
    print_sizing(f, cfg, fs, target, roof, budget, chosen);
    if (console_on)
        print_sizing(stdout, cfg, fs, target, roof, budget, chosen);
    return (fclose(f) == 0);
}

/* Part 4: the scenario sweep table. */
int append_sweep_table(const char *path, const SimResult sweep[], int n)
{
    FILE *f;

    if (!path || !sweep || n <= 0)
        return (0);
    f = fopen(path, "a");
    if (!f)
        return (0);
    next_section("4. SCENARIO SWEEP TABLE");
    print_sweep(f, sweep, n);
    if (console_on)
        print_sweep(stdout, sweep, n);
    return (fclose(f) == 0);
}

/* Compatibility wrapper for callers that only have the original shared
 * prototype. The integrated M5 main program would call export_csvs_with_config(). */
int export_csvs(const char *dir, const Dataset *d, const SimResult sweep[], int n)
{
    if (!have_report_config)
        return (0);
    return (export_csvs_with_config(dir, d, &last_report_config, sweep, n));
}
