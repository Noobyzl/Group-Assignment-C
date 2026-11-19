/* report_print.c - Module 5 (M5) report text (split out of report.c).
 *
 * Each report part is written by one function that prints to any stream. The
 * caller in report.c prints it to the file, then to the console while the
 * console is still on.
 */
#include "report.h"
#include "report_internal.h"
#include "../Module_4/tnb.h"

#define INSTALL_COST_RM_PER_W_REPORT   INSTALL_COST_RM_PER_W

/* The "never pays back" sentinel (-1), NaN and inf are shown as words, not numbers. */
void print_payback(FILE *out, double years)
{
    if (years < 0.0 || !isfinite(years))
        fprintf(out, "never pays back");
    else
        fprintf(out, "%.2f years", years);
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

/* ---- part 1: data and inputs ------------------------------------------ */

static void print_dataset_facts(FILE *out)
{
    if (report_facts.records < 0)
        return;
    fprintf(out, "Dataset: %d records kept, %d rows dropped (bad timestamp), %d rows skipped (unusable values)\n",
            report_facts.records, report_facts.dropped, report_facts.invalid);
    fprintf(out, "Time step: %.2f h (from the file's timestamps); specific yield: %.1f kWh/kWp/year\n",
            report_facts.step_h, report_facts.specific_yield);
    print_coverage_warning(out, report_facts.warn_kind, report_facts.warn_value);
}

void print_inputs(FILE *out, const SystemConfig *cfg)
{
    fprintf(out, "SOLAR BUILD DECISION PLANNER - SIMULATION REPORT\n");
    fprintf(out, "================================================\n\n");

    fprintf(out, "1. DATA AND INPUTS\n------------------\n");
    fprintf(out, "Available site area: %.2f m2\n", cfg->area_m2);
    fprintf(out, "Annual electricity use: %.2f kWh\n", cfg->annual_kwh);
    if (cfg->monthly_bill_rm > 0.0)
        fprintf(out, "Electricity tariff: TNB Domestic General Tariff (average RM %.4f/kWh at this use)\n", cfg->tariff_rm_kwh);
    else
        fprintf(out, "Electricity tariff: RM %.4f/kWh\n", cfg->tariff_rm_kwh);
    fprintf(out, "Panel: %s\nRated power: %.1f W\nPanel dimensions: %.3f m x %.3f m\nPanel price: RM %.2f\n",
            cfg->panel.name, cfg->panel.rated_w, cfg->panel.length_m, cfg->panel.width_m, cfg->panel.price_rm);
    print_dataset_facts(out);
    if (report_source_file)
        fprintf(out, "Data source: User-supplied dataset %s\n", report_source_file);
    else
        fprintf(out, "Data source: %s\n", report_data_source);
    fprintf(out, "Irradiance description: %s\n\n", report_irradiance_type);
}

/* ---- parts 2 and 3: scenario and verdict ------------------------------ */

void print_scenario(FILE *out, const SimResult *res)
{
    fprintf(out, "2. SELECTED SCENARIO RESULTS\n----------------------------\n");
    fprintf(out, "Panel count: %d\nSystem capacity: %.3f kWp\nAnnual generation: %.2f kWh\nEnergy offset: %.2f%%\n",
            res->n_panels, res->kwp, res->annual_gen_kwh, res->offset_pct);
    fprintf(out, "Initial cost: RM %.2f\nAnnual bill savings: RM %.2f\nSimple payback: ",
            res->capex_rm, res->saving_rm_per_year);
    print_payback(out, res->payback_years);
    fprintf(out, "\nCO2 avoided: %.2f kg/year\n\n", res->co2_avoided_kg);
}

void print_verdict(FILE *out, const SimResult *res)
{
    fprintf(out, "3. VERDICT AND REASON\n---------------------\n");
    fprintf(out, "Recommendation: %s\n", verdict_text(res->verdict));
    fprintf(out, "Reason: %s\n", decide_reason(res->offset_pct, res->payback_years));
    fprintf(out, "This is a screening estimate, not an engineering quotation. Confirm roof condition, shading, permits, wiring, and current installed cost before making a real purchase.\n");
}

/* ---- parts 5 and 4: sizing and sweep table ---------------------------- */

void print_sizing(FILE *out, const SystemConfig *cfg, const FaultSummary *fs,
                  int target, int roof, int budget, int chosen)
{
    int least = target;

    fprintf(out, "\n5. FAULT DETECTION AND SIZING\n-----------------------------\n");
    fprintf(out, "Fault detector: %d of %d rows rejected (%d unusable, %d implausible irradiance); estimated energy loss %.2f kWh/kWp (%.2f%%).\n",
            fs->faulty_rows, fs->rows_checked, fs->unusable_rows, fs->implausible_rows,
            fs->loss_kwh_per_kwp, fs->loss_pct);
    fprintf(out, "AC power is modelled from irradiance (no AC column in the dataset): kWp x G/1000 x %.2f.\n", PERF_RATIO);
    fprintf(out, "Monthly bill: RM %.2f -> %.1f kWh/month (inverse of the TNB tariff); budget: RM %.2f.\n",
            cfg->monthly_bill_rm, cfg->monthly_kwh, cfg->budget_rm);
    fprintf(out, "Panels needed to cover the demand: %d | fit on roof: %d | affordable at RM %.2f/W: %d\n",
            target, roof, INSTALL_COST_RM_PER_W_REPORT, budget);
    if (roof < least)
        least = roof;
    if (budget < least)
        least = budget;
    if (chosen < least)
        fprintf(out, "Chosen panel count = %d (limited to %d panels: the program caps a sweep at that size; the minimum of the three is %d)\n",
                chosen, chosen, least);
    else
        fprintf(out, "Chosen panel count = min of the three = %d\n", chosen);
}

void print_sweep(FILE *out, const SimResult sweep[], int n)
{
    int i;

    fprintf(out, "\n4. SCENARIO SWEEP TABLE\n-----------------------\n");
    fprintf(out, "Panels | kWp | Generation kWh | Offset %% | Capex RM | Savings RM/year | Payback years | CO2 kg | Verdict\n");
    for (i = 0; i < n; ++i)
    {
        fprintf(out, "%d | %.3f | %.2f | %.2f | %.2f | %.2f | ",
                sweep[i].n_panels, sweep[i].kwp, sweep[i].annual_gen_kwh, sweep[i].offset_pct,
                sweep[i].capex_rm, sweep[i].saving_rm_per_year);
        print_payback(out, sweep[i].payback_years);
        fprintf(out, " | %.2f | %s\n", sweep[i].co2_avoided_kg, short_verdict(sweep[i].verdict));
    }
}
