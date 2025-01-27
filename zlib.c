#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define UNREACHABLE() do { fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__); fflush(stderr); abort(); } while (0)
#define UNIMPLENTED() do { fprintf(stderr, "%s:%d: UNIMPLENTED %s\n", __FILE__, __LINE__, __func__); fflush(stderr); abort(); } while (0)
#define INFO(fmt, ...) do { fprintf(stderr, "%s:%d: INFO: " fmt, __FILE__, __LINE__, ##__VA_ARGS__); fflush(stderr); } while (0)

#define C_ARRAY_LEN(arr) (sizeof((arr))/(sizeof((arr)[0])))

#define PRINT_BITS(num, bits) do { uint64_t n = (num); for (size_t i = 0; i < (bits); i ++) { fprintf(stderr, "%ld", (n >> ((bits) - i - 1)) & 0x1); } fflush(stderr); } while (0)

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

typedef struct {
    uint8_t *data;
    uint8_t index;
    long size;
} bitstream;

uint8_t bitstream_next(bitstream *stream) {
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
    INFO("bitstream_next: ret %d, index = %d, size = %lu\n", ret, stream->index, stream->size);
    return ret;
}

uint64_t bitstream_next_bits(bitstream *stream, uint8_t bits) {
    assert(bits <= 63);
    uint64_t ret = 0;
    uint8_t i = 0;
    while (i < bits) {
        uint8_t next = bitstream_next(stream);
        if (next > 1) return EOF;
        assert((next & ~0x1) == 0);
        ret = (next << (i++)) | ret;
    }
    INFO("bitstream_next_bits(%d): 0x%lx, 0b", bits, ret);
    PRINT_BITS(ret, bits);
    fprintf(stderr, "\n");
    return ret;
}

void deflate_no_compression(bitstream *stream) {
    UNIMPLENTED();(void)stream;
}

void deflate_fixed_compression(bitstream *stream) {
    uint64_t code = bitstream_next_bits(stream, 8);
    INFO("Read code %lx\n", code);
    UNIMPLENTED();(void)stream;
}

void deflate_dynamic_compression_build_tree(bitstream *stream, huffman_tree *tree) {
    // This may need the bits swapped?
    // I was working through this, but turns out the reference implementation uses fixed for my input while stepping through debugger
    UNIMPLENTED();

    uint8_t hlit = bitstream_next_bits(stream, 5);
    uint8_t hdist = bitstream_next_bits(stream, 5);
    uint8_t hclen = bitstream_next_bits(stream, 4);
    INFO("Deflating dynamic huffman code, hlit = %d hdist = %d hclen = %d\n", hlit, hdist, hclen);
    uint8_t code_lengths[19] = {0};
    uint8_t order[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    assert(C_ARRAY_LEN(order) == C_ARRAY_LEN(code_lengths));
    assert((size_t)hclen + 4 <= C_ARRAY_LEN(code_lengths));
    uint8_t max_code = 0;
    for (size_t i = 0 ; i < (size_t)hclen + 4; i ++) {
        assert(order[i] < C_ARRAY_LEN(code_lengths));
        uint8_t code_len = bitstream_next_bits(stream, 3);
        code_lengths[order[i]] = code_len;
        if (code_len > 0 && order[i] > max_code) {
            max_code = order[i];
        }
    }
    fprintf(stderr, "max_code %d\n", max_code);
    tree->data = calloc(max_code + 1, sizeof(huffman_tree));
    if (tree->data == NULL) return;
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
}

void deflate_block(bitstream *stream) {
    uint8_t bfinal = bitstream_next(stream);
    uint8_t btype = bitstream_next_bits(stream, 2);

    INFO("Deflating block, bfinal = %d, btype = %x\n", bfinal, btype);
    assert(btype <= DEFLATE_RESERVED);

    switch ((deflate_t)btype) {
        case DEFLATE_NO_COMPRESSION:
            deflate_no_compression(stream);
            break;

        case DEFLATE_FIXED_COMPRESSION:
            deflate_fixed_compression(stream);
            break;

        case DEFLATE_DYNAMIC_COMPRESSION: {
            huffman_tree tree = {0};
            deflate_dynamic_compression_build_tree(stream, &tree);

            INFO("Huffman tree of dynamic compression\n");
            for (size_t i = 0; i < tree.length; i ++) {
                fprintf(stderr, "Tree[%ld] = {.length = %ld, .code = 0x%02lx}\n",
                        i, tree.data[i].length, tree.data[i].code);
            }

            free(tree.data);
        }; break;

        case DEFLATE_RESERVED:
            UNREACHABLE();
            break;

        default:
            UNREACHABLE();
            break;
    }
}

void zlib_decompress(uint8_t *data, long size) {
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

    if (fdict != 0) UNIMPLENTED();
    if (flevel != 0) UNIMPLENTED();

    bitstream bits = {
        .data = data + 2,
        .size = size - 2,
    };

    deflate_block(&bits);
    UNIMPLENTED();
}

int main() {
    int ret = 0;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define expect(expr) if (!(expr)) return_defer(1)
    char *object_file = ".git/objects/78/981922613b2afb6025042ff6bd878ac1994e85";
    FILE *file = NULL;
    file = fopen(object_file, "rb");
    expect(file != NULL);

    expect(fseek(file, 0L, SEEK_END) != -1);
    long size = ftell(file);
    expect(size != -1);
    expect(fseek(file, 0L, SEEK_SET) != -1);

    uint8_t *data = malloc(size);
    expect(data != NULL);
    expect(fread(data, size, 1, file) == 1);

    zlib_decompress(data, size);
    UNIMPLENTED();

#undef return_defer
#undef expect
defer:
    if (data) free(data);
    if (file) fclose(file);
    return ret;
}
