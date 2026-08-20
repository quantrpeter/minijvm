#ifndef MINIJVM_JAR_H
#define MINIJVM_JAR_H

#include <stddef.h>
#include <stdint.h>

/* A jar is a zip archive, so reading one means reading zip: find the central
 * directory, locate an entry by name, and inflate it if it is deflated. */
typedef struct Jar Jar;

/* Exits with an error message if the file cannot be read or is not a zip. */
Jar *jar_open(const char *path);
void jar_close(Jar *jar);

/* Uncompressed bytes of one entry, NUL-terminated for convenience (the
 * terminator is not counted in *size_out). Caller frees. NULL if absent. */
uint8_t *jar_read(Jar *jar, const char *name, size_t *size_out);

/* Main-Class from META-INF/MANIFEST.MF, as the dotted name written there.
 * Caller frees. NULL if there is no manifest or no such header. */
char *jar_main_class(Jar *jar);

#endif
