#include "jar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xmalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fprintf(stderr, "minijvm: out of memory\n");
        exit(1);
    }
    return p;
}

/* Zip stores its integers little-endian, unlike class files. */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---------------------------------------------------------------------------
 * DEFLATE (RFC 1951)
 *
 * The jar tool compresses entries with deflate, so a zip reader needs an
 * inflater. This is the canonical Huffman-table decoder: symbols are kept in
 * code-length order, so decoding walks lengths 1..15 accumulating bits until
 * the code falls inside the range held by that length.
 * ------------------------------------------------------------------------- */

#define MAX_BITS 15

typedef struct {
    const uint8_t *in;
    size_t in_len;
    size_t in_pos;
    long bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t out_len;
    size_t out_pos;
    int failed; /* set on truncated input, so callers can stop early */
} Inflate;

typedef struct {
    short count[MAX_BITS + 1]; /* how many symbols use each code length */
    short symbol[288];         /* symbols ordered by code length */
} Huffman;

static int bits(Inflate *s, int need) {
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->in_pos >= s->in_len) {
            s->failed = 1;
            return 0;
        }
        val |= (long)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static int decode(Inflate *s, const Huffman *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAX_BITS; len++) {
        code |= bits(s, 1);
        if (s->failed) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1; /* code longer than any in the table */
}

/* Build a decoding table from a list of code lengths. Returns 0 for a
 * complete code, >0 for an incomplete one, <0 if it is over-subscribed. */
static int construct(Huffman *h, const short *length, int n) {
    for (int len = 0; len <= MAX_BITS; len++) h->count[len] = 0;
    for (int symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0; /* no codes at all */

    int left = 1;
    for (int len = 1; len <= MAX_BITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }

    short offs[MAX_BITS + 1];
    offs[1] = 0;
    for (int len = 1; len < MAX_BITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;

    return left;
}

/* Length and distance bases, with the number of extra bits each one reads. */
static const short len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                     193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                     6145, 8193, 12289, 16385, 24577 };
static const short dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(Inflate *s, const Huffman *lencode, const Huffman *distcode) {
    for (;;) {
        int symbol = decode(s, lencode);
        if (symbol < 0) return -1;

        if (symbol < 256) {
            if (s->out_pos >= s->out_len) return -1;
            s->out[s->out_pos++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256) return 0; /* end of block */

        symbol -= 257;
        if (symbol >= 29) return -1;
        int len = len_base[symbol] + bits(s, len_extra[symbol]);

        symbol = decode(s, distcode);
        if (symbol < 0 || symbol >= 30) return -1;
        int dist = dist_base[symbol] + bits(s, dist_extra[symbol]);
        if (s->failed) return -1;

        /* Matches may reach back into what we just wrote, and may overlap the
         * current position, so copy one byte at a time rather than memcpy. */
        if ((size_t)dist > s->out_pos) return -1;
        if (s->out_pos + (size_t)len > s->out_len) return -1;
        for (int i = 0; i < len; i++) {
            s->out[s->out_pos] = s->out[s->out_pos - (size_t)dist];
            s->out_pos++;
        }
    }
}

static void build_fixed(Huffman *lencode, Huffman *distcode) {
    short lengths[288];
    int symbol = 0;
    for (; symbol < 144; symbol++) lengths[symbol] = 8;
    for (; symbol < 256; symbol++) lengths[symbol] = 9;
    for (; symbol < 280; symbol++) lengths[symbol] = 7;
    for (; symbol < 288; symbol++) lengths[symbol] = 8;
    construct(lencode, lengths, 288);

    for (symbol = 0; symbol < 30; symbol++) lengths[symbol] = 5;
    construct(distcode, lengths, 30);
}

/* The code lengths of the code-length code arrive in this order. */
static const short clen_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                      11, 4, 12, 3, 13, 2, 14, 1, 15 };

static int dynamic_block(Inflate *s) {
    int nlen = bits(s, 5) + 257;
    int ndist = bits(s, 5) + 1;
    int ncode = bits(s, 4) + 4;
    if (s->failed || nlen > 286 || ndist > 30) return -1;

    /* First the code that describes the other two codes' lengths. */
    short lengths[286 + 30];
    memset(lengths, 0, sizeof lengths);
    for (int i = 0; i < ncode; i++) lengths[clen_order[i]] = (short)bits(s, 3);
    if (s->failed) return -1;

    Huffman lencode, distcode;
    if (construct(&lencode, lengths, 19) != 0) return -1; /* must be complete */

    /* Then the literal/length and distance lengths, run-length encoded. */
    int index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        if (symbol < 0) return -1;

        if (symbol < 16) {
            lengths[index++] = (short)symbol;
            continue;
        }

        short value = 0;
        int repeat;
        if (symbol == 16) { /* repeat the previous length */
            if (index == 0) return -1;
            value = lengths[index - 1];
            repeat = 3 + bits(s, 2);
        } else if (symbol == 17) { /* a run of zeros */
            repeat = 3 + bits(s, 3);
        } else {
            repeat = 11 + bits(s, 7);
        }
        if (s->failed || index + repeat > nlen + ndist) return -1;
        while (repeat--) lengths[index++] = value;
    }
    if (lengths[256] == 0) return -1; /* no end-of-block symbol */

    if (construct(&lencode, lengths, nlen) < 0) return -1;
    if (construct(&distcode, lengths + nlen, ndist) < 0) return -1;
    return codes(s, &lencode, &distcode);
}

static int stored_block(Inflate *s) {
    s->bitbuf = 0;
    s->bitcnt = 0; /* stored blocks are byte-aligned */
    if (s->in_pos + 4 > s->in_len) return -1;

    unsigned len = rd16(s->in + s->in_pos);
    unsigned nlen = rd16(s->in + s->in_pos + 2);
    s->in_pos += 4;
    if (nlen != (~len & 0xffffu)) return -1;
    if (s->in_pos + len > s->in_len || s->out_pos + len > s->out_len) return -1;

    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
    return 0;
}

/* Raw deflate stream (no zlib header) into a buffer of known size. */
static int inflate_raw(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
    Inflate s;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.in_len = in_len;
    s.out = out;
    s.out_len = out_len;

    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.failed) return -1;

        int err;
        if (type == 0) {
            err = stored_block(&s);
        } else if (type == 1) {
            Huffman lencode, distcode;
            build_fixed(&lencode, &distcode);
            err = codes(&s, &lencode, &distcode);
        } else if (type == 2) {
            err = dynamic_block(&s);
        } else {
            return -1; /* reserved block type */
        }
        if (err != 0) return -1;
    } while (!last);

    return s.out_pos == out_len ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Zip
 * ------------------------------------------------------------------------- */

#define SIG_EOCD  0x06054b50u
#define SIG_CDIR  0x02014b50u
#define SIG_LOCAL 0x04034b50u

#define METHOD_STORED  0
#define METHOD_DEFLATE 8

struct Jar {
    char *path;
    uint8_t *data;
    size_t size;
    size_t cd_offset;   /* start of the central directory */
    uint16_t entries;   /* how many records it holds */
};

static void jar_die(const Jar *jar, const char *msg) {
    fprintf(stderr, "minijvm: %s: %s\n", jar->path, msg);
    exit(1);
}

/* The end-of-central-directory record sits last, but a trailing archive
 * comment may follow it, so scan backwards for the signature. */
static int find_eocd(const uint8_t *data, size_t size, size_t *offset) {
    if (size < 22) return 0;
    size_t limit = size < 22 + 0xffff ? size : 22 + 0xffff;
    for (size_t back = 22; back <= limit; back++) {
        size_t p = size - back;
        if (rd32(data + p) == SIG_EOCD) {
            *offset = p;
            return 1;
        }
    }
    return 0;
}

Jar *jar_open(const char *path) {
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

    Jar *jar = xmalloc(sizeof(Jar));
    jar->path = xmalloc(strlen(path) + 1);
    strcpy(jar->path, path);
    jar->size = (size_t)size;
    jar->data = xmalloc(jar->size);
    if (fread(jar->data, 1, jar->size, f) != jar->size) {
        fclose(f);
        jar_die(jar, "cannot read file");
    }
    fclose(f);

    size_t eocd;
    if (!find_eocd(jar->data, jar->size, &eocd))
        jar_die(jar, "not a jar (no zip central directory found)");

    jar->entries = rd16(jar->data + eocd + 10);
    jar->cd_offset = rd32(jar->data + eocd + 16);
    /* Both fields are saturated when the real values live in a zip64 record,
     * which nothing here knows how to read. */
    if (jar->entries == 0xffff || jar->cd_offset == 0xffffffffu)
        jar_die(jar, "zip64 jars are not supported");
    if (jar->cd_offset > jar->size) jar_die(jar, "central directory offset out of range");

    return jar;
}

void jar_close(Jar *jar) {
    if (!jar) return;
    free(jar->data);
    free(jar->path);
    free(jar);
}

/* Locate one entry in the central directory. */
static int find_entry(Jar *jar, const char *name, uint16_t *method,
                      uint32_t *comp_size, uint32_t *uncomp_size, uint32_t *local_offset) {
    size_t name_len = strlen(name);
    size_t p = jar->cd_offset;

    for (uint16_t i = 0; i < jar->entries; i++) {
        if (p + 46 > jar->size || rd32(jar->data + p) != SIG_CDIR) return 0;
        uint16_t n = rd16(jar->data + p + 28);
        uint16_t extra = rd16(jar->data + p + 30);
        uint16_t comment = rd16(jar->data + p + 32);
        if (p + 46 + n > jar->size) return 0;

        if (n == name_len && memcmp(jar->data + p + 46, name, name_len) == 0) {
            *method = rd16(jar->data + p + 10);
            *comp_size = rd32(jar->data + p + 20);
            *uncomp_size = rd32(jar->data + p + 24);
            *local_offset = rd32(jar->data + p + 42);
            return 1;
        }
        p += 46u + n + extra + comment;
    }
    return 0;
}

uint8_t *jar_read(Jar *jar, const char *name, size_t *size_out) {
    uint16_t method;
    uint32_t comp_size, uncomp_size, local_offset;
    if (!find_entry(jar, name, &method, &comp_size, &uncomp_size, &local_offset))
        return NULL;

    /* The local header repeats the name and may carry a different extra
     * field, so the data offset has to be computed from it, not from the
     * central directory record. */
    if (local_offset + 30 > jar->size || rd32(jar->data + local_offset) != SIG_LOCAL)
        jar_die(jar, "bad local file header");
    uint16_t local_name_len = rd16(jar->data + local_offset + 26);
    uint16_t local_extra_len = rd16(jar->data + local_offset + 28);
    size_t data = local_offset + 30u + local_name_len + local_extra_len;
    if (data + comp_size > jar->size) jar_die(jar, "entry data overruns file");

    uint8_t *out = xmalloc((size_t)uncomp_size + 1); /* room for a NUL */
    if (method == METHOD_STORED) {
        if (comp_size != uncomp_size) jar_die(jar, "stored entry has mismatched sizes");
        memcpy(out, jar->data + data, uncomp_size);
    } else if (method == METHOD_DEFLATE) {
        if (inflate_raw(jar->data + data, comp_size, out, uncomp_size) != 0) {
            fprintf(stderr, "minijvm: %s: cannot inflate %s\n", jar->path, name);
            exit(1);
        }
    } else {
        fprintf(stderr, "minijvm: %s: %s uses unsupported compression method %u\n",
                jar->path, name, method);
        exit(1);
    }
    out[uncomp_size] = '\0';

    if (size_out) *size_out = uncomp_size;
    return out;
}

/* ---------------------------------------------------------------------------
 * Manifest
 * ------------------------------------------------------------------------- */

/* Manifest headers wrap at 72 bytes, and a line starting with a single space
 * continues the one before it. Join those back together first. */
static char *manifest_unfold(const char *src, size_t n) {
    char *out = xmalloc(n + 2);
    size_t o = 0, i = 0;

    while (i < n) {
        size_t start = i;
        while (i < n && src[i] != '\n' && src[i] != '\r') i++;
        size_t end = i;
        if (i < n && src[i] == '\r') i++;
        if (i < n && src[i] == '\n') i++;

        if (start < end && src[start] == ' ') {
            if (o > 0 && out[o - 1] == '\n') o--; /* undo the previous break */
            memcpy(out + o, src + start + 1, end - start - 1);
            o += end - start - 1;
        } else {
            memcpy(out + o, src + start, end - start);
            o += end - start;
        }
        out[o++] = '\n';
    }
    out[o] = '\0';
    return out;
}

/* Header names are case-insensitive. */
static int header_matches(const char *line, const char *key, size_t key_len) {
    for (size_t i = 0; i < key_len; i++) {
        char a = line[i], b = key[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return line[key_len] == ':';
}

static char *manifest_value(const char *unfolded, const char *key) {
    size_t key_len = strlen(key);

    for (const char *p = unfolded; *p;) {
        const char *eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);

        if (line_len > key_len && header_matches(p, key, key_len)) {
            const char *v = p + key_len + 1;
            size_t v_len = line_len - key_len - 1;
            while (v_len && (*v == ' ' || *v == '\t')) { v++; v_len--; }
            while (v_len && (v[v_len - 1] == ' ' || v[v_len - 1] == '\t')) v_len--;

            char *out = xmalloc(v_len + 1);
            memcpy(out, v, v_len);
            out[v_len] = '\0';
            return out;
        }

        if (!eol) break;
        p = eol + 1;
    }
    return NULL;
}

char *jar_main_class(Jar *jar) {
    size_t size;
    uint8_t *manifest = jar_read(jar, "META-INF/MANIFEST.MF", &size);
    if (!manifest) return NULL;

    char *unfolded = manifest_unfold((const char *)manifest, size);
    char *main_class = manifest_value(unfolded, "Main-Class");

    free(unfolded);
    free(manifest);
    return main_class;
}
