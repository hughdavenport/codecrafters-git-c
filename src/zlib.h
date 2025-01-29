#ifndef ZLIB_H
#define ZLIB_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    DEFLATE_NO_COMPRESSION,
    DEFLATE_FIXED_COMPRESSION,
    DEFLATE_DYNAMIC_COMPRESSION,
    DEFLATE_RESERVED,
} deflate_t;

typedef enum {
    DEFLATE_HEADER,
    DEFLATE_UNCOMPRESSED,
    DEFLATE_UNCOMPRESSED_DATA,
    DEFLATE_COMPRESSED_DYNAMIC,
    DEFLATE_COMPRESSED_DYNAMIC_LITERAL,
    DEFLATE_COMPRESSED_DYNAMIC_LENGTH,
    DEFLATE_COMPRESSED_FIXED,
    DEFLATE_COMPRESSED_FIXED_LENGTH,
    DEFLATE_COMPRESSED_FIXED_LENGTH_EXTRA,
    DEFLATE_COMPRESSED_FIXED_DISTANCE,
    DEFLATE_COMPRESSED_FIXED_DISTANCE_EXTRA,
    DEFLATE_COMPRESSED_FIXED_COPY,
    DEFLATE_FINISHED,

    DEFLATE_ERROR, // Keep this at the end. _Static_asserts count on it
} deflate_state;

typedef struct {
    uint8_t *data;
    uint8_t index;
    size_t size;
} deflate_bitstream;

typedef struct {
    size_t size;
    size_t capacity;
    uint8_t *data;
} deflate_array;

typedef struct {
    deflate_state state;
    deflate_bitstream bits;
    deflate_array decompressed;
    uint32_t saved;
    bool last;
} deflate_context;

typedef enum {
    ZLIB_HEADER,
    ZLIB_DICT,
    ZLIB_DEFLATE,
    ZLIB_ADLER,
    ZLIB_FINISHED,

    ZLIB_ERROR, // Keep this at the end. _Static_asserts count on it
} zlib_state;

typedef enum {
    ZLIB_FASTEST_COMPRESSOR,
    ZLIB_FAST_COMPRESSOR,
    ZLIB_DEFAULT_COMPRESSOR,
    ZLIB_MAX_COMPRESSOR,
} zlib_compression_level;

typedef struct {
    zlib_state state;
    zlib_compression_level flevel;
    deflate_context deflate;
} zlib_context;

bool zlib_decompress(zlib_context *ctx);

void hexdump(uint8_t *data, size_t len);

#endif // ZLIB_H

#ifdef ZLIB_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#ifndef UNREACHABLE
#define UNREACHABLE() do { fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__); fflush(stderr); abort(); } while (0)
#endif
#ifndef UNIMPLENTED
#define UNIMPLENTED(fmt, ...) do { fprintf(stderr, "%s:%d: UNIMPLENTED %s: " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); fflush(stderr); abort(); } while (0)
#endif
#ifndef INFO
#define INFO(fmt, ...) do { fprintf(stderr, "%s:%d: INFO: " fmt, __FILE__, __LINE__, ##__VA_ARGS__); fflush(stderr); } while (0)
#endif // INFO
#ifndef WARN
#define WARN(fmt, ...) do { fprintf(stderr, "%s:%d: WARN: " fmt, __FILE__, __LINE__, ##__VA_ARGS__); fflush(stderr); } while (0)
#endif

#ifndef C_ARRAY_LEN
#define C_ARRAY_LEN(arr) (sizeof((arr))/(sizeof((arr)[0])))
#endif

void hexdump(uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i ++) {
        if (i % 16 == 0) fprintf(stderr, "%08lx:", i);

        if (i % 2 == 0) fprintf(stderr, " ");
        switch (data[i]) {
            case 0x00:
                fprintf(stderr, "\033[1;37m");
                break;
            case '\t':
            case '\n':
            case '\r':
                fprintf(stderr, "\033[1;33m");
                break;

            default:
                if (isprint(data[i])) {
                    fprintf(stderr, "\033[1;32m");
                } else {
                    fprintf(stderr, "\033[1;31m");
                }
        }
        fprintf(stderr, "%02x\033[0m", data[i]);
        if (i % 16 == 15) fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

#define DEFLATE_BITS(ctx) ((ctx)->bits.size * 8 - (ctx)->bits.index)

#define DEFLATE_CLEAR_BITS(ctx) do { if ((ctx)->bits.index > 0) { (ctx)->bits.index = 0; (ctx)->bits.data ++; (ctx)->bits.size --; } } while (0)

#define DEFLATE_BYTES(ctx) (ctx)->bits.size

#define DEFLATE_DECOMPRESS_ENSURE(ctx, inc) do { \
    if ((ctx)->decompressed.size + (inc) > (ctx)->decompressed.capacity) { \
        size_t new_cap = (ctx)->decompressed.capacity == 0 ? 16 : (ctx)->decompressed.capacity * 2; \
        while (new_cap < (inc)) new_cap *= 2; \
        (ctx)->decompressed.data = realloc((ctx)->decompressed.data, new_cap * sizeof((ctx)->decompressed.data[0])); \
        assert((ctx)->decompressed.data != NULL); \
        (ctx)->decompressed.capacity = new_cap; \
    } \
} while (0)

#define DEFLATE_DECOMPRESS_APPEND(ctx, v) do { \
    DEFLATE_DECOMPRESS_ENSURE(ctx, 1); \
    /* if (isprint((v))) { INFO("Added '%c'\n", (v)); } else { INFO("Added 0x%02x\n", (v)); } */ \
    (ctx)->decompressed.data[(ctx)->decompressed.size ++] = (v); \
} while (0)

#define DEFLATE_DECOMPRESS_APPEND_BYTES(ctx, src, len) do { \
    DEFLATE_DECOMPRESS_ENSURE(ctx, (len)); \
    /* INFO("Appending bytes\n"); hexdump((src), (len)); */ \
    memcpy((ctx)->decompressed.data + (ctx)->decompressed.size, (src), (len)); \
    (ctx)->decompressed.size += (len); \
} while (0)

#ifndef PRINT_BITS
#define PRINT_BITS(num, bits) do { \
    uint64_t n = (num); \
    for (size_t i = 0; i < (bits); i ++) { \
        fprintf(stderr, "%ld", (n >> ((bits) - i - 1)) & 0x1); \
    } \
    fprintf(stderr, "\n"); \
    fflush(stderr); \
} while (0)
#endif // PRINT_BITS

typedef struct {
    uint64_t length;
    uint64_t code;
} huffman_tree_entry;

typedef struct {
    huffman_tree_entry *data;
    size_t length;
} huffman_tree;

uint8_t deflate_next(deflate_context *ctx) {
    assert(ctx != NULL);
    assert(ctx->bits.data != NULL);
    if (ctx->bits.size == 0) {
        return EOF;
    }
    assert(ctx->bits.index <= 7);
    uint8_t ret = (ctx->bits.data[0] >> ctx->bits.index) & 0x1;
    ctx->bits.index ++;
    if (ctx->bits.index == 8) {
        ctx->bits.size --;
        ctx->bits.data ++;
        ctx->bits.index = 0;
    }
    /* INFO("deflate_next: ret %d, index = %d, size = %lu\n", ret, ctx->bits.index, ctx->bits.size); */
    return ret;
}

uint64_t _deflate_reverse_bits(uint64_t num, size_t idx, size_t bits) {
    assert(bits <= 63);
    if (bits <= 1) return num;
    uint64_t fullmask = ((1UL << bits) - 1) << idx;
    uint64_t extra = num & ~fullmask;
    uint64_t highmask = ((1UL << (bits / 2)) - 1) << (bits / 2 + bits % 2 + idx);
    uint64_t lowmask = ((1UL << (bits / 2)) - 1) << idx;
    uint64_t midmask = ((uint64_t)(bits % 2) << (bits / 2)) << idx;
    extra |= num & midmask;
    fullmask &= ~midmask;
    num = (num & highmask) >> (bits / 2 + bits % 2) | (num & lowmask) << (bits / 2 + bits % 2);
    num = (_deflate_reverse_bits(num, idx, bits / 2) & lowmask) | (_deflate_reverse_bits(num, idx + bits / 2 + bits % 2, bits / 2) & highmask);
    num |= extra;
    return num;
}

uint64_t deflate_reverse_bits(uint64_t num, size_t bits) {
    assert(bits <= 63);
    return _deflate_reverse_bits(num, 0, bits);
}

uint64_t _deflate_peek_bits_rev(deflate_context *ctx, size_t bits) {
    if (DEFLATE_BITS(ctx) < bits) return EOF;
    assert(bits <= 63);
    uint8_t bytes = (bits + ctx->bits.index + 7) / 8;
    switch (bytes) {
        case 0: return 0;
        case 1: return (*ctx->bits.data >> ctx->bits.index) & ((1UL << bits) - 1);
        case 2: return (*(uint16_t *)ctx->bits.data >> ctx->bits.index) & ((1UL << bits) - 1);
        case 3: return ((((uint64_t)*ctx->bits.data << 16) | (*(uint16_t *)(ctx->bits.data + 1))) >> ctx->bits.index) & ((1UL << bits) - 1);
        case 4: return (*(uint32_t *)ctx->bits.data >> ctx->bits.index) & ((1UL << bits) - 1);
        case 5: return (((uint64_t)*ctx->bits.data << 32 | (*(uint32_t *)(ctx->bits.data + 1))) >> ctx->bits.index) & ((1UL << bits) - 1);
        case 6: return (((uint64_t)*(uint16_t *)ctx->bits.data << 32 | (*(uint32_t *)(ctx->bits.data + 2))) >> ctx->bits.index) & ((1UL << bits) - 1);
        case 7: return ((((uint64_t)*ctx->bits.data << 48) | ((uint64_t)*(uint16_t *)(ctx->bits.data + 1) << 32) | (*(uint32_t *)(ctx->bits.data + 3))) >> ctx->bits.index) & ((1UL << bits) - 1);
        case 8: return ((*(uint64_t *)ctx->bits.data) >> ctx->bits.index) & ((1UL << bits) - 1);
        default:
            UNREACHABLE();
            ctx->state = DEFLATE_ERROR;
            return 0;
    }
}

uint64_t deflate_peek_bits(deflate_context *ctx, size_t bits) {
    if (DEFLATE_BITS(ctx) < bits) return EOF;
    assert(bits <= 63);
    /* uint64_t ret = deflate_reverse_bits(_deflate_peek_bits_rev(ctx, bits), bits); */
    /* INFO("deflate_peek_bits(%ld), 0x%lx, 0b", bits, ret); */
    /* PRINT_BITS(ret, bits); */
    return deflate_reverse_bits(_deflate_peek_bits_rev(ctx, bits), bits);
}

void deflate_drop_bits(deflate_context *ctx, size_t bits) {
    assert(bits <= 63);
    assert(DEFLATE_BITS(ctx) >= bits);
    ctx->bits.index += bits;
    ctx->bits.data += ctx->bits.index / 8;
    ctx->bits.size -= ctx->bits.index / 8;
    ctx->bits.index %= 8;
}

uint64_t deflate_next_bits(deflate_context *ctx, size_t bits) {
    assert(bits <= 63);
    if (DEFLATE_BITS(ctx) < bits) return EOF;
    uint64_t ret = deflate_peek_bits(ctx, bits);
    deflate_drop_bits(ctx, bits);
    /* INFO("deflate_next_bits(%ld), 0x%lx, 0b", bits, ret); */
    /* PRINT_BITS(ret, bits); */
    return ret;
}

uint64_t deflate_next_bits_rev(deflate_context *ctx, size_t bits) {
    assert(bits <= 63);
    if (DEFLATE_BITS(ctx) < bits) return EOF;
    uint64_t ret = _deflate_peek_bits_rev(ctx, bits);
    ctx->bits.index += bits;
    ctx->bits.data += ctx->bits.index / 8;
    ctx->bits.size -= ctx->bits.index / 8;
    ctx->bits.index %= 8;
    /* INFO("deflate_next_bits_rev(%ld), 0x%lx, 0b", bits, ret); */
    /* PRINT_BITS(ret, bits); */
    return ret;
}

uint64_t deflate_next_bytes(deflate_context *ctx, size_t bytes) {
    assert(ctx->bits.index == 0);
    if (ctx->bits.size < bytes) return EOF;
    assert(bytes <= 8);
    ctx->bits.data += bytes;
    ctx->bits.size -= bytes;
    switch (bytes) {
        case 0: return 0;
        case 1: return *(ctx->bits.data - bytes);
        case 2: return *(uint16_t *)(ctx->bits.data - bytes);
        case 3: return ((uint64_t)*(ctx->bits.data - bytes) << 16) | (*(uint16_t *)(ctx->bits.data - bytes + 1));
        case 4: return *(uint32_t *)(ctx->bits.data - bytes);
        case 5: return (uint64_t)*(ctx->bits.data - bytes) << 32 | (*(uint32_t *)(ctx->bits.data - bytes + 1));
        case 6: return (uint64_t)*(uint16_t *)(ctx->bits.data - bytes) << 32 | (*(uint32_t *)(ctx->bits.data - bytes + 2));
        case 7: return ((uint64_t)*(ctx->bits.data - bytes) << 48) | ((uint64_t)*(uint16_t *)(ctx->bits.data - bytes + 1) << 32) | (*(uint32_t *)(ctx->bits.data - bytes + 3));
        case 8: return *(uint64_t *)(ctx->bits.data - bytes);
        default: UNREACHABLE();
    }
}

bool _deflate_uncompressed(deflate_context *ctx) {
    _Static_assert(DEFLATE_ERROR == 13, "States have changed. May need handling here");
    switch (ctx->state) {
        case DEFLATE_UNCOMPRESSED: {
            DEFLATE_CLEAR_BITS(ctx);
            if (DEFLATE_BYTES(ctx) < 4) return false;
            uint16_t len = deflate_next_bytes(ctx, 2);
            int16_t nlen = deflate_next_bytes(ctx, 2);
            assert(len == (uint16_t)~nlen);

            ctx->saved = len;
            ctx->state = DEFLATE_UNCOMPRESSED_DATA;
        }; // fallthrough

        case DEFLATE_UNCOMPRESSED_DATA: {
            assert(ctx->bits.index == 0);
            size_t bytes = ctx->saved >= DEFLATE_BYTES(ctx) ? DEFLATE_BYTES(ctx) : ctx->saved;
            DEFLATE_DECOMPRESS_APPEND_BYTES(ctx, ctx->bits.data, bytes);
            ctx->bits.data += bytes;
            ctx->bits.size -= bytes;
            ctx->saved -= bytes;
            if (ctx->saved == 0) {
                ctx->state = DEFLATE_FINISHED;
                return true;
            }
            return false;
        }; break;

        default:
            UNREACHABLE();
            return false;
    }
    UNREACHABLE();
}

int _deflate_fixed_compression_code(deflate_context *ctx) {
    // RFC 1951 - 3.2.6 Compression with fixed Huffman codes (BTYPE=01)
    int code = deflate_peek_bits(ctx, 7);
    if (code == EOF) return EOF;
    /* INFO("Trying 7 bit code: "); */
    /* PRINT_BITS(code, 7); fprintf(stderr, "\n"); */
    if (code <= 0x17) {
        // 256 - 279, codes 0b0000000 - 0b0010111
        /* INFO("256 - 279, codes 0b0000000 - 0b0010111\n"); */
        code += 256;
        deflate_drop_bits(ctx, 7);
    } else {
        code = deflate_peek_bits(ctx, 8);
        if (code == EOF) return EOF;
        /* INFO("Trying 8 bit code: "); */
        /* PRINT_BITS(code, 8); fprintf(stderr, "\n"); */
        assert(code >= 0x30);
        if (code <= 0xBF) {
            // 0 - 143, codes 0b00110000 - 0b10111111
            /* INFO("0 - 143, codes 0b00110000 - 0b10111111\n"); */
            code -= 0x30;
            deflate_drop_bits(ctx, 8);
        } else if (code <= 0xC7) {
            // 280 - 287, codes 0b11000000 - 0b11000111
            /* INFO("280 - 287, codes 0b11000000 - 0b11000111\n"); */
            code -= 0xc0;
            code += 280;
            deflate_drop_bits(ctx, 8);
        } else {
            code = deflate_peek_bits(ctx, 9);
            if (code == EOF) return EOF;
            /* INFO("Trying 9 bit code: "); */
            /* PRINT_BITS(code, 9); fprintf(stderr, "\n"); */
            assert(code >= 0x190);
            assert(code <= 0x1ff);
            // 144 - 255, codes 0b110010000 - 0b111111111
            /* INFO("144 - 255, codes 0b110010000 - 0b111111111\n"); */
            code -= 0x190;
            code += 144;
            deflate_drop_bits(ctx, 9);
        }
    }
    return code;
}

int _deflate_fixed_compression_length_extra(deflate_context *ctx) {
    uint16_t code = ctx->saved;
    // RFC 1951 - 3.2.5
    assert(code >= 265 && code < 285);
    if (code < 269) {
        uint8_t offset = 1;
        uint16_t next = deflate_next_bits(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 11 + ((code - 265) << offset) + next;
    } else if (code < 273) {
        // FIXME: Does this need to be _rev
        uint8_t offset = 2;
        uint16_t next = deflate_next_bits(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 19 + ((code - 269) << offset) + next;
    } else if (code < 277) {
        // FIXME: Does this need to be _rev
        uint8_t offset = 3;
        uint16_t next = deflate_next_bits(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 35 + ((code - 273) << offset) + next;
    } else if (code < 281) {
        // FIXME: Does this need to be _rev
        uint8_t offset = 4;
        uint16_t next = deflate_next_bits(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 67 + ((code - 277) << offset) + next;
    } else if (code < 285) {
        // FIXME: Does this need to be _rev
        uint8_t offset = 5;
        uint16_t next = deflate_next_bits(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 131 + ((code - 281) << offset) + next;
    }
    UNREACHABLE();
    ctx->state = DEFLATE_ERROR;
    return EOF;
}

int _deflate_fixed_compression_dist_extra(deflate_context *ctx) {
    uint16_t len = ctx->saved >> 16;
    uint8_t code = ctx->saved & 0xFF;
    assert(len >= 3 && len <= 258);
    assert(code >= 4 && code < 30);
    // RFC 1951 - 3.2.5
    if (code < 6) {
        uint8_t offset = 1;
        uint16_t next = deflate_next_bits(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 5 + ((code - 4) << offset) + next;
    } else if (code < 8) {
        uint8_t offset = 2;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 9 + ((code - 6) << offset) + next;
    } else if (code < 10) {
        uint8_t offset = 3;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 17 + ((code - 8) << offset) + next;
    } else if (code < 12) {
        uint8_t offset = 4;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 33 + ((code - 10) << offset) + next;
    } else if (code < 14) {
        uint8_t offset = 5;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 65 + ((code - 12) << offset) + next;
    } else if (code < 16) {
        uint8_t offset = 6;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 129 + ((code - 14) << offset) + next;
    } else if (code < 18) {
        uint8_t offset = 7;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 257 + ((code - 16) << offset) + next;
    } else if (code < 20) {
        uint8_t offset = 8;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 513 + ((code - 18) << offset) + next;
    } else if (code < 22) {
        uint8_t offset = 9;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 1025 + ((code - 20) << offset) + next;
    } else if (code < 24) {
        uint8_t offset = 10;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 2049 + ((code - 22) << offset) + next;
    } else if (code < 26) {
        uint8_t offset = 11;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 4097 + ((code - 24) << offset) + next;
    } else if (code < 28) {
        uint8_t offset = 12;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 8193 + ((code - 26) << offset) + next;
    } else if (code < 30) {
        uint8_t offset = 13;
        uint16_t next = deflate_next_bits_rev(ctx, offset);
        if (next == (uint16_t)EOF) return EOF;
        return 16385 + ((code - 28) << offset) + next;
    }
    UNREACHABLE();
    ctx->state = DEFLATE_ERROR;
    return EOF;
}

bool _deflate_fixed_compression(deflate_context *ctx) {
    for (;;) {
        _Static_assert(DEFLATE_ERROR == 13, "States have changed. May need handling here");
        switch (ctx->state) {
            case DEFLATE_COMPRESSED_FIXED: {
                uint64_t code = _deflate_fixed_compression_code(ctx);
                if (code == (uint64_t)EOF) return false;

                /* INFO("Got code %ld\n", code); */
                // RFC 1951 - 3.2.5. Compressed blocks (length and distance codes)
                if (code < 256) {
                    DEFLATE_DECOMPRESS_APPEND(ctx, (uint8_t)code);
                    break;
                } else if (code == 256) {
                    ctx->state = DEFLATE_FINISHED;
                    return true;
                } else if (code > 285) {
                    UNREACHABLE();
                    ctx->state = DEFLATE_ERROR;
                    return false;
                }

                ctx->saved = code;
                ctx->state = DEFLATE_COMPRESSED_FIXED_LENGTH;
            };
                // fall through
            case DEFLATE_COMPRESSED_FIXED_LENGTH: {
                uint16_t code = ctx->saved;
                // RFC 1951 - 3.2.5
                assert(code > 256 && code < 285);
                if (code < 265) {
                    ctx->saved = 3 + code - 257;
                    ctx->state = DEFLATE_COMPRESSED_FIXED_DISTANCE;
                    break;
                } else if (code == 285) {
                    ctx->saved = 258; // Great for dsylexics. Intentionally different as per spec.
                    ctx->state = DEFLATE_COMPRESSED_FIXED_DISTANCE;
                    break;
                } else if (code > 285) {
                    UNREACHABLE();
                    ctx->state = DEFLATE_ERROR;
                    return false;
                }

                ctx->state = DEFLATE_COMPRESSED_FIXED_LENGTH_EXTRA;
            };
                // fall through
            case DEFLATE_COMPRESSED_FIXED_LENGTH_EXTRA: {
                ctx->saved = _deflate_fixed_compression_length_extra(ctx);
                ctx->state = DEFLATE_COMPRESSED_FIXED_DISTANCE;
            };
                // fall through
            case DEFLATE_COMPRESSED_FIXED_DISTANCE: {
                uint16_t len = ctx->saved;
                assert(len >= 3 && len <= 258);
                // RFC 1951 - 3.2.6
                uint8_t code = deflate_next_bits(ctx, 5);
                if (code == (uint8_t)EOF) return EOF;
                assert(code < 30);
                // RFC 1951 - 3.2.5
                if (code < 4) {
                    ctx->saved = len << 16 | (code + 1);
                    ctx->state = DEFLATE_COMPRESSED_FIXED_COPY;
                    break;
                }

                ctx->saved = len << 16 | code;
                ctx->state = DEFLATE_COMPRESSED_FIXED_DISTANCE_EXTRA;
            };
                // fall through
            case DEFLATE_COMPRESSED_FIXED_DISTANCE_EXTRA: {
                uint16_t len = ctx->saved >> 16;
                uint32_t dist = _deflate_fixed_compression_dist_extra(ctx);
                if (dist == (uint32_t)EOF) return false;
                assert(dist < 32768);

                ctx->saved = len << 16 | dist;
                ctx->state = DEFLATE_COMPRESSED_FIXED_COPY;
            };
                    // fall through
            case DEFLATE_COMPRESSED_FIXED_COPY: {
                uint16_t len = ctx->saved >> 16;
                uint32_t dist = ctx->saved & 0xFFFF;
                assert(dist <= ctx->decompressed.size);
                /* INFO("Current output at %lx, copying %d bytes from %d back\n", */
                /*         ctx->decompressed.size, len, dist); */
                DEFLATE_DECOMPRESS_ENSURE(ctx, len);
                while (len > dist) {
                    DEFLATE_DECOMPRESS_APPEND_BYTES(ctx,
                            ctx->decompressed.data + ctx->decompressed.size - dist,
                            dist);
                    len -= dist;
                    dist *= 2;
                }
                if (len > 0) {
                    DEFLATE_DECOMPRESS_APPEND_BYTES(ctx,
                            ctx->decompressed.data + ctx->decompressed.size - dist,
                            len);
                }

                ctx->state = DEFLATE_COMPRESSED_FIXED;
            }; break;

            default:
                UNREACHABLE();
                ctx->state = DEFLATE_ERROR;
                return false;
        }
    }
    UNREACHABLE();
    ctx->state = DEFLATE_ERROR;
    return false;
}

bool deflate_dynamic_compression_build_tree(deflate_context *ctx, huffman_tree *tree) {
    // This may need the bits swapped?
    // I was working through this, but turns out the reference implementation uses fixed for my input while stepping through debugger
    UNIMPLENTED("");(void)ctx;(void)tree;

    /* uint8_t hlit = deflate_next_bits(ctx, 5); */
    /* uint8_t hdist = deflate_next_bits(ctx, 5); */
    /* uint8_t hclen = deflate_next_bits(ctx, 4); */
    /* INFO("Deflating dynamic huffman code, hlit = %d hdist = %d hclen = %d\n", hlit, hdist, hclen); */
    /* uint8_t code_lengths[19] = {0}; */
    /* uint8_t order[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15}; */
    /* assert(C_ARRAY_LEN(order) == C_ARRAY_LEN(code_lengths)); */
    /* assert((size_t)hclen + 4 <= C_ARRAY_LEN(code_lengths)); */
    /* uint8_t max_code = 0; */
    /* for (size_t i = 0 ; i < (size_t)hclen + 4; i ++) { */
    /*     assert(order[i] < C_ARRAY_LEN(code_lengths)); */
    /*     uint8_t code_len = deflate_next_bits(ctx, 3); */
    /*     code_lengths[order[i]] = code_len; */
    /*     if (code_len > 0 && order[i] > max_code) { */
    /*         max_code = order[i]; */
    /*     } */
    /* } */
    /* fprintf(stderr, "max_code %d\n", max_code); */
    /* tree->data = calloc(max_code + 1, sizeof(huffman_tree)); */
    /* if (tree->data == NULL) return false; */
    /* tree->length = max_code + 1; */

    /* for (size_t i = 0; i <= max_code; i ++) { */
    /*     tree->data[i].length = code_lengths[i]; */
    /* } */

    /* /1* RFC 1951 - 3.2.2 - Step 1 *1/ */
    /* uint8_t bl_count[8] = {0}; */
    /* uint8_t max_bits = 0; */
    /* for (size_t i = 0 ; i < C_ARRAY_LEN(code_lengths); i ++) { */
    /*     if (code_lengths[i] == 0) continue; */
    /*     assert(code_lengths[i] < C_ARRAY_LEN(bl_count)); */
    /*     bl_count[code_lengths[i]] ++; */
    /*     if (code_lengths[i] > max_bits) { */
    /*         max_bits = code_lengths[i]; */
    /*     } */

    /*     fprintf(stderr, "code_lengths[%ld] = %d\n", i, code_lengths[i]); */
    /* } */

    /* /1* RFC 1951 - 3.2.2 - Step 2 *1/ */
    /* uint64_t code = 0; */
    /* uint64_t next_code[9] = {0}; */
    /* assert(max_bits < C_ARRAY_LEN(next_code)); */
    /* assert(max_bits <= C_ARRAY_LEN(bl_count)); */
    /* bl_count[0] = 0; */
    /* for (uint8_t bits = 1; bits <= max_bits; bits ++) { */
    /*     code = (code + bl_count[bits - 1]) << 1; */
    /*     next_code[bits] = code; */

    /*     fprintf(stderr, "next_code[%d] = 0x%02lx\n", bits, next_code[bits]); */
    /* } */

    /* /1* RFC 1951 - 3.2.2 - Step 3 *1/ */
    /* for (uint8_t n = 0; n <= max_code; n ++) { */
    /*     uint8_t len = tree->data[n].length; */
    /*     if (len > 0) { */
    /*         tree->data[n].code = next_code[len]; */
    /*         next_code[len] ++; */
    /*     } */
    /* } */
    /* UNIMPLENTED(""); */
    /* return false; */
}

bool _deflate_header(deflate_context *ctx) {
    assert(ctx->state == DEFLATE_HEADER);
    if (DEFLATE_BITS(ctx) < 3) return false;
    ctx->last = deflate_next_bits(ctx, 1) == 0x1;
    uint8_t btype = deflate_next_bits_rev(ctx, 2);

    /* INFO("Deflating block, bfinal = %d, btype = %x\n", bfinal, btype); */
    assert(btype <= DEFLATE_RESERVED);

    switch ((deflate_t)btype) {
        case DEFLATE_NO_COMPRESSION: ctx->state = DEFLATE_UNCOMPRESSED; break;
        case DEFLATE_FIXED_COMPRESSION: ctx->state = DEFLATE_COMPRESSED_FIXED; break;
        case DEFLATE_DYNAMIC_COMPRESSION: ctx->state = DEFLATE_COMPRESSED_DYNAMIC; break;

        case DEFLATE_RESERVED:
        default:
            UNREACHABLE();
            ctx->state = DEFLATE_ERROR;
            return false;
    }
    return true; // We haven't decompressed the stream, but the header seems OK
}

bool deflate_block(deflate_context *ctx) {
    for (;;) {
        _Static_assert(DEFLATE_ERROR == 13, "States have changed. May need handling here");
        switch (ctx->state) {
            case DEFLATE_HEADER: if (!_deflate_header(ctx)) return false; break;

            case DEFLATE_UNCOMPRESSED:
            case DEFLATE_UNCOMPRESSED_DATA:
                return _deflate_uncompressed(ctx);

            case DEFLATE_COMPRESSED_DYNAMIC: {
                                                 /* FIXME make sure this works with state machine */
                UNIMPLENTED();
                /* huffman_tree tree = {0}; */
                /* if (!deflate_dynamic_compression_build_tree(ctx, &tree)) return false; */

                /* INFO("Huffman tree of dynamic compression\n"); */
                /* for (size_t i = 0; i < tree.length; i ++) { */
                /*     fprintf(stderr, "Tree[%ld] = {.length = %ld, .code = 0x%02lx}\n", */
                /*             i, tree.data[i].length, tree.data[i].code); */
                /* } */

                /* free(tree.data); */
            }; break;

            case DEFLATE_COMPRESSED_FIXED:
            case DEFLATE_COMPRESSED_FIXED_LENGTH:
            case DEFLATE_COMPRESSED_FIXED_LENGTH_EXTRA:
            case DEFLATE_COMPRESSED_FIXED_DISTANCE:
            case DEFLATE_COMPRESSED_FIXED_DISTANCE_EXTRA:
            case DEFLATE_COMPRESSED_FIXED_COPY:
                return _deflate_fixed_compression(ctx);

            case DEFLATE_FINISHED: return true;

            default:
                UNIMPLENTED("ctx->state = %d", ctx->state);
                break;
        }
    }
}

bool deflate(deflate_context *ctx) {
    while (ctx->state != DEFLATE_FINISHED) {
        if (!deflate_block(ctx)) return false;
    }
    return true;
}

bool zlib_decompress(zlib_context *ctx) {
    assert(ctx != NULL);
    for (;;) {
        _Static_assert(ZLIB_ERROR == 5, "States have changed. May need handling here");
        switch (ctx->state) {
            case ZLIB_HEADER: {
                if (DEFLATE_BYTES(&ctx->deflate) < 2) return false;

                uint8_t cmf = deflate_next_bytes(&ctx->deflate, 1);

                uint8_t cm = cmf & 0xF;
                uint8_t cinfo = (cmf & 0xF0) >> 4;

                assert(cm == 8);
                assert(cinfo <= 7);
                if (cinfo != 7) UNIMPLENTED("Reading different window sizes than 32K");

                uint8_t flg = deflate_next_bytes(&ctx->deflate, 1);
                uint16_t check = (uint16_t)cmf * 256 + (uint8_t)flg;
                assert(check % 31 == 0);

                uint8_t fdict = (flg >> 5) & 0x1;
                uint8_t flevel = (flg >> 6) & 0x3;
                assert(flevel <= ZLIB_MAX_COMPRESSOR);

                if (flevel != 0) {
                    WARN("FLEVEL not 0. Not used in decompression, but useful for recompression. Ignoring value (%d)\n", flevel);
                }
                ctx->state = fdict != 0 ? ZLIB_DICT : ZLIB_DEFLATE;
            }; break;

            case ZLIB_DICT:
                UNIMPLENTED("Reading zlib DICTID (see RFC 1950 2.2 Data format)");
                break; // potentially should fall through?

            case ZLIB_DEFLATE:
                if (!deflate(&ctx->deflate)) {
                    if (ctx->deflate.state == DEFLATE_ERROR) {
                        ctx->state = ZLIB_ERROR;
                    }
                    return false;
                }
                ctx->state = ZLIB_ADLER;
                // fall through
            case ZLIB_ADLER: {
                uint16_t s1 = 1;
                uint16_t s2 = 0;
                for (size_t i = 0; i < ctx->deflate.decompressed.size; i ++) {
                    s1 = ((uint32_t)s1 + ctx->deflate.decompressed.data[i]) % 65521;
                    s2 = ((uint32_t)s2 + s1) % 65521;
                }

                // FIXME check adler-32 code
            };
                // fall through
            case ZLIB_FINISHED: return true;

            case ZLIB_ERROR:
            default:
                UNREACHABLE();
                break;
        }
    }
}

#endif
