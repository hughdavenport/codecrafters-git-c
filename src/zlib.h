#ifndef ZLIB_H
#define ZLIB_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

typedef enum {
    ZLIB_HEADER,
    ZLIB_DICT,
    ZLIB_DEFLATE,
    ZLIB_DECOMPRESSED,
} zlib_state;

typedef enum {
    ZLIB_FASTEST_COMPRESSOR,
    ZLIB_FAST_COMPRESSOR,
    ZLIB_DEFAULT_COMPRESSOR,
    ZLIB_MAX_COMPRESSOR,
} zlib_compression_level;

typedef struct {
    uint8_t *data;
    uint8_t index;
    long size;
} zlib_bitstream;

typedef struct {
    size_t size;
    size_t capacity;
    uint8_t *data;
} zlib_array;

typedef struct {
    zlib_state state;
    zlib_compression_level flevel;
    zlib_bitstream bits;
    zlib_array decompressed;
} zlib_context;

bool zlib_decompress(uint8_t *data, long size, zlib_data_array *ret);

#endif // ZLIB_H

#ifdef ZLIB_IMPLEMENTATION

#ifndef UNREACHABLE
#define UNREACHABLE() do { fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__); fflush(stderr); abort(); } while (0)
#endif
#ifndef UNIMPLENTED
#define UNIMPLENTED() do { fprintf(stderr, "%s:%d: UNIMPLENTED %s\n", __FILE__, __LINE__, __func__); fflush(stderr); abort(); } while (0)
#endif
#ifndef INFO
#define INFO(fmt, ...) do { fprintf(stderr, "%s:%d: INFO: " fmt, __FILE__, __LINE__, ##__VA_ARGS__); fflush(stderr); } while (0)
#endif
#ifndef WARN
#define WARN(fmt, ...) do { fprintf(stderr, "%s:%d: WARN: " fmt, __FILE__, __LINE__, ##__VA_ARGS__); fflush(stderr); } while (0)
#endif

#ifndef C_ARRAY_LEN
#define C_ARRAY_LEN(arr) (sizeof((arr))/(sizeof((arr)[0])))
#endif

#define ZLIB_DECOMPRESS_APPEND(ctx, v) do { \
    if ((ctx).decompressed.size + 1 > (ctx).decompressed.capacity) { \
        size_t new_cap = (ctx).decompressed.capacity == 0 ? 16 : (ctx).decompressed.capacity * 2; \
        (ctx).decompressed.data = realloc((ctx).decompressed.data, new_cap * sizeof((ctx).decompressed.data[0])); \
        assert((ctx).decompressed.data != NULL); \
        (ctx).decompressed.capacity = new_cap; \
    } \
    (ctx).decompressed.data[(ctx).decompressed.size ++] = (v); \
} while (0)

#define PRINT_BITS(num, bits) do { \
    uint64_t n = (num); \
    for (size_t i = 0; i < (bits); i ++) { \
        fprintf(stderr, "%ld", (n >> ((bits) - i - 1)) & 0x1); \
    } \
    fprintf(stderr, "\n"); \
    fflush(stderr); \
} while (0)

typedef struct {
    uint64_t length;
    uint64_t code;
} huffman_tree_entry;

typedef struct {
    huffman_tree_entry *data;
    size_t length;
} huffman_tree;

typedef enum {
    DEFLATE_NO_COMPRESSION,
    DEFLATE_FIXED_COMPRESSION,
    DEFLATE_DYNAMIC_COMPRESSION,
    DEFLATE_RESERVED,
} deflate_t;

uint8_t zlib_next(bitstream *stream) {
    if (stream == NULL || stream->data == NULL || stream->size == 0) {
        return EOF;
    }
    assert(stream->index <= 7);
    uint8_t ret = (stream->data[0] >> stream->index) & 0x1;
    stream->index ++;
    if (stream->index == 8) {
        stream->size --;
        stream->data ++;
        stream->index = 0;
    }
    /* INFO("zlib_next: ret %d, index = %d, size = %lu\n", ret, stream->index, stream->size); */
    return ret;
}

uint64_t zlib_next_bits(bitstream *stream, uint8_t bits) {
    assert(bits <= 63);
    uint64_t ret = 0;
    uint8_t i = 0;
    while (i < bits) {
        uint8_t next = zlib_next(ctx);
        if (next > 1) return EOF;
        assert((next & ~0x1) == 0);
        ret = (ret << 1) | next;
        i ++;
    }
    /* INFO("zlib_next_bits(%d): 0x%lx, 0b", bits, ret); */
    /* PRINT_BITS(ret, bits); */
    return ret;
}

uint64_t zlib_next_bits_rev(bitstream *stream, uint8_t bits) {
    assert(bits <= 63);
    uint64_t ret = 0;
    uint8_t i = 0;
    while (i < bits) {
        uint8_t next = zlib_next(ctx);
        if (next > 1) return EOF;
        assert((next & ~0x1) == 0);
        ret = (next << (i++)) | ret;
    }
    /* INFO("zlib_next_bits_rev(%d): 0x%lx, 0b", bits, ret); */
    /* PRINT_BITS(ret, bits); */
    return ret;
}


bool deflate_no_compression(bitstream *stream, zlib_data_array *ret) {
    UNIMPLENTED();(void)stream;(void)ret;
    return false;
}

bool deflate_fixed_compression(bitstream *stream, zlib_data_array *ret) {
    for (;;) {
        // RFC 1951 - 3.2.6 Compression with fixed Huffman codes (BTYPE=01)
        uint64_t code = zlib_next_bits(ctx, 7);
        if ((int)code == EOF) return false;
        if (code <= 0x17) {
            // 256 - 279, codes 0b0000000 - 0b0010111
            /* INFO("256 - 279, codes 0b0000000 - 0b0010111\n"); */
            code += 256;
        } else {
            code <<= 1;
            uint64_t next = zlib_next(ctx);
            if ((int)next == EOF) return false;
            code |= next;
            assert(code >= 0x30);
            if (code <= 0xBF) {
                // 0 - 143, codes 0b00110000 - 0b10111111
                /* INFO("0 - 143, codes 0b00110000 - 0b10111111\n"); */
                code -= 0x30;
            } else if (code <= 0xC7) {
                // 280 - 287, codes 0b11000000 - 0b11000111
                /* INFO("280 - 287, codes 0b11000000 - 0b11000111\n"); */
                code -= 0xc0;
                code += 280;
            } else {
                code <<= 1;
                uint64_t next = zlib_next(ctx);
                if ((int)next == EOF) return false;
                code |= next;
                assert(code >= 0x190);
                assert(code <= 0x1ff);
                // 144 - 255, codes 0b110010000 - 0b111111111
                /* INFO("144 - 255, codes 0b110010000 - 0b111111111\n"); */
                code -= 0x190;
                code += 144;
            }
        }
        /* INFO("Read code %lx\n", code); */
        /* PRINT_BITS(code, 9); */

        // RFC 1951 - 3.2.5. Compressed blocks (length and distance codes)
        if (code < 256) {
            ZLIB_ARRAY_APPEND(*ret, code);
        } else if (code == 256) {
            return true;
        } else {
            UNIMPLENTED();
        }
    }
    UNREACHABLE();
}

bool deflate_dynamic_compression_build_tree(bitstream *stream, huffman_tree *tree, zlib_data_array *ret) {
    // This may need the bits swapped?
    // I was working through this, but turns out the reference implementation uses fixed for my input while stepping through debugger
    UNIMPLENTED();(void)ret;

    uint8_t hlit = zlib_next_bits(ctx, 5);
    uint8_t hdist = zlib_next_bits(ctx, 5);
    uint8_t hclen = zlib_next_bits(ctx, 4);
    INFO("Deflating dynamic huffman code, hlit = %d hdist = %d hclen = %d\n", hlit, hdist, hclen);
    uint8_t code_lengths[19] = {0};
    uint8_t order[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    assert(C_ARRAY_LEN(order) == C_ARRAY_LEN(code_lengths));
    assert((size_t)hclen + 4 <= C_ARRAY_LEN(code_lengths));
    uint8_t max_code = 0;
    for (size_t i = 0 ; i < (size_t)hclen + 4; i ++) {
        assert(order[i] < C_ARRAY_LEN(code_lengths));
        uint8_t code_len = zlib_next_bits(ctx, 3);
        code_lengths[order[i]] = code_len;
        if (code_len > 0 && order[i] > max_code) {
            max_code = order[i];
        }
    }
    fprintf(stderr, "max_code %d\n", max_code);
    tree->data = calloc(max_code + 1, sizeof(huffman_tree));
    if (tree->data == NULL) return false;
    tree->length = max_code + 1;

    for (size_t i = 0; i <= max_code; i ++) {
        tree->data[i].length = code_lengths[i];
    }

    /* RFC 1951 - 3.2.2 - Step 1 */
    uint8_t bl_count[8] = {0};
    uint8_t max_bits = 0;
    for (size_t i = 0 ; i < C_ARRAY_LEN(code_lengths); i ++) {
        if (code_lengths[i] == 0) continue;
        assert(code_lengths[i] < C_ARRAY_LEN(bl_count));
        bl_count[code_lengths[i]] ++;
        if (code_lengths[i] > max_bits) {
            max_bits = code_lengths[i];
        }

        fprintf(stderr, "code_lengths[%ld] = %d\n", i, code_lengths[i]);
    }

    /* RFC 1951 - 3.2.2 - Step 2 */
    uint64_t code = 0;
    uint64_t next_code[9] = {0};
    assert(max_bits < C_ARRAY_LEN(next_code));
    assert(max_bits <= C_ARRAY_LEN(bl_count));
    bl_count[0] = 0;
    for (uint8_t bits = 1; bits <= max_bits; bits ++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;

        fprintf(stderr, "next_code[%d] = 0x%02lx\n", bits, next_code[bits]);
    }

    /* RFC 1951 - 3.2.2 - Step 3 */
    for (uint8_t n = 0; n <= max_code; n ++) {
        uint8_t len = tree->data[n].length;
        if (len > 0) {
            tree->data[n].code = next_code[len];
            next_code[len] ++;
        }
    }
    UNIMPLENTED();
    return false;
}

bool deflate_block(bitstream *stream, zlib_data_array *ret) {
    uint8_t bfinal = 0;
    do {
        bfinal = zlib_next(ctx, 1);
        uint8_t btype = zlib_next_bits_rev(ctx, 2);

        /* INFO("Deflating block, bfinal = %d, btype = %x\n", bfinal, btype); */
        assert(btype <= DEFLATE_RESERVED);

        switch ((deflate_t)btype) {
            case DEFLATE_NO_COMPRESSION:
                if (!deflate_no_compression(stream, ret)) return false;
                break;

            case DEFLATE_FIXED_COMPRESSION:
                if (!deflate_fixed_compression(stream, ret)) return false;
                break;

            case DEFLATE_DYNAMIC_COMPRESSION: {
                huffman_tree tree = {0};
                if (!deflate_dynamic_compression_build_tree(stream, &tree, ret)) return false;

                INFO("Huffman tree of dynamic compression\n");
                for (size_t i = 0; i < tree.length; i ++) {
                    fprintf(stderr, "Tree[%ld] = {.length = %ld, .code = 0x%02lx}\n",
                            i, tree.data[i].length, tree.data[i].code);
                }

                free(tree.data);
            }; break;

            case DEFLATE_RESERVED:
                UNREACHABLE();
                return false;

            default:
                UNREACHABLE();
                return false;
        }
    } while (bfinal != 1);
    return true;
}

bool zlib_decompress(zlib_context *ctx) {
    assert(size >= 2);

    uint8_t cmf = data[0];

    uint8_t cm = cmf & 0xF;
    uint8_t cinfo = (cmf & 0xF0) >> 4;

    assert(cm == 8);
    assert(cinfo <= 7);
    if (cinfo != 7) UNIMPLENTED();

    uint8_t flg = data[1];
    uint16_t check = (uint16_t)cmf * 256 + (uint8_t)flg;
    assert(check % 31 == 0);

    uint8_t fdict = (flg >> 5) & 0x1;
    uint8_t flevel = (flg >> 6) & 0x3;
    assert(flevel <= ZLIB_MAX_COMPRESSOR);

    if (fdict != 0) UNIMPLENTED();
    if (flevel != 0) {
        WARN("FLEVEL not 0. Not used in decompression, but useful for recompression. Ignoring value (%d)\n", flevel);
    }

    bitstream bits = {
        .data = data + 2,
        .size = size - 2,
    };

    if (!deflate_block(&bits, ret)) {
        return false;
    }

    uint16_t s1 = 1;
    uint16_t s2 = 0;
    for (size_t i = 0; i < ret->size; i ++) {
        s1 = ((uint32_t)s1 + ret->data[i]) % 65521;
        s2 = ((uint32_t)s2 + s1) % 65521;
    }

    // FIXME check adler-32 code

    return true;
}

#endif
