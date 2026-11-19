/* input.c - Module 5 (M5) console input helpers (split out of main.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include "input.h"

static int input_eof = 0;           /* set when stdin ends, so every prompt loop can stop */

/* Prompt and read one line. An over-long line is discarded so the rest of it
 * cannot answer later prompts. Returns 0 when input ends. */
static int prompt_line(const char *prompt, char *out, size_t size)
{
    int c;

    printf("%s", prompt);
    if (!fgets(out, (int)size, stdin))
    {
        input_eof = 1;
        return (0);
    }
    if (!strchr(out, '\n'))
    {
        c = getchar();
        if (c != '\n' && c != EOF)
        {
            /* over-long line: discard all of it and answer with an empty string, so the
             * prompt rejects it instead of accepting the first part */
            while (c != '\n' && c != EOF)
                c = getchar();
            out[0] = '\0';
        }
    }
    out[strcspn(out, "\r\n")] = '\0';
    return (1);
}

int ask_double(const char *prompt, double *value, double min_value)
{
    char line[128];
    char *end;
    double v;

    while (1)
    {
        if (!prompt_line(prompt, line, sizeof line))
            return (0);
        errno = 0;
        v = strtod(line, &end);
        while (*end == ' ' || *end == '\t')
            ++end;
        /* reject nan/inf (isfinite) and hex floats such as 0x1p-1 */
        if (line == end || *end || errno == ERANGE || !isfinite(v) || strpbrk(line, "xX") || v < min_value)
        {
            printf("Invalid input: enter a number of at least %g.\n", min_value);
            continue;
        }
        *value = v;
        return (1);
    }
}

int ask_int(const char *prompt, int *value, int min_value, int max_value)
{
    char line[128];
    char *end;
    long v;

    while (1)
    {
        if (!prompt_line(prompt, line, sizeof line))
            return (0);
        errno = 0;
        v = strtol(line, &end, 10);
        while (*end == ' ' || *end == '\t')
            ++end;
        if (line == end || *end || errno == ERANGE || v < min_value || v > max_value)
        {
            printf("Invalid input: enter a whole number from %d to %d.\n", min_value, max_value);
            continue;
        }
        *value = (int)v;
        return (1);
    }
}

int ask_yes_no(const char *prompt, int *yes)
{
    char line[64];

    while (1)
    {
        if (!prompt_line(prompt, line, sizeof line))
            return (0);
        if (line[0] == 'y' || line[0] == 'Y')
        {
            *yes = 1;
            return (1);
        }
        if (line[0] == 'n' || line[0] == 'N')
        {
            *yes = 0;
            return (1);
        }
        printf("Please answer y or n.\n");
    }
}

/* After each report part, ask whether to continue to the next one. */
int wait_for_section(const char *next)
{
    int yes;

    (void)next;
    if (input_eof)
        return (0);
    if (!ask_yes_no("\nContinue to the next part? (y/n): ", &yes))
        return (0);
    printf("\n");
    return (yes);
}
