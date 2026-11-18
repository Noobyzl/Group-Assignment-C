#ifndef REPORT_H
#define REPORT_H
#include "common.h"
#include "Fault.h"
int write_report(const char *path, const SystemConfig *cfg, const SimResult *res);
/* Console pacing: hook is called with the next section's title before it prints. */
void set_report_pause(int (*hook)(const char *next_section));  /* hook returns 0 to stop showing the rest */
int report_stopped(void);   /* 1 if the user declined to continue */
int export_csvs(const char *dir, const Dataset *d, const SimResult sweep[], int n);
void set_report_dataset(const Dataset *d);
const char *dataset_coverage_warning(const Dataset *d, char *buf, size_t size);
void set_report_metadata(const char *source, const char *irradiance_description);
int report_best_index(const SimResult sweep[], int n);
int append_sweep_table(const char *path, const SimResult sweep[], int n);
/* Pin the scenario the report and CSVs describe (flowchart: the sized panel count). -1 = use the best-index rule. */
void set_report_selected(int index);
/* Flowchart sections: fault summary, inputs and the Min(target, roof, budget) sizing. */
int append_planner_section(const char *path, const SystemConfig *cfg, const FaultSummary *fs,
                           int target, int roof, int budget, int chosen);
int export_csvs_with_config(const char *dir, const Dataset *d, const SystemConfig *cfg, const SimResult sweep[], int n);
#endif
