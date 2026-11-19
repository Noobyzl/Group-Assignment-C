/* panels.c - panel catalogue loading (panels.csv), split out of loader.c.
 * Shares the line reader and BOM skipper with loader.c via loader_shared.h.
 */
#include "../common.h"
#include "loader_shared.h"
#include "loader_helper.h"

/* ---- panel catalogue --------------------------------------------------- */

/* Row layout: name,rated_w,length_m,width_m,price_rm[,anything else]
 * Extra trailing columns (e.g. "source") are ignored. The first non-blank line
 * is skipped as a header if its first cell contains "name" or "model" and its
 * rated_w cell is not a number.
 *
 * A row is rejected (reason and line number to stderr, loading continues) if
 * it has fewer than 5 fields, a blank name, a missing/non-numeric number,
 * rated_w <= 0, length <= 0, width <= 0, price < 0, or a length/width above
 * MAX_PANEL_SIDE_M (almost certainly millimetres typed in a metres column).
 * A name longer than 31 characters is truncated with a warning.
 * A line longer than LINE_BUF - 1 characters is still accepted when its first
 * 5 fields are complete (a 6th field exists), so a very long "source" cell
 * cannot kill an otherwise valid row; otherwise it is rejected as too long.
 *
 * Returns the number of panels stored in out[0..max-1]. Returns 0 (after a
 * message on stderr) for a NULL path/out, max <= 0, or a file that cannot be
 * opened; callers treat 0 as "no catalogue". Rows beyond max are not read. */
#define PANEL_FIELDS      5
#define MAX_PANEL_SIDE_M  5.0

static int parse_panel(char *f[], int nf, Panel *p, const char **why)
{
    double v[4];
    char *name;
    int i;

    if (nf < PANEL_FIELDS)
        return (*why = "fewer than 5 fields", -1);
    name = trim(f[0]);
    if (*name == '\0')
        return (*why = "blank name", -1);
    for (i = 0; i < 4; i++)
        if (parse_value(f[i + 1], &v[i]) != 0)
            return (*why = "number missing or not numeric", -1);
    if (v[0] <= 0.0)
        return (*why = "rated watts not positive", -1);
    if (v[1] <= 0.0 || v[2] <= 0.0)
        return (*why = "length or width not positive", -1);
    if (v[1] > MAX_PANEL_SIDE_M || v[2] > MAX_PANEL_SIDE_M)
        return (*why = "length or width above 5 m (millimetres?)", -1);
    if (v[3] < 0.0)
        return (*why = "negative price", -1);
    if (strlen(name) >= sizeof p->name)
        fprintf(stderr, "load_panels: name '%s' truncated to %d characters\n",
                name, (int)sizeof p->name - 1);
    strncpy(p->name, name, sizeof p->name - 1);
    p->name[sizeof p->name - 1] = '\0';
    p->rated_w = v[0];
    p->length_m = v[1];
    p->width_m = v[2];
    p->price_rm = v[3];
    return (0);
}

/* 1 = the first non-blank line is a header row (its rated_w cell is not a number
 * and its first cell says "name" or "model"), so it is skipped. */
static int is_panel_header(char *f[], int nf)
{
    double tmp;

    return (nf >= 2 && parse_value(f[1], &tmp) != 0 &&
            (ci_contains(f[0], "name") || ci_contains(f[0], "model")));
}

/* Parse one row into *p. Prints the reason and returns 0 if it is rejected, else 1. */
static int store_panel(char *f[], int nf, Panel *p, int line_no)
{
    const char *why;

    if (parse_panel(f, nf, p, &why) != 0)
    {
        fprintf(stderr, "load_panels: line %d rejected (%s)\n", line_no, why);
        return (0);
    }
    return (1);
}

int load_panels(const char *path, Panel out[], int max)
{
    FILE *fp;
    char line[LINE_BUF], *row, *s, *f[MAX_COLS];
    int n = 0, line_no = 0, first = 1, status, nf, truncated;

    if (!path || !out || max <= 0)
        return (0);
    fp = fopen(path, "r");
    if (!fp)
    {
        fprintf(stderr, "load_panels: cannot open '%s'\n", path);
        return (0);
    }
    while ((status = read_line(fp, line, sizeof line)) != READ_EOF)
    {
        line_no++;
        truncated = (status == READ_TOO_LONG);
        row = line;
        if (line_no == 1)
            row = skip_bom(line);
        s = trim(row);
        if (*s == '\0')
            continue;
        nf = split_fields(s, f, MAX_COLS);
        if (truncated && nf <= PANEL_FIELDS)
        {
            fprintf(stderr, "load_panels: line %d rejected (line too long)\n", line_no);
            continue;
        }
        if (first)
        {
            first = 0;
            if (is_panel_header(f, nf))
                continue;                       /* header row */
        }
        if (n == max)
        {
            fprintf(stderr, "load_panels: catalogue full, ignoring rows from line %d\n", line_no);
            break;
        }
        n += store_panel(f, nf, &out[n], line_no);
    }
    fclose(fp);
    if (n == 0)
        fprintf(stderr, "load_panels: no usable panels in '%s'\n", path);
    return (n);
}
