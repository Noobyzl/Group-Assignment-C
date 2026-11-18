/* yield.h  --  M2 Capacity and yield */
#ifndef YIELD_H
#define YIELD_H

#include "common.h"

/* ---- agreed prototypes (from the shared contract) ---- */
double panel_area_m2(const Panel *p);
int    panels_that_fit(double site_area_m2, const Panel *p);   /* floor(usable area / panel area) */
double array_kwp(int n_panels, const Panel *p);
double annual_generation_kwh(const Dataset *d, double kwp);    /* skips valid == 0 rows */
double specific_yield(const Dataset *d);                       /* kWh per kWp per year */

/* ---- extra helpers (tell M5 they exist; the report can use them) ---- */
double time_step_hours(const Dataset *d);                      /* step between rows, from the timestamps */
int    count_invalid_rows(const Dataset *d);                   /* how many rows have valid == 0 */
void   monthly_generation_kwh(const Dataset *d, double kwp, double out[12]); /* out[0]=Jan ... out[11]=Dec */
void   print_m2_summary(const Dataset *d, const Panel *p, double site_area_m2); /* prints M2 results incl. skipped rows */

#endif
