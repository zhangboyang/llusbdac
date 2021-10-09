// draw status to screen

#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/kallsyms.h>
#include <linux/dma-mapping.h>
#include <linux/input.h>
#include <asm/cacheflush.h>

#include "llusbdac.h"

static struct task_struct *redraw_thread;
static int gui_enabled = 0;

#define TEXTAREA_H 11   // in chars
#define TEXTAREA_W 27
static unsigned char text_buf[TEXTAREA_H][TEXTAREA_W];
static unsigned char color_buf[TEXTAREA_H][TEXTAREA_W];

enum {
    FG_WHITE,
    FG_RED,
    FG_GREEN,
    FG_BLUE,
    FG_YELLOW,
    MAX_COLOR // END
};
static const char color_map[256] = {
    ['w'] = FG_WHITE,
    ['r'] = FG_RED,
    ['g'] = FG_GREEN,
    ['b'] = FG_BLUE,
    ['y'] = FG_YELLOW,
};

static void print(int y, int x, char *str)
{
    unsigned char color = FG_WHITE;
    int yy = y, xx = x;
    char ch;
    while ((ch = *str++)) {
        if (ch == '\n') {
            yy++;
            xx = x;
            continue;
        }
        if (ch == '$') {
            color = color_map[(unsigned char)*str++];
            continue;
        }
        if (yy * TEXTAREA_W + xx >= sizeof(text_buf)) {
            break;
        }
        text_buf[yy][xx] = ch;
        color_buf[yy][xx] = color;
        xx++;
    }
}

#define FONT_DATA fontdata_16x32
#define FONT_WIDTH 16
#define FONT_HEIGHT 32
#define FONT_PITCH DIV_ROUND_UP(FONT_WIDTH, 8)

#define GUI_Y 280
#define GUI_X 24
#define GUI_H (TEXTAREA_H * FONT_HEIGHT)
#define GUI_W (TEXTAREA_W * FONT_WIDTH)
static char __iomem *gui_vmem;
static unsigned long gui_pmem;
static unsigned long gui_pitch;

#define BG_COLOR ((255*80/100)<<24)
static unsigned color_palette[MAX_COLOR] = {
    [FG_WHITE]  = 0xFFAAAAAA,
    [FG_RED]    = 0xFFFF0000,
    [FG_GREEN]  = 0xFF00AA00,
    [FG_BLUE]   = 0xFF00AAAA,
    [FG_YELLOW] = 0xFFAAAA00,
};

#define LUT_BITS 4
#define LUT_KEYS (1<<LUT_BITS)
#define LUT_MASK ((1<<LUT_BITS)-1)
typedef struct {
    unsigned pixels[LUT_BITS];
} pixel_block __attribute__((aligned(16)));
static pixel_block pixel_lut[MAX_COLOR][LUT_KEYS];
static void make_lut(void)
{
    printk("fontdata_16x32=%p pixel_lut=%p lut_size=%d\n", fontdata_16x32, pixel_lut, sizeof(pixel_lut));
    for (int fg = 0; fg < MAX_COLOR; fg++) {
        for (int key = 0; key < LUT_KEYS; key++) {
            for (int bit = 0; bit < LUT_BITS; bit++) {
                pixel_lut[fg][key].pixels[bit] = (key & (1 << (LUT_BITS - bit - 1))) ? color_palette[fg] : BG_COLOR;
            }
        }
    }
}
static void draw_gui(void)
{
    for (int i = 0; i < TEXTAREA_H; i++) {
        for (int j = 0; j < TEXTAREA_W; j++) {
            char *ptr = gui_vmem + i * FONT_HEIGHT * gui_pitch + j * FONT_WIDTH * 4;
            u8 ch = text_buf[i][j], fg = color_buf[i][j];
            u16 *ft = (void *) (FONT_DATA + ch * FONT_HEIGHT * FONT_PITCH);
            pixel_block *sub_table = pixel_lut[fg];
            BUILD_BUG_ON(FONT_WIDTH != 16 || LUT_BITS != 4);
            __asm__ __volatile__ (
                "1:"
                "ldrh r0, [%[ft]], #2\n" // r0 = *ft++
                "and r1, r0, #0xF0\n"
                "add r1, %[sub_table], r1\n"
                "ldm r1, {r2, r3, r4, r5}\n"
                "and r1, r0, #0x0F\n"
                "add r1, %[sub_table], r1, lsl #4\n"
                "stmia %[ptr]!, {r2, r3, r4, r5}\n"
                "ldm r1, {r2, r3, r4, r5}\n"
                "lsr r0, r0, #8\n"
                "and r1, r0, #0xF0\n"
                "add r1, %[sub_table], r1\n"
                "stmia %[ptr]!, {r2, r3, r4, r5}\n"
                "ldm r1, {r2, r3, r4, r5}\n"
                "and r1, r0, #0x0F\n"
                "add r1, %[sub_table], r1, lsl #4\n"
                "stmia %[ptr]!, {r2, r3, r4, r5}\n"
                "ldm r1, {r2, r3, r4, r5}\n"
                "cmp %[ft], %[ft_end]\n"
                "stmia %[ptr], {r2, r3, r4, r5}\n"
                "add %[ptr], %[ptr], %[ptr_step]\n"
                "bcc 1b\n"
            :   [ptr] "+r" (ptr),
                [ft] "+r" (ft)
            :   [sub_table] "r" (sub_table),
                [ptr_step] "r" (gui_pitch - (16 * 3)),
                [ft_end] "r" (ft + FONT_HEIGHT)
            :   "r0", "r1",
                "r2", "r3", "r4", "r5",
                "cc"
            );
        }
    }
    dsb();
}


////////////////////////////////////////////////////////////////////////
////// SUPER DIRTY HACK: overlay our gui on top of framebuffer
//////

enum OVL_LAYER_SOURCE {
    OVL_LAYER_SOURCE_MEM    = 0,
    OVL_LAYER_SOURCE_RESERVED = 1,
    OVL_LAYER_SOURCE_SCL     = 2,
    OVL_LAYER_SOURCE_PQ     = 3,
};
typedef struct _OVL_CONFIG_STRUCT
{
    unsigned int layer;
	unsigned int layer_en;
    enum OVL_LAYER_SOURCE source;
    unsigned int fmt;
    unsigned int addr; 
    unsigned int vaddr;
    unsigned int src_x;
    unsigned int src_y;
    unsigned int src_w;
    unsigned int src_h;
    unsigned int src_pitch;
    unsigned int dst_x;
    unsigned int dst_y;
    unsigned int dst_w;
    unsigned int dst_h;                  // clip region
    unsigned int keyEn;
    unsigned int key; 
    unsigned int aen; 
    unsigned char alpha;  

    unsigned int isTdshp;
    unsigned int isDirty;

    int buff_idx;
    int identity;
    int connected_type;
    unsigned int security;
    int fence_fd;   
    struct ion_handle *ion_handles;
    bool fgIonHandleImport;	
}OVL_CONFIG_STRUCT;
#define OVL_ENGINE_INSTANCE_MAX_INDEX 3
#define OVL_ENGINE_OVL_BUFFER_NUMBER 4
typedef enum {
    OVERLAY_STATUS_IDLE,
    OVERLAY_STATUS_TRIGGER,
    OVERLAY_STATUS_BUSY,
    OVERLAY_STATUS_COMPLETE,
    OVERLAY_STATUS_MAX,
} OVERLAY_STATUS_T;
typedef enum
{
    COUPLE_MODE,
    DECOUPLE_MODE,
}OVL_INSTANCE_MODE;
#define DDP_OVL_LAYER_MUN 4
struct DISP_REGION
{
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
};
struct disp_path_config_mem_out_struct
{
    unsigned int enable;
    unsigned int dirty;
	unsigned int outFormat; 
    unsigned int dstAddr;
    struct DISP_REGION srcROI;        // ROI
    unsigned int security;
	int ion_fd;
};
struct disp_path_config_struct
{
    unsigned int srcModule; // DISP_MODULE_ENUM

	// if srcModule=RDMA0, set following value, else do not have to set following value
    unsigned int addr; 
    unsigned int inFormat; 
    unsigned int pitch;
    struct DISP_REGION srcROI;        // ROI

    OVL_CONFIG_STRUCT ovl_config;

    struct DISP_REGION bgROI;         // background ROI
    unsigned int bgColor;  // background color

    unsigned int dstModule; // DISP_MODULE_ENUM
    unsigned int outFormat; 
    unsigned int dstAddr;  // only take effect when dstModule=DISP_MODULE_WDMA or DISP_MODULE_WDMA1
    unsigned int enableUFOE;
    int srcWidth, srcHeight;
    int dstWidth, dstHeight;
    int dstPitch;
#if 0
	unsigned int RDMA0Security;
	unsigned int WDMA1Security;
#endif
};
typedef struct
{
    unsigned int        index;
    bool                bUsed;
    OVL_INSTANCE_MODE   mode;
    OVERLAY_STATUS_T    status; // current status
    OVL_CONFIG_STRUCT   cached_layer_config[DDP_OVL_LAYER_MUN]; // record all layers config;
    atomic_t OverlaySettingDirtyFlag;
    atomic_t OverlaySettingApplied;
    struct disp_path_config_mem_out_struct MemOutConfig;
    struct disp_path_config_struct path_info;
    bool                fgNeedConfigM4U;
    atomic_t            fgCompleted;
#if 1
	struct kthread_worker	 trigger_overlay_worker;
	struct task_struct*	     trigger_overlay_thread;
	struct kthread_work	     trigger_overlay_work;
	struct sw_sync_timeline* timeline;
    int                 timeline_max;
    int                 timeline_skip;
	struct sync_fence*  fences[DDP_OVL_LAYER_MUN];
    struct workqueue_struct* wq;
    int  outFence;
#endif
}DISP_OVL_ENGINE_INSTANCE;
typedef struct
{
    bool bInit;

    DISP_OVL_ENGINE_INSTANCE Instance[OVL_ENGINE_INSTANCE_MAX_INDEX];
    DISP_OVL_ENGINE_INSTANCE *current_instance;

    OVL_CONFIG_STRUCT* captured_layer_config;
    OVL_CONFIG_STRUCT* realtime_layer_config;
    OVL_CONFIG_STRUCT  layer_config[2][DDP_OVL_LAYER_MUN];
    unsigned int       layer_config_index;

    struct mutex OverlaySettingMutex;
    bool bCouple; // overlay mode
    bool bModeSwitch; // switch between couple mode & de-couple mode
    unsigned int Ovlmva;
    unsigned int OvlWrIdx;
    unsigned int RdmaRdIdx;
    bool OvlBufSecurity[OVL_ENGINE_OVL_BUFFER_NUMBER];
    unsigned int OvlBufAddr[OVL_ENGINE_OVL_BUFFER_NUMBER];
    unsigned int OvlBufAddr_va[OVL_ENGINE_OVL_BUFFER_NUMBER];
	struct ion_client *ion_client;
}DISP_OVL_ENGINE_PARAMS;

static DISP_OVL_ENGINE_PARAMS *p_disp_ovl_engine;
#define disp_ovl_engine (*p_disp_ovl_engine)
static atomic_t *p_gWakeupOvlEngineThread;
static wait_queue_head_t *p_disp_ovl_engine_wq;
#define gWakeupOvlEngineThread (*p_gWakeupOvlEngineThread)
#define disp_ovl_engine_wq (*p_disp_ovl_engine_wq)


static void request_redraw(void)
{
    atomic_set(&disp_ovl_engine.Instance[0].OverlaySettingDirtyFlag, 1);
    wake_up_process(redraw_thread);
}
static void set_my_overlay_layer(DISP_OVL_ENGINE_INSTANCE *inst)
{
    //printk("set_my_overlay_layer\n");
    int template = 0;
    int victim = 1;
    if (inst->cached_layer_config[template].layer_en && !inst->cached_layer_config[victim].layer_en) {
        OVL_CONFIG_STRUCT *layer = &inst->cached_layer_config[victim];
        *layer = inst->cached_layer_config[template];
        layer->layer = victim;
        layer->layer_en = 1;
        layer->source = 0;   // data source (0=memory)
        layer->fmt = 16785443; // eARGB8888
        layer->addr = gui_pmem; // addr
        layer->src_x = 0;  // x
        layer->src_y = 0;  // y
        layer->src_pitch = gui_pitch; //pitch, pixel number
        layer->dst_x = GUI_X; // x
        layer->dst_y = GUI_Y; // y
        layer->dst_w = GUI_W;
        layer->dst_h = GUI_H;
        layer->keyEn = 0;  //color key
        layer->key = 0;  //color key
        layer->aen = 1;
        layer->alpha = 0xFF;
    }
}
static void my_disp_ovl_engine_wake_up_ovl_engine_thread(void)
{
    //printk("my_disp_ovl_engine_wake_up_ovl_engine_thread\n");
    if (gui_enabled) {
        if (disp_ovl_engine.Instance[0].status == OVERLAY_STATUS_TRIGGER) {
            // called from trigger overlay
            set_my_overlay_layer(&disp_ovl_engine.Instance[0]);
        } else {
            // called from complete interrupt
            request_redraw();
        }
    }
    // original code
    atomic_set(&gWakeupOvlEngineThread, 1);
    wake_up_interruptible(&disp_ovl_engine_wq);
}
static void do_dirty_hack(int en)
{
    if (en) {
        IMPORT_KALLSYMS(disp_ovl_engine);
        IMPORT_KALLSYMS(gWakeupOvlEngineThread);
        IMPORT_KALLSYMS(disp_ovl_engine_wq);

        struct fb_info *info = registered_fb[0];
        unsigned long offset = 2 * info->var.yres * info->fix.line_length;
        gui_vmem = info->screen_base + offset;
        gui_pmem = info->fix.smem_start + offset;
        gui_pitch = info->fix.line_length;

        make_lut();
    }


    unsigned long fn_to_patch = kallsyms_lookup_name("disp_ovl_engine_wake_up_ovl_engine_thread");
    BUG_ON(!fn_to_patch);
    static const unsigned payload_opcode[2] = {
        0xe51ff004,   // ldr pc, [pc, #-4]
        (unsigned) my_disp_ovl_engine_wake_up_ovl_engine_thread
    };
    static unsigned orig_opcode[2];
    if (en) {
        patch_kernel((void *)fn_to_patch, payload_opcode, orig_opcode, NULL, sizeof(payload_opcode));
    } else {
        patch_kernel((void *)fn_to_patch, orig_opcode, NULL, payload_opcode, sizeof(orig_opcode));
    }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#define MAX_GUI_PAGE 3
volatile int gui_page = 0;
static void llusbdac_input_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
    printk("llusbdac_input_event(%p, %u, %u, %d)\n", handle, type, code, value);
    if (type == 1 && value == 1) {
        switch (code) {
        case 105: case 106: case 28:
            gui_page = (gui_page + 1) % MAX_GUI_PAGE;
            break;
        }
    }
}

static struct input_handle llusbdac_input_handle;
static int llusbdac_input_connect(struct input_handler *handler,
                         struct input_dev *dev,
                         const struct input_device_id *id)
{
    int error;
    BUG_ON(llusbdac_input_handle.dev);
    llusbdac_input_handle.dev = dev;
    llusbdac_input_handle.handler = handler;
    llusbdac_input_handle.name = "llusbdac_input";
    if ((error = input_register_handle(&llusbdac_input_handle)))
        goto fail;
    if ((error = input_open_device(&llusbdac_input_handle)))
        goto fail;
    return 0;
fail:
    memset(&llusbdac_input_handle, 0, sizeof(llusbdac_input_handle));
    return error;
}
static void llusbdac_input_disconnect(struct input_handle *handle)
{
    input_close_device(&llusbdac_input_handle);
    input_unregister_handle(&llusbdac_input_handle);
}
static bool llusbdac_input_match(struct input_handler *handler, struct input_dev *dev)
{
    //printk("llusbdac_input_match: dev=%p name=%s\n", dev, dev->name);
    return dev && dev->name && strcmp(dev->name, "icx_key") == 0;
}
static const struct input_device_id llusbdac_input_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
        .evbit = { BIT_MASK(EV_KEY) },
    },
    { },
};
static struct input_handler llusbdac_input_handler = {
    .event          = llusbdac_input_event,
    .connect        = llusbdac_input_connect,
    .disconnect     = llusbdac_input_disconnect,
    .match          = llusbdac_input_match,
    .name           = "llusbdac_input",
    .id_table       = llusbdac_input_ids,
};


volatile uac_stats_t uac_stats;


#define MAX_TRACK_CRC 100
#define MAX_TRACK_CRC_DISPLAY 17
typedef struct {
    u32 full_crc;
    
    u32 track_crc[MAX_TRACK_CRC];
    u8 track_id[MAX_TRACK_CRC];
    u8 track_state[MAX_TRACK_CRC];
    u8 now_track;
    int disp_ptr;
} crc32_stats_t;
enum {
    TRACK_STATE_NORMAL,
    TRACK_STATE_ERROR,
    TRACK_STATE_VERIFIED,
};
static struct zero_trimmed_crc32 ztcrc32_full, ztcrc32_track;
static uac_stats_err_t crc32_err;
static u64 ztcrc32_track_timestamp;
static unsigned crc32mgr_threshold;
static volatile crc32_stats_t crc32_stats;

void crc32mgr_init(unsigned silent_threshold_ms)
{
    crc32mgr_threshold = silent_threshold_ms;
    ztcrc32_reset(&ztcrc32_full);
    ztcrc32_reset(&ztcrc32_track);
    memset((void *)&crc32_stats, 0, sizeof(crc32_stats));
    crc32_stats.now_track = 1;
}
static void crc32mgr_nexttrack(void)
{
    if (ztcrc32_started(&ztcrc32_track) && crc32_stats.track_id[crc32_stats.disp_ptr]) {
        if (crc32_stats.track_state[crc32_stats.disp_ptr] != TRACK_STATE_ERROR) {
            for (int i = 0; i < MAX_TRACK_CRC; i++) {
                if (i != crc32_stats.disp_ptr && crc32_stats.track_id[i] && crc32_stats.track_state[i] != TRACK_STATE_ERROR) {
                    if (crc32_stats.track_crc[i] == crc32_stats.track_crc[crc32_stats.disp_ptr]) {
                        crc32_stats.track_state[i] = TRACK_STATE_VERIFIED;
                        crc32_stats.track_state[crc32_stats.disp_ptr] = TRACK_STATE_VERIFIED;
                    }
                }
            }
        }
        printk("end of track detected: %d: %08X state=%d\n",
            crc32_stats.track_id[crc32_stats.disp_ptr], crc32_stats.track_crc[crc32_stats.disp_ptr], crc32_stats.track_state[crc32_stats.disp_ptr]);
        crc32_stats.now_track = crc32_stats.now_track == 99 ? 1 : crc32_stats.now_track + 1;
        crc32_stats.disp_ptr = (crc32_stats.disp_ptr + 1) % MAX_TRACK_CRC;
        crc32_stats.track_id[crc32_stats.disp_ptr] = 0;
        crc32_stats.track_state[crc32_stats.disp_ptr] = TRACK_STATE_NORMAL;
        crc32_stats.track_crc[crc32_stats.disp_ptr] = 0;
        ztcrc32_reset(&ztcrc32_track);
    }
}
void crc32mgr_reset(void)
{
    crc32mgr_nexttrack();
    ztcrc32_reset(&ztcrc32_full);
    ztcrc32_reset(&ztcrc32_track);
    memset(&crc32_err, 0, sizeof(crc32_err));
    ztcrc32_track_timestamp = ktime_get_boottime_ns();
}
void crc32mgr_update(const void *restrict data, size_t len, unsigned sample_bits)
{
    ztcrc32_update_samples(&ztcrc32_full, data, len, sample_bits);
    crc32_stats.full_crc = ztcrc32_get(&ztcrc32_full);

    crc32_stats.track_id[crc32_stats.disp_ptr] = crc32_stats.now_track;
    if (ztcrc32_update_samples(&ztcrc32_track, data, len, sample_bits)) {
        ztcrc32_track_timestamp = ktime_get_boottime_ns();
        crc32_stats.track_crc[crc32_stats.disp_ptr] = ztcrc32_get(&ztcrc32_track);
        static uac_stats_err_t cur_err;
        cur_err = uac_stats.err;
        if (memcmp(&cur_err, &crc32_err, sizeof(cur_err)) != 0) {
            crc32_stats.track_state[crc32_stats.disp_ptr] = TRACK_STATE_ERROR;
            crc32_err = cur_err;
        }
    } else if (ktime_get_boottime_ns() - ztcrc32_track_timestamp > 1000000ULL * crc32mgr_threshold) {
        crc32mgr_nexttrack();
    }
}



static void update_text(void)
{
    memset(text_buf, ' ', sizeof(text_buf));
    memset(color_buf, FG_WHITE, sizeof(color_buf));
    for (int i = 1; i < TEXTAREA_H - 1; i++) {
        text_buf[i][0] = text_buf[i][TEXTAREA_W - 1] = '\xB3';
    }
    for (int j = 1; j < TEXTAREA_W - 1; j++) {
        text_buf[0][j] = text_buf[TEXTAREA_H - 1][j] = '\xC4';
    }
    text_buf[0][0] = '\xDA';
    text_buf[0][TEXTAREA_W - 1] = '\xBF';
    text_buf[TEXTAREA_H - 1][0] = '\xC0';
    text_buf[TEXTAREA_H - 1][TEXTAREA_W - 1] = '\xD9';
    print(0, 2, "\xB4" " Low-latency USB DAC " "\xC3");


    static char str[TEXTAREA_W * TEXTAREA_H + 1];
    static uac_stats_err_t last_err;
    static uac_stats_t s;
    static crc32_stats_t c;
    s = uac_stats;
    c = crc32_stats;

    if (gui_page == 0) {
        int hh = 0, mm = 0, ss = 0, ms = 0;
        unsigned buftime = 0;
        if (s.sample_rate) {
            u32 rem;
            ss = div_u64_rem(s.n_frames, s.sample_rate, &rem);
            ms = rem * 1000 / s.sample_rate;
            hh = ss / 3600;
            ss %= 3600;
            mm = ss / 60;
            ss %= 60;

            buftime = div_u64(s.buf_frames * 10000, s.sample_rate);
        }

        u64 n_errors = s.err.overrun + s.err.underrun + s.err.usb;

        snprintf(str, sizeof(str),
            " STATE: %s\n"
            "FORMAT: %s%u$w @ %s%u$w\n"
            "  TIME: %s%02d:%02d:%02d.%03d$w\n"
            "SAMPLE: %s%llu$w\n"
            " CRC32: %s%08X$w\n"
            "BUFFER: %s%d.%01dms$w\n"
            " ERROR: %s%llu$w %s%llu$w %s%llu$w\n",
            usb_connected() ? (gadget_enabled() ? (s.running ? "$gSTREAMING$w" : "$bIDLE$w") : "$rDISABLED$w") : "$rDISCONNECTED$w",
            s.running ? "$b" : "", s.sample_rate, s.running ? "$b" : "", s.sample_bits,
            s.running ? "$g" : "", hh, mm, ss, ms,
            s.running ? "$g" : "", s.n_frames,
            s.running ? "$y" : "", c.full_crc,
            s.running ? "$b" : "", buftime / 10, buftime % 10,
            s.running && n_errors ? (s.err.overrun > last_err.overrun ? "$r" : "$y") : "", min_t(u64, 999, s.err.overrun),
            s.running && n_errors ? (s.err.underrun > last_err.underrun ? "$r" : "$y") : "", min_t(u64, 999, s.err.underrun),
            s.running && n_errors ? (s.err.usb > last_err.usb ? "$r" : "$y") : "", min_t(u64, 999, s.err.usb)
        );
        print(2, 3, str);

    } else if (gui_page == 1) {
        for (int i = (c.disp_ptr - MAX_TRACK_CRC_DISPLAY + 1 + MAX_TRACK_CRC) % MAX_TRACK_CRC, j = 0; j < MAX_TRACK_CRC_DISPLAY; i = (i + 1) % MAX_TRACK_CRC, j++) {
            const char *cc;
            if (i == c.disp_ptr)
                cc = s.running ? (c.track_state[i] == TRACK_STATE_ERROR ? "$r" : "$y") : "$w";
            else
                cc = c.track_id[i] ? (c.track_state[i] == TRACK_STATE_ERROR ? "$r" : (c.track_state[i] == TRACK_STATE_VERIFIED ? "$g" : "$w")) : "$w";
            if (c.track_id[i])
                snprintf(str, sizeof(str), "%s%02d:%08X$w", cc, c.track_id[i], c.track_crc[i]);
            else
                snprintf(str, sizeof(str), "%s--:--------$w", cc);
            if (j < MAX_TRACK_CRC_DISPLAY / 2)
                print(1 + j, 2, str);
            else
                print(1 + j - MAX_TRACK_CRC_DISPLAY / 2, 2 + 3 + 8 + 1, str);
        }
        snprintf(str, sizeof(str), "$bTRACK CRC32$w");
        print(1 + MAX_TRACK_CRC_DISPLAY / 2, 2, str);

    } else if (gui_page == 2) {
        snprintf(str, sizeof(str),
            "      $gLL-USBDAC$w\n"
            "       ver %s\n"
            "\n"
            "$bZhang Boyang$w (C) $b2021$w\n"
            "     (GNU GPLv2)\n"
            "\n"
            " $ygithub: zhangboyang$w",
            LLUSBDAC_VERSTRING
        );
        print(2, 3, str);
    }
    
    last_err = s.err;
}

static int redraw_thread_fn(void *data)
{
    while (!kthread_should_stop()) {
        // go to sleep
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule();

        // after wake up, draw gui
        update_text();
        draw_gui();
    }
    return 0;
}
void enable_gui(int en)
{
    gui_enabled = en;
    if (en) {
        request_redraw();
    }
}


void init_gui(void)
{
    if (input_register_handler(&llusbdac_input_handler) != 0) {
        panic("input_register_handler failed!");
    }

    do_dirty_hack(1);

    redraw_thread = kthread_create(redraw_thread_fn, NULL, "llusbdac_redraw");
    BUG_ON(IS_ERR(redraw_thread));
    kthread_bind(redraw_thread, 1);
    wake_up_process(redraw_thread);
}
void cleanup_gui(void)
{
    kthread_stop(redraw_thread);
    
    do_dirty_hack(0);

    input_unregister_handler(&llusbdac_input_handler);
}
