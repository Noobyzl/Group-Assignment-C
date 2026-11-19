/* main.c - M5 main program, follows the program flowchart:
 *   START -> 1 data loader -> 2 fault detector -> 3 user input -> bill >= RM 200?
 *   -> 4 energy & sizing -> 5 impact & sweep -> 6 report -> search loop -> END
 */
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p)   _mkdir(p)
#else
#include <sys/stat.h>
#define MAKE_DIR(p)   mkdir(p, 0777)
#endif
#include "../common.h"
#include "Yield.h"
#include "Fault.h"
#include "sweep.h"
#include "tnb.h"
#include "report.h"
#include "search.h"

#define MAX_SWEEP_RESULTS   5000
#define MAX_PANEL_OPTIONS   100
#define PANELS_PATH         "datasets/panels.csv"
#define DEFAULT_CSV         "datasets/Timeseries_5.700_101.840_E5_10deg_0deg_2019_2019.csv"
#define OUTPUT_DIR          "output"
#define REPORT_PATH         "output/report.txt"

static int input_eof = 0;           /* set when stdin ends, so every prompt loop can stop */

/* ---- input ------------------------------------------------------------ */

/* Prompt and read one line. An over-long line is discarded so the rest of it
 * cannot answer later prompts. Returns 0 when input ends. */
static int read_line(const char *prompt, char *out, size_t size)
{
    int c;

    printf("%s", prompt);
    if (!fgets(out, (int)size, stdin))
    {
        input_eof = 1;
        return (0);
    }
    if (!strchr(out, '\n'))
    {
        c = getchar();
        if (c != '\n' && c != EOF)
        {
            /* over-long line: discard all of it and answer with an empty string, so the
             * prompt rejects it instead of accepting the first part */
            while (c != '\n' && c != EOF)
                c = getchar();
            out[0] = '\0';
        }
    }
    out[strcspn(out, "\r\n")] = '\0';
    return (1);
}

/* Asks until a valid number >= min_value is typed. Returns 0 only when input ends. */
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

static int ask_double(const char *prompt, double *value, double min_value)
{
    char line[128];
    char *end;
    double v;

    while (1)
    {
        if (!read_line(prompt, line, sizeof line))
            return (0);
        errno = 0;
        v = strtod(line, &end);
        while (*end == ' ' || *end == '\t')
            ++end;
        /* reject nan/inf (isfinite) and hex floats such as 0x1p-1 */
        if (line == end || *end || errno == ERANGE || !isfinite(v) || strpbrk(line, "xX") || v < min_value)
        {
            printf("Invalid input: enter a number of at least %g.\n", min_value);
            continue;
        }
        *value = v;
        return (1);
    }
}

static int ask_int(const char *prompt, int *value, int min_value, int max_value)
{
    char line[128];
    char *end;
    long v;

    while (1)
    {
        if (!read_line(prompt, line, sizeof line))
            return (0);
        errno = 0;
        v = strtol(line, &end, 10);
        while (*end == ' ' || *end == '\t')
            ++end;
        if (line == end || *end || errno == ERANGE || v < min_value || v > max_value)
        {
            printf("Invalid input: enter a whole number from %d to %d.\n", min_value, max_value);
            continue;
        }
        *value = (int)v;
        return (1);
    }
}

static int ask_yes_no(const char *prompt, int *yes)
{
    char line[64];

    while (1)
    {
        if (!read_line(prompt, line, sizeof line))
            return (0);
        if (line[0] == 'y' || line[0] == 'Y')
        {
            *yes = 1;
            return (1);
        }
        if (line[0] == 'n' || line[0] == 'N')
        {
            *yes = 0;
            return (1);
        }
        puts("Please answer y or n.");
    }
}

/* ---- report pacing ---------------------------------------------------- */

/* After each report part, ask whether to continue to the next one. */
static int wait_for_section(const char *next)
{
    int yes;

    (void)next;
    if (input_eof)
        return (0);
    if (!ask_yes_no("\nContinue to the next part? (y/n): ", &yes))
        return (0);
    puts("");
    return (yes);
}

static void show_panel(const Panel *p, int index)
{
    printf("%d. %s | %.0f W | %.3f x %.3f m\n", index + 1, p->name, p->rated_w, p->length_m, p->width_m);
}

/* ---- main ------------------------------------------------------------- */

int main(int argc, char **argv)
{
    Dataset ds = {0};
    SystemConfig cfg = {0};
    FaultSummary fault;
    static SimResult results[MAX_SWEEP_RESULTS];
    static Panel panels[MAX_PANEL_OPTIONS];
    const char *csv_path = DEFAULT_CSV;
    char source[256];
    char cov[512];
    int panel_count;
    int selected;
    int target;
    int roof;
    int budget;
    int n;
    int i;
    int again;
    int best;
    double per_panel_yield;
    double sy;

    if (argc > 1)
        csv_path = argv[1];         /* optional dataset override for testing */
    puts("============================================");
    puts("   RURAL SOLAR MICROGRID PLANNER");
    puts("============================================");

    /* ---- 1. Data loader: panels.csv and the PVGIS hourly records, no prompts ---- */
    panel_count = load_panels(PANELS_PATH, panels, MAX_PANEL_OPTIONS);
    if (panel_count <= 0)
    {
        printf("Could not load the panel catalogue %s.\n", PANELS_PATH);
        return (1);
    }
    if (load_csv(csv_path, &ds) != 0)
    {
        printf("Could not load the PVGIS dataset %s.\n", csv_path);
        return (1);
    }
    printf("Loaded %d panel types and %d hourly records (%d timestamp rows dropped).\n",
           panel_count, ds.n, ds.n_dropped);
    if (dataset_coverage_warning(&ds, cov, sizeof cov))
        printf("WARNING: %s\n", cov);
    if (ds.n < MIN_RECORDS)
        printf("WARNING: fewer than %d records; this does not meet the assignment minimum.\n", MIN_RECORDS);
    if (argc > 1)
        snprintf(source, sizeof source, "User-supplied dataset %s", csv_path);
    else
        snprintf(source, sizeof source, "PVGIS-ERA5 hourly time series, latitude 5.700, longitude 101.840, year 2019");
    set_report_metadata(source, "Tilted plane (10 deg slope, 0 deg azimuth), G(i) in W/m2");

    /* ---- 2. Fault detector: irradiance vs modelled AC power, energy loss ---- */
    fault_scan(&ds, &fault);
    printf("Fault detector: %d of %d rows rejected (%d unusable, %d implausible irradiance); estimated energy loss %.2f kWh/kWp (%.2f%%).\n",
           fault.faulty_rows, fault.rows_checked, fault.unusable_rows, fault.implausible_rows,
           fault.loss_kwh_per_kwp, fault.loss_pct);
    set_report_dataset(&ds);        /* after fault_scan, so the report counts the rejected rows too */
    sy = specific_yield(&ds);
    if (!(sy > 0.0))
    {
        puts("The dataset has no usable irradiance, so no yield can be calculated.");
        free_dataset(&ds);
        return (1);
    }

    /* ---- 3. User input: monthly bill, roof area, budget, panel type ---- */
    puts("\nEnter your details.");
    if (!ask_double("Monthly electricity bill (RM): ", &cfg.monthly_bill_rm, 0.0) ||
        !ask_double("Usable roof area (m2): ", &cfg.area_m2, 0.01) ||
        !ask_double("Budget for the installation (RM): ", &cfg.budget_rm, 0.0))
    {
        puts("\nInput ended. Exiting.");
        free_dataset(&ds);
        return (0);
    }
    puts("Available panel types:");
    for (i = 0; i < panel_count; ++i)
        show_panel(&panels[i], i);
    if (!ask_int("Choose panel number: ", &selected, 1, panel_count))
    {
        puts("\nInput ended. Exiting.");
        free_dataset(&ds);
        return (0);
    }
    cfg.panel = panels[selected - 1];

    /* ---- Decision: monthly bill >= RM 200? ---- */
    if (cfg.monthly_bill_rm < ELIGIBLE_MIN_BILL_RM)
    {
        printf("\nIneligible: a monthly bill of RM %.2f is below the RM %.0f minimum for this programme. Terminating.\n",
               cfg.monthly_bill_rm, ELIGIBLE_MIN_BILL_RM);
        free_dataset(&ds);
        return (0);
    }

    /* ---- 4. Energy & sizing: TNB tiered kWh, panel yield, min(target, roof, budget) ---- */
    cfg.monthly_kwh = tnb_kwh_from_bill(cfg.monthly_bill_rm);
    cfg.annual_kwh = cfg.monthly_kwh * 12.0;
    cfg.tariff_rm_kwh = cfg.monthly_bill_rm / cfg.monthly_kwh;      /* effective average, for the report */
    cfg.panel.price_rm = cfg.panel.rated_w * INSTALL_COST_RM_PER_W; /* flowchart: constant RM/W cost */
    per_panel_yield = sy * cfg.panel.rated_w / 1000.0;              /* kWh per panel per year */

    target = to_count(ceil(cfg.annual_kwh / per_panel_yield));
    roof = panels_that_fit(cfg.area_m2, &cfg.panel);
    budget = to_count(floor(cfg.budget_rm / cfg.panel.price_rm));
    if (target < roof)
        n = target;
    else
        n = roof;
    if (budget < n)
        n = budget;
    printf("\nBill RM %.2f/month = %.1f kWh/month (%.0f kWh/year) using the TNB Domestic General Tariff.\n",
           cfg.monthly_bill_rm, cfg.monthly_kwh, cfg.annual_kwh);
    printf("Panel yield: %.1f kWh per panel per year.\n", per_panel_yield);
    printf("Panels to cover demand: %d | fit on roof: %d | affordable at RM %.2f/W: %d\n",
           target, roof, INSTALL_COST_RM_PER_W, budget);
    if (n < 1)
    {
        if (budget < 1)
            puts("The budget does not cover even one panel.");
        else
            puts("The roof is too small for even one panel.");
        free_dataset(&ds);
        return (0);
    }
    if (n > MAX_SWEEP_RESULTS)
    {
        printf("WARNING: capped at %d panels.\n", MAX_SWEEP_RESULTS);
        n = MAX_SWEEP_RESULTS;
    }
    printf("Chosen panel count = min(target, roof, budget) = %d\n", n);

    /* ---- 5. Impact & sweep: RM 4.00/W, tiered capped savings, RM 3 min charge, payback ---- */
    n = sweep_panels(&ds, &cfg, results, n);
    if (n < 1)
    {
        puts("The scenario sweep failed; check the inputs.");
        free_dataset(&ds);
        return (1);
    }
    tnb_apply_savings(&ds, &cfg, results, n);
    printf("Sweep completed: %d configurations (1 to %d panels).\n", n, n);

    /* ---- 6. Generate report.txt and console summary ---- */
    best = n - 1;                   /* the sized system is the last (largest allowed) row */
    set_report_selected(best);
    MAKE_DIR(OUTPUT_DIR);
    set_report_pause(wait_for_section);
    if (!write_report(REPORT_PATH, &cfg, &results[best]))
        puts("Could not write output/report.txt. Ensure output/ exists and is writable.");
    else
    {
        if (!append_sweep_table(REPORT_PATH, results, n))
            puts("Could not append the sweep table to output/report.txt.");
        if (!append_planner_section(REPORT_PATH, &cfg, &fault, target, roof, budget, n))
            puts("Could not append the sizing section to output/report.txt.");
        puts("\nReport saved: output/report.txt");
    }
    if (report_stopped())           /* user answered n to "Continue to the next part?" */
    {
        puts("Stopped by user. The full report is saved in output/report.txt. Terminating.");
        free_dataset(&ds);
        return (0);
    }

    /* ---- Search hourly records? yes -> filter by month / timestamp / payback ---- */
    while (1)
    {
        if (!ask_yes_no("\nSearch hourly records? (y/n): ", &again))
        {
            puts("\nInput ended.");
            break;
        }
        if (!again)
            break;
        search_menu(&ds, results, n);
    }
    puts("Done. Thank you.");
    free_dataset(&ds);
    return (0);
}
