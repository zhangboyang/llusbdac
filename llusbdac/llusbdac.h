#pragma once

#include <linux/hrtimer.h>
#include <linux/ktime.h>

#define LLUSBDAC_VERSTRING "1.2"
#define LLUSBDAC_IDVENDER 0x054C
#define LLUSBDAC_IDPRODUCT 0x0B8B
#define LLUSBDAC_NAME "LLUSBDAC (NW-ZX300A)"

#define IMPORT_KALLSYMS(x) do { \
    if (!p_ ## x) \
        p_ ## x = (void *) kallsyms_lookup_name(#x); \
} while (0)

extern void patch_kernel(void *pc, const void *code_new, void *code_save, const void *code_verify, size_t len);
extern void lock_cpufreq(int en);
extern int usb_connected(void);

struct usb_configuration;
extern void set_feedback_limits_base10000(unsigned low_limit, unsigned high_limit);
extern int audio_bind_config(struct usb_configuration *cfg);
extern void uac2_unbind_config(struct usb_configuration *cfg);
extern int gadget_enabled(void);
extern void uac2_init(void);
#define MAX_RATE 384000

extern const unsigned char fontdata_16x32[];

typedef struct {
    u64 overrun;
    u64 underrun;
    u64 usb;
} uac_stats_err_t;
typedef struct {
    int running;
    unsigned sample_rate;
    unsigned sample_bits;
    u64 n_frames;
    unsigned buf_frames;
    uac_stats_err_t err;
} uac_stats_t;
extern volatile uac_stats_t uac_stats;
extern void crc32mgr_init(unsigned silent_threshold_ms);
extern void crc32mgr_reset(void);
extern void crc32mgr_update(const void *restrict data, size_t len, unsigned sample_rate, unsigned sample_bits);
extern void init_gui(void);
extern void cleanup_gui(void);
extern void enable_gui(int en);

extern void ringbuf_clear(unsigned rate, unsigned bits);
extern int ringbuf_report(unsigned *inflight_frames, unsigned *target_frames);
extern unsigned ringbuf_push(const void *restrict data, unsigned len);
extern void set_player_param(const char *new_devpath, unsigned new_xrun_us, unsigned new_target_us, unsigned new_buffer_us);
extern void start_player(void);
extern void stop_player(void);

struct zero_trimmed_crc32 {
    int started;
    u32 crc32_ltrim;
    u32 crc32_trim;
    u64 count_ltrim;
    u64 count_trim;
};
extern void ztcrc32_init(void);
extern int ztcrc32_update_samples(struct zero_trimmed_crc32 *restrict ctx, const void *restrict data, size_t len, unsigned sample_bits);
extern void ztcrc32_reset(struct zero_trimmed_crc32 *ctx);
extern int ztcrc32_started(struct zero_trimmed_crc32 *ctx);
extern u32 ztcrc32_get(struct zero_trimmed_crc32 *ctx);
extern u64 ztcrc32_cnt(struct zero_trimmed_crc32 *ctx);





// import __future__ :)

#ifndef __READ_ONCE
#define __READ_ONCE(x)	(*(const volatile typeof(x) *)&(x))
#endif
#define READ_ONCE(x)							\
({									\
	__READ_ONCE(x);							\
})
#define __WRITE_ONCE(x, val)						\
do {									\
	*(volatile typeof(x) *)&(x) = (val);				\
} while (0)
#define WRITE_ONCE(x, val)						\
do {									\
	__WRITE_ONCE(x, val);						\
} while (0)

#ifndef smp_store_release
# define smp_store_release(p, v)		\
do {						\
	smp_mb();				\
	WRITE_ONCE(*p, v);			\
} while (0)
#endif
#ifndef smp_load_acquire
# define smp_load_acquire(p)			\
({						\
	typeof(*p) ___p1 = READ_ONCE(*p);	\
	smp_mb();				\
	___p1;					\
})
#endif

static inline u64 ktime_get_ns(void)
{
	return ktime_to_ns(ktime_get());
}
static inline u64 ktime_get_boottime_ns(void)
{
	return ktime_to_ns(ktime_get_boottime());
}
