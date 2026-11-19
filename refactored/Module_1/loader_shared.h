/* loader_shared.h - internal link between the M1 loader files
 * (loader.c, loader_row.c, loader_order.c, panels.c).
 * Not part of the public API (that is common.h); include only from those files.
 */
#ifndef LOADER_SHARED_H
#define LOADER_SHARED_H

#include "../common.h"

#define LINE_BUF   1024      /* longest accepted line is LINE_BUF - 1 characters */
#define MAX_COLS   32

#define READ_EOF       0
#define READ_OK        1
#define READ_TOO_LONG  2

/* Physical limits. G(i) is on a tilted plane: the solar constant is about
 * 1361 W/m2 and cloud-edge enhancement can briefly push ground readings a
 * little above clear-sky values, so 1500 W/m2 is a generous ceiling that no
 * real hourly mean exceeds. Irradiance cannot be negative. Air temperature:
 * the recorded extremes are about -90 C and +57 C; -60..60 keeps every
 * plausible site and rejects sensor garbage such as -999 or 9999. */
#define MAX_IRRADIANCE   1500.0
#define MIN_TEMP_C       (-60.0)
#define MAX_TEMP_C       60.0

/* Progress of one load, shared by the loader files. */
typedef struct {
    int cap;                     /* allocated rows in ds->rec */
    int line_no;                 /* physical line currently being read */
    int n_valid;                 /* rows kept with valid == 1 */
    int n_flagged;               /* rows kept with valid == 0 only because a value was out of range */
    int any_content;             /* saw at least one non-blank line */
    int seen_data;               /* saw at least one good timestamp */
    int c_irr, c_tmp;            /* column index of irradiance / temperature */
    int comp[3];                 /* Gb/Gd/Gr columns when the file has components, else -1 */
    int header_error;            /* header maps irradiance and temperature to the same column */
} LoadState;

/* Read one line into buf (size >= 3). Returns READ_OK, READ_EOF or
 * READ_TOO_LONG (the rest of an over-long line is discarded). Defined in loader.c. */
int   read_line(FILE *fp, char *buf, int size);

/* Skip a UTF-8 byte order mark at the start of a line. Defined in loader.c. */
char *skip_bom(char *line);

/* Report a dropped row (line number to stderr) and count it. Defined in loader_row.c. */
void  drop_line(Dataset *ds, const LoadState *st, const char *why);

/* Handle one physical line: 0 = handled (kept, dropped or skipped), -1 = out of
 * memory. Defined in loader_row.c. */
int   process_line(Dataset *ds, LoadState *st, char *line);

/* Put the rows in time order and drop repeated timestamps. 0 ok, -1 out of
 * memory (ds untouched). Defined in loader_order.c. */
int   order_and_dedupe(Dataset *ds);

#endif
