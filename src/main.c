#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "classfile.h"
#include "interp.h"
#include "jar.h"

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s <file.class> [args...]\n", argv0);
    fprintf(stderr, "       %s -jar <file.jar> [args...]\n", argv0);
}

static int run_main(const ClassFile *cf) {
    MethodInfo *m = cf_find_method(cf, "main", "([Ljava/lang/String;)V");
    if (!m) {
        fprintf(stderr, "minijvm: no public static void main(String[]) in %s\n",
                cf_class_name(cf, cf->this_class));
        return 1;
    }

    /* local 0 is the String[] args reference; we pass a null (0) slot */
    interp_run(cf, m, NULL, 0);
    return 0;
}

static int run_class_file(const char *path) {
    ClassFile *cf = classfile_load(path);
    int rc = run_main(cf);
    classfile_free(cf);
    return rc;
}

/* Main-Class is written as com.foo.Bar; the jar entry is com/foo/Bar.class */
static char *class_entry_name(const char *dotted) {
    size_t n = strlen(dotted);
    char *entry = malloc(n + sizeof(".class"));
    if (!entry) {
        fprintf(stderr, "minijvm: out of memory\n");
        exit(1);
    }
    for (size_t i = 0; i < n; i++) entry[i] = dotted[i] == '.' ? '/' : dotted[i];
    strcpy(entry + n, ".class");
    return entry;
}

static int run_jar(const char *path) {
    Jar *jar = jar_open(path);

    char *main_class = jar_main_class(jar);
    if (!main_class) {
        fprintf(stderr, "minijvm: %s: no Main-Class in META-INF/MANIFEST.MF\n", path);
        jar_close(jar);
        return 1;
    }

    char *entry = class_entry_name(main_class);
    size_t size;
    uint8_t *bytes = jar_read(jar, entry, &size);
    if (!bytes) {
        fprintf(stderr, "minijvm: %s: Main-Class %s names a missing entry %s\n",
                path, main_class, entry);
        free(entry);
        free(main_class);
        jar_close(jar);
        return 1;
    }

    ClassFile *cf = classfile_load_bytes(bytes, size, entry);
    int rc = run_main(cf);

    classfile_free(cf);
    free(bytes);
    free(entry);
    free(main_class);
    jar_close(jar);
    return rc;
}

int main(int argc, char **argv) {
    const char *target;
    int is_jar;
    int first_arg; /* where the program's own arguments start */

    if (argc >= 3 && strcmp(argv[1], "-jar") == 0) {
        is_jar = 1;
        target = argv[2];
        first_arg = 3;
    } else if (argc >= 2 && argv[1][0] != '-') {
        is_jar = 0;
        target = argv[1];
        first_arg = 2;
    } else {
        usage(argv[0]);
        return 2;
    }

    /* Arguments are accepted so that a real java command line still works, but
     * handing them over needs a String[], and objects are exactly what this
     * interpreter does not have. main sees a null in local 0 either way. */
    if (first_arg < argc) {
        int n = argc - first_arg;
        fprintf(stderr, "minijvm: ignoring %d program argument%s: "
                        "String[] args is always null\n",
                n, n == 1 ? "" : "s");
    }

    return is_jar ? run_jar(target) : run_class_file(target);
}
