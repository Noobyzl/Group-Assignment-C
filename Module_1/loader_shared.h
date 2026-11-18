/* loader_shared.h - internal link between loader.c and panels.c.
 * Not part of the public API (that is common.h); include only from those files.
 */
#ifndef LOADER_SHARED_H
#define LOADER_SHARED_H

#include <stdio.h>

#define LINE_BUF   1024      /* longest accepted line is LINE_BUF - 1 characters */
#define MAX_COLS   32

#define READ_EOF       0
#define READ_OK        1
#define READ_TOO_LONG  2

/* Read one line into buf (size >= 3). Returns READ_OK, READ_EOF or
 * READ_TOO_LONG (the rest of an over-long line is discarded). Defined in loader.c. */
int   read_line(FILE *fp, char *buf, int size);

/* Skip a UTF-8 byte order mark at the start of a line. Defined in loader.c. */
char *skip_bom(char *line);

#endif
