/* M1 loader.c - CSV loading for hourly irradiance/temperature data.
 *
 * Accepted layout (PVGIS style and plain CSVs both work):
 *   - optional UTF-8 byte order mark at the start of the file
 *   - optional preamble lines BEFORE the data that contain no comma
 *     (e.g. "Latitude (decimal degrees):<TAB>5.700") are skipped silently
 *   - optional header row whose first field is time/timestamp/date/datetime;
 *     columns named G(i)/G(h)/GHI/irradiance and T2m/temp are located by name,
 *     otherwise column 2 = irradiance and column 3 = temperature
 *   - timestamps: YYYYMMDD:HHMM, YYYY-MM-DD HH:MM, YYYY-MM-DDTHH:MM, YYYY/MM/DD HH:MM
 *   - LF or CRLF line endings, blank lines, trailing commas
 *
 * Row policy:
 *   - timestamp unparseable or not a real calendar date -> row dropped,
 *     line number printed to stderr
 *   - line longer than LINE_BUF - 1 characters -> row dropped (never kept truncated)
 *   - timestamp ok, value blank/NaN/non-numeric -> row kept with valid = 0
 *
 * Range checks (flag, never crash): a row whose numbers parse but fall outside
 * the physical limits below is kept with valid = 0, like a blank value, and
 * counted; one summary line is printed at the end of the load.
 *
 * load_csv() returns 0 on success and -1 on any failure. On every call, ds is
 * reset first, so free_dataset() is always safe afterwards. Because of that
 * reset, a ds that already holds records MUST be freed before it is reloaded.
 *
 * File layout: this file has the line reader, the end-of-load checks and the
 * public load_csv()/free_dataset(). loader_row.c turns one line into a record,
 * loader_order.c sorts and removes duplicate timestamps, and load_panels() lives
 * in panels.c. Their shared declarations are in loader_shared.h.
 */
#include "../common.h"
#include "loader_shared.h"

/* ---- reading ---------------------------------------------------------- */

/* Read one line into buf. A line that does not fit is reported as
 * READ_TOO_LONG and the rest of it is discarded, so it is neither parsed
 * truncated nor mistaken for a new row. Sentinels in the last two bytes tell
 * whether fgets filled the whole buffer; this stays correct even if the line
 * contains a NUL byte (where strlen/strchr would be fooled). */

static void discard_rest_of_line(FILE *fp)
{
    int ch;

    ch = fgetc(fp);
    while (ch != '\n' && ch != EOF)
        ch = fgetc(fp);
}

int read_line(FILE *fp, char *buf, int size)
{
    buf[size - 2] = 1;
    buf[size - 1] = 1;
    if (!fgets(buf, size, fp))
        return (READ_EOF);
    if (buf[size - 1] == 0 && buf[size - 2] != '\n')
    {
        /* Buffer is full. It is still a whole line if the file ends here
         * (no final newline) or if only the '\n' of a CRLF is left. */
        int ch = fgetc(fp);

        if (ch == EOF)
            return (READ_OK);
        if (ch == '\n' && buf[size - 2] == '\r')
        {
            buf[size - 2] = '\n';
            return (READ_OK);
        }
        if (ch != '\n')
            discard_rest_of_line(fp);
        return (READ_TOO_LONG);
    }
    return (READ_OK);
}

/* Skip a UTF-8 byte order mark at the start of the file. */
char *skip_bom(char *line)
{
    if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
        (unsigned char)line[2] == 0xBF)
        return (line + 3);
    return (line);
}

/* ---- wrap-up ---------------------------------------------------------- */

/* Report problems with the finished load. 0 ok, -1 failure (ds is freed). */
static int finish_load(const char *path, Dataset *ds, LoadState *st)
{
    if (ds->n > 0)
    {
        int i;
        if (order_and_dedupe(ds) != 0)
        {
            fprintf(stderr, "load_csv: out of memory\n");
            free_dataset(ds);
            return (-1);
        }
        st->n_valid = 0;                    /* recount: duplicates may have been removed */
        for (i = 0; i < ds->n; i++)
            if (ds->rec[i].valid)
                st->n_valid++;
    }
    if (ds->n_dropped > 0)
        fprintf(stderr, "load_csv: dropped %d unusable row(s)\n", ds->n_dropped);
    if (!st->any_content)
    {
        fprintf(stderr, "load_csv: '%s' is empty\n", path);
        free_dataset(ds);
        return (-1);
    }
    if (ds->n == 0)
    {
        fprintf(stderr, "load_csv: '%s' has a header but no usable data rows\n", path);
        free_dataset(ds);
        return (-1);
    }
    if (st->n_flagged > 0)
        fprintf(stderr, "load_csv: %d row(s) flagged invalid: irradiance outside 0..%g W/m2 or temperature outside %g..%g C\n",
                st->n_flagged, MAX_IRRADIANCE, MIN_TEMP_C, MAX_TEMP_C);
    if (st->n_valid < MIN_RECORDS)
        fprintf(stderr, "load_csv: warning: only %d valid row(s), fewer than MIN_RECORDS (%d)\n", st->n_valid, MIN_RECORDS);
    return (0);
}

/* ---- public API ------------------------------------------------------- */

void free_dataset(Dataset *ds)
{
    if (!ds)
        return;
    free(ds->rec);
    ds->rec = NULL;
    ds->n = 0;
    ds->n_dropped = 0;
}

/* Start a load: empty dataset, default column layout (G(i) in column 2, T in column 3). */
static void init_load(Dataset *ds, LoadState *st)
{
    ds->rec = NULL;                 /* always leave ds in a freeable state */
    ds->n = 0;
    ds->n_dropped = 0;
    st->c_irr = 1;
    st->c_tmp = 2;
    st->comp[0] = st->comp[1] = st->comp[2] = -1;
}

/* Read every line of the open file into ds. 0 = ok. -1 = fatal (out of memory or a
 * bad header): the message is printed, fp is closed and ds is freed. */
static int read_all_lines(FILE *fp, Dataset *ds, LoadState *st)
{
    char line[LINE_BUF];
    char *row;
    int status;

    while ((status = read_line(fp, line, sizeof line)) != READ_EOF)
    {
        st->line_no++;
        if (status == READ_TOO_LONG)
        {
            st->any_content = 1;
            drop_line(ds, st, "line too long");
            continue;
        }
        row = line;
        if (st->line_no == 1)
            row = skip_bom(line);       /* a BOM can only start the file */
        if (process_line(ds, st, row) != 0)
        {
            fprintf(stderr, "load_csv: out of memory\n");
            fclose(fp);
            free_dataset(ds);
            return (-1);
        }
        if (st->header_error)
        {
            fprintf(stderr, "load_csv: line %d: header gives irradiance and temperature the same column (one of them is not named)\n", st->line_no);
            fclose(fp);
            free_dataset(ds);
            return (-1);
        }
    }
    return (0);
}

int load_csv(const char *path, Dataset *ds)
{
    FILE *fp;
    LoadState st = {0};

    if (!ds)
        return (-1);
    init_load(ds, &st);
    if (!path)
        return (-1);
    fp = fopen(path, "r");
    if (!fp)
    {
        fprintf(stderr, "load_csv: cannot open '%s'\n", path);
        return (-1);
    }
    if (read_all_lines(fp, ds, &st) != 0)
        return (-1);
    fclose(fp);
    return (finish_load(path, ds, &st));
}
