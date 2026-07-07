#ifndef MINIJVM_INTERP_H
#define MINIJVM_INTERP_H

#include "classfile.h"

/* Execute a static method of cf. args are the initial local variables
 * (nargs int slots). Returns the method's int return value (0 for void). */
int32_t interp_run(const ClassFile *cf, const MethodInfo *method,
                   const int32_t *args, int nargs);

#endif
