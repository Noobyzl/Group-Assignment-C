/* input.h - Module 5 (M5) console input helpers (split out of main.c).
 *
 * Every ask_* function prints the prompt, reads one line and keeps asking until
 * the answer is valid. They return 0 only when stdin ends, 1 otherwise.
 */
#ifndef INPUT_H
#define INPUT_H

#include "../common.h"

/* Asks until a valid number >= min_value is typed. */
int ask_double(const char *prompt, double *value, double min_value);

/* Asks until a whole number from min_value to max_value is typed. */
int ask_int(const char *prompt, int *value, int min_value, int max_value);

/* Asks until the answer starts with y/Y (*yes = 1) or n/N (*yes = 0). */
int ask_yes_no(const char *prompt, int *yes);

/* Report pacing hook for set_report_pause(): asks "Continue to the next part?".
 * Returns 1 to continue, 0 to stop (also when stdin has ended). */
int wait_for_section(const char *next);

#endif
