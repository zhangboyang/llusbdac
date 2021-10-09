#include <linux/module.h>

#include "llusbdac.h"

// http://home.thep.lu.se/~bjorn/crc/

static u32 table[0x100];

void ztcrc32_init(void)
{
    for (size_t i = 0; i < 0x100; ++i) {
        u32 r = i;
        for (int j = 0; j < 8; ++j)
            r = (r & 1? 0: (u32)0xEDB88320L) ^ r >> 1;
        table[i] = r ^ (u32)0xFF000000L;
    }
}

void ztcrc32_reset(struct zero_trimmed_crc32 *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

static __always_inline int __ztcrc32_update(struct zero_trimmed_crc32 *ctx, const void *restrict data, size_t len, int group, int skip, int valid)
{
    BUG_ON(len % (group * (skip + valid)) != 0);
    int updated = 0;
    const u8 *ptr = data;
    const u8 *end = ptr + len;
    if (!ctx->started) {
        while (ptr < end) {
            u8 o = 0;
            const u8 *fwd = ptr;
            for (int i = 0; i < group; i++) {
                fwd += skip;
                for (int j = 0; j < valid; j++) {
                    o |= *fwd++;
                }
            }
            if (o) {
                ctx->started = 1;
                break;
            }
            ptr = fwd;
        }
    }
    while (ptr < end) {
        u8 o = 0;
        for (int i = 0; i < group; i++) {
            ptr += skip;
            for (int j = 0; j < valid; j++) {
                u8 b = *ptr++;
                ctx->crc32 = table[(u8)ctx->crc32 ^ b] ^ ctx->crc32 >> 8;
                o |= b;
            }
        }
        if (o) {
            ctx->crc32_trimmed = ctx->crc32;
            updated = 1;
        }
    }
    return updated;
}
static noinline int ztcrc32_update_2_0_2(struct zero_trimmed_crc32 *ctx, const void *restrict data, size_t len)
{
    return __ztcrc32_update(ctx, data, len, 2, 0, 2);
}
static noinline int ztcrc32_update_2_1_3(struct zero_trimmed_crc32 *ctx, const void *restrict data, size_t len)
{
    return __ztcrc32_update(ctx, data, len, 2, 1, 3);
}
static noinline int ztcrc32_update_2_0_4(struct zero_trimmed_crc32 *ctx, const void *restrict data, size_t len)
{
    return __ztcrc32_update(ctx, data, len, 2, 0, 4);
}
int ztcrc32_update_samples(struct zero_trimmed_crc32 *ctx, const void *restrict data, size_t len, unsigned sample_bits)
{
    switch (sample_bits) {
    case 16: return ztcrc32_update_2_0_2(ctx, data, len);
    case 24: return ztcrc32_update_2_1_3(ctx, data, len);
    case 32: return ztcrc32_update_2_0_4(ctx, data, len);
    default: BUG(); return 0;
    }
}

u32 ztcrc32_get(struct zero_trimmed_crc32 *ctx)
{
    return ctx->crc32_trimmed;
}

int ztcrc32_started(struct zero_trimmed_crc32 *ctx)
{
    return ctx->started;
}