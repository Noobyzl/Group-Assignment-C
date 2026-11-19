#ifndef REPORT_H
#define REPORT_H
#include "../common.h"
#include "../Module_2/Fault.h"
int write_report(const char *path, const SystemConfig *cfg, const SimResult *res);
/* Console pacing: hook is called with the next section's title before it prints. */
void set_report_pause(int (*hook)(const char *next_section));  /* hook returns 0 to stop showing the rest */
int report_stopped(void);   /* 1 if the user declined to continue */
int export_csvs(const char *dir, const Dataset *d, const SimResult sweep[], int n);
void set_report_dataset(const Dataset *d);

/* Dataset coverage check (coverage.c). The kinds are what dataset_coverage_warning() returns. */
#define COVERAGE_OK             0
#define COVERAGE_NOT_ONE_YEAR   1     /* value = days of data in the file */
#define COVERAGE_HOURS_MISSING  2     /* value = percentage of hours missing */
int  dataset_coverage_warning(const Dataset *d, double *value);
void print_coverage_warning(FILE *out, int kind, double value);   /* prints "WARNING: ...\n" */

/* The text pointers are stored, not copied: pass string literals or strings that
 * live for the whole run (such as argv). NULL or "" restores the placeholder and
 * clears any dataset file set below. */
void set_report_metadata(const char *source, const char *irradiance_description);
/* The data came from a user-supplied file: the report says "User-supplied dataset <path>". */
void set_report_source_file(const char *path);
int report_best_index(const SimResult sweep[], int n);
int append_sweep_table(const char *path, const SimResult sweep[], int n);
/* Pin the scenario the report and CSVs describe (flowchart: the sized panel count). -1 = use the best-index rule. */
void set_report_selected(int index);
/* Flowchart sections: fault summary, inputs and the Min(target, roof, budget) sizing. */
int append_planner_section(const char *path, const SystemConfig *cfg, const FaultSummary *fs,
                           int target, int roof, int budget, int chosen);
int export_csvs_with_config(const char *dir, const Dataset *d, const SystemConfig *cfg, const SimResult sweep[], int n);

/* Print a payback figure: "never pays back" for the -1 sentinel, NaN or inf, else "N.NN years". */
void print_payback(FILE *out, double years);
#endif
