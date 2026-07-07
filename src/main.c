#include <stdio.h>

#include "classfile.h"
#include "interp.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.class>\n", argv[0]);
        return 2;
    }

    ClassFile *cf = classfile_load(argv[1]);

    MethodInfo *m = cf_find_method(cf, "main", "([Ljava/lang/String;)V");
    if (!m) {
        fprintf(stderr, "minijvm: no public static void main(String[]) in %s\n",
                cf_class_name(cf, cf->this_class));
        classfile_free(cf);
        return 1;
    }

    /* local 0 is the String[] args reference; we pass a null (0) slot */
    interp_run(cf, m, NULL, 0);

    classfile_free(cf);
    return 0;
}
