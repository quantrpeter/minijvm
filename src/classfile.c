#include "classfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Big-endian byte reader over the loaded file --- */

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    const char *path; /* for error messages */
} Reader;

static void die(const Reader *r, const char *msg) {
    fprintf(stderr, "minijvm: %s: %s (at byte offset %zu)\n", r->path, msg, r->pos);
    exit(1);
}

static uint8_t read_u1(Reader *r) {
    if (r->pos + 1 > r->size) die(r, "unexpected end of file");
    return r->data[r->pos++];
}

static uint16_t read_u2(Reader *r) {
    if (r->pos + 2 > r->size) die(r, "unexpected end of file");
    uint16_t v = (uint16_t)((r->data[r->pos] << 8) | r->data[r->pos + 1]);
    r->pos += 2;
    return v;
}

static uint32_t read_u4(Reader *r) {
    if (r->pos + 4 > r->size) die(r, "unexpected end of file");
    uint32_t v = ((uint32_t)r->data[r->pos] << 24) | ((uint32_t)r->data[r->pos + 1] << 16) |
                 ((uint32_t)r->data[r->pos + 2] << 8) | (uint32_t)r->data[r->pos + 3];
    r->pos += 4;
    return v;
}

static void skip_bytes(Reader *r, size_t n) {
    if (r->pos + n > r->size) die(r, "unexpected end of file");
    r->pos += n;
}

static void *xmalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fprintf(stderr, "minijvm: out of memory\n");
        exit(1);
    }
    return p;
}

/* --- Parsing --- */

static void parse_constant_pool(Reader *r, ClassFile *cf) {
    uint16_t count = read_u2(r);
    cf->constant_pool_count = count;
    cf->constant_pool = xmalloc(sizeof(ConstantPoolEntry) * count);

    for (uint16_t i = 1; i < count; i++) {
        ConstantPoolEntry *e = &cf->constant_pool[i];
        e->tag = read_u1(r);
        switch (e->tag) {
        case CONST_UTF8: {
            uint16_t len = read_u2(r);
            if (r->pos + len > r->size) die(r, "utf8 constant overruns file");
            e->u.utf8.length = len;
            e->u.utf8.bytes = xmalloc((size_t)len + 1);
            memcpy(e->u.utf8.bytes, r->data + r->pos, len);
            e->u.utf8.bytes[len] = '\0';
            r->pos += len;
            break;
        }
        case CONST_INTEGER:
            e->u.integer = (int32_t)read_u4(r);
            break;
        case CONST_FLOAT:
            skip_bytes(r, 4); /* parsed but unsupported by the interpreter */
            break;
        case CONST_LONG:
        case CONST_DOUBLE:
            skip_bytes(r, 8);
            i++; /* longs and doubles occupy two constant pool slots */
            break;
        case CONST_CLASS:
            e->u.class_info.name_index = read_u2(r);
            break;
        case CONST_STRING:
            e->u.string.string_index = read_u2(r);
            break;
        case CONST_FIELDREF:
        case CONST_METHODREF:
        case CONST_INTERFACE_METHODREF:
            e->u.ref.class_index = read_u2(r);
            e->u.ref.name_and_type_index = read_u2(r);
            break;
        case CONST_NAME_AND_TYPE:
            e->u.name_and_type.name_index = read_u2(r);
            e->u.name_and_type.descriptor_index = read_u2(r);
            break;
        case CONST_METHOD_HANDLE:
            skip_bytes(r, 3);
            break;
        case CONST_METHOD_TYPE:
            skip_bytes(r, 2);
            break;
        case CONST_DYNAMIC:
            skip_bytes(r, 4);
            break;
        case CONST_INVOKE_DYNAMIC:
            e->u.invoke_dynamic.bootstrap_index = read_u2(r);
            e->u.invoke_dynamic.name_and_type_index = read_u2(r);
            break;
        case CONST_MODULE:
        case CONST_PACKAGE:
            skip_bytes(r, 2);
            break;
        default:
            die(r, "unknown constant pool tag");
        }
    }
}

static void parse_code_attribute(Reader *r, const ClassFile *cf, CodeAttribute *code) {
    code->max_stack = read_u2(r);
    code->max_locals = read_u2(r);
    code->code_length = read_u4(r);
    if (r->pos + code->code_length > r->size) die(r, "code attribute overruns file");
    code->code = xmalloc(code->code_length);
    memcpy(code->code, r->data + r->pos, code->code_length);
    r->pos += code->code_length;

    uint16_t exception_table_length = read_u2(r);
    skip_bytes(r, (size_t)exception_table_length * 8);

    uint16_t attributes_count = read_u2(r);
    for (uint16_t i = 0; i < attributes_count; i++) {
        skip_bytes(r, 2); /* attribute_name_index */
        uint32_t len = read_u4(r);
        skip_bytes(r, len);
    }
    (void)cf;
}

/* Parse the attributes of one field/method; extract Code if present. */
static void parse_member_attributes(Reader *r, const ClassFile *cf, CodeAttribute *code_out) {
    uint16_t attributes_count = read_u2(r);
    for (uint16_t i = 0; i < attributes_count; i++) {
        uint16_t name_index = read_u2(r);
        uint32_t len = read_u4(r);
        const char *name = cf_utf8(cf, name_index);
        if (code_out && strcmp(name, "Code") == 0) {
            parse_code_attribute(r, cf, code_out);
        } else {
            skip_bytes(r, len);
        }
    }
}

static void parse_bootstrap_methods(Reader *r, ClassFile *cf, uint32_t len) {
    size_t end = r->pos + len;
    if (end > r->size) die(r, "BootstrapMethods attribute overruns file");

    cf->bootstrap_methods_count = read_u2(r);
    cf->bootstrap_methods = xmalloc(sizeof(BootstrapMethod) * cf->bootstrap_methods_count);
    for (uint16_t i = 0; i < cf->bootstrap_methods_count; i++) {
        BootstrapMethod *bm = &cf->bootstrap_methods[i];
        skip_bytes(r, 2); /* bootstrap_method_ref */
        bm->argc = read_u2(r);
        bm->argv = xmalloc(sizeof(uint16_t) * bm->argc);
        for (uint16_t j = 0; j < bm->argc; j++)
            bm->argv[j] = read_u2(r);
    }
    r->pos = end;
}

static void parse_class_attributes(Reader *r, ClassFile *cf) {
    uint16_t attributes_count = read_u2(r);
    for (uint16_t i = 0; i < attributes_count; i++) {
        uint16_t name_index = read_u2(r);
        uint32_t len = read_u4(r);
        const char *name = cf_utf8(cf, name_index);
        if (strcmp(name, "BootstrapMethods") == 0)
            parse_bootstrap_methods(r, cf, len);
        else
            skip_bytes(r, len);
    }
}

ClassFile *classfile_load_bytes(const uint8_t *data, size_t size, const char *name) {
    Reader r = { data, size, 0, name };
    ClassFile *cf = xmalloc(sizeof(ClassFile));

    if (read_u4(&r) != 0xCAFEBABEu) die(&r, "bad magic number (not a class file)");
    cf->minor_version = read_u2(&r);
    cf->major_version = read_u2(&r);

    parse_constant_pool(&r, cf);

    cf->access_flags = read_u2(&r);
    cf->this_class = read_u2(&r);
    cf->super_class = read_u2(&r);

    uint16_t interfaces_count = read_u2(&r);
    skip_bytes(&r, (size_t)interfaces_count * 2);

    uint16_t fields_count = read_u2(&r);
    for (uint16_t i = 0; i < fields_count; i++) {
        skip_bytes(&r, 6); /* access_flags, name_index, descriptor_index */
        parse_member_attributes(&r, cf, NULL);
    }

    cf->methods_count = read_u2(&r);
    cf->methods = xmalloc(sizeof(MethodInfo) * cf->methods_count);
    for (uint16_t i = 0; i < cf->methods_count; i++) {
        MethodInfo *m = &cf->methods[i];
        m->access_flags = read_u2(&r);
        m->name_index = read_u2(&r);
        m->descriptor_index = read_u2(&r);
        parse_member_attributes(&r, cf, &m->code);
    }

    parse_class_attributes(&r, cf);
    return cf;
}

ClassFile *classfile_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "minijvm: cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fprintf(stderr, "minijvm: cannot stat %s\n", path);
        exit(1);
    }
    uint8_t *data = xmalloc((size_t)size);
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "minijvm: cannot read %s\n", path);
        exit(1);
    }
    fclose(f);

    ClassFile *cf = classfile_load_bytes(data, (size_t)size, path);
    free(data);
    return cf;
}

void classfile_free(ClassFile *cf) {
    if (!cf) return;
    for (uint16_t i = 1; i < cf->constant_pool_count; i++) {
        if (cf->constant_pool[i].tag == CONST_UTF8)
            free(cf->constant_pool[i].u.utf8.bytes);
    }
    free(cf->constant_pool);
    for (uint16_t i = 0; i < cf->bootstrap_methods_count; i++)
        free(cf->bootstrap_methods[i].argv);
    free(cf->bootstrap_methods);
    for (uint16_t i = 0; i < cf->methods_count; i++)
        free(cf->methods[i].code.code);
    free(cf->methods);
    free(cf);
}

/* --- Constant pool helpers --- */

static const ConstantPoolEntry *cp_entry(const ClassFile *cf, uint16_t index, uint8_t tag) {
    if (index == 0 || index >= cf->constant_pool_count || cf->constant_pool[index].tag != tag) {
        fprintf(stderr, "minijvm: bad constant pool reference #%u (expected tag %u)\n", index, tag);
        exit(1);
    }
    return &cf->constant_pool[index];
}

const char *cf_utf8(const ClassFile *cf, uint16_t index) {
    return cp_entry(cf, index, CONST_UTF8)->u.utf8.bytes;
}

const char *cf_class_name(const ClassFile *cf, uint16_t class_index) {
    return cf_utf8(cf, cp_entry(cf, class_index, CONST_CLASS)->u.class_info.name_index);
}

void cf_resolve_ref(const ClassFile *cf, uint16_t ref_index,
                    const char **class_name, const char **name, const char **descriptor) {
    if (ref_index == 0 || ref_index >= cf->constant_pool_count) {
        fprintf(stderr, "minijvm: bad ref index #%u\n", ref_index);
        exit(1);
    }
    const ConstantPoolEntry *e = &cf->constant_pool[ref_index];
    if (e->tag != CONST_FIELDREF && e->tag != CONST_METHODREF && e->tag != CONST_INTERFACE_METHODREF) {
        fprintf(stderr, "minijvm: constant #%u is not a field/method ref\n", ref_index);
        exit(1);
    }
    *class_name = cf_class_name(cf, e->u.ref.class_index);
    const ConstantPoolEntry *nt = cp_entry(cf, e->u.ref.name_and_type_index, CONST_NAME_AND_TYPE);
    *name = cf_utf8(cf, nt->u.name_and_type.name_index);
    *descriptor = cf_utf8(cf, nt->u.name_and_type.descriptor_index);
}

MethodInfo *cf_find_method(const ClassFile *cf, const char *name, const char *descriptor) {
    for (uint16_t i = 0; i < cf->methods_count; i++) {
        MethodInfo *m = &cf->methods[i];
        if (strcmp(cf_utf8(cf, m->name_index), name) == 0 &&
            strcmp(cf_utf8(cf, m->descriptor_index), descriptor) == 0)
            return m;
    }
    return NULL;
}

void cf_resolve_invoke_dynamic(const ClassFile *cf, uint16_t index,
                               const char **name, const char **descriptor,
                               uint16_t *bootstrap_index) {
    const ConstantPoolEntry *e = cp_entry(cf, index, CONST_INVOKE_DYNAMIC);
    const ConstantPoolEntry *nt =
        cp_entry(cf, e->u.invoke_dynamic.name_and_type_index, CONST_NAME_AND_TYPE);
    *name = cf_utf8(cf, nt->u.name_and_type.name_index);
    *descriptor = cf_utf8(cf, nt->u.name_and_type.descriptor_index);
    *bootstrap_index = e->u.invoke_dynamic.bootstrap_index;
}

const char *cf_bootstrap_string_arg(const ClassFile *cf, uint16_t bootstrap_index) {
    if (bootstrap_index >= cf->bootstrap_methods_count) {
        fprintf(stderr, "minijvm: bad bootstrap method index %u\n", bootstrap_index);
        exit(1);
    }
    const BootstrapMethod *bm = &cf->bootstrap_methods[bootstrap_index];
    if (bm->argc < 1) {
        fprintf(stderr, "minijvm: bootstrap method %u has no arguments\n", bootstrap_index);
        exit(1);
    }
    const ConstantPoolEntry *e = &cf->constant_pool[bm->argv[0]];
    if (e->tag != CONST_STRING) {
        fprintf(stderr, "minijvm: expected String bootstrap argument\n");
        exit(1);
    }
    return cf_utf8(cf, e->u.string.string_index);
}
