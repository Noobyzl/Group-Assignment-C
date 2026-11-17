/* loader_helper.h - text/parsing helpers used only by the M1 loader files
 * (loader.c, panels.c, loader_helper.c). Kept out of common.h so their short,
 * generic names (trim, split_fields, ...) cannot clash with other modules.
 */
#ifndef LOADER_HELPER_H
#define LOADER_HELPER_H

#include "../common.h"

int   ci_contains(const char *s, const char *needle);        /* 1 = found, 0 = not */
int   split_fields(char *line, char *f[], int max);          /* returns field count */
char *trim(char *s);                                         /* trims in place, returns new start */
int   parse_timestamp(const char *s, HourRecord *r);         /* 0 ok, -1 not a timestamp */
int   parse_value(const char *s, double *out);               /* 0 ok, -1 blank/NaN/non-numeric */
int   header_like(const char *first);                        /* 1 = looks like a header cell */
void  map_columns(char *f[], int nf, int *c_irr, int *c_tmp);

#endif
