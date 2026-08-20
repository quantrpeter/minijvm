#include "interp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One stack slot. Pointer-sized so that ldc of a String constant can push
 * the C string pointer directly; ints live in the low 32 bits. */
typedef intptr_t Slot;

typedef struct {
    const ClassFile *cf;
    const MethodInfo *method;
    Slot *locals;
    Slot *stack;
    int sp;
    uint32_t pc;      /* next byte to fetch */
    uint32_t insn_pc; /* start of the instruction being executed */
} Frame;

/* Mnemonics for every opcode the JVM defines, so an unsupported one can be
 * reported by name. Gaps are the unused and reserved encodings. */
static const char *const opcode_names[256] = {
    "nop", "aconst_null", "iconst_m1", "iconst_0", "iconst_1", "iconst_2",
    "iconst_3", "iconst_4", "iconst_5", "lconst_0", "lconst_1", "fconst_0",
    "fconst_1", "fconst_2", "dconst_0", "dconst_1", "bipush", "sipush", "ldc",
    "ldc_w", "ldc2_w", "iload", "lload", "fload", "dload", "aload", "iload_0",
    "iload_1", "iload_2", "iload_3", "lload_0", "lload_1", "lload_2", "lload_3",
    "fload_0", "fload_1", "fload_2", "fload_3", "dload_0", "dload_1", "dload_2",
    "dload_3", "aload_0", "aload_1", "aload_2", "aload_3", "iaload", "laload",
    "faload", "daload", "aaload", "baload", "caload", "saload", "istore",
    "lstore", "fstore", "dstore", "astore", "istore_0", "istore_1", "istore_2",
    "istore_3", "lstore_0", "lstore_1", "lstore_2", "lstore_3", "fstore_0",
    "fstore_1", "fstore_2", "fstore_3", "dstore_0", "dstore_1", "dstore_2",
    "dstore_3", "astore_0", "astore_1", "astore_2", "astore_3", "iastore",
    "lastore", "fastore", "dastore", "aastore", "bastore", "castore", "sastore",
    "pop", "pop2", "dup", "dup_x1", "dup_x2", "dup2", "dup2_x1", "dup2_x2",
    "swap", "iadd", "ladd", "fadd", "dadd", "isub", "lsub", "fsub", "dsub",
    "imul", "lmul", "fmul", "dmul", "idiv", "ldiv", "fdiv", "ddiv", "irem",
    "lrem", "frem", "drem", "ineg", "lneg", "fneg", "dneg", "ishl", "lshl",
    "ishr", "lshr", "iushr", "lushr", "iand", "land", "ior", "lor", "ixor",
    "lxor", "iinc", "i2l", "i2f", "i2d", "l2i", "l2f", "l2d", "f2i", "f2l",
    "f2d", "d2i", "d2l", "d2f", "i2b", "i2c", "i2s", "lcmp", "fcmpl", "fcmpg",
    "dcmpl", "dcmpg", "ifeq", "ifne", "iflt", "ifge", "ifgt", "ifle",
    "if_icmpeq", "if_icmpne", "if_icmplt", "if_icmpge", "if_icmpgt",
    "if_icmple", "if_acmpeq", "if_acmpne", "goto", "jsr", "ret",
    "tableswitch", "lookupswitch", "ireturn", "lreturn", "freturn", "dreturn",
    "areturn", "return", "getstatic", "putstatic", "getfield", "putfield",
    "invokevirtual", "invokespecial", "invokestatic", "invokeinterface",
    "invokedynamic", "new", "newarray", "anewarray", "arraylength", "athrow",
    "checkcast", "instanceof", "monitorenter", "monitorexit", "wide",
    "multianewarray", "ifnull", "ifnonnull", "goto_w", "jsr_w",
};

static const char *opcode_name(uint8_t op) {
    const char *name = opcode_names[op];
    return name ? name : "reserved";
}

static void trap(const Frame *f, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "minijvm: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, " at pc=%u in method %s%s\n",
            f->insn_pc, cf_utf8(f->cf, f->method->name_index),
            cf_utf8(f->cf, f->method->descriptor_index));
    exit(1);
}

static void push(Frame *f, Slot v) {
    if (f->sp >= f->method->code.max_stack)
        trap(f, "operand stack overflow in %s", opcode_name(f->method->code.code[f->insn_pc]));
    f->stack[f->sp++] = v;
}

static Slot pop(Frame *f) {
    if (f->sp <= 0)
        trap(f, "operand stack underflow in %s", opcode_name(f->method->code.code[f->insn_pc]));
    return f->stack[--f->sp];
}

static void push_i(Frame *f, int32_t v) { push(f, (Slot)v); }
static int32_t pop_i(Frame *f) { return (int32_t)pop(f); }

/* Count parameter slots in a method descriptor like "(II)I".
 * Only int ("I") parameters are supported for interpreted calls. */
static int descriptor_arg_count(const Frame *f, const char *desc) {
    int n = 0;
    const char *p = desc;
    if (*p++ != '(') trap(f, "bad method descriptor %s", desc);
    while (*p && *p != ')') {
        if (*p == 'I') {
            n++;
            p++;
        } else {
            fprintf(stderr, "minijvm: unsupported parameter type in descriptor %s\n", desc);
            exit(1);
        }
    }
    return n;
}

/* Fetch helpers for bytecode operands */
static uint8_t fetch_u1(Frame *f) { return f->method->code.code[f->pc++]; }
static uint16_t fetch_u2(Frame *f) {
    uint16_t v = (uint16_t)((f->method->code.code[f->pc] << 8) | f->method->code.code[f->pc + 1]);
    f->pc += 2;
    return v;
}

/* Handle invokevirtual on java/io/PrintStream (print/println), mapped to stdio. */
static void intercept_printstream(Frame *f, const char *name, const char *desc) {
    if (strcmp(desc, "(I)V") == 0) {
        int32_t v = pop_i(f);
        pop(f); /* objectref (dummy System.out) */
        printf(strcmp(name, "println") == 0 ? "%d\n" : "%d", v);
    } else if (strcmp(desc, "(Ljava/lang/String;)V") == 0) {
        const char *s = (const char *)pop(f);
        pop(f);
        printf(strcmp(name, "println") == 0 ? "%s\n" : "%s", s);
    } else if (strcmp(desc, "()V") == 0) {
        pop(f);
        printf("\n");
    } else {
        fprintf(stderr, "minijvm: unsupported PrintStream.%s%s\n", name, desc);
        exit(1);
    }
    fflush(stdout);
}

/* Recipe from StringConcatFactory: constant text with \u0001 placeholders. */
static char concat_buf[512];

static const char *string_concat_i(const char *recipe, int32_t v) {
    char *out = concat_buf;
    size_t room = sizeof(concat_buf) - 1;
    size_t n = 0;

    for (const char *p = recipe; *p && n < room; p++) {
        if ((unsigned char)*p == 0x01) {
            int written = snprintf(out + n, room - n + 1, "%d", v);
            if (written < 0 || (size_t)written > room - n) {
                fprintf(stderr, "minijvm: string concat overflow\n");
                exit(1);
            }
            n += (size_t)written;
            continue;
        }
        out[n++] = *p;
    }
    out[n] = '\0';
    return concat_buf;
}

int32_t interp_run(const ClassFile *cf, const MethodInfo *method,
                   const int32_t *args, int nargs) {
    if (!method->code.code) {
        fprintf(stderr, "minijvm: method %s has no Code attribute (abstract/native?)\n",
                cf_utf8(cf, method->name_index));
        exit(1);
    }

    Slot locals_buf[256], stack_buf[256];
    if (method->code.max_locals > 256 || method->code.max_stack > 256) {
        fprintf(stderr, "minijvm: frame too large\n");
        exit(1);
    }

    Frame f = { cf, method, locals_buf, stack_buf, 0, 0, 0 };
    memset(locals_buf, 0, sizeof(Slot) * method->code.max_locals);
    for (int i = 0; i < nargs; i++)
        f.locals[i] = (Slot)args[i];

    for (;;) {
        if (f.pc >= method->code.code_length) {
            fprintf(stderr, "minijvm: pc fell off the end of the code\n");
            exit(1);
        }
        f.insn_pc = f.pc;
        uint8_t op = fetch_u1(&f);

        switch (op) {
        case 0x00: /* nop */
            break;

        /* --- Constants --- */
        case 0x02: case 0x03: case 0x04: case 0x05:
        case 0x06: case 0x07: case 0x08: /* iconst_m1 .. iconst_5 */
            push_i(&f, (int32_t)op - 0x03);
            break;
        case 0x10: /* bipush */
            push_i(&f, (int8_t)fetch_u1(&f));
            break;
        case 0x11: /* sipush */
            push_i(&f, (int16_t)fetch_u2(&f));
            break;
        case 0x12: { /* ldc */
            uint16_t index = fetch_u1(&f);
            const ConstantPoolEntry *e = &cf->constant_pool[index];
            if (e->tag == CONST_INTEGER) {
                push_i(&f, e->u.integer);
            } else if (e->tag == CONST_STRING) {
                push(&f, (Slot)cf_utf8(cf, e->u.string.string_index));
            } else {
                trap(&f, "ldc of unsupported constant type (tag %u)", e->tag);
            }
            break;
        }

        /* --- Locals --- */
        case 0x15: /* iload */
            push(&f, f.locals[fetch_u1(&f)]);
            break;
        case 0x1a: case 0x1b: case 0x1c: case 0x1d: /* iload_0..3 */
            push(&f, f.locals[op - 0x1a]);
            break;
        case 0x36: /* istore */
            f.locals[fetch_u1(&f)] = pop(&f);
            break;
        case 0x3b: case 0x3c: case 0x3d: case 0x3e: /* istore_0..3 */
            f.locals[op - 0x3b] = pop(&f);
            break;
        case 0x84: { /* iinc */
            uint8_t index = fetch_u1(&f);
            int8_t delta = (int8_t)fetch_u1(&f);
            f.locals[index] = (Slot)((int32_t)f.locals[index] + delta);
            break;
        }

        /* --- Stack --- */
        case 0x57: /* pop */
            pop(&f);
            break;
        case 0x59: /* dup */ {
            Slot v = pop(&f);
            push(&f, v);
            push(&f, v);
            break;
        }

        /* --- Arithmetic --- */
        case 0x60: { int32_t b = pop_i(&f), a = pop_i(&f); push_i(&f, a + b); break; } /* iadd */
        case 0x64: { int32_t b = pop_i(&f), a = pop_i(&f); push_i(&f, a - b); break; } /* isub */
        case 0x68: { int32_t b = pop_i(&f), a = pop_i(&f); push_i(&f, a * b); break; } /* imul */
        case 0x6c: { /* idiv */
            int32_t b = pop_i(&f), a = pop_i(&f);
            if (b == 0) { fprintf(stderr, "minijvm: java.lang.ArithmeticException: / by zero\n"); exit(1); }
            push_i(&f, a / b);
            break;
        }
        case 0x70: { /* irem */
            int32_t b = pop_i(&f), a = pop_i(&f);
            if (b == 0) { fprintf(stderr, "minijvm: java.lang.ArithmeticException: / by zero\n"); exit(1); }
            push_i(&f, a % b);
            break;
        }
        case 0x74: /* ineg */
            push_i(&f, -pop_i(&f));
            break;

        /* --- Branches (offsets are relative to the opcode's own pc) --- */
        case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: { /* ifeq..ifle */
            int16_t offset = (int16_t)fetch_u2(&f);
            int32_t v = pop_i(&f);
            int taken = 0;
            switch (op) {
            case 0x99: taken = v == 0; break;
            case 0x9a: taken = v != 0; break;
            case 0x9b: taken = v < 0;  break;
            case 0x9c: taken = v >= 0; break;
            case 0x9d: taken = v > 0;  break;
            case 0x9e: taken = v <= 0; break;
            }
            if (taken) f.pc = f.insn_pc + offset;
            break;
        }
        case 0x9f: case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: { /* if_icmpeq..le */
            int16_t offset = (int16_t)fetch_u2(&f);
            int32_t b = pop_i(&f), a = pop_i(&f);
            int taken = 0;
            switch (op) {
            case 0x9f: taken = a == b; break;
            case 0xa0: taken = a != b; break;
            case 0xa1: taken = a < b;  break;
            case 0xa2: taken = a >= b; break;
            case 0xa3: taken = a > b;  break;
            case 0xa4: taken = a <= b; break;
            }
            if (taken) f.pc = f.insn_pc + offset;
            break;
        }
        case 0xa7: { /* goto */
            int16_t offset = (int16_t)fetch_u2(&f);
            f.pc = f.insn_pc + offset;
            break;
        }

        /* --- Returns --- */
        case 0xac: /* ireturn */
            return pop_i(&f);
        case 0xb1: /* return */
            return 0;

        /* --- Field access: only System.out is recognized --- */
        case 0xb2: { /* getstatic */
            uint16_t index = fetch_u2(&f);
            const char *cls, *name, *desc;
            cf_resolve_ref(cf, index, &cls, &name, &desc);
            if (strcmp(cls, "java/lang/System") == 0 && strcmp(name, "out") == 0) {
                push(&f, 0); /* dummy reference to System.out */
            } else {
                fprintf(stderr, "minijvm: unsupported getstatic %s.%s\n", cls, name);
                exit(1);
            }
            break;
        }

        /* --- Method calls --- */
        case 0xb6: { /* invokevirtual */
            uint16_t index = fetch_u2(&f);
            const char *cls, *name, *desc;
            cf_resolve_ref(cf, index, &cls, &name, &desc);
            if (strcmp(cls, "java/io/PrintStream") == 0 &&
                (strcmp(name, "println") == 0 || strcmp(name, "print") == 0)) {
                intercept_printstream(&f, name, desc);
            } else {
                fprintf(stderr, "minijvm: unsupported invokevirtual %s.%s%s\n", cls, name, desc);
                exit(1);
            }
            break;
        }
        case 0xb8: { /* invokestatic */
            uint16_t index = fetch_u2(&f);
            const char *cls, *name, *desc;
            cf_resolve_ref(cf, index, &cls, &name, &desc);
            const char *this_class = cf_class_name(cf, cf->this_class);
            if (strcmp(cls, this_class) != 0) {
                fprintf(stderr, "minijvm: invokestatic to foreign class %s.%s%s is unsupported\n",
                        cls, name, desc);
                exit(1);
            }
            MethodInfo *callee = cf_find_method(cf, name, desc);
            if (!callee) {
                fprintf(stderr, "minijvm: method %s%s not found\n", name, desc);
                exit(1);
            }
            int nargs = descriptor_arg_count(&f, desc);
            int32_t call_args[64];
            for (int i = nargs - 1; i >= 0; i--)
                call_args[i] = pop_i(&f);
            int32_t result = interp_run(cf, callee, call_args, nargs);
            if (strchr(desc, ')')[1] != 'V')
                push_i(&f, result);
            break;
        }
        case 0xba: { /* invokedynamic */
            uint16_t index = fetch_u2(&f);
            fetch_u2(&f); /* reserved operand, must be 0 */
            const char *name, *desc;
            uint16_t bootstrap_index;
            cf_resolve_invoke_dynamic(cf, index, &name, &desc, &bootstrap_index);
            if (strcmp(name, "makeConcatWithConstants") == 0 &&
                strcmp(desc, "(I)Ljava/lang/String;") == 0) {
                const char *recipe = cf_bootstrap_string_arg(cf, bootstrap_index);
                int32_t v = pop_i(&f);
                push(&f, (Slot)string_concat_i(recipe, v));
            } else {
                fprintf(stderr, "minijvm: unsupported invokedynamic %s%s\n", name, desc);
                exit(1);
            }
            break;
        }

        default:
            trap(&f, "unimplemented opcode %s (0x%02x)", opcode_name(op), op);
        }
    }
}
