// dump panic message to screen
// for NW-ZX300A only
// various code snippets copied from vendor kernel (under GPL)

#define DUMP_ALL_KMSG 1
#define DUMP_TRACE    1
#define FEED_WATCHDOG 1

#define BG_COLOR 0xFF0000FF // BSOD style
//#define BG_COLOR 0xFF000000 // linux style
#define FG_COLOR 0xFFFFFFFF


/////////////////  below is super dirty

#define DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kdebug.h>
#include <asm/rodata.h>
#include <asm/cacheflush.h>
//#include <mach/sync_write.h>

extern const unsigned char fontdata_6x8[];
//extern const unsigned char fontdata_mini_4x6[];


static struct fb_info *info = NULL;

#define FONT_DATA fontdata_6x8
#define FONT_WIDTH 6
#define FONT_HEIGHT 8

#define NUM_BUF_PAGE 2
static int xx_arr[NUM_BUF_PAGE], yy_arr[NUM_BUF_PAGE];
static char *text_buf;
static char *flag_buf;
static int nx, ny;
static int pg;
#define xx (xx_arr[pg])
#define yy (yy_arr[pg])
#define tbuf(x, y) (text_buf[pg * (nx + 1) * (ny + 1) + (y) * (nx + 1) + (x)])
#define fbuf(x, y) (flag_buf[pg * (nx + 1) * (ny + 1) + (y) * (nx + 1) + (x)])

static void print_to_buffer(const char *str, int len, unsigned char flag)
{
    unsigned char ch;
    while ((ch = *str++) && len--) {
        if (xx == 0) {
            memset(&tbuf(xx, yy), ' ', nx);
            memset(&fbuf(xx, yy), 0, nx);
        }
        if (ch != '\n') {
            tbuf(xx, yy) = ch;
            fbuf(xx, yy) = flag;
        }
        if (++xx >= nx || ch == '\n') {
            xx = 0;
            if (++yy >= ny) {
                yy = 0;
            }
        }
    }
}
static void swap_buffer(void)
{
    pg = (pg + 1) % NUM_BUF_PAGE;
}
static void reset_buffer(void)
{
    pg = 0;
    memset(xx_arr, 0, sizeof(xx_arr));
    memset(yy_arr, 0, sizeof(yy_arr));
    memset(text_buf, 0, NUM_BUF_PAGE * (nx + 1) * (ny + 1));
    memset(flag_buf, 0, NUM_BUF_PAGE * (nx + 1) * (ny + 1));
}
static void alloc_buffer(void)
{
    nx = info->var.xres / FONT_WIDTH;
    ny = info->var.yres / FONT_HEIGHT;
    text_buf = kmalloc(NUM_BUF_PAGE * (nx + 1) * (ny + 1), GFP_KERNEL);
    flag_buf = kmalloc(NUM_BUF_PAGE * (nx + 1) * (ny + 1), GFP_KERNEL);
    reset_buffer();
}

static void render_fb(void)
{

	((u32 *)(info->pseudo_palette))[0] = BG_COLOR;
	((u32 *)(info->pseudo_palette))[1] = FG_COLOR;

	struct fb_image image;
	memset(&image, 0, sizeof(image));
    image.depth = 1;
    image.width = FONT_WIDTH;
    image.height = FONT_HEIGHT;

	for (int y = yy + !!xx, i = 0; i < ny; y = (y + 1) % ny, i++) {
        for (int x = 0; x < nx; x++) {
            unsigned ch = tbuf(x, y);
            if (!ch) {
                continue;
            }
            image.fg_color = !fbuf(x, y);
		    image.bg_color = !image.fg_color,
            image.data = FONT_DATA + ch * FONT_HEIGHT * DIV_ROUND_UP(FONT_WIDTH, 8);
            image.dx = x * FONT_WIDTH;
            image.dy = info->var.yoffset + i * FONT_HEIGHT;
            /*if (xx == 0) {
                struct fb_fillrect rect = {
                    .dx = 0, .dy = yy,
                    .width = info->var.xres, .height = image.height,
                    .color = 0,
                    .rop = ROP_COPY,
                };
                info->fbops->fb_fillrect(info, &rect);
                rect.dy = rect.dy + image.height;
                if (rect.dy >= info->var.yoffset + info->var.yres) {
                    rect.dy = info->var.yoffset;
                }
                rect.color = 1,
                info->fbops->fb_fillrect(info, &rect);
            }*/
            info->fbops->fb_imageblit(info, &image);
        }
	}

	//printk("%p\n", info->fbops->fb_pan_display);


	//info->var.yoffset = 0;
	//info->fbops->fb_pan_display(&info->var, info);
	//return;

	/*printk("base=%p size=%lu\n", info->screen_base, info->screen_size);
	printk("xoffset=%u, yoffset=%u, xres=%u, yres=%u, xresv=%u, yresv=%u\n",
        info->var.xoffset, info->var.yoffset,
        info->var.xres, info->var.yres,
        info->var.xres_virtual,
        info->var.yres_virtual);*/

	//return;
	//while(1);
	/*void **lcm_drv = *(void **) kallsyms_lookup_name("lcm_drv");
	for (int i = 0; i < 100; i++) {
		printk("lcm_drv[%d]=%p\n", i, lcm_drv[i]);
	}*/

/*{
#define MAKE_MTK_FB_FORMAT_ID(id, bpp)  (((id) << 8) | (bpp))
typedef enum
{
    MTK_FB_FORMAT_UNKNOWN = 0,
        
    MTK_FB_FORMAT_RGB565   = MAKE_MTK_FB_FORMAT_ID(1, 2),
    MTK_FB_FORMAT_RGB888   = MAKE_MTK_FB_FORMAT_ID(2, 3),
    MTK_FB_FORMAT_BGR888   = MAKE_MTK_FB_FORMAT_ID(3, 3),
    MTK_FB_FORMAT_ARGB8888 = MAKE_MTK_FB_FORMAT_ID(4, 4),
    MTK_FB_FORMAT_ABGR8888 = MAKE_MTK_FB_FORMAT_ID(5, 4),
    MTK_FB_FORMAT_YUV422   = MAKE_MTK_FB_FORMAT_ID(6, 2),
    MTK_FB_FORMAT_XRGB8888 = MAKE_MTK_FB_FORMAT_ID(7, 4),
    MTK_FB_FORMAT_XBGR8888 = MAKE_MTK_FB_FORMAT_ID(8, 4),
    MTK_FB_FORMAT_UYVY     = MAKE_MTK_FB_FORMAT_ID(9, 2),
    MTK_FB_FORMAT_YUV420_P = MAKE_MTK_FB_FORMAT_ID(10, 2),
    MTK_FB_FORMAT_YUY2	= MAKE_MTK_FB_FORMAT_ID(11, 2),
    MTK_FB_FORMAT_BPP_MASK = 0xFF,
} MTK_FB_FORMAT;
typedef enum
{
    MTK_FB_ORIENTATION_0   = 0,
    MTK_FB_ORIENTATION_90  = 1,
    MTK_FB_ORIENTATION_180 = 2,
    MTK_FB_ORIENTATION_270 = 3,
} MTK_FB_ORIENTATION;
typedef enum
{
	LAYER_2D 			= 0,
	LAYER_3D_SBS_0 		= 0x1,
	LAYER_3D_SBS_90 	= 0x2,
	LAYER_3D_SBS_180 	= 0x3,
	LAYER_3D_SBS_270 	= 0x4,
	LAYER_3D_TAB_0 		= 0x10,
	LAYER_3D_TAB_90 	= 0x20,
	LAYER_3D_TAB_180 	= 0x30,
	LAYER_3D_TAB_270 	= 0x40,
} MTK_FB_LAYER_TYPE;
struct fb_overlay_layer {
    unsigned int layer_id;
    unsigned int layer_enable;

    void* src_base_addr;
    void* src_phy_addr;
    unsigned int  src_direct_link;
    MTK_FB_FORMAT src_fmt;
    unsigned int  src_use_color_key;
    unsigned int  src_color_key;
    unsigned int  src_pitch;
    unsigned int  src_offset_x, src_offset_y;
    unsigned int  src_width, src_height;

    unsigned int  tgt_offset_x, tgt_offset_y;
    unsigned int  tgt_width, tgt_height;
    MTK_FB_ORIENTATION layer_rotation;
    MTK_FB_LAYER_TYPE	layer_type;
    MTK_FB_ORIENTATION video_rotation;
    
    unsigned int isTdshp;  // set to 1, will go through tdshp first, then layer blending, then to color

    // TODO remove next_buff_idx, connected_type and identity
    int next_buff_idx;
    int identity;
    int connected_type;
    unsigned int security;
	unsigned int alpha_enable;
    unsigned int alpha;
    int fence_fd;
    int ion_fd; //CL 2340210
};
	
	int (*Disp_Ovl_Engine_Set_layer_info)(int, void *) = (void *) kallsyms_lookup_name("Disp_Ovl_Engine_Set_layer_info");

        struct fb_overlay_layer layerInfo = {0};
        layerInfo.layer_id = 0;//FB_LAYER;
        layerInfo.layer_enable = 1;
        layerInfo.src_base_addr = (void *)0xdf800000;
        layerInfo.src_phy_addr = (void *)0x9dd00000;
        layerInfo.src_direct_link = 0;
        switch(info->var.bits_per_pixel)
        {
        case 16:
            layerInfo.src_fmt = MTK_FB_FORMAT_RGB565;
            break;
        case 24:
            layerInfo.src_fmt = MTK_FB_FORMAT_RGB888;
            break;
        case 32:
            layerInfo.src_fmt = MTK_FB_FORMAT_ARGB8888;
            break;
        default:
            printk("Invalid color format bpp: 0x%d\n", info->var.bits_per_pixel);
            return;
        }
        layerInfo.src_use_color_key = 0;
        layerInfo.src_color_key = 0xFF;
        layerInfo.src_pitch = ALIGN_TO(info->var.xres, disphal_get_fb_alignment());
        layerInfo.src_offset_x = 0;
        layerInfo.src_offset_y = 0;
        layerInfo.src_width = info->var.xres;
        layerInfo.src_height = info->var.yres;
        layerInfo.tgt_offset_x = 0;
        layerInfo.tgt_offset_y = 0;
        layerInfo.tgt_width = info->var.xres;
        layerInfo.tgt_height = info->var.yres;
        layerInfo.layer_rotation = MTK_FB_ORIENTATION_0;
        layerInfo.layer_type = LAYER_2D;
        layerInfo.video_rotation = MTK_FB_ORIENTATION_0;
        layerInfo.isTdshp = 1;  // set to 1, will go through tdshp first, then layer blending, then to color
        layerInfo.next_buff_idx = -1;
        layerInfo.identity = 0;
        layerInfo.connected_type = 0;
        layerInfo.security = 0;
#ifndef CONFIG_ICX_SILENT_LOG
        pr_info("[mtkfb] pan display set va=0x%x, pa=0x%x \n",(unsigned int)vaStart,paStart);
#endif
#define mtkfb_instance 0
        Disp_Ovl_Engine_Set_layer_info(mtkfb_instance, &layerInfo);

        layerInfo.layer_id = 1;
        layerInfo.layer_enable = 0;
        Disp_Ovl_Engine_Set_layer_info(mtkfb_instance, &layerInfo);
        layerInfo.layer_id = 2;
        layerInfo.layer_enable = 0;
        Disp_Ovl_Engine_Set_layer_info(mtkfb_instance, &layerInfo);
        layerInfo.layer_id = 3;
        layerInfo.layer_enable = 0;
        Disp_Ovl_Engine_Set_layer_info(mtkfb_instance, &layerInfo);
    }

void (*mtkfb_dump_layer_info)(void) = (void *) kallsyms_lookup_name("mtkfb_dump_layer_info");
mtkfb_dump_layer_info();
*/

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

    int (*disphal_get_fb_alignment)(void) = (void *) kallsyms_lookup_name("disphal_get_fb_alignment");
#define ALIGN_TO(x, n)  \
    (((x) + ((n) - 1)) & ~((n) - 1))

    DISP_OVL_ENGINE_PARAMS *p_disp_ovl_engine = (void *) kallsyms_lookup_name("disp_ovl_engine");
    #define disp_ovl_engine (*p_disp_ovl_engine)

/*
//OVL_CONFIG_STRUCT *ovl_config_tmp = &disp_ovl_engine.Instance[0].cached_layer_config[0];
//ovl_config_tmp->isDirty = true;
atomic_set(&disp_ovl_engine.Instance[0].OverlaySettingDirtyFlag, 1);
    //atomic_set(&disp_ovl_engine.Instance[0].OverlaySettingApplied, 0);
    //atomic_set(&disp_ovl_engine.Instance[0].fgCompleted, 0);
memcpy((void *)0xc0cbabb0, &disp_ovl_engine.Instance[0], sizeof(disp_ovl_engine.Instance[0]));

//disp_ovl_engine.Instance[0].status = OVERLAY_STATUS_IDLE;
atomic_set(&disp_ovl_engine.Instance[0].OverlaySettingDirtyFlag, 0);


//local_irq_disable();
//printk("ZZZZ: %d %d\n", disp_ovl_engine.RdmaRdIdx, disp_ovl_engine.OvlWrIdx);

int (*disp_ovl_engine_trigger_hw_overlay_couple) (void) = (void *) kallsyms_lookup_name("disp_ovl_engine_trigger_hw_overlay_couple");
disp_ovl_engine_trigger_hw_overlay_couple();
//printk("AAAA: %d %d\n", disp_ovl_engine.RdmaRdIdx, disp_ovl_engine.OvlWrIdx);
//local_irq_enable();
*/



//local_irq_disable();
//mdelay(1);
//printk("SET BUSY\n");
//disp_ovl_engine.Instance[0].status = OVERLAY_STATUS_BUSY;
//disp_ovl_engine.Instance[0].status = OVERLAY_STATUS_COMPLETE;
//disp_ovl_engine.RdmaRdIdx = disp_ovl_engine.OvlWrIdx;
//static int x = 0;

	//int addrs[] = {0x00040000, 0x00159400, 0x00272800, 0x0038bc00};

    const int gOvlWdmaMutexID = 3;
    int (*disp_path_config_OVL_WDMA_path)(int)= (void *) kallsyms_lookup_name("disp_path_config_OVL_WDMA_path");
    int (*disp_path_get_mutex_)(int)= (void *) kallsyms_lookup_name("disp_path_get_mutex_");
    int (*disp_path_release_mutex_)(int)= (void *) kallsyms_lookup_name("disp_path_release_mutex_");
    int (*disp_path_wait_reg_update)(int)= (void *) kallsyms_lookup_name("disp_path_wait_reg_update");
    void (*disp_path_config_OVL_WDMA)(void *, int)= (void *) kallsyms_lookup_name("disp_path_config_OVL_WDMA");
    int (*disp_path_config_layer_)(OVL_CONFIG_STRUCT* pOvlConfig,int OvlSecure) =  (void *) kallsyms_lookup_name("disp_path_config_layer_");
    unsigned int *p_fb_pa =  (void *) kallsyms_lookup_name("fb_pa");
#define fb_pa (*p_fb_pa)

    for (int i = 0; i < OVL_ENGINE_OVL_BUFFER_NUMBER; i++) {

        disp_path_config_OVL_WDMA_path(gOvlWdmaMutexID);
        disp_path_get_mutex_(gOvlWdmaMutexID);

        for (int layer_id=0; layer_id<DDP_OVL_LAYER_MUN; layer_id++) {
            OVL_CONFIG_STRUCT layer;
            memset(&layer, 0, sizeof(layer));
            layer.layer = layer_id;
            layer.layer_en = 1;
            //layer.source,   // data source (0=memory)
            layer.fmt = 16785443; // eARGB8888
            layer.addr = fb_pa + info->var.yoffset * info->fix.line_length; // addr
            //layer.src_x,  // x
            //layer.src_y,  // y
            layer.src_pitch = 4 * ALIGN_TO(info->var.xres, disphal_get_fb_alignment()); //pitch, pixel number
            //layer.dst_x,  // x
            //layer.dst_y,  // y
            layer.dst_w = info->var.xres;
            layer.dst_h = info->var.yres;
            //layer.keyEn,  //color key
            //layer.key,  //color key
            //layer.aen = 0,
            layer.alpha = 0xFF;
            disp_path_config_layer_(&layer, 0);
        }

        struct disp_path_config_mem_out_struct config;
        memset(&config, 0, sizeof(config));
        config.srcROI.width = info->var.xres;
        config.srcROI.height = info->var.yres;
        config.enable = 1;
        config.dirty = 1;
        config.outFormat = 16783393; // eRGB888
        //config.dstAddr = addrs[i];
        //config.dstAddr = addrs[disp_ovl_engine.RdmaRdIdx];
        config.dstAddr = disp_ovl_engine.OvlBufAddr[i];
            
        disp_path_config_OVL_WDMA(&config, 0);

        disp_path_release_mutex_(gOvlWdmaMutexID);
        disp_path_wait_reg_update(gOvlWdmaMutexID);

        mdelay(100);
    }

//disp_ovl_engine.RdmaRdIdx = 2;
//if(0)disp_ovl_engine.RdmaRdIdx = !disp_ovl_engine.RdmaRdIdx;


//printk("set disp_ovl_engine.RdmaRdIdx=%d\n", disp_ovl_engine.RdmaRdIdx);
//DISP_OVL_ENGINE_PARAMS zzz;
//const int x = (char *)&zzz.OvlWrIdx - (char *)&zzz;
//printk("BBBB: %d %d x=%d\n", disp_ovl_engine.RdmaRdIdx, disp_ovl_engine.OvlWrIdx, x);

//mdelay(100);
//printk("SET %d => IDLE\n", disp_ovl_engine.Instance[0].status);
//disp_ovl_engine.Instance[0].status = OVERLAY_STATUS_IDLE;
//local_irq_enable();

//printk("CCCC: %d %d\n", disp_ovl_engine.RdmaRdIdx, disp_ovl_engine.OvlWrIdx);

/*void (*dsi_update_screen)(int) = (void *) kallsyms_lookup_name("dsi_update_screen");
	dsi_update_screen(0);*/


/*void **p_disp_if_drv = (void *) kallsyms_lookup_name("disp_if_drv");
printk("disp_if_drv=%p\n", *p_disp_if_drv);*/

	

	/*
	*(int *)kallsyms_lookup_name("PanDispSettingPending") = 1;
    *(int *)kallsyms_lookup_name("PanDispSettingDirty") = 1;
    *(int *)kallsyms_lookup_name("PanDispSettingApplied") = 0;
*/

/*
	void (*Disp_Ovl_Engine_Trigger_Overlay)(int) = (void *) kallsyms_lookup_name("Disp_Ovl_Engine_Trigger_Overlay");
	Disp_Ovl_Engine_Trigger_Overlay(0);
*/


/*void (*disp_ovl_engine_trigger_hw_overlay)(void) = (void *) kallsyms_lookup_name("disp_ovl_engine_trigger_hw_overlay");
	disp_ovl_engine_trigger_hw_overlay();

void (*disp_ovl_engine_trigger_hw_overlay_decouple)(void) = (void *) kallsyms_lookup_name("disp_ovl_engine_trigger_hw_overlay_decouple");
	disp_ovl_engine_trigger_hw_overlay_decouple();*/
	//mt65xx_reg_sync_writel(0x10000000, 0xF4008028);
/*
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

*/

}


static char **p_log_buf;
static u32 *p_log_next_idx;
static u32 *p_log_first_idx;
#define log_buf (*p_log_buf)
#define log_next_idx (*p_log_next_idx)
#define log_first_idx (*p_log_first_idx)



struct log {
        u64 ts_nsec;            /* timestamp in nanoseconds */
        u16 len;                /* length of entire record */
        u16 text_len;           /* length of text buffer */
        u16 dict_len;           /* length of dictionary buffer */
        u8 facility;            /* syslog facility */
        u8 flags:5;             /* internal record flags */
        u8 level:3;             /* syslog level */
};

/* human readable text of the record */
static char *log_text(const struct log *msg)
{
        return (char *)msg + sizeof(struct log);
}

/* get record by index; idx must point to valid msg */
static struct log *log_from_idx(u32 idx)
{
        struct log *msg = (struct log *)(log_buf + idx);

        /*
         * A length == 0 record is the end of buffer marker. Wrap around and
         * read the message at the start of the buffer.
         */
        if (!msg->len)
                return (struct log *)log_buf;
        return msg;
}

/* get next record; idx must point to valid msg */
static u32 log_next(u32 idx)
{
        struct log *msg = (struct log *)(log_buf + idx);

        /* length == 0 indicates the end of the buffer; wrap */
        /*
         * A length == 0 record is the end of buffer marker. Wrap around and
         * read the message at the start of the buffer as *this* one, and
         * return the one after that.
         */
        if (!msg->len) {
                msg = (struct log *)log_buf;
                return msg->len;
        }
        return idx + msg->len;
}


static int panic_by_die = 0;
static int at_panic(struct notifier_block *this, unsigned long event, void *ptr)
{
	for (int flash = 0; flash < NUM_BUF_PAGE; flash++) {
        swap_buffer();
		int flag = DUMP_ALL_KMSG;
        int countdown = -1;
        const char *last_hdr = NULL;
		for (int idx = log_first_idx; idx != log_next_idx; idx = log_next(idx)) {
			struct log *msg = log_from_idx(idx);
            const char *str = log_text(msg);
            int len = msg->text_len;
            if (panic_by_die && strnstr(str, "[ cut here ]", len)) {
                flag = 1;
            }
            if (panic_by_die && strnstr(str, "Internal error", len)) {
				flag = 1;
                print_to_buffer("  ===== KERNEL PANIC =====  \n", -1, flash%2);
                if (!DUMP_TRACE) {
                    countdown = 1;
                }
			}
            if (!panic_by_die && strnstr(str, "Kernel panic", len)) {
				flag = 1;
                print_to_buffer("  ===== KERNEL PANIC =====  \n", -1, flash%2);
                if (!DUMP_TRACE) {
                    countdown = 1;
                }
			}
            if (flag) {
                if (!strnstr(str, "]", len)) { // strnchr is buggy?
                    last_hdr = NULL;
                } else {
                    int hdr_len = strnstr(str, "]", len) - str + 1;
                    int new_hdr = !last_hdr || strncmp(str + 1, last_hdr + 1, hdr_len - 1) != 0;
                    last_hdr = str;
                    if (new_hdr) {
                        print_to_buffer("  ", -1, 0);
                        print_to_buffer("  ", -1, 1);
                        print_to_buffer(str, hdr_len, 0);
                        print_to_buffer("  ", -1, 1);
                        print_to_buffer("\n", -1, 0);
                    }
                    str += hdr_len;
                    len -= hdr_len;
                }
				print_to_buffer(str, len, 0);
				print_to_buffer("\n", -1, 0);
                if (countdown > 0 && --countdown == 0) {
                    break;
                }
			}
            if (panic_by_die && strnstr(str, "Kernel panic", len)) {
                break;
            }
		}
    }

    while (1) {
        swap_buffer();
		render_fb();
		mdelay(100);

        if (FEED_WATCHDOG) {
            void (*mtk_wdt_restart)(int) = (void *)kallsyms_lookup_name("mtk_wdt_restart");
            mtk_wdt_restart(1);
        }
	}
    return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
    .notifier_call = at_panic,
	.priority = 100,
};

static int at_die(struct notifier_block *this, unsigned long event, void *ptr)
{
    panic_by_die = 1;

    // HACK: stop chain and avoid retval == NOTIFY_STOP
    return NOTIFY_BAD;
    //return NOTIFY_STOP;
}

static struct notifier_block die_block = {
    .notifier_call = at_die,
	.priority = 100,
};

static int __init init(void)
{
	/*
    *(int *)kallsyms_lookup_name("disp_ovl_engine_log_level") = 10;
	*(int *)kallsyms_lookup_name("disp_log_on") = 1;
	*(int *)kallsyms_lookup_name("dbg_log") = 1;
	*(int *)kallsyms_lookup_name("irq_log") = 1;
    */
    //*(void **)kallsyms_lookup_name("xLogMem") = NULL;
	printk(KERN_INFO "panic2screen init\n");

	info = registered_fb[0];
    alloc_buffer();

	p_log_buf = (void *) kallsyms_lookup_name("log_buf");
	p_log_next_idx = (void *) kallsyms_lookup_name("log_next_idx");
	p_log_first_idx = (void *) kallsyms_lookup_name("log_first_idx");

	/*printk("log_first_idx=%d\n", log_first_idx);
	printk("log_next_idx=%d\n", log_next_idx);
	for (int idx = log_first_idx; idx != log_next_idx; idx = log_next(idx)) {
		
	}
	return 0;*/

    if (0) {
        for (int j = 0; j < 2; j++) {
            swap_buffer();
            for (int i = 0; i < 456; i++) {
                char buf[1000];
                sprintf(buf, "hello world %d", i);
                if (j%2) strcat(buf, "\n");
                print_to_buffer(buf, -1, j%2);
            }
        }
        for (int i = 0; i < 10; i++) {
            swap_buffer();
            render_fb();
            mdelay(100);
        }
    }

	/*local_irq_disable();
    
	for (int i = 0; i < 5; i++) {
        char buf[1000];
        sprintf(buf, "hello world %d", i);
        printk("############################## %s\n", buf);
        
        print_to_fb(buf, -1);
        print_to_fb("\n", -1);
        //print_to_fb(buf, -1);
        print_to_fb("panic2screen loaded!", -1);
        render_fb();
        mdelay(100);

        //(void (*mtk_wdt_restart)(int) = (void *)kallsyms_lookup_name("mtk_wdt_restart");
        //mtk_wdt_restart(1);

        //at_panic(0, 0, "test");

        //void (*smp_send_stop)(void) = (void *)kallsyms_lookup_name("smp_send_stop");
        //smp_send_stop();
	}
	//atomic_notifier_chain_register(&panic_notifier_list, &panic_block);
	for (struct notifier_block *p = panic_notifier_list.head; p; p = p->next) {
		printk("panic chain %p: cb=%p prio=%d\n", p, p->notifier_call, p->priority);
	}
	if(0){
        atomic_notifier_chain_register(&panic_notifier_list, &panic_block);
        //panic("123");
	}

	local_irq_enable();*/

    atomic_notifier_chain_register(&panic_notifier_list, &panic_block);

    /*struct atomic_notifier_head *p_die_chain = (void *)kallsyms_lookup_name("die_chain");
#define die_chain (*p_die_chain)
    for (struct notifier_block *p = die_chain.head; p; p = p->next) {
		printk("die chain %p: cb=%p prio=%d\n", p, p->notifier_call, p->priority);
	}*/

    register_die_notifier(&die_block);

    
    unsigned long fn_to_nop = kallsyms_lookup_name("show_data.part.2.constprop.4");
    if (!fn_to_nop) {
        panic("bad fn_to_nop");
    }
    set_memory_rw(fn_to_nop, 1);
    memcpy((void *)fn_to_nop, "\x1e\xff\x2f\xe1", 4); // e12fff1e 	bx	lr
    set_memory_ro(fn_to_nop, 1);
    __cpuc_coherent_kern_range(fn_to_nop, fn_to_nop + 4);
    
    //BUG();
    //memcpy((void *)1, "1234", 4);
    //panic("test");

	return 0;
}
static void __exit cleanup(void)
{
    printk(KERN_INFO "panic2screen cleanup\n");

    atomic_notifier_chain_unregister(&panic_notifier_list, &panic_block);

    unregister_die_notifier(&die_block);

    kfree(text_buf);
    kfree(flag_buf);
}

module_init(init);
module_exit(cleanup);
MODULE_DESCRIPTION("panic2screen");
MODULE_LICENSE("GPL");
