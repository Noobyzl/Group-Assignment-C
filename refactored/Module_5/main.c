/* main.c - M5 main program, follows the program flowchart:
 *   START -> 1 data loader -> 2 fault detector -> 3 user input -> bill >= RM 200?
 *   -> 4 energy & sizing -> 5 impact & sweep -> 6 report -> search loop -> END
 *
 * Each flowchart step is one stage function. A stage returns CONTINUE to go on
 * to the next one, or the exit code of the program to stop. The console input
 * helpers are in input.c.
 */
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p)   _mkdir(p)
#else
#include <sys/stat.h>
#define MAKE_DIR(p)   mkdir(p, 0777)
#endif
#include "../common.h"
#include "../Module_2/Yield.h"
#include "../Module_2/Fault.h"
#include "../Module_4/sweep.h"
#include "../Module_4/tnb.h"
#include "report.h"
#include "search.h"
#include "input.h"

#define MAX_SWEEP_RESULTS   5000
#define MAX_PANEL_OPTIONS   100
#define PANELS_PATH         "datasets/panels.csv"
#define DEFAULT_CSV         "datasets/Timeseries_5.700_101.840_E5_10deg_0deg_2019_2019.csv"
#define OUTPUT_DIR          "output"
#define REPORT_PATH         "output/report.txt"
#define CONTINUE            (-1)    /* stage result: go on to the next stage */

#define PVGIS_SOURCE        "PVGIS-ERA5 hourly time series, latitude 5.700, longitude 101.840, year 2019"
#define IRRADIANCE_TEXT     "Tilted plane (10 deg slope, 0 deg azimuth), G(i) in W/m2"

/* Everything the stages share. It is static (zero-filled) so the big arrays stay off the stack. */
typedef struct {
    Dataset ds;
    SystemConfig cfg;
    FaultSummary fault;
    const char *csv_path;       /* dataset to load */
    int user_file;              /* 1 when the dataset path came from the command line */
    int panel_count;
    double yield_per_kwp;       /* specific yield, kWh per kWp per year */
    int target;                 /* panels needed to cover the demand */
    int roof;                   /* panels that fit on the roof */
    int budget;                 /* panels the budget pays for */
    int n;                      /* panels chosen: min of the three, then the sweep size */
} Planner;

static SimResult results[MAX_SWEEP_RESULTS];
static Panel panels[MAX_PANEL_OPTIONS];

/* ---- helpers ---------------------------------------------------------- */

/* double -> int count without undefined behaviour: negative or NaN gives 0, anything
 * above INT_MAX gives INT_MAX. */
static int to_count(double v)
{
    if (!(v > 0.0))
        return (0);
    if (v >= (double)INT_MAX)
        return (INT_MAX);
    return ((int)v);
}

static void show_panel(const Panel *p, int index)
{
    printf("%d. %s | %.0f W | %.3f x %.3f m\n", index + 1, p->name, p->rated_w, p->length_m, p->width_m);
}

/* ---- 1. data loader: panels.csv and the PVGIS hourly records, no prompts ---- */

static int stage_load(Planner *pl)
{
    double cover_value = 0.0;
    int cover_kind;

    pl->panel_count = load_panels(PANELS_PATH, panels, MAX_PANEL_OPTIONS);
    if (pl->panel_count <= 0)
    {
        printf("Could not load the panel catalogue %s.\n", PANELS_PATH);
        return (1);
    }
    if (load_csv(pl->csv_path, &pl->ds) != 0)
    {
        printf("Could not load the PVGIS dataset %s.\n", pl->csv_path);
        return (1);
    }
    printf("Loaded %d panel types and %d hourly records (%d timestamp rows dropped).\n",
           pl->panel_count, pl->ds.n, pl->ds.n_dropped);
    cover_kind = dataset_coverage_warning(&pl->ds, &cover_value);
    print_coverage_warning(stdout, cover_kind, cover_value);
    if (pl->ds.n < MIN_RECORDS)
        printf("WARNING: fewer than %d records; this does not meet the assignment minimum.\n", MIN_RECORDS);
    set_report_metadata(PVGIS_SOURCE, IRRADIANCE_TEXT);
    if (pl->user_file)
        set_report_source_file(pl->csv_path);
    return (CONTINUE);
}

/* ---- 2. fault detector: irradiance vs modelled AC power, energy loss ---- */

static int stage_fault(Planner *pl)
{
    FaultSummary *fs = &pl->fault;

    fault_scan(&pl->ds, fs);
    printf("Fault detector: %d of %d rows rejected (%d unusable, %d implausible irradiance); estimated energy loss %.2f kWh/kWp (%.2f%%).\n",
           fs->faulty_rows, fs->rows_checked, fs->unusable_rows, fs->implausible_rows,
           fs->loss_kwh_per_kwp, fs->loss_pct);
    set_report_dataset(&pl->ds);    /* after fault_scan, so the report counts the rejected rows too */
    pl->yield_per_kwp = specific_yield(&pl->ds);
    if (!(pl->yield_per_kwp > 0.0))
    {
        printf("The dataset has no usable irradiance, so no yield can be calculated.\n");
        return (1);
    }
    return (CONTINUE);
}

/* ---- 3. user input: monthly bill, roof area, budget, panel type ---- */

static int stage_input(Planner *pl)
{
    SystemConfig *cfg = &pl->cfg;
    int selected;
    int i;

    printf("\nEnter your details.\n");
    if (!ask_double("Monthly electricity bill (RM): ", &cfg->monthly_bill_rm, 0.0) ||
        !ask_double("Usable roof area (m2): ", &cfg->area_m2, 0.01) ||
        !ask_double("Budget for the installation (RM): ", &cfg->budget_rm, 0.0))
    {
        printf("\nInput ended. Exiting.\n");
        return (0);
    }
    printf("Available panel types:\n");
    for (i = 0; i < pl->panel_count; ++i)
        show_panel(&panels[i], i);
    if (!ask_int("Choose panel number: ", &selected, 1, pl->panel_count))
    {
        printf("\nInput ended. Exiting.\n");
        return (0);
    }
    cfg->panel = panels[selected - 1];
    return (CONTINUE);
}

/* ---- decision: monthly bill >= RM 200? ---- */

static int stage_eligibility(Planner *pl)
{
    if (pl->cfg.monthly_bill_rm < ELIGIBLE_MIN_BILL_RM)
    {
        printf("\nIneligible: a monthly bill of RM %.2f is below the RM %.0f minimum for this programme. Terminating.\n",
               pl->cfg.monthly_bill_rm, ELIGIBLE_MIN_BILL_RM);
        return (0);
    }
    return (CONTINUE);
}

/* ---- 4. energy & sizing: TNB tiered kWh, panel yield, min(target, roof, budget) ---- */

static int stage_sizing(Planner *pl)
{
    SystemConfig *cfg = &pl->cfg;
    double per_panel_yield;

    cfg->monthly_kwh = tnb_kwh_from_bill(cfg->monthly_bill_rm);
    cfg->annual_kwh = cfg->monthly_kwh * 12.0;
    cfg->tariff_rm_kwh = cfg->monthly_bill_rm / cfg->monthly_kwh;      /* effective average, for the report */
    cfg->panel.price_rm = cfg->panel.rated_w * INSTALL_COST_RM_PER_W;  /* flowchart: constant RM/W cost */
    per_panel_yield = pl->yield_per_kwp * cfg->panel.rated_w / 1000.0; /* kWh per panel per year */

    pl->target = to_count(ceil(cfg->annual_kwh / per_panel_yield));
    pl->roof = panels_that_fit(cfg->area_m2, &cfg->panel);
    pl->budget = to_count(floor(cfg->budget_rm / cfg->panel.price_rm));
    pl->n = pl->target;
    if (pl->roof < pl->n)
        pl->n = pl->roof;
    if (pl->budget < pl->n)
        pl->n = pl->budget;
    printf("\nBill RM %.2f/month = %.1f kWh/month (%.0f kWh/year) using the TNB Domestic General Tariff.\n",
           cfg->monthly_bill_rm, cfg->monthly_kwh, cfg->annual_kwh);
    printf("Panel yield: %.1f kWh per panel per year.\n", per_panel_yield);
    printf("Panels to cover demand: %d | fit on roof: %d | affordable at RM %.2f/W: %d\n",
           pl->target, pl->roof, INSTALL_COST_RM_PER_W, pl->budget);
    if (pl->n < 1)
    {
        if (pl->budget < 1)
            printf("The budget does not cover even one panel.\n");
        else
            printf("The roof is too small for even one panel.\n");
        return (0);
    }
    if (pl->n > MAX_SWEEP_RESULTS)
    {
        printf("WARNING: capped at %d panels.\n", MAX_SWEEP_RESULTS);
        pl->n = MAX_SWEEP_RESULTS;
    }
    printf("Chosen panel count = min(target, roof, budget) = %d\n", pl->n);
    return (CONTINUE);
}

/* ---- 5. impact & sweep: RM 4.00/W, tiered capped savings, RM 3 min charge, payback ---- */

static int stage_sweep(Planner *pl)
{
    pl->n = sweep_panels(&pl->ds, &pl->cfg, results, pl->n);
    if (pl->n < 1)
    {
        printf("The scenario sweep failed; check the inputs.\n");
        return (1);
    }
    tnb_apply_savings(&pl->ds, &pl->cfg, results, pl->n);
    printf("Sweep completed: %d configurations (1 to %d panels).\n", pl->n, pl->n);
    return (CONTINUE);
}

/* ---- 6. generate report.txt and console summary ---- */

static int stage_report(Planner *pl)
{
    int best = pl->n - 1;           /* the sized system is the last (largest allowed) row */

    set_report_selected(best);
    MAKE_DIR(OUTPUT_DIR);
    set_report_pause(wait_for_section);
    if (!write_report(REPORT_PATH, &pl->cfg, &results[best]))
        printf("Could not write output/report.txt. Ensure output/ exists and is writable.\n");
    else
    {
        if (!append_sweep_table(REPORT_PATH, results, pl->n))
            printf("Could not append the sweep table to output/report.txt.\n");
        if (!append_planner_section(REPORT_PATH, &pl->cfg, &pl->fault, pl->target, pl->roof, pl->budget, pl->n))
            printf("Could not append the sizing section to output/report.txt.\n");
        printf("\nReport saved: output/report.txt\n");
    }
    if (report_stopped())           /* user answered n to "Continue to the next part?" */
    {
        printf("Stopped by user. The full report is saved in output/report.txt. Terminating.\n");
        return (0);
    }
    return (CONTINUE);
}

/* ---- search hourly records? yes -> filter by month / timestamp / payback ---- */

static int stage_search(Planner *pl)
{
    int again;

    while (1)
    {
        if (!ask_yes_no("\nSearch hourly records? (y/n): ", &again))
        {
            printf("\nInput ended.\n");
            break;
        }
        if (!again)
            break;
        search_menu(&pl->ds, results, pl->n);
    }
    printf("Done. Thank you.\n");
    return (0);
}

/* ---- main ------------------------------------------------------------- */

int main(int argc, char **argv)
{
    static Planner pl;
    static int (*const stages[])(Planner *) = {
        stage_load, stage_fault, stage_input, stage_eligibility,
        stage_sizing, stage_sweep, stage_report, stage_search
    };
    int code = CONTINUE;
    size_t i;

    pl.csv_path = DEFAULT_CSV;
    if (argc > 1)
    {
        pl.csv_path = argv[1];      /* optional dataset override for testing */
        pl.user_file = 1;
    }
    printf("============================================\n");
    printf("   RURAL SOLAR MICROGRID PLANNER\n");
    printf("============================================\n");
    for (i = 0; code == CONTINUE && i < sizeof stages / sizeof stages[0]; ++i)
        code = stages[i](&pl);
    free_dataset(&pl.ds);
    return (code);
}
