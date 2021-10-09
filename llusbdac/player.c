#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/kallsyms.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <asm/irq_regs.h>

#include "llusbdac.h"

static struct task_struct *player_thread = NULL;


static const char *devpath;
static unsigned xrun_us;   // when hw_buffered < xrun_us, xrun occurs
static unsigned target_us; // optimal value of inflight_frames
static unsigned buffer_us; // max value of rb_buffered
static unsigned rb_size; // ring buffer size in bytes
void set_player_param(const char *new_devpath, unsigned new_xrun_us, unsigned new_target_us, unsigned new_buffer_us)
{
    devpath = new_devpath;
    xrun_us = new_xrun_us;
    target_us = new_target_us;
    buffer_us = new_buffer_us;

    rb_size = div_u64((u64)8 * MAX_RATE * buffer_us, 1000000);
}



static inline void set_mask(struct snd_pcm_hw_params *params, int var, unsigned int val)
{
    struct snd_mask *m = hw_param_mask(params, var);
    memset(m, 0, sizeof(*m));
    snd_mask_set(hw_param_mask(params, var), val);
}
static inline void set_interval(struct snd_pcm_hw_params *params, int var, unsigned int val)
{
    struct snd_interval *i = hw_param_interval(params, var);
    memset(i, 0, sizeof(*i));
	i->empty = 0;
	i->min = i->max = val;
	i->openmin = i->openmax = 0;
	i->integer = 1;
}



#define PERIOD_TIME 1000 // us
static inline void calc_period_info(unsigned sample_rate,
    unsigned *xrun_frames, unsigned *target_frames, unsigned *buffer_frames,
    unsigned *period_frames, unsigned *period_count)
{
    *period_frames = div_u64((u64)sample_rate * PERIOD_TIME, 1000000);
    *xrun_frames = div_u64((u64)sample_rate * xrun_us, 1000000);
    *target_frames = div_u64((u64)sample_rate * target_us, 1000000);

    *period_count = buffer_us / PERIOD_TIME;
    *buffer_frames = *period_count * *period_frames;
}
static inline unsigned bytes_per_sample(unsigned bits)
{
    return bits == 16 ? 2 : 4;
}
static inline unsigned f2bytes(unsigned frames, unsigned bits)
{
    return frames * (2/*channels*/ * bytes_per_sample(bits));
}
static inline unsigned bytes2f(unsigned bytes, unsigned bits)
{
    BUG_ON(bytes % (2/*channels*/ * bytes_per_sample(bits)) != 0);
    return bytes / (2/*channels*/ * bytes_per_sample(bits));
}


typedef struct {
    void *buf;
    unsigned bufsz;

    unsigned cap;
    
    u64 n_read; // consumer-only
    unsigned head;

    u64 n_write; // producer-only
    unsigned tail;

    int pcm_valid;
    unsigned inflight_at_isr;

    unsigned sample_rate, sample_bits;
    unsigned xrun_frames, target_frames, buffer_frames;
    unsigned period_frames, period_count;
} ringbuf_t;
static ringbuf_t ringbuf[2];
#define another_ringbuf(b) (&ringbuf[((b) - ringbuf) ^ 1])

static spinlock_t bufptr_lock;
static ringbuf_t *writer_buf, *reader_buf; // protected by bufptr_lock

static wait_queue_head_t ringbuf_wq;

static struct snd_pcm_substream *pcm_substream = NULL; // must use smp_load_acquire() outside player thread

// inflight info
static u64 inflight_fixup;
static inline unsigned get_inflight(ringbuf_t *wb, unsigned size, int *is_running, int *pcm_state, unsigned long *hw_ptr, unsigned long *appl_ptr)
{
    s64 inflight = -1;
    struct snd_pcm_substream *substream = smp_load_acquire(&pcm_substream); // called by producer
    
    if (substream) {
        unsigned long flags;
        snd_pcm_stream_lock_irqsave(substream, flags);
        struct snd_pcm_runtime *runtime = substream->runtime;
        if (pcm_state) *pcm_state = runtime->status->state;
        if (hw_ptr) *hw_ptr = runtime->status->hw_ptr;
        if (appl_ptr) *appl_ptr = runtime->control->appl_ptr;
        if (runtime->status->state == SNDRV_PCM_STATE_RUNNING) {
            u64 hw_ptr64 = f2bytes(runtime->hw_ptr_wrap + runtime->status->hw_ptr, wb->sample_bits);
            //printk("now=%llu hw_ptr64=%llu\n", ktime_get_ns(), hw_ptr64);
            inflight = wb->n_write - hw_ptr64 + inflight_fixup;
            //printk("get_inflight=%lld nw=%llu hw_ptr64=%llu fixup=%lld\n", inflight, wb->n_write, hw_ptr64, inflight_fixup);
        }
        //printk("get_inflight=%lld\n",inflight);
        snd_pcm_stream_unlock_irqrestore(substream, flags);
    }
    if (is_running != NULL)
        *is_running = (inflight >= 0);
    if (inflight < 0)
        return size; // snd not running, use ringbuf size
    if ((unsigned)inflight < size)
        return size; // consumer may update head later than hardware
    return (unsigned)inflight;
}
static inline void reset_inflight(ringbuf_t *rb)
{
    struct snd_pcm_substream *substream = pcm_substream; // called by player thread
	unsigned long flags;
	snd_pcm_stream_lock_irqsave(substream, flags);
    struct snd_pcm_runtime *runtime = substream->runtime;
    u64 hw_ptr64 = f2bytes(runtime->hw_ptr_wrap + runtime->status->hw_ptr, rb->sample_bits);
    inflight_fixup = hw_ptr64 - rb->n_read;
    printk("reset_inflight hw_ptr_wrap=%llu hw_ptr=%lu fixup=%lld hw_ptr64=%llu nr=%llu cap=%u\n", runtime->hw_ptr_wrap, runtime->status->hw_ptr, inflight_fixup, hw_ptr64, rb->n_read, rb->cap);
	snd_pcm_stream_unlock_irqrestore(substream, flags);
}


// producer
int ringbuf_report(unsigned *inflight_frames, unsigned *target_frames)
{
    ringbuf_t *wb = writer_buf;
    if (wb->inflight_at_isr != -1) {
        *inflight_frames = bytes2f(wb->inflight_at_isr, wb->sample_bits);
        *target_frames = wb->target_frames;
        return 1;
    } else {
        return 0;
    }
}
void ringbuf_clear(unsigned sample_rate, unsigned sample_bits)
{
    ringbuf_t b;
    memset(&b, 0, sizeof(b));
    b.inflight_at_isr = -1;
    calc_period_info(sample_rate, &b.xrun_frames, &b.target_frames, &b.buffer_frames, &b.period_frames, &b.period_count);
    b.sample_rate = sample_rate;
    b.sample_bits = sample_bits;
    b.cap = f2bytes(b.buffer_frames, sample_bits);

    unsigned long flags;
    spin_lock_irqsave(&bufptr_lock, flags);
    writer_buf = another_ringbuf(reader_buf);
    b.buf = writer_buf->buf;
    b.bufsz = writer_buf->bufsz;
    *writer_buf = b;
    spin_unlock_irqrestore(&bufptr_lock, flags);

    wake_up(&ringbuf_wq);

    BUG_ON(b.cap >= b.bufsz);
    printk("ringbuf_clear >>>>>>>>>>>>>>> %u @ %u\n", sample_rate, sample_bits);
    printk("b.cap = %u\n", b.cap);
    printk("b.bufsz = %u\n", b.bufsz);
    printk("xrun_frames = %u\n", b.xrun_frames);
    printk("target_frames = %u\n", b.target_frames);
    printk("period_frames = %u\n", b.period_frames);
    printk("period_count = %u\n", b.period_count);
    printk("buffer_frames = %u\n", b.buffer_frames);
}
#define MAX_RB_PUSH_HISTORY 20
typedef struct {
	u64 ts;
    int pcm_state;
    unsigned long hw_ptr, appl_ptr;
	unsigned len0, len, size, inflight;
    long player_state;
    pid_t curr_pid;
    char curr_comm[TASK_COMM_LEN];
    int curr_preempt;
    unsigned long curr_pc, curr_lr;
} rb_push_history_t;
static rb_push_history_t rb_push_history[MAX_RB_PUSH_HISTORY];
static int rb_push_history_ptr = 0;
#define alloc_rb_push_history() (&rb_push_history[rb_push_history_ptr = (rb_push_history_ptr + 1) % MAX_RB_PUSH_HISTORY])
static void dump_rb_push_history(u64 marker)
{
    printk("RINGBUF PUSH HISTORY DUMP: marker=%llu\n", marker);
    u64 last_ts = 0;
    for (int i = (rb_push_history_ptr + 1) % MAX_RB_PUSH_HISTORY, j = 0; j < MAX_RB_PUSH_HISTORY; i = (i + 1) % MAX_RB_PUSH_HISTORY, j++) {
        rb_push_history_t *cur = &rb_push_history[i];
        printk("rb_push[%2d]: pcm=%d %lu,%lu len=%u,%u %u,%u (ts=%llu %6llu %lld) player=%ld curr=%.16s(%d)[%x] PC=%pS LR=%pS\n",
            j, cur->pcm_state, cur->hw_ptr, cur->appl_ptr, cur->len0, cur->len, cur->size, cur->inflight,
            cur->ts, cur->ts - last_ts, cur->ts - marker,
            cur->player_state, cur->curr_comm, cur->curr_pid, cur->curr_preempt, (void *)cur->curr_pc, (void *)cur->curr_lr);
        last_ts = cur->ts;
    }
}
unsigned ringbuf_push(const void *restrict data, unsigned len)
{
    int pcm_state = -1;
    unsigned long hw_ptr = 0, appl_ptr = 0;

    unsigned len0 = len;
    ringbuf_t *wb = writer_buf;

    unsigned head = READ_ONCE(wb->head);
    unsigned tail = wb->tail;
    unsigned size = (tail - head + wb->bufsz) % wb->bufsz;
    unsigned inflight = get_inflight(wb, size, NULL, &pcm_state, &hw_ptr, &appl_ptr);
    //printk("push len=%u cap=%u inflight=%u size=%u\n", len, wb->cap, inflight, size);
    BUG_ON(size > wb->cap || inflight > wb->cap);

    len = min(len, wb->cap - inflight);

    rb_push_history_t *h = alloc_rb_push_history();
    h->ts = ktime_get_ns();
    h->pcm_state = pcm_state, h->hw_ptr = hw_ptr, h->appl_ptr = appl_ptr;
    h->len0 = len0, h->len = len, h->size = size, h->inflight = inflight;
    h->player_state = player_thread->state;
    h->curr_pid = current->pid;
    memcpy(h->curr_comm, current->comm, sizeof(h->curr_comm));
    h->curr_preempt = preempt_count();
    h->curr_pc = get_irq_regs()->ARM_pc, h->curr_lr = get_irq_regs()->ARM_lr;

    //printk("push size=%u newlen=%u cap=%u inflight=%d\n", size, len, wb->cap, inflight);
    if (len) {
        unsigned l1 = min(len, wb->bufsz - tail);
        unsigned l2 = len - l1;
        
        memcpy(wb->buf + tail, data, l1);
        memcpy(wb->buf, data + l1, l2);

        smp_store_release(&wb->tail, (tail + len) % wb->bufsz);
        wake_up(&ringbuf_wq);
        wb->n_write += len;
    }

    return len;
}

// consumer
static unsigned ringbuf_peek(void **d1, unsigned *l1, void **d2, unsigned *l2)
{
    ringbuf_t *rb = reader_buf;

    unsigned head = rb->head;
    unsigned tail = smp_load_acquire(&rb->tail);
    unsigned size = (tail - head + rb->bufsz) % rb->bufsz;

    *l1 = min(rb->bufsz - head, size);
    *l2 = size - *l1;
    *d1 = rb->buf + head;
    *d2 = rb->buf;
    return tail;
}
static void ringbuf_pop(unsigned len)
{
    ringbuf_t *rb = reader_buf;
    smp_store_release(&rb->head, (rb->head + len) % rb->bufsz);
    rb->n_read += len;
}
static int ringbuf_param(unsigned *sample_rate, unsigned *sample_bits)
{
    unsigned long flags;
    spin_lock_irqsave(&bufptr_lock, flags);
    reader_buf = writer_buf;
    spin_unlock_irqrestore(&bufptr_lock, flags);

    ringbuf_t *rb = reader_buf;
    int mode_changed = *sample_rate != rb->sample_rate || *sample_bits != rb->sample_bits;
    printk("ringbuf_param ========= old: %u @ %u ========= new %u @ %u =========\n", *sample_rate, *sample_bits, rb->sample_rate, rb->sample_bits);
    *sample_rate = rb->sample_rate;
    *sample_bits = rb->sample_bits;
    return mode_changed;
}

// audio isr hook
#define MAX_AUDIO_ISR_HISTORY 20
typedef struct {
    u64 ts;
    unsigned inflight;
    int pcm_state;
    unsigned long hw_ptr, appl_ptr;
} audio_isr_history_t;
static audio_isr_history_t audio_isr_history[MAX_AUDIO_ISR_HISTORY];
static int audio_isr_history_ptr = 0;
#define alloc_audio_isr_history() (&audio_isr_history[audio_isr_history_ptr = (audio_isr_history_ptr + 1) % MAX_AUDIO_ISR_HISTORY])
static void dump_audio_isr_history(u64 marker)
{
    printk("AUDIO ISR HISTORY DUMP: marker=%llu\n", marker);
    u64 last_ts = 0;
    for (int i = (audio_isr_history_ptr + 1) % MAX_AUDIO_ISR_HISTORY, j = 0; j < MAX_AUDIO_ISR_HISTORY; i = (i + 1) % MAX_AUDIO_ISR_HISTORY, j++) {
        audio_isr_history_t *cur = &audio_isr_history[i];
        printk("audio_isr[%02d] pcm=%d %lu,%lu inflight=%u (ts=%llu %7llu %lld)\n",
            j, cur->pcm_state, cur->hw_ptr, cur->appl_ptr, cur->inflight,
            cur->ts, cur->ts - last_ts, cur->ts - marker);
        last_ts = cur->ts;
    }
}
static void audio_isr_hook(struct snd_pcm_substream *substream)
{
    ringbuf_t *wb = writer_buf;
    if (wb->pcm_valid) {
        unsigned head = READ_ONCE(wb->head);
        unsigned tail = wb->tail;
        unsigned size = (tail - head + wb->bufsz) % wb->bufsz;
        int is_running, pcm_state;
        unsigned long hw_ptr, appl_ptr;
        unsigned inflight = get_inflight(wb, size, &is_running, &pcm_state, &hw_ptr, &appl_ptr);
        if (is_running) {
            wb->inflight_at_isr = inflight;
            //printk("audio_isr_hook inflight=%u\n", inflight);
            *alloc_audio_isr_history() = (audio_isr_history_t) {
                .ts = ktime_get_ns(),
                .inflight = inflight,
                .pcm_state = pcm_state,
                .hw_ptr = hw_ptr,
                .appl_ptr = appl_ptr,
            };
        }
    }
}



static void try_make_kernel_more_real_time(void)
{
    static void (*p___purge_vmap_area_lazy)(unsigned long *start, unsigned long *end, int sync, int force_flush);
#define __purge_vmap_area_lazy p___purge_vmap_area_lazy
    IMPORT_KALLSYMS(__purge_vmap_area_lazy);

	unsigned long start = ULONG_MAX, end = 0;
	__purge_vmap_area_lazy(&start, &end, 0, 0); // don't be lazy :)
}


#define MAX_PLAYER_LOOP_HISTORY 20
    typedef struct {
        u64 loop_enter_ts;
        unsigned l1, l2;
        u64 write_ts;
        u64 write_ns;
        int write_ret;
        unsigned result_bytes;
    } player_loop_history_t;
    static player_loop_history_t player_loop_history[MAX_PLAYER_LOOP_HISTORY];
    static int player_loop_history_ptr = 0;
#define cur_history (player_loop_history[player_loop_history_ptr])
#define new_history() do { \
    player_loop_history_ptr = (player_loop_history_ptr + 1) % MAX_PLAYER_LOOP_HISTORY; \
    memset(&cur_history, 0, sizeof(cur_history)); \
} while (0)
static void dump_player_loop_history(u64 marker)
{
    printk("PLAYER LOOP HISTORY DUMP: marker=%llu\n", marker);
    u64 last_ts = 0;
    for (int i = (player_loop_history_ptr + 1) % MAX_PLAYER_LOOP_HISTORY, j = 0; j < MAX_PLAYER_LOOP_HISTORY; i = (i + 1) % MAX_PLAYER_LOOP_HISTORY, j++) {
        player_loop_history_t *cur = &player_loop_history[i];
        printk("player_loop[%2d] enter=%llu %6llu %lld len=%4u+%u ret=%d result_bytes=%-4u (write_ts=%llu write_ns=%llu)\n",
            j, cur->loop_enter_ts, cur->loop_enter_ts - last_ts, cur->loop_enter_ts - marker, cur->l1, cur->l2,
            cur->write_ret, cur->result_bytes, cur->write_ts, cur->write_ns);
        last_ts = cur->loop_enter_ts;
    }
}
static int player_thread_fn(void *data)
{
    struct sched_param param = { .sched_priority = RTPM_PRIO_AUDIO_PLAYBACK };
    sched_setscheduler(current, SCHED_RR, &param);
    //struct sched_param param = { .sched_priority = 0 };
    //sched_setscheduler(current, SCHED_IDLE, &param);    
    printk("llusbdac player started\n");

    struct file *pcm_device = NULL;

#define open_pcm_device() do { \
    if (!pcm_device) { \
        pcm_device = filp_open(devpath, O_RDWR, 0); \
        ((struct snd_pcm_file *)pcm_device->private_data)->substream->runtime->transfer_ack_end = audio_isr_hook; \
        smp_store_release(&pcm_substream, ((struct snd_pcm_file *)pcm_device->private_data)->substream); \
        printk("open_pcm_device: pcm_device=%p pcm_substream=%p\n", pcm_device, pcm_substream); \
    } \
} while (0)
#define close_pcm_device() do { \
    if (pcm_device) { \
        pcm_ioctl(SNDRV_PCM_IOCTL_DROP, NULL); \
        smp_store_release(&pcm_substream, NULL); \
        filp_close(pcm_device, NULL); \
        pcm_device = NULL; \
        printk("close_pcm_device: file closed\n"); \
    } \
} while (0)

#define pcm_ioctl(cmd, arg) \
    snd_pcm_kernel_ioctl(pcm_substream, cmd, arg)
#define pcm_ioctl_verify(cmd, arg) ({ \
    int __ret = pcm_ioctl(cmd, arg); \
    if (__ret) { \
        panic("pcm_ioctl_verify(%s, %s) = error %d\n", #cmd, #arg, __ret); \
    } \
    __ret; \
})

    unsigned cur_rate = 0, cur_bits = -1;
    unsigned wait_bytes;
    u64 last_write_ts;
    
    while (1) {
        crc32mgr_reset();
        
        while (cur_rate == 0 || cur_bits == 0 || cur_bits == -1) {
            if (cur_bits == -1) {
                close_pcm_device();
            } else if (cur_bits == 0 || cur_rate == 0) {
                open_pcm_device();
            }
            wait_event(ringbuf_wq, writer_buf != reader_buf || kthread_should_stop());
            if (kthread_should_stop()) goto done;
            ringbuf_param(&cur_rate, &cur_bits);
        }
        try_make_kernel_more_real_time();
        open_pcm_device();

        static struct snd_pcm_info info;
        memset(&info, 0, sizeof(info));
        pcm_ioctl_verify(SNDRV_PCM_IOCTL_INFO, &info);
        //printk("info.id=%s\n", info.id);
        //printk("info.name=%s\n", info.name);
        //printk("info.subname=%s\n", info.subname);

        int ver;
        pcm_ioctl_verify(SNDRV_PCM_IOCTL_PVERSION, &ver);
        //printk("ver=%x\n", ver);
        
        int ttstamp = SNDRV_PCM_TSTAMP_TYPE_MONOTONIC;
        pcm_ioctl_verify(SNDRV_PCM_IOCTL_TTSTAMP, &ttstamp);

        static struct snd_pcm_sync_ptr sync_ptr;
        memset(&sync_ptr, 0, sizeof(sync_ptr));
        sync_ptr.c.control.appl_ptr = 0;
        sync_ptr.c.control.avail_min = 1;
        pcm_ioctl_verify(SNDRV_PCM_IOCTL_SYNC_PTR, &sync_ptr);

        static struct snd_pcm_hw_params hw;
        memset(&hw, 0, sizeof(hw));
        _snd_pcm_hw_params_any(&hw);
        hw.flags |= SNDRV_PCM_HW_PARAMS_NORESAMPLE;
        set_mask(&hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
        set_mask(&hw, SNDRV_PCM_HW_PARAM_FORMAT, cur_bits != 16 ? SNDRV_PCM_FORMAT_S32_LE : SNDRV_PCM_FORMAT_S16_LE);
        set_interval(&hw, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
        set_interval(&hw, SNDRV_PCM_HW_PARAM_RATE, cur_rate);

        set_interval(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, reader_buf->period_frames);
        set_interval(&hw, SNDRV_PCM_HW_PARAM_PERIODS, reader_buf->period_count);
        //(hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_PERIOD_TIME))->min = 100;
        //(hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_PERIOD_TIME))->max = 100;
        //(hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_PERIODS))->min = 1024;
        //(hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_PERIODS))->max = 4;
        //(hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE))->min = 100000;
        //(hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE))->max = 4920;
        pcm_ioctl_verify(SNDRV_PCM_IOCTL_HW_PARAMS, &hw);
        //printk("period time = %u\n", hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_PERIOD_TIME)->min);
        //printk("period bytes = %u\n", hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_PERIOD_BYTES)->min);
        //printk("buffer time = %u\n", hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_BUFFER_TIME)->min);
        //printk("buffer size = %u\n", hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE)->min);
        //printk("buffer bytes = %u\n", hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->min);
        BUG_ON(hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->min != reader_buf->period_frames);
        BUG_ON(hw_param_interval(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE)->min != reader_buf->buffer_frames);
        
        sync_ptr.flags = 0;
        pcm_ioctl_verify(SNDRV_PCM_IOCTL_SYNC_PTR, &sync_ptr);
        
        static struct snd_pcm_sw_params sw;
        memset(&sw, 0, sizeof(sw));
        sw.proto = SNDRV_PCM_VERSION;
        sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
        sw.tstamp_type = ttstamp;
        sw.period_step = 1;
        sw.sleep_min = 0;
        sw.avail_min = reader_buf->period_frames;
        sw.xfer_align = 1;
        sw.start_threshold = reader_buf->target_frames;
        sw.stop_threshold = reader_buf->buffer_frames - reader_buf->xrun_frames;
        sw.silence_threshold = 0;
        sw.silence_size = 0;
        sw.boundary = reader_buf->buffer_frames;
        while (sw.boundary * 2 <= LONG_MAX - reader_buf->buffer_frames)
            sw.boundary *= 2;
        pcm_ioctl_verify(SNDRV_PCM_IOCTL_SW_PARAMS, &sw);

        smp_store_release(&reader_buf->pcm_valid, 1);
prepare:
        pcm_ioctl_verify(SNDRV_PCM_IOCTL_PREPARE, NULL);
        reset_inflight(reader_buf);
        wait_bytes = f2bytes(reader_buf->target_frames, reader_buf->sample_bits) - 1;
        last_write_ts = 0;

        while (1) {
            new_history();
            cur_history.loop_enter_ts = ktime_get_ns();

            void *d1, *d2;
            unsigned l1, l2, len;
            unsigned old_tail = ringbuf_peek(&d1, &l1, &d2, &l2);
            len = l1 + l2;
            cur_history.l1 = l1;
            cur_history.l2 = l2;

            //printk("peek len=%u l1=%u l2=%u\n", len, l1, l2);
            wait_event(ringbuf_wq, len > wait_bytes || writer_buf != reader_buf || READ_ONCE(reader_buf->tail) != old_tail || kthread_should_stop());
            /*if (!(len > wait_bytes || writer_buf != reader_buf || READ_ONCE(reader_buf->tail) != old_tail || kthread_should_stop()))
                usleep_range(100, 100);
            barrier();*/

            if (kthread_should_stop()) {
                goto done;
            }
            if (writer_buf != reader_buf) {
                if (ringbuf_param(&cur_rate, &cur_bits)) {
                    break;
                } else {
                    crc32mgr_reset();
                    smp_store_release(&reader_buf->pcm_valid, 1);
                    continue;
                }
            }
            if (len <= wait_bytes) {
                continue;
            }
            if (wait_bytes > 0 && l2 > 0) {
                // we are in start_threshold
                // concat d2 after d1, to workaround alsa bug
                memcpy(d1 + l1, d2, l2);
                l1 += l2;
            }
            wait_bytes = 0;

            static struct snd_xferi xferi;
            memset(&xferi, 0, sizeof(xferi));
            xferi.buf = d1;
            xferi.frames = bytes2f(l1, reader_buf->sample_bits);
            xferi.result = 0;
            //printk("xferi.buf=%p xferi.frames=%lu\n", xferi.buf, xferi.frames);
            cur_history.write_ts = ktime_get_ns();
            int ret = pcm_ioctl(SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xferi);
            cur_history.write_ns = ktime_get_ns() - cur_history.write_ts;
            cur_history.write_ret = ret;

            if (ret < 0) {
                uac_stats.err.underrun++;
                printk("UNDERRUN=%llu !!!!!!!!!!!!!!!!!!!! now=%llu last_write=%llu cur_write=%llu delta_ns=%llu\n",
                    uac_stats.err.underrun, ktime_get_ns(), last_write_ts, cur_history.write_ts, cur_history.write_ts - last_write_ts);
                
                local_irq_disable(); // not smp-safe
                u64 dump_start_ns = ktime_get_ns();
                u64 marker = last_write_ts;
                dump_rb_push_history(marker);
                dump_player_loop_history(marker);
                dump_audio_isr_history(marker);
                u64 dump_end_ns = ktime_get_ns();
                local_irq_enable();
                printk("dump cost %llu ns\n", dump_end_ns - dump_start_ns);
                goto prepare;
            }

            unsigned result_bytes = f2bytes(xferi.result, reader_buf->sample_bits);
            //printk("xferi.result=%lu (%u bytes)\n", xferi.result, result_bytes);
            uac_stats.n_frames += xferi.result;
            crc32mgr_update(d1, result_bytes, reader_buf->sample_bits);
            ringbuf_pop(result_bytes);

            cur_history.result_bytes = result_bytes;
            last_write_ts = cur_history.write_ts;

            sync_ptr.flags = SNDRV_PCM_SYNC_PTR_APPL;
            pcm_ioctl_verify(SNDRV_PCM_IOCTL_SYNC_PTR, &sync_ptr);

            try_make_kernel_more_real_time();
        }

        pcm_ioctl_verify(SNDRV_PCM_IOCTL_DROP, NULL);
    }

done:
    close_pcm_device();
    printk("llusbdac player exited\n");
    return 0;
}

void start_player(void)
{
    spin_lock_init(&bufptr_lock);
    init_waitqueue_head(&ringbuf_wq);

    memset(ringbuf, 0, sizeof(ringbuf));
    ringbuf[0].buf = kzalloc(3/*make room for concat*/ * rb_size, GFP_KERNEL);
    ringbuf[0].bufsz = 2/*avoid cap==rb_size*/ * rb_size;
    ringbuf[1].buf = kzalloc(3/*make room for concat*/ * rb_size, GFP_KERNEL);
    ringbuf[1].bufsz = 2/*avoid cap==rb_size*/ * rb_size;
    reader_buf = writer_buf = &ringbuf[0];
    //printk("ringbuf[0].buf=%p ringbuf[0].bufsz=%u\n", ringbuf[0].buf, ringbuf[0].bufsz);
    //printk("ringbuf[1].buf=%p ringbuf[1].bufsz=%u\n", ringbuf[1].buf, ringbuf[1].bufsz);
    printk("total ringbuf size = %u ( 6 * %u )\n", 6*rb_size, rb_size);

    player_thread = kthread_create(player_thread_fn, NULL, "llusbdac_player");
    BUG_ON(IS_ERR(player_thread));
    kthread_bind(player_thread, 0);
    wake_up_process(player_thread);
}
void stop_player(void)
{
    kthread_stop(player_thread);
    player_thread = NULL;

    kfree(ringbuf[0].buf);
    kfree(ringbuf[1].buf);
    memset(ringbuf, 0, sizeof(ringbuf));
}