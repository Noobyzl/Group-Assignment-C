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
 * load_panels() lives in panels.c; the line reader it shares is declared in
 * loader_shared.h.
 */
#include "../common.h"
#include "loader_shared.h"
#include "loader_helper.h"

#define INIT_CAP   8800          /* one year of hourly rows */

/* Physical limits. G(i) is on a tilted plane: the solar constant is about
 * 1361 W/m2 and cloud-edge enhancement can briefly push ground readings a
 * little above clear-sky values, so 1500 W/m2 is a generous ceiling that no
 * real hourly mean exceeds. Irradiance cannot be negative. Air temperature:
 * the recorded extremes are about -90 C and +57 C; -60..60 keeps every
 * plausible site and rejects sensor garbage such as -999 or 9999. */
#define MAX_IRRADIANCE   1500.0
#define MIN_TEMP_C       (-60.0)
#define MAX_TEMP_C       60.0

/* Progress of one load, shared by the helpers below. */
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

/* ---- classifying a line ----------------------------------------------- */

/* 1 = every field is non-blank text (no numbers): a header, whatever its first cell says. */
static int all_text(char *f[], int nf)
{
    double v;
    int i;

    while (nf > 0 && *trim(f[nf - 1]) == '\0')
        nf--;                                         /* trailing commas leave blank end cells */
    if (nf == 0)
        return (0);
    for (i = 0; i < nf; i++)
    {
        char *t = trim(f[i]);

        if (*t == '\0' || parse_value(t, &v) == 0)
            return (0);
    }
    return (1);
}

/* Before the first data row, text lines and the header row are not data.
 * Returns 1 if the line was consumed (preamble or header), 0 otherwise. */
static int consume_preamble_or_header(char *f[], int nf, int has_comma, LoadState *st)
{
    if (st->seen_data)
        return (0);
    if (!has_comma)
        return (1);                                   /* preamble text */
    if (header_like(f[0]) || all_text(f, nf))
    {
        map_columns(f, nf, &st->c_irr, &st->c_tmp, st->comp); /* header row */
        if (st->c_irr == st->c_tmp)
            st->header_error = 1;
        return (1);
    }
    return (0);
}

static void drop_line(Dataset *ds, const LoadState *st, const char *why)
{
    fprintf(stderr, "load_csv: line %d dropped (%s)\n", st->line_no, why);
    ds->n_dropped++;
}

/* ---- building a record ------------------------------------------------ */

/* 1 = both values are inside the physical limits. */
static int in_range(const HourRecord *r)
{
    return (r->irradiance >= 0.0 && r->irradiance <= MAX_IRRADIANCE &&
            r->temp_c >= MIN_TEMP_C && r->temp_c <= MAX_TEMP_C);
}

/* Components file (Gb(i), Gd(i), optional Gr(i)): irradiance = their sum.
 * Any unusable component makes the row invalid. */
static void read_components(HourRecord *r, char *f[], int nf, const LoadState *st)
{
    double v;
    int k;

    for (k = 0; k < 3; k++)
    {
        if (st->comp[k] < 0)
            continue;
        if (st->comp[k] >= nf || parse_value(f[st->comp[k]], &v) != 0)
        {
            r->valid = 0;
            return;
        }
        r->irradiance += v;
    }
}

/* Fill the irradiance/temperature of r; valid = 0 if either is unusable
 * (blank/non-numeric) or physically implausible (counted in n_flagged). */
static void read_values(HourRecord *r, char *f[], int nf, LoadState *st)
{
    r->valid = 1;
    r->irradiance = 0.0;
    r->temp_c = 0.0;
    if (st->comp[0] >= 0)
        read_components(r, f, nf, st);
    else if (st->c_irr >= nf || parse_value(f[st->c_irr], &r->irradiance) != 0)
        r->valid = 0;
    /* c_tmp < 0: the header names an irradiance column but no temperature column, so there is
     * no temperature to read (temp_c stays 0) */
    if (st->c_tmp >= 0 && (st->c_tmp >= nf || parse_value(f[st->c_tmp], &r->temp_c) != 0))
        r->valid = 0;
    if (r->valid && !in_range(r))
    {
        fprintf(stderr, "load_csv: line %d flagged (G=%g W/m2, T=%g C out of range)\n",
                st->line_no, r->irradiance, r->temp_c);
        r->valid = 0;
        st->n_flagged++;
    }
    if (!r->valid)
    {
        r->irradiance = 0.0;
        r->temp_c = 0.0;
    }
}

/* Append r to ds, growing the array when full. 0 ok, -1 out of memory. */
static int append_record(Dataset *ds, LoadState *st, const HourRecord *r)
{
    if (ds->n == st->cap)
    {
        int newcap;
        HourRecord *tmp;

        if (st->cap == 0)
            newcap = INIT_CAP;      /* first allocation */
        else
            newcap = st->cap * 2;   /* double when full */
        tmp = realloc(ds->rec, (size_t)newcap * sizeof *tmp);
        if (!tmp)
            return (-1);
        ds->rec = tmp;
        st->cap = newcap;
    }
    ds->rec[ds->n++] = *r;
    if (r->valid)
        st->n_valid++;
    return (0);
}

/* ---- one line, start to finish ---------------------------------------- */

static int is_same_time(const HourRecord *a, const HourRecord *b)
{
    return (a->year == b->year && a->month == b->month && a->day == b->day &&
            a->hour == b->hour && a->minute == b->minute);
}

/* 0 = handled (kept, dropped or skipped), -1 = fatal (out of memory). */
static int process_line(Dataset *ds, LoadState *st, char *line)
{
    char *s = trim(line);                  /* removes CR, LF, spaces */
    char *f[MAX_COLS];
    HourRecord r;
    int nf, has_comma;

    if (*s == '\0')
        return (0);                          /* blank line */
    st->any_content = 1;
    has_comma = strchr(s, ',') != NULL;
    nf = split_fields(s, f, MAX_COLS);
    f[0] = trim(f[0]);
    if (parse_timestamp(f[0], &r) != 0)
    {
        if (!consume_preamble_or_header(f, nf, has_comma, st))
            drop_line(ds, st, "bad timestamp");
        return (0);
    }
    st->seen_data = 1;
    if (ds->n > 0 && is_same_time(&ds->rec[ds->n - 1], &r))
    {
        drop_line(ds, st, "duplicate timestamp");
        return (0);
    }
    read_values(&r, f, nf, st);
    return (append_record(ds, st, &r));
}

/* ---- time order and duplicates ---------------------------------------- */

typedef struct {
    long long key;               /* grows with time (minute resolution) */
    int       idx;               /* position in the file, keeps the sort stable */
} SortKey;

static long long time_key(const HourRecord *r)
{
    return ((((long long)r->year * 12 + r->month) * 32 + r->day) * 24 + r->hour) * 60 + r->minute;
}

static int cmp_sortkey(const void *a, const void *b)
{
    const SortKey *x = a, *y = b;
    if (x->key != y->key)
    {
        if (x->key < y->key)
            return (-1);
        return (1);
    }
    return (x->idx > y->idx) - (x->idx < y->idx);
}

/* Put the rows in time order and drop a repeated timestamp wherever it sits
 * (process_line only catches a repeat right after the previous row). The
 * first occurrence in the file is kept. Later modules measure the time step
 * from neighbouring rows, so unsorted data would give a meaningless step and
 * a pasted-twice file would count every hour twice. Does nothing, and
 * allocates nothing, when the file is already strictly increasing.
 * Returns 0 ok, -1 out of memory (ds is left untouched). */
static int order_and_dedupe(Dataset *ds)
{
    SortKey *k;
    HourRecord *out;
    int i, kept = 0, sorted = 1, dups = 0;

    for (i = 1; i < ds->n; i++)
        if (time_key(&ds->rec[i]) <= time_key(&ds->rec[i - 1])) { sorted = 0; break; }
    if (sorted)
        return (0);

    k = malloc((size_t)ds->n * sizeof *k);
    out = malloc((size_t)ds->n * sizeof *out);
    if (!k || !out)
    {
        free(k);
        free(out);
        return (-1);
    }
    for (i = 0; i < ds->n; i++)
    {
        k[i].key = time_key(&ds->rec[i]);
        k[i].idx = i;
    }
    qsort(k, (size_t)ds->n, sizeof *k, cmp_sortkey);
    for (i = 0; i < ds->n; i++)
    {
        if (i > 0 && k[i].key == k[i - 1].key)
        {
            dups++;
            continue;                       /* same timestamp as an earlier row */
        }
        out[kept++] = ds->rec[k[i].idx];
    }
    fprintf(stderr, "load_csv: rows were not in time order; sorted by timestamp\n");
    if (dups > 0)
        fprintf(stderr, "load_csv: dropped %d row(s) with a repeated timestamp\n", dups);
    memcpy(ds->rec, out, (size_t)kept * sizeof *out);
    ds->n = kept;
    ds->n_dropped += dups;
    free(k);
    free(out);
    return (0);
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

int load_csv(const char *path, Dataset *ds)
{
    FILE *fp;
    char line[LINE_BUF];
    LoadState st = {0};
    char *row;
    int status;

    if (!ds)
        return (-1);
    ds->rec = NULL;                 /* always leave ds in a freeable state */
    ds->n = 0;
    ds->n_dropped = 0;
    if (!path)
        return (-1);
    st.c_irr = 1;
    st.c_tmp = 2;
    st.comp[0] = st.comp[1] = st.comp[2] = -1;
    fp = fopen(path, "r");
    if (!fp)
    {
        fprintf(stderr, "load_csv: cannot open '%s'\n", path);
        return (-1);
    }
    while ((status = read_line(fp, line, sizeof line)) != READ_EOF)
    {
        st.line_no++;
        if (status == READ_TOO_LONG)
        {
            st.any_content = 1;
            drop_line(ds, &st, "line too long");
            continue;
        }
        row = line;
        if (st.line_no == 1)
            row = skip_bom(line);       /* a BOM can only start the file */
        if (process_line(ds, &st, row) != 0)
        {
            fprintf(stderr, "load_csv: out of memory\n");
            fclose(fp);
            free_dataset(ds);
            return (-1);
        }
        if (st.header_error)
        {
            fprintf(stderr, "load_csv: line %d: header gives irradiance and temperature the same column (one of them is not named)\n", st.line_no);
            fclose(fp);
            free_dataset(ds);
            return (-1);
        }
    }
    fclose(fp);
    return (finish_load(path, ds, &st));
}
