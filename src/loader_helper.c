#include "../common.h"
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

/* Try the four accepted formats. 0 = one matched (numbers stored), -1 = none. */
static int scan_timestamp(const char *s, int *y, int *mo, int *d, int *h, int *mi)
{
    char c;
    int n = 0;

    *mi = 0;                        /* the HH-only format has no minutes */
    if (sscanf(s, "%4d%2d%2d:%2d%2d%n", y, mo, d, h, mi, &n) == 5 && s[n] == '\0')
        return (0);
    if (sscanf(s, "%4d%2d%2d:%2d%n", y, mo, d, h, &n) == 4 && s[n] == '\0')
        return (0);
    if (sscanf(s, "%d-%d-%d%c%d:%d%n", y, mo, d, &c, h, mi, &n) == 6
        && (c == ' ' || c == 'T') && s[n] == '\0')
        return (0);
    if (sscanf(s, "%d/%d/%d%c%d:%d%n", y, mo, d, &c, h, mi, &n) == 6
        && (c == ' ' || c == 'T') && s[n] == '\0')
        return (0);
    return (-1);
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
    return (0);
}

/* 0 = finite number stored in *out, -1 = blank / NaN / inf / non-numeric */
int parse_value(const char *s, double *out)
{
    double v;
    int n = 0;
    if (!s)
        return (-1);
    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return (-1);
    if (sscanf(s, "%lf%n", &v, &n) != 1)
        return (-1);
    while (isspace((unsigned char)s[n]))
        n++;
    if (s[n] != '\0')
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
void    map_columns(char *f[], int nf, int *c_irr, int *c_tmp)
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
    }
    if (irr >= 0)
        *c_irr = irr;
    if (tmp >= 0)
        *c_tmp = tmp;
}
