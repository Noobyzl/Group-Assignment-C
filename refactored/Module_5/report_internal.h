/* report_internal.h - state and helpers shared by the M5 report files
 * (report.c, report_print.c, report_csv.c, coverage.c).
 * Not part of the public API (that is report.h); include only from those files.
 */
#ifndef REPORT_INTERNAL_H
#define REPORT_INTERNAL_H

#include "../common.h"
#include "../Module_2/Fault.h"

/* Dataset facts that M2 already computes, remembered so the report can state them.
 * records < 0 means "no dataset facts known". warn_kind is the coverage warning
 * (0 none, 1 not about one year, 2 hours missing); warn_value is its number. */
typedef struct {
    int    records;
    int    dropped;
    int    invalid;
    double step_h;
    double specific_yield;
    int    warn_kind;
    double warn_value;
} ReportFacts;

/* Defined in report.c. The text pointers are not copied: they must stay valid
 * for the rest of the run (string literals or argv). */
extern ReportFacts report_facts;
extern const char *report_data_source;       /* where the data came from */
extern const char *report_source_file;       /* user-supplied dataset path, or NULL */
extern const char *report_irradiance_type;   /* horizontal / tilted description */

/* Defined in report_print.c: one report part each, written to any stream. */
void print_inputs(FILE *out, const SystemConfig *cfg);
void print_scenario(FILE *out, const SimResult *res);
void print_verdict(FILE *out, const SimResult *res);
void print_sizing(FILE *out, const SystemConfig *cfg, const FaultSummary *fs,
                  int target, int roof, int budget, int chosen);
void print_sweep(FILE *out, const SimResult sweep[], int n);

#endif
