/* main.c - integration of M1 (loader) and M2 (capacity and yield).
 * Build:  gcc -Wall -Wextra -I. Module_1/loader.c Module_1/panels.c Module_1/loader_helper.c Module_2/Yield.c main.c -o solar -lm
 * Run:    ./solar [dataset.csv] [panels.csv] [site_area_m2]
 */
#include "common.h"
#include "Module_2/Yield.h"

#define DEFAULT_DATA   "datasets/Timeseries_5.700_101.840_E5_10deg_0deg_2019_2019.csv"
#define DEFAULT_PANELS "datasets/panels.csv"
#define DEFAULT_AREA   100.0

int main(int argc, char **argv)
{
    const char *data_path   = argc > 1 ? argv[1] : DEFAULT_DATA;
    const char *panels_path = argc > 2 ? argv[2] : DEFAULT_PANELS;
    double area = DEFAULT_AREA;
    Dataset ds = {0};
    Panel panels[MAX_PANELS_CATALOGUE];

    if (argc > 3) {
        char *end;

        area = strtod(argv[3], &end);
        if (end == argv[3] || *end != '\0' || !isfinite(area) || area <= 0.0) {
            fprintf(stderr, "Site area '%s' is not a positive number\n", argv[3]);
            return 1;
        }
    }
    if (load_csv(data_path, &ds) != 0) {
        fprintf(stderr, "Could not load dataset '%s'\n", data_path);
        return 1;
    }
    int np = load_panels(panels_path, panels, MAX_PANELS_CATALOGUE);
    if (np <= 0) {
        fprintf(stderr, "No usable panels in '%s'\n", panels_path);
        free_dataset(&ds);
        return 1;
    }
    printf("Loaded %d records (%d dropped) and %d panel(s).\n", ds.n, ds.n_dropped, np);

    for (int i = 0; i < np; i++)
        print_m2_summary(&ds, &panels[i], area);

    free_dataset(&ds);
    return 0;
}
