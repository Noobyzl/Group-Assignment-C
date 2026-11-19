/* M1 loader_row.c - turning one CSV line into a record (split out of loader.c).
 *
 * process_line() classifies a line (preamble, header, bad timestamp, duplicate
 * or data row), builds the HourRecord and appends it to the dataset. The row
 * policy and accepted layouts are described at the top of loader.c.
 */
#include "../common.h"
#include "loader_shared.h"
#include "loader_helper.h"

#define INIT_CAP   8800          /* one year of hourly rows */

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

void drop_line(Dataset *ds, const LoadState *st, const char *why)
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
int process_line(Dataset *ds, LoadState *st, char *line)
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
