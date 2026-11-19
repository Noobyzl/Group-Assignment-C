/* M1 loader_order.c - time order and duplicate removal (split out of loader.c). */
#include "../common.h"
#include "loader_shared.h"

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
int order_and_dedupe(Dataset *ds)
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
