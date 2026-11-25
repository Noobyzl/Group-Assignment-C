#include "loader_helper.h"

int ci_contains(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    for (; *s; s++)
    {
        size_t i = 0;
        while (i < n && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n)
            return (1);
    }
    return (0);
}

/* Split line in place on commas (keeps empty fields). Returns field count. */
int split_fields(char *line, char *f[], int max)
{
    int n = 0;
    char *p = line;
    while (n < max)
    {
        f[n++] = p;
        p = strchr(p, ',');
        if (!p)
            break;
        *p++ = '\0';
    }
    return (n);
}

char *trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s))
        s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return (s);
}

/* Days in a month, with the Gregorian leap-year rule. */
static int days_in_month(int y, int m)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (m == 2 && leap)
        return (29);
    return (days[m - 1]);
}

/* Read an optionally signed integer of at most `width` characters (0 = no limit) at *p,
 * after skipping spaces, and move *p past it. 0 = read, -1 = no digits. */
static int take_int(const char **p, int width, int *out)
{
    const char *s = *p;
    long v = 0;
    int neg = 0;
    int used = 0;
    int digits = 0;

    while (isspace((unsigned char)*s))
        s++;
    if ((*s == '-' || *s == '+') && (width == 0 || used < width))
    {
        neg = (*s == '-');
        s++;
        used++;
    }
    while (isdigit((unsigned char)*s) && (width == 0 || used < width))
    {
        if (v < 100000000L)
            v = v * 10 + (*s - '0');
        s++;
        used++;
        digits++;
    }
    if (!digits)
        return (-1);
    if (neg)
        *out = (int)(-v);
    else
        *out = (int)v;
    *p = s;
    return (0);
}

/* Match one literal character at *p. 0 = matched, -1 = not there. */
static int take_char(const char **p, char c)
{
    if (**p != c)
        return (-1);
    (*p)++;
    return (0);
}

/* YYYYMMDD:HH or YYYYMMDD:HHMM. */
static int scan_compact(const char *s, int v[5])
{
    v[4] = 0;                       /* the HH-only form has no minutes */
    if (take_int(&s, 4, &v[0]) || take_int(&s, 2, &v[1]) || take_int(&s, 2, &v[2])
        || take_char(&s, ':') || take_int(&s, 2, &v[3]))
        return (-1);
    if (take_int(&s, 2, &v[4]) != 0)
        v[4] = 0;
    if (*s == '\0')
        return (0);
    return (-1);
}

/* YYYY<sep>MM<sep>DD then a space or T, then HH:MM. sep is '-' or '/'. */
static int scan_separated(const char *s, char sep, int v[5])
{
    if (take_int(&s, 0, &v[0]) || take_char(&s, sep) || take_int(&s, 0, &v[1])
        || take_char(&s, sep) || take_int(&s, 0, &v[2]))
        return (-1);
    if (*s != ' ' && *s != 'T')
        return (-1);
    s++;
    if (take_int(&s, 0, &v[3]) || take_char(&s, ':') || take_int(&s, 0, &v[4]))
        return (-1);
    if (*s == '\0')
        return (0);
    return (-1);
}

/* Try the four accepted formats. 0 = one matched (numbers stored), -1 = none. */
static int scan_timestamp(const char *s, int *y, int *mo, int *d, int *h, int *mi)
{
    int v[5];

    if (scan_compact(s, v) != 0 && scan_separated(s, '-', v) != 0
        && scan_separated(s, '/', v) != 0)
        return (-1);
    *y = v[0];
    *mo = v[1];
    *d = v[2];
    *h = v[3];
    *mi = v[4];
    return (0);
}

/* 0 = parsed, -1 = not a timestamp or not a real date/time */
int parse_timestamp(const char *s, HourRecord *r)
{
    int y, mo, d, h, mi;

    if (scan_timestamp(s, &y, &mo, &d, &h, &mi) != 0)
        return (-1);
    if (y < 1900 || y > 2200 || mo < 1 || mo > 12 || d < 1 ||
        d > days_in_month(y, mo) || h < 0 || h > 23 || mi < 0 || mi > 59)
        return (-1);
    r->year = y;
    r->month = mo;
    r->day = d;
    r->hour = h;
    r->minute = mi;
    return (0);
}

/* 0 = finite number stored in *out, -1 = blank / NaN / inf / non-numeric */
int parse_value(const char *s, double *out)
{
    double v;
    char *end;
    if (!s)
        return (-1);
    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return (-1);
    v = strtod(s, &end);
    if (end == s || memchr(s, 'x', (size_t)(end - s)) || memchr(s, 'X', (size_t)(end - s)))
        return (-1);              /* nothing read, or a hex number (decimal only) */
    while (isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return (-1);              /* e.g. "12abc" */
    if (!isfinite(v))
        return (-1);              /* nan, inf */
    *out = v;
    return (0);
}

int header_like(const char *first)
{
    return (ci_contains(first, "time") || ci_contains(first, "date"));
}

/* Work out column positions from a header row. */
void    map_columns(char *f[], int nf, int *c_irr, int *c_tmp, int comp[3])
{
    int i, irr = -1, tmp = -1;
    for (i = 1; i < nf; i++)
    {
        char *name = trim(f[i]);
        if (irr < 0 && (ci_contains(name, "G(i)") || ci_contains(name, "G(h)") ||
                        ci_contains(name, "ghi")  || ci_contains(name, "irrad")))
            irr = i;
        else if (tmp < 0 && (ci_contains(name, "T2m") || ci_contains(name, "temp")))
            tmp = i;
        if (comp[0] < 0 && ci_contains(name, "Gb(i)"))
            comp[0] = i;                    /* beam */
        else if (comp[1] < 0 && ci_contains(name, "Gd(i)"))
            comp[1] = i;                    /* diffuse */
        else if (comp[2] < 0 && ci_contains(name, "Gr(i)"))
            comp[2] = i;                    /* ground-reflected */
    }
    if (irr >= 0)
    {
        *c_irr = irr;
        comp[0] = comp[1] = comp[2] = -1;   /* a total column wins over components */
    }
    else if (comp[0] >= 0 && comp[1] >= 0)
        *c_irr = comp[0];                   /* components mode: irradiance = Gb + Gd (+ Gr) */
    else
        comp[0] = comp[1] = comp[2] = -1;   /* beam alone is not a total: keep old default */
    if (tmp >= 0)
        *c_tmp = tmp;
    else if (irr >= 0 || comp[0] >= 0)
        *c_tmp = -1;                        /* irradiance named but temperature not: do not guess column 2 */
}
