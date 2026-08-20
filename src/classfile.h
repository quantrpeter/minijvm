#ifndef MINIJVM_CLASSFILE_H
#define MINIJVM_CLASSFILE_H

#include <stdint.h>
#include <stddef.h>

/* Constant pool tags (JVM spec section 4.4) */
enum {
    CONST_UTF8               = 1,
    CONST_INTEGER            = 3,
    CONST_FLOAT              = 4,
    CONST_LONG               = 5,
    CONST_DOUBLE             = 6,
    CONST_CLASS              = 7,
    CONST_STRING             = 8,
    CONST_FIELDREF           = 9,
    CONST_METHODREF          = 10,
    CONST_INTERFACE_METHODREF = 11,
    CONST_NAME_AND_TYPE      = 12,
    CONST_METHOD_HANDLE      = 15,
    CONST_METHOD_TYPE        = 16,
    CONST_DYNAMIC            = 17,
    CONST_INVOKE_DYNAMIC     = 18,
    CONST_MODULE             = 19,
    CONST_PACKAGE            = 20,
};

typedef struct {
    uint8_t tag; /* 0 means unused slot (index 0, or second slot of long/double) */
    union {
        struct { uint16_t length; char *bytes; } utf8;    /* NUL-terminated copy */
        int32_t integer;
        struct { uint16_t name_index; } class_info;       /* -> Utf8 */
        struct { uint16_t string_index; } string;          /* -> Utf8 */
        struct { uint16_t class_index, name_and_type_index; } ref; /* Field/Method/InterfaceMethod */
        struct { uint16_t name_index, descriptor_index; } name_and_type;
        struct { uint16_t bootstrap_index, name_and_type_index; } invoke_dynamic;
    } u;
} ConstantPoolEntry;

typedef struct {
    uint16_t argc;
    uint16_t *argv; /* constant pool indices */
} BootstrapMethod;

typedef struct {
    uint16_t max_stack;
    uint16_t max_locals;
    uint32_t code_length;
    uint8_t *code;
} CodeAttribute;

typedef struct {
    uint16_t access_flags;
    uint16_t name_index;        /* -> Utf8 */
    uint16_t descriptor_index;  /* -> Utf8 */
    CodeAttribute code;         /* code.code == NULL if no Code attribute */
} MethodInfo;

typedef struct {
    uint16_t minor_version;
    uint16_t major_version;
    uint16_t constant_pool_count;
    ConstantPoolEntry *constant_pool; /* indices 1..count-1 valid */
    uint16_t access_flags;
    uint16_t this_class;
    uint16_t super_class;
    uint16_t methods_count;
    MethodInfo *methods;
    uint16_t bootstrap_methods_count;
    BootstrapMethod *bootstrap_methods;
} ClassFile;

/* Parse a .class file from disk. Exits with an error message on failure. */
ClassFile *classfile_load(const char *path);
/* Same, from bytes already in memory (a jar entry). Everything the parser
 * keeps is copied, so the buffer can be freed afterwards. name is only used
 * in error messages. */
ClassFile *classfile_load_bytes(const uint8_t *data, size_t size, const char *name);
void classfile_free(ClassFile *cf);

/* Constant pool helpers. Exit on invalid index/tag. */
const char *cf_utf8(const ClassFile *cf, uint16_t index);
const char *cf_class_name(const ClassFile *cf, uint16_t class_index);
/* Resolve a Fieldref/Methodref: fills class name, member name, descriptor. */
void cf_resolve_ref(const ClassFile *cf, uint16_t ref_index,
                    const char **class_name, const char **name, const char **descriptor);

/* Find a method by name and descriptor; returns NULL if not found. */
MethodInfo *cf_find_method(const ClassFile *cf, const char *name, const char *descriptor);

/* Resolve an InvokeDynamic constant: name, descriptor, bootstrap table index. */
void cf_resolve_invoke_dynamic(const ClassFile *cf, uint16_t index,
                               const char **name, const char **descriptor,
                               uint16_t *bootstrap_index);

/* First bootstrap argument as a String constant recipe (for makeConcatWithConstants). */
const char *cf_bootstrap_string_arg(const ClassFile *cf, uint16_t bootstrap_index);

#endif
