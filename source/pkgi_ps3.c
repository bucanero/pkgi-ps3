#include "pkgi.h"
#include "pkgi_style.h"
#include "font-16x32.h"

#include <sys/stat.h>
#include <sys/thread.h>
#include <sys/mutex.h>

#include <http/https.h>
#include <io/pad.h>
#include <lv2/sysfs.h>
#include <net/net.h>

#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include <ya2d/ya2d.h>

#include <dbglogger.h>


#define SCE_IME_DIALOG_MAX_TITLE_LENGTH	(128)
#define SCE_IME_DIALOG_MAX_TEXT_LENGTH	(512)

#define ANALOG_CENTER 0x78
//128
#define ANALOG_THRESHOLD 0x68
//64
#define ANALOG_SENSITIVITY 16

#define PKGI_USER_AGENT "Mozilla/5.0 (PLAYSTATION 3; 1.00)"


static sys_mutex_t g_dialog_lock;
//static volatile int g_power_lock;

static int g_ok_button;
static int g_cancel_button;
static uint32_t g_button_frame_count;
static u64 g_time;


static struct t_tex_buttons
{
    pkgi_texture circle;
    pkgi_texture cross;
    pkgi_texture triangle;
//    pkgi_texture square;
} tex_buttons;


#ifdef PKGI_ENABLE_LOGGING
void pkgi_log(const char* msg, ...)
{
    char buffer[512];

    va_list args;
    va_start(args, msg);
    vsnprintf(buffer, sizeof(buffer) - 1, msg, args);
    va_end(args);

    dbglogger_log(buffer);
}
#endif


int pkgi_snprintf(char* buffer, uint32_t size, const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    // TODO: why sceClibVsnprintf doesn't work here?
    int len = vsnprintf(buffer, size - 1, msg, args);
    va_end(args);
    buffer[len] = 0;
    return len;
}

void pkgi_vsnprintf(char* buffer, uint32_t size, const char* msg, va_list args)
{
    // TODO: why sceClibVsnprintf doesn't work here?
    int len = vsnprintf(buffer, size - 1, msg, args);
    buffer[len] = 0;
}

char* pkgi_strstr(const char* str, const char* sub)
{
    return strstr(str, sub);
}

int pkgi_stricontains(const char* str, const char* sub)
{
    return strcasestr(str, sub) != NULL;
}

int pkgi_stricmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

void pkgi_strncpy(char* dst, uint32_t size, const char* src)
{
    strncpy(dst, src, size);
}

char* pkgi_strrchr(const char* str, char ch)
{
    return strrchr(str, ch);
}

void pkgi_memcpy(void* dst, const void* src, uint32_t size)
{
    memcpy(dst, src, size);
}

void pkgi_memmove(void* dst, const void* src, uint32_t size)
{
    memmove(dst, src, size);
}

int pkgi_memequ(const void* a, const void* b, uint32_t size)
{
    return memcmp(a, b, size) == 0;
}

static void pkgi_start_debug_log(void)
{
#ifdef PKGI_ENABLE_LOGGING
    dbglogger_init_file(PKGI_APP_FOLDER "/pkgi.dbg");
//    dbglogger_init_mode(TCP_LOGGER, "192.168.1.102", 18999);
//    dbglogger_init_str("udp:239.255.0.100:30000");
//    dbglogger_init();
    LOG("PKGi PS3 logging initialized");
#endif
}

static void pkgi_stop_debug_log(void)
{
#ifdef PKGI_ENABLE_LOGGING
    dbglogger_stop();
#endif
}

int pkgi_ok_button(void)
{
    return g_ok_button;
}

int pkgi_cancel_button(void)
{
    return g_cancel_button;
}

/*
static int pkgi_power_thread(SceSize args, void *argp)
{
    return 0;

    PKGI_UNUSED(args);
    PKGI_UNUSED(argp);
    for (;;)
    {
        int lock;
        __atomic_load(&g_power_lock, &lock, __ATOMIC_SEQ_CST);
        if (lock > 0)
        {
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        }

        sceKernelDelayThread(10 * 1000 * 1000);
    }
    return 0;
}
    */

int pkgi_dialog_lock(void)
{
    int res = 0;
//    int res = sysMutexLock(g_dialog_lock, 10000);
    //int res = sceKernelLockLwMutex(&g_dialog_lock, 1, NULL);
    if (res != 0)
    {
        LOG("dialog lock failed error=0x%08x", res);
    }
    return (res == 0);
}

int pkgi_dialog_unlock(void)
{
    int res = 0;
//    int res = sysMutexUnlock(g_dialog_lock);
    //int res = sceKernelUnlockLwMutex(&g_dialog_lock, 1);
    if (res != 0)
    {
        LOG("dialog unlock failed error=0x%08x", res);
    }
    return (res == 0);
}

static int g_ime_active;

static uint16_t g_ime_title[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static uint16_t g_ime_text[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
static uint16_t g_ime_input[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

static int convert_to_utf16(const char* utf8, uint16_t* utf16, uint32_t available)
{
    int count = 0;
    while (*utf8)
    {
        uint8_t ch = (uint8_t)*utf8++;
        uint32_t code;
        uint32_t extra;

        if (ch < 0x80)
        {
            code = ch;
            extra = 0;
        }
        else if ((ch & 0xe0) == 0xc0)
        {
            code = ch & 31;
            extra = 1;
        }
        else if ((ch & 0xf0) == 0xe0)
        {
            code = ch & 15;
            extra = 2;
        }
        else
        {
            // TODO: this assumes there won't be invalid utf8 codepoints
            code = ch & 7;
            extra = 3;
        }

        for (uint32_t i=0; i<extra; i++)
        {
            uint8_t next = (uint8_t)*utf8++;
            if (next == 0 || (next & 0xc0) != 0x80)
            {
                return count;
            }
            code = (code << 6) | (next & 0x3f);
        }

        if (code < 0xd800 || code >= 0xe000)
        {
            if (available < 1) return count;
            utf16[count++] = (uint16_t)code;
            available--;
        }
        else // surrogate pair
        {
            if (available < 2) return count;
            code -= 0x10000;
            utf16[count++] = 0xd800 | (code >> 10);
            utf16[count++] = 0xdc00 | (code & 0x3ff);
            available -= 2;
        }
    }
    return count;
}

static int convert_from_utf16(const uint16_t* utf16, char* utf8, uint32_t size)
{
    int count = 0;
    while (*utf16)
    {
        uint32_t code;
        uint16_t ch = *utf16++;
        if (ch < 0xd800 || ch >= 0xe000)
        {
            code = ch;
        }
        else // surrogate pair
        {
            uint16_t ch2 = *utf16++;
            if (ch < 0xdc00 || ch > 0xe000 || ch2 < 0xd800 || ch2 > 0xdc00)
            {
                return count;
            }
            code = 0x10000 + ((ch & 0x03FF) << 10) + (ch2 & 0x03FF);
        }

        if (code < 0x80)
        {
            if (size < 1) return count;
            utf8[count++] = (char)code;
            size--;
        }
        else if (code < 0x800)
        {
            if (size < 2) return count;
            utf8[count++] = (char)(0xc0 | (code >> 6));
            utf8[count++] = (char)(0x80 | (code & 0x3f));
            size -= 2;
        }
        else if (code < 0x10000)
        {
            if (size < 3) return count;
            utf8[count++] = (char)(0xe0 | (code >> 12));
            utf8[count++] = (char)(0x80 | ((code >> 6) & 0x3f));
            utf8[count++] = (char)(0x80 | (code & 0x3f));
            size -= 3;
        }
        else
        {
            if (size < 4) return count;
            utf8[count++] = (char)(0xf0 | (code >> 18));
            utf8[count++] = (char)(0x80 | ((code >> 12) & 0x3f));
            utf8[count++] = (char)(0x80 | ((code >> 6) & 0x3f));
            utf8[count++] = (char)(0x80 | (code & 0x3f));
            size -= 4;
        }
    }
    return count;
}

void pkgi_dialog_input_text(const char* title, const char* text)
{
/*
    SceImeDialogParam param;
    sceImeDialogParamInit(&param);

    int title_len = convert_to_utf16(title, g_ime_title, PKGI_COUNTOF(g_ime_title) - 1);
    int text_len = convert_to_utf16(text, g_ime_text, PKGI_COUNTOF(g_ime_text) - 1);
    g_ime_title[title_len] = 0;
    g_ime_text[text_len] = 0;

    param.supportedLanguages = 0x0001FFFF;
    param.languagesForced = SCE_TRUE;
    param.type = SCE_IME_TYPE_DEFAULT;
    param.option = 0;
    param.title = g_ime_title;
    param.maxTextLength = 128;
    param.initialText = g_ime_text;
    param.inputTextBuffer = g_ime_input;

    int res = sceImeDialogInit(&param);
    if (res < 0)
    {
        LOG("sceImeDialogInit failed, error 0x%08x", res);
    }
    else
    {
        g_ime_active = 1;
    }
    */
}

int pkgi_dialog_input_update(void)
{
    if (!g_ime_active)
    {
        return 0;
    }
/*
    SceCommonDialogStatus status = sceImeDialogGetStatus();
    if (status == SCE_COMMON_DIALOG_STATUS_FINISHED)
    {
        SceImeDialogResult result = { 0 };
        sceImeDialogGetResult(&result);

        g_ime_active = 0;
        sceImeDialogTerm();

        if (result.button == SCE_IME_DIALOG_BUTTON_ENTER)
        {
            return 1;
        }
    }
*/
    return 0;
}

void pkgi_dialog_input_get_text(char* text, uint32_t size)
{
    int count = convert_from_utf16(g_ime_input, text, size - 1);
    text[count] = 0;
}


void pkgi_start(void)
{
//    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
//    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
//    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
//    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
/*
    sceNetInit(&net);
    sceNetCtlInit();
*/
    pkgi_start_debug_log();
    
    netInitialize();

//    LOG("initializing SSL");
//    sceSslInit(1024 * 1024);
//    sceHttpInit(1024 * 1024);

//    sceHttpsDisableOption(SCE_HTTPS_FLAG_SERVER_VERIFY);

//    sceKernelCreateLwMutex(&g_dialog_lock, "dialog_lock", 2, 0, NULL);

//    sceShellUtilInitEvents(0);
//    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_USB_CONNECTION);

//    SceAppUtilInitParam init = { 0 };
//    SceAppUtilBootParam boot = { 0 };
//    sceAppUtilInit(&init, &boot);

//    SceCommonDialogConfigParam config;
//    sceCommonDialogConfigParamInit(&config);
//    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, (int*)&config.language);
//    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int*)&config.enterButtonAssign);
//    sceCommonDialogSetConfigParam(&config);

//    if (config.enterButtonAssign == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE)
    if (false)
    {
        g_ok_button = PKGI_BUTTON_O;
        g_cancel_button = PKGI_BUTTON_X;
    }
    else
    {
        g_ok_button = PKGI_BUTTON_X;
        g_cancel_button = PKGI_BUTTON_O;
    }
    
/*
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    g_power_lock = 0;
    SceUID power_thread = sceKernelCreateThread("power_thread", &pkgi_power_thread, 0x10000100, 0x40000, 0, 0, NULL);
    if (power_thread >= 0)
    {
        sceKernelStartThread(power_thread, 0, NULL);
    }    
*/


	ya2d_init();

    tex_buttons.circle   = pkgi_load_png(CIRCLE);
    tex_buttons.cross    = pkgi_load_png(CROSS);
    tex_buttons.triangle = pkgi_load_png(TRIANGLE);
//    tex_buttons.square   = pkgi_load_png(SQUARE);

    ResetFont();
    ya2d_texturePointer = (u32 *) AddFontFromBitmapArray((u8 *) console_font_16x32, (u8 *) ya2d_texturePointer, 0, 255, 16, 32, 1, BIT7_FIRST_PIXEL);
    SetFontSize(PKGI_FONT_WIDTH, PKGI_FONT_HEIGHT);
    SetFontZ(PKGI_FONT_Z);


//    sysModuleLoad(SYSMODULE_GCM_SYS);
//    sysModuleLoad(SYSMODULE_NET);
    sysModuleLoad(SYSMODULE_HTTP);
    sysModuleLoad(SYSMODULE_HTTPS);
//    sysModuleLoad(SYSMODULE_SYSUTIL);

    LOG("initializing HTTP");
    void *http_p = http_p = malloc(0x10000);
    if(http_p) {
        if(httpInit(http_p, 0x10000) < 0) {
            LOG("network initialized");
        }
    }


    g_time = pkgi_time_msec();

    sys_mutex_attr_t mutex_attr;
    mutex_attr.attr_protocol = SYS_MUTEX_PROTOCOL_FIFO;
    mutex_attr.attr_recursive = SYS_MUTEX_ATTR_NOT_RECURSIVE;
    mutex_attr.attr_pshared = SYS_MUTEX_ATTR_PSHARED;
    mutex_attr.attr_adaptive = SYS_MUTEX_ATTR_ADAPTIVE;
    strcpy(mutex_attr.name, "dialog");

    int res = sysMutexCreate(&g_dialog_lock, &mutex_attr);
    if (res !=0) {
        LOG("mutex create error %d", res);
    }
}


int pkgi_update(pkgi_input* input)
{
	ya2d_controlsRead();

//    SceCtrlData pad = { 0 };
//    sceCtrlPeekBufferPositive(0, &pad, 1);
    
    uint32_t previous = input->down;
//0x78
//0x68
    input->down = 0;

    if (ya2d_paddata[0].BTN_CROSS)      input->down |= PKGI_BUTTON_X;
    if (ya2d_paddata[0].BTN_TRIANGLE)   input->down |= PKGI_BUTTON_T;
    if (ya2d_paddata[0].BTN_CIRCLE)     input->down |= PKGI_BUTTON_O;
    if (ya2d_paddata[0].BTN_SQUARE)     input->down |= PKGI_BUTTON_S;

    if (ya2d_paddata[0].BTN_UP)         input->down |= PKGI_BUTTON_UP;
    if (ya2d_paddata[0].BTN_DOWN)       input->down |= PKGI_BUTTON_DOWN;
    if (ya2d_paddata[0].BTN_LEFT)       input->down |= PKGI_BUTTON_LEFT;
    if (ya2d_paddata[0].BTN_RIGHT)      input->down |= PKGI_BUTTON_RIGHT;

    if (ya2d_paddata[0].BTN_L1 || ya2d_paddata[0].BTN_L2)      input->down |= PKGI_BUTTON_LT;
    if (ya2d_paddata[0].BTN_R1 || ya2d_paddata[0].BTN_R2)      input->down |= PKGI_BUTTON_RT;

    if (ya2d_paddata[0].BTN_SELECT)     input->down |= PKGI_BUTTON_SELECT;
    if (ya2d_paddata[0].BTN_START)      input->down |= PKGI_BUTTON_START;

//    if (ya2d_paddata[0].ANA_L_H < (ANALOG_CENTER - ANALOG_THRESHOLD)) input->down |= PKGI_BUTTON_LEFT;
//    if (ya2d_paddata[0].ANA_L_H > (ANALOG_CENTER + ANALOG_THRESHOLD)) input->down |= PKGI_BUTTON_RIGHT;
//    if (ya2d_paddata[0].ANA_L_V < (ANALOG_CENTER - ANALOG_THRESHOLD)) input->down |= PKGI_BUTTON_UP;
//    if (ya2d_paddata[0].ANA_L_V > (ANALOG_CENTER + ANALOG_THRESHOLD)) input->down |= PKGI_BUTTON_DOWN;
 
    input->pressed = input->down & ~previous;
    input->active = input->pressed;

    if (input->down == previous)
    {
        if (g_button_frame_count >= 10)
        {
            input->active = input->down;
        }
        g_button_frame_count++;
    }
    else
    {
        g_button_frame_count = 0;
    }

    if (input->active & PKGI_BUTTON_SELECT) {
        LOG("screenshot");
        dbglogger_screenshot_tmp(0);
    }

    if (input->active & PKGI_BUTTON_START) {
        LOG("exit");
        return 0;
    }
	ya2d_screenClear();
	ya2d_screenBeginDrawing();

    uint64_t time = pkgi_time_msec();
    input->delta = time - g_time;
    g_time = time;

    return 1;
}

void pkgi_swap(void)
{
    // LOG("vita2d pool free space = %u KB", vita2d_pool_free_space() / 1024);
	ya2d_screenFlip();
}

void pkgi_end(void)
{
    pkgi_stop_debug_log();


    pkgi_free_texture(tex_buttons.circle);
    pkgi_free_texture(tex_buttons.cross);
    pkgi_free_texture(tex_buttons.triangle);
//    pkgi_free_texture(tex_buttons.square);

//    vita2d_fini();
//    vita2d_free_pgf(g_font);
	ya2d_deinit();

    httpEnd();

    sysMutexDestroy(g_dialog_lock);
//    scePromoterUtilityExit();

//    sceAppUtilShutdown();

//    sceKernelDeleteLwMutex(&g_dialog_lock);

//    sceHttpTerm();
    //sceSslTerm();
//    sceNetCtlTerm();
//    sceNetTerm();

    sysModuleUnload(SYSMODULE_HTTPS);
    sysModuleUnload(SYSMODULE_HTTP);
//    sysModuleUnload(SYSMODULE_NET);

//    sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
//    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
//    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
//    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);

//    sceKernelExitProcess(0);
}

int pkgi_battery_present()
{
    return 0;
}

int pkgi_bettery_get_level()
{
    return 0;
}

int pkgi_battery_is_low()
{
    return 0;
}

int pkgi_battery_is_charging()
{
    return 0;
}

uint64_t pkgi_get_free_space(void)
{
    u32 blockSize;
    u64 freeSize;
    sysFsGetFreeSize("/dev_hdd0/", &blockSize, &freeSize);
    return (blockSize * freeSize);
}

const char* pkgi_get_config_folder(void)
{
    return PKGI_APP_FOLDER;
}

const char* pkgi_get_temp_folder(void)
{
    return PKGI_TMP_FOLDER;
}

const char* pkgi_get_app_folder(void)
{
    return PKGI_APP_FOLDER;
}

int pkgi_is_incomplete(const char* titleid)
{
    char path[256];
    pkgi_snprintf(path, sizeof(path), "%s/%s.resume", pkgi_get_temp_folder(), titleid);

    struct stat st;
    int res = stat(path, &st);
    return res == 0;
}

int pkgi_is_installed(const char* titleid)
{    
    int res = -1;
    char path[128];
    snprintf(path, sizeof(path), "/dev_hdd0/game/%s", titleid);

    LOG("checking if folder %s exists", path);

    struct stat sb;
    if ((stat(path, &sb) == 0) && S_ISDIR(sb.st_mode)) {
        res = 0;
    }
    
    return res == 0;
}

int pkgi_install(const char* titleid)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", pkgi_get_temp_folder(), titleid);

    LOG("calling scePromoterUtilityPromotePkgWithRif on %s", path);
    
    int res = 0;
//    int res = scePromoterUtilityPromotePkgWithRif(path, 1);
    if (res == 0)
    {
        LOG("scePromoterUtilityPromotePkgWithRif succeeded");
    }
    else
    {
        LOG("scePromoterUtilityPromotePkgWithRif failed");
    }
    return res == 0;
}

uint32_t pkgi_time_msec()
{
    u64 sec, nsec;
    sysGetCurrentTime(&sec, &nsec);
    return(nsec/1000);
}

/* static int pkgi_vita_thread(SceSize args, void* argp)
static void pkgi_vita_thread(void* argp)
{
    PKGI_UNUSED(args);
    pkgi_thread_entry* start = *((pkgi_thread_entry**)argp);
    start();
    sysThreadExit(0);
    //
	s32 running = 0;
	sys_ppu_thread_t id;
	sys_ppu_thread_stack_t stackinfo;

	sysThreadGetId(&id);
	sysThreadGetStackInformation(&stackinfo);

	LOG("stack\naddr: %p, size: %d\n",stackinfo.addr,stackinfo.size);
	while(running<5) {
		LOG("Thread: %08llX\n",(unsigned long long int)id);

		sysThreadYield();
		sleep(2);
		running++;
	}

	sysThreadExit(0);
}
*/

void pkgi_start_thread(const char* name, pkgi_thread_entry* start)
{
	s32 ret;
	sys_ppu_thread_t id;
//	u64 prio = 1500;
//	size_t stacksize = 1024*1024; //0xF000;
//	void *threadarg = (void*)0x1337;

//pkgi_vita_thread
	ret = sysThreadCreate(&id, (void (*)(void *))start, (void*)0x1337, 1500, 1024*1024, THREAD_JOINABLE, (char*)name);
	LOG("name %s | sysThreadCreate: %d\n",name, ret);

//    SceUID id = sceKernelCreateThread(name, &pkgi_vita_thread, 0x40, 1024*1024, 0, 0, NULL);
    if (ret != 0)
    {
        LOG("failed to start %s thread", name);
    }
}

void pkgi_sleep(uint32_t msec)
{
    usleep(msec);
    //sceKernelDelayThread(msec * 1000);
}

int pkgi_load(const char* name, void* data, uint32_t max)
{
    FILE* fd = fopen(name, "rb");
    if (!fd)
    {
        return -1;
    }
    
    char* data8 = data;

    int total = 0;
    
    while (max != 0)
    {
        int read = fread(data8 + total, 1, max, fd);
        if (read < 0)
        {
            total = -1;
            break;
        }
        else if (read == 0)
        {
            break;
        }
        total += read;
        max -= read;
    }

    fclose(fd);
    return total;
}

int pkgi_save(const char* name, const void* data, uint32_t size)
{
    FILE* fd = fopen(name, "wb");
    if (!fd)
    {
        return 0;
    }

    int ret = 1;
    const char* data8 = data;
    while (size != 0)
    {
        int written = fwrite(data8, 1, size, fd);
        if (written <= 0)
        {
            ret = 0;
            break;
        }
        data8 += written;
        size -= written;
    }

    fclose(fd);
    return ret;
}

void pkgi_lock_process(void)
{
/*
    if (__atomic_fetch_add(&g_power_lock, 1, __ATOMIC_SEQ_CST) == 0)
    {
        LOG("locking shell functionality");
        if (sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN) < 0)
        {
            LOG("sceShellUtilLock failed");
        }
    }
    */
}

void pkgi_unlock_process(void)
{
/*
    if (__atomic_sub_fetch(&g_power_lock, 1, __ATOMIC_SEQ_CST) == 0)
    {
        LOG("unlocking shell functionality");
        if (sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN) < 0)
        {
            LOG("sceShellUtilUnlock failed");
        }
    }
    */
}

pkgi_texture pkgi_load_png_raw(const void* data, uint32_t size)
{
	ya2d_Texture *tex = ya2d_loadPNGfromBuffer((void *)data, size);

    if (!tex)
    {
        LOG("failed to load texture");
    }
    return tex;
}

void pkgi_draw_texture(pkgi_texture texture, int x, int y)
{
	ya2d_Texture *tex = texture;
    ya2d_drawTexture(tex, x, y);
}

void pkgi_draw_textureZ(pkgi_texture texture, int x, int y, int z)
{
	ya2d_Texture *tex = texture;
    ya2d_drawTextureZ(tex, x, y, z);
}

void pkgi_free_texture(pkgi_texture texture)
{
	ya2d_Texture *tex = texture;
    ya2d_freeTexture(tex);
}


void pkgi_clip_set(int x, int y, int w, int h)
{
//    vita2d_enable_clipping();
//    vita2d_set_clip_rectangle(x, y, x + w - 1, y + h - 1);
}

void pkgi_clip_remove(void)
{
//    vita2d_disable_clipping();
}

void pkgi_draw_fill_rect(int x, int y, int w, int h, uint32_t color)
{
     ya2d_drawFillRectZ(x, y, PKGI_MENU_Z, w, h, RGBA_COLOR(color, 255));
    ya2d_drawRectZ(x, y, PKGI_MENU_Z, w, h, RGBA_COLOR(PKGI_COLOR_MENU_BORDER, 255));
}


void pkgi_draw_rect(int x, int y, int w, int h, uint32_t color)
{
	ya2d_drawRect(x, y, w, h, RGBA_COLOR(color, 255));
}

void pkgi_draw_text_z(int x, int y, int z, uint32_t color, char* text)
{
    int i=x, j=y;
    SetFontColor(RGBA_COLOR(color, 255), 0);
    while (*text) {
        if(*text == '\n') {
            j += PKGI_FONT_HEIGHT;
            text++;
            continue;
        }
        
        switch(*text) {
            case '\xfa':
                pkgi_draw_textureZ(tex_buttons.circle, i, j, z);
                *text=' ';
                break;
            case '\xfb':
                pkgi_draw_textureZ(tex_buttons.cross, i, j, z);
                *text=' ';
                break;
            case '\xfc':
                pkgi_draw_textureZ(tex_buttons.triangle, i, j, z);
                *text=' ';
                break;
            case '\xfd':
//                pkgi_draw_texture(tex_buttons.square, x+i*PKGI_FONT_WIDTH, y+3);
                *text=' ';
                break;
        }
        
        DrawChar(i, j, z, (u8) *text);
        i += PKGI_FONT_WIDTH;
        text++; 
    }    
}

void pkgi_draw_text(int x, int y, uint32_t color, const char* text)
{
    SetFontColor(RGBA_COLOR(color, 255), 0);
    DrawString((float)x, (float)y, (char *)text);
//    vita2d_pgf_draw_text(g_font, x, y + 20, RGBA_COLOR(color), 1.f, text);
}

void pkgi_draw_text_and_icons(int x, int y, uint32_t color, char* text)
{
    for (int i=0; i < strlen(text); i++) {
        switch(text[i]) {
            case '\xfa':
                pkgi_draw_texture(tex_buttons.circle, x+i*PKGI_FONT_WIDTH, y+3);
                text[i]=' ';
                break;
            case '\xfb':
                pkgi_draw_texture(tex_buttons.cross, x+i*PKGI_FONT_WIDTH, y+3);
                text[i]=' ';
                break;
            case '\xfc':
                pkgi_draw_texture(tex_buttons.triangle, x+i*PKGI_FONT_WIDTH, y+3);
                text[i]=' ';
                break;
            case '\xfd':
//                pkgi_draw_texture(tex_buttons.square, x+i*PKGI_FONT_WIDTH, y+3);
                text[i]=' ';
                break;
        }
    }
    pkgi_draw_text(x, y, color, text);
}

int pkgi_text_width(const char* text)
{
    return (strlen(text) * PKGI_FONT_WIDTH);
}

int pkgi_text_height(const char* text)
{
//    PKGI_UNUSED(text);
    return PKGI_FONT_HEIGHT;
}

#define USE_LOCAL 0

struct pkgi_http
{
    int used;
    int local;

    FILE* fd;
    uint64_t size;
    uint64_t offset;

    httpClientId client;
    httpTransId transaction;
};

static pkgi_http g_http[4];

pkgi_http* pkgi_http_get(const char* url, const char* content, uint64_t offset)
{
    LOG("http get");

    pkgi_http* http = NULL;
    for (size_t i = 0; i < 4; i++)
    {
        if (g_http[i].used == 0)
        {
            http = g_http + i;
            break;
        }
    }

    if (!http)
    {
        LOG("too many simultaneous http requests");
        return NULL;
    }

    pkgi_http* result = NULL;

    char path[256];

    if (content)
    {
        strcpy(path, pkgi_get_temp_folder());
        strcat(path, strrchr(url, '/'));

        http->fd = fopen(path, "rb");
        if (http->fd < 0)
        {
            LOG("%s not found, trying shorter path", path);
            pkgi_snprintf(path, sizeof(path), "%s/%s.pkg", pkgi_get_temp_folder(), content);
        }

        http->fd = fopen(path, "rb");
    }
    else
    {
        http->fd = NULL;
    }

    if (http->fd)
    {
        LOG("%s found, using it", path);

        struct stat st;
        if (stat(path, &st) < 0)
        {
            LOG("cannot get size of file %s", path);
            fclose(http->fd);
            return NULL;
        }

        http->used = 1;
        http->local = 1;
        http->offset = 0;
        http->size = st.st_size;

        result = http;
    }
    else
    {
        if (content)
        {
            LOG("%s not found, downloading url", path);
        }

        httpClientId clientID;
        httpTransId transID = 0;
        httpUri uri;
        int ret;
        void *uri_p = NULL;
        s32 pool_size = 0;

        LOG("starting http GET request for %s", url);

        ret = httpCreateClient(&clientID);
        if (ret < 0)
        {
            LOG("httpCreateClient failed: 0x%08x", ret);
            goto bail;
        }
        httpClientSetConnTimeout(clientID, 10 * 1000 * 1000);
        httpClientSetUserAgent(clientID, PKGI_USER_AGENT);


    	ret = httpUtilParseUri(&uri, url, NULL, 0, &pool_size);
        if (ret < 0)
        {
            LOG("httpUtilParseUri failed: 0x%08x", ret);
            goto bail;
        }

        uri_p = malloc(pool_size);
        if (!uri_p) goto bail;

        ret = httpUtilParseUri(&uri, url, uri_p, pool_size, NULL);
        if (ret < 0)
        {
            LOG("httpUtilParseUri failed: 0x%08x", ret);
            goto bail;
        }
        
        ret = httpCreateTransaction(&transID, clientID, HTTP_METHOD_GET, &uri);
        if (ret < 0)
        {
            LOG("httpCreateTransaction failed: 0x%08x", ret);
            goto bail;
        }
        
        free(uri_p); 
        uri_p = NULL;

        if (offset != 0)
        {
            char range[64];
            pkgi_snprintf(range, sizeof(range), "bytes=%llu-", offset);
//            if ((err = sceHttpAddRequestHeader(req, "Range", range, SCE_HTTP_HEADER_ADD)) < 0)
            httpHeader reqHead;
            reqHead.name = "Range";
            reqHead.value = range;
            ret = httpRequestAddHeader(transID, &reqHead);
            if (ret < 0)
            {
                LOG("httpRequestAddHeader failed: 0x%08x", ret);
                goto bail;
            }
        }


        ret = httpSendRequest(transID, NULL, 0, NULL);
        if (ret < 0)
        {
            LOG("httpSendRequest failed: 0x%08x", ret);
            goto bail;
        }

/*
        int code;
        
        ret = httpResponseGetStatusCode(transID, &code);
        if (ret < 0) goto bail;
        
        if (code == HTTP_STATUS_CODE_Not_Found || code == HTTP_STATUS_CODE_Forbidden) {ret=-4; goto bail;}
*/

/*
        if ((err = sceHttpSendRequest(req, NULL, 0)) < 0)
        {
            LOG("sceHttpSendRequest failed: 0x%08x", err);
            goto bail;
        }
*/

        http->used = 1;
        http->local = 0;
        http->client = clientID;
        http->transaction = transID;

        return(http);
//        result = http;

    bail:
        if (transID) httpDestroyTransaction(transID);        
        if (clientID) httpDestroyClient(clientID);
    }

    return result;

}

int pkgi_http_response_length(pkgi_http* http, int64_t* length)
{
    if (http->local)
    {
        *length = (int64_t)http->size;
        return 1;
    }
    else
    {
        int res;
        int status;
        if ((res = httpResponseGetStatusCode(http->transaction, &status)) < 0)
        {
            LOG("httpResponseGetStatusCode failed: 0x%08x", res);
            return 0;
        }

        LOG("http status code = %d", status);

        if (status == HTTP_STATUS_CODE_OK || status == HTTP_STATUS_CODE_Partial_Content)
        {
/*            char* headers;
            unsigned int size;
            if (sceHttpGetAllResponseHeaders(http->req, &headers, &size) >= 0)
            {
                LOG("response headers:");
                LOG("%.*s", (int)size, headers);
            }
*/
            uint64_t content_length;

	        res = httpResponseGetContentLength(http->transaction, &content_length);
            
/*            if (res == (int)SCE_HTTP_ERROR_NO_CONTENT_LENGTH || res == (int)SCE_HTTP_ERROR_CHUNK_ENC)
            {
                LOG("http response has no content length (or chunked encoding)");
                *length = 0;
            }
            else */ 
            if (res < 0)
            {
                LOG("httpResponseGetContentLength failed: 0x%08x", res);
                return 0;
            }
            else
            {
                LOG("http response length = %llu", content_length);
                *length = (int64_t)content_length;
                http->size=content_length;
            }
            return 1;
        }
        return 0;
    }
}

int pkgi_http_read(pkgi_http* http, void* buffer, uint32_t size)
{
    if (http->local)
    {
//        int read = sceIoPread(http->fd, buffer, size, http->offset);
//        int read = fread(buffer, size, http->offset, http->fd);
        int read = 1000;
        http->offset += read;
        return read;
    }
    else
    {
        // LOG("http asking to read %u bytes", size);
        s32 recv;
        int res = httpRecvResponse(http->transaction, buffer, size, &recv);

//        LOG("http read (%d) %d bytes", size, recv);
        
        if (recv < 0)
        {
            LOG("httpRecvResponse failed: 0x%08x", res);
        }
        return recv;
    }
}

void pkgi_http_close(pkgi_http* http)
{
    LOG("http close");
    if (http->local)
    {
        fclose(http->fd);
    }
    else
    {
        httpDestroyTransaction(http->transaction);        
        httpDestroyClient(http->client);
    }
    http->used = 0;
}

int pkgi_mkdirs(char* path)
{
    LOG("pkgi_mkdirs for %s", path);
    char* ptr = path;
    ptr++;
    while (*ptr)
    {
        while (*ptr && *ptr != '/')
        {
            ptr++;
        }
        char last = *ptr;
        *ptr = 0;
        LOG("mkdir %s", path);

        struct stat sb;
        if ((stat(path, &sb) == 0) && S_ISDIR(sb.st_mode)) {
            LOG("mkdir %s exists!", path);
        } 
        else
        {
            int err = mkdir(path, 0777);
            if (err < 0)
            {
                LOG("mkdir %s err=0x%08x", path, (uint32_t)err);
                return 0;
            }
        }
        
        *ptr++ = last;
        if (last == 0)
        {
            break;
        }
    }

    return 1;
}

void pkgi_rm(const char* file)
{
    struct stat sb;
    if (stat(file, &sb) == 0) {
        LOG("removing file %s", file);

        int err = sysFsUnlink(file);
        //int err = remove(file);
        if (err < 0)
        {
            LOG("error removing %s file, err=0x%08x", err);
        }
    }
}

int64_t pkgi_get_size(const char* path)
{
    struct stat st;
    int err = stat(path, &st);
    if (err < 0)
    {
        LOG("cannot get size of %s, err=0x%08x", path, err);
        return -1;
    }
    return st.st_size;
}

void* pkgi_create(const char* path)
{
    LOG("fopen create on %s", path);
    FILE* fd = fopen(path, "wb");
    if (!fd)
    {
        LOG("cannot create %s, err=0x%08x", path, fd);
        return NULL;
    }
    LOG("fopen returned fd=%d", fd);

    return (void*)fd;
}

void* pkgi_openrw(const char* path)
{
    LOG("fopen openrw on %s", path);
    FILE* fd = fopen(path, "bw+");
    if (!fd)
    {
        LOG("cannot openrw %s, err=0x%08x", path, fd);
        return NULL;
    }
    LOG("fopen returned fd=%d", fd);

    return (void*)fd;
}

void* pkgi_append(const char* path)
{
    LOG("fopen append on %s", path);
    FILE* fd = fopen(path, "ab");
    if (!fd)
    {
        LOG("cannot append %s, err=0x%08x", path, fd);
        return NULL;
    }
    LOG("fopen returned fd=%d", fd);

    return (void*)fd;
}

int pkgi_read(void* f, void* buffer, uint32_t size)
{
    LOG("asking to read %u bytes", size);
    size_t read = fread(buffer, 1, size, (FILE*)f);
    if (read < 0)
    {
        LOG("fread error 0x%08x", read);
    }
    else
    {
        LOG("read %d bytes", read);
    }
    return read;
}

int pkgi_write(void* f, const void* buffer, uint32_t size)
{
//    LOG("asking to write %u bytes", size);
    size_t write = fwrite(buffer, 1, size, (FILE*)f);
    if (write < 0)
    {
        LOG("fwrite error 0x%08x", write);
        return -1;
    }
    else
    {
//        LOG("wrote %d bytes", write);
    }
    return (uint32_t)write == size;
}

void pkgi_close(void* f)
{
    FILE *fd = (FILE*)f;
    LOG("closing file %d", fd);
    int err = fclose(fd);
    if (err < 0)
    {
        LOG("close error 0x%08x", err);
    }
}


/*


//====================================|
// DigiCert High Assurance EV Root CA |
//====================================|
static char github_cert[] __attribute__((aligned(64))) =
	"-----BEGIN CERTIFICATE-----\n"
	"MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQUFADBs\n"
	"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
	"d3cuZGlnaWNlcnQuY29tMSswKQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5j\n"
	"ZSBFViBSb290IENBMB4XDTA2MTExMDAwMDAwMFoXDTMxMTExMDAwMDAwMFowbDEL\n"
	"MAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZMBcGA1UECxMQd3d3\n"
	"LmRpZ2ljZXJ0LmNvbTErMCkGA1UEAxMiRGlnaUNlcnQgSGlnaCBBc3N1cmFuY2Ug\n"
	"RVYgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMbM5XPm\n"
	"+9S75S0tMqbf5YE/yc0lSbZxKsPVlDRnogocsF9ppkCxxLeyj9CYpKlBWTrT3JTW\n"
	"PNt0OKRKzE0lgvdKpVMSOO7zSW1xkX5jtqumX8OkhPhPYlG++MXs2ziS4wblCJEM\n"
	"xChBVfvLWokVfnHoNb9Ncgk9vjo4UFt3MRuNs8ckRZqnrG0AFFoEt7oT61EKmEFB\n"
	"Ik5lYYeBQVCmeVyJ3hlKV9Uu5l0cUyx+mM0aBhakaHPQNAQTXKFx01p8VdteZOE3\n"
	"hzBWBOURtCmAEvF5OYiiAhF8J2a3iLd48soKqDirCmTCv2ZdlYTBoSUeh10aUAsg\n"
	"EsxBu24LUTi4S8sCAwEAAaNjMGEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQF\n"
	"MAMBAf8wHQYDVR0OBBYEFLE+w2kD+L9HAdSYJhoIAu9jZCvDMB8GA1UdIwQYMBaA\n"
	"FLE+w2kD+L9HAdSYJhoIAu9jZCvDMA0GCSqGSIb3DQEBBQUAA4IBAQAcGgaX3Nec\n"
	"nzyIZgYIVyHbIUf4KmeqvxgydkAQV8GK83rZEWWONfqe/EW1ntlMMUu4kehDLI6z\n"
	"eM7b41N5cdblIZQB2lWHmiRk9opmzN6cN82oNLFpmyPInngiK3BD41VHMWEZ71jF\n"
	"hS9OMPagMRYjyOfiZRYzy78aG6A9+MpeizGLYAiJLQwGXFK3xPkKmNEVX58Svnw2\n"
	"Yzi9RKR/5CYrCsSXaQ3pjOLAEFe4yHYSkVXySGnYvCoCWw9E1CAx2/S6cCZdkGCe\n"
	"vEsXCS+0yx5DaMkHJ8HSXPfqIbloEpw8nL+e/IBcm2PN7EeqJSdnoDfzAIJ9VNep\n"
	"+OkuE6N36B9K\n"
	"-----END CERTIFICATE-----\n";
	
#define YES			1
#define NO 			0
#define TRUE 		1
#define FALSE 		0
#define SUCCESS 	1
#define FAILED	 	0
	
static char getBuffer[1024];

int download(char *url, char *dst)
{
	int ret = 0, httpCode = 0;
	s32 cert_size=0;
	httpUri uri;
	httpClientId httpClient = 0;
	httpTransId httpTrans = 0;
	FILE* fp=NULL;
	s32 nRecv = -1;
	s32 size = 0;
	u64 dl=0;
	uint64_t length = 0;
	void *http_pool = NULL;
	void *uri_pool = NULL;
	void *ssl_pool = NULL;
	void *cert_buffer = NULL;
	httpsData *caList=NULL;
	
	u8 module_https_loaded=NO;
	u8 module_http_loaded=NO;
	u8 module_net_loaded=NO;
	u8 module_ssl_loaded=NO;
	
	u8 https_init=NO;
	u8 http_init=NO;
	u8 net_init=NO;
	u8 ssl_init=NO;
	
	int cancel=NO, prog_bar1_value = 0, prog_bar2_value = 0;

	//init
	ret = sysModuleLoad(SYSMODULE_NET);
	if (ret < 0) {
		LOG("Error : sysModuleLoad(SYSMODULE_NET) failed (%x)", ret);
		ret=FAILED;
		goto end;
	} else module_net_loaded=YES;

	ret = netInitialize();
	if (ret < 0) {
		LOG("Error : netInitialize failed (%x)", ret);
		ret=FAILED;
		goto end;
	} else net_init=YES;

	ret = sysModuleLoad(SYSMODULE_HTTP);
	if (ret < 0) {
		LOG("Error : sysModuleLoad(SYSMODULE_HTTP) failed (%x)", ret);
		ret=FAILED;
		goto end;
	} else module_http_loaded=YES;

	http_pool = malloc(0x10000);
	if (http_pool == NULL) {
		LOG("Error : out of memory (http_pool)");
		ret=FAILED;
		goto end;
	}

	ret = httpInit(http_pool, 0x10000);
	if (ret < 0) {
		LOG("Error : httpInit failed (%x)", ret);
		ret=FAILED;
		goto end;
	} else http_init=YES;

	// init SSL
	if(strstr(url, "https")) {
		ret = sysModuleLoad(SYSMODULE_HTTPS);
		if (ret < 0) {
			LOG("Error : sysModuleLoad(SYSMODULE_HTTP) failed (%x)", ret);
			ret=FAILED;
			goto end;
		} else module_https_loaded=YES;

		ret = sysModuleLoad(SYSMODULE_SSL);
		if (ret < 0) {
			LOG("Error : sysModuleLoad(SYSMODULE_HTTP) failed (%x)", ret);
			ret=FAILED;
			goto end;
		} else module_ssl_loaded=YES;

		ssl_pool = malloc(0x40000);
		if (ret < 0) {
			LOG("Error : out of memory (http_pool)");
			ret=FAILED;
			goto end;
		}

		ret = sslInit(ssl_pool, 0x40000);
		if (ret < 0) {
			LOG("Error : sslInit failed (%x)", ret);
			ret=FAILED;
			goto end;
		} else ssl_init=YES;

		caList = (httpsData *)malloc(sizeof(httpsData));
		ret = sslCertificateLoader(SSL_LOAD_CERT_ALL, NULL, 0, &cert_size);
		if (ret < 0) {
			LOG("Error : sslCertificateLoader failed (%x)", ret);
			ret=FAILED;
			goto end;
		}

		cert_buffer = malloc(cert_size);
		if (cert_buffer==NULL) {
			LOG("Error : out of memory (cert_buffer)");
			ret=FAILED;
			goto end;
		}

		ret = sslCertificateLoader(SSL_LOAD_CERT_ALL, cert_buffer, cert_size, NULL);
		if (ret < 0) {
			LOG("Error : sslCertificateLoader failed (%x)", ret);
			ret=FAILED;
			goto end;
		}

		(&caList[0])->ptr = cert_buffer;
		(&caList[0])->size = cert_size;
		
		(&caList[1])->ptr = github_cert;
		(&caList[1])->size = sizeof(github_cert);

		ret = httpsInit(2, (httpsData *) caList);
		if (ret < 0) {
			LOG("Error : httpsInit failed (%x)", ret);
			ret=FAILED;
			goto end;
		} else https_init=YES;

	}
	// END of SSL
	httpClient = 0;
	httpTrans = 0;
 
	ret = httpCreateClient(&httpClient);
	if (ret < 0) {
		LOG("Error : httpCreateClient failed (%x)", ret);
		ret=FAILED;
		goto end;
	}
	// End of init

	//URI
	ret = httpUtilParseUri(&uri, url, NULL, 0, &size);
	if (ret < 0) {
		LOG("Error : httpUtilParseUri() failed (%x)", ret);
		ret=FAILED;
		goto end;
	}

	uri_pool = malloc(size);
	if (uri_pool == NULL) {
		LOG("Error : out of memory (uri_pool)");
		ret=FAILED;
		goto end;
	}

	ret = httpUtilParseUri(&uri, url, uri_pool, size, 0);
	if (ret < 0) {
		LOG("Error : httpUtilParseUri() failed (%x)", ret);
		ret=FAILED;
		goto end;
	}
	//END of URI	

	//SEND REQUEST
	ret = httpCreateTransaction(&httpTrans, httpClient, HTTP_METHOD_GET, &uri);
	if (ret < 0) {
		LOG("Error : httpCreateTransaction() failed (%x)", ret);
		ret=FAILED;
		goto end;
	}

//	if(strstr(url, "gamecovers.ezyro.com") != NULL) {
//		httpHeader headerCookie = { (const char*) "Cookie", (const char*) "__test=8f82a74fa6f891b017602f64b6aa7942" };
//		httpRequestAddHeader(httpTrans, &headerCookie);
//	}

	ret = httpSendRequest(httpTrans, NULL, 0, NULL);
	if (ret < 0) {
		LOG("Error : httpSendRequest() failed (%x)", ret);
		ret=FAILED;
		goto end;
	}
	
	//GET SIZE
	httpResponseGetContentLength(httpTrans, &length);

	ret = httpResponseGetStatusCode(httpTrans, &httpCode);
	if (ret < 0) {
		LOG("Error : cellHttpResponseGetStatusCode() failed (%x)", ret);
		ret=FAILED;
		goto end;
	}

	if(httpCode != HTTP_STATUS_CODE_OK && httpCode >= 400 ) {
		//LOG("Error : Status code (%d)", httpCode);
		ret=FAILED;
		goto end;
	}
	
//TRANSFERT
	fp=NULL;
	fp = fopen(dst, "wb");
	if(fp == NULL) {
		LOG("Error : fopen() failed : %s", dst);
		ret=FAILED;
		goto end;
	}
	
	if(length != 0) {
		if(prog_bar1_value!=-1) prog_bar2_value=0;
		else prog_bar1_value=0;
	}
	
	while(nRecv != 0) {
		if(httpRecvResponse(httpTrans, (void*) getBuffer, sizeof(getBuffer)-1, &nRecv) > 0) break;
		if(nRecv == 0)	break;
		fwrite((char*) getBuffer, nRecv, 1, fp);
		if(cancel==YES) break;
		dl+=nRecv;
		if(length != 0) {
			if(prog_bar2_value!=-1) prog_bar2_value=(dl*100)/length;
			else prog_bar1_value = (dl*100)/length;
		}
	}
	fclose(fp);
	
	if(cancel==YES) {
//		Delete(dst);
		ret=FAILED;
		cancel=NO;
	}
	
	if(prog_bar2_value!=-1) prog_bar2_value=-1;
	else prog_bar1_value=-1;
	
//END of TRANSFERT

	ret=SUCCESS;
	
end:
	if(caList) free(caList);
	if(httpTrans) httpDestroyTransaction(httpTrans);
	if(httpClient) httpDestroyClient(httpClient);
	if(https_init) httpsEnd();
	if(ssl_init) sslEnd();
	if(http_init) httpEnd();
	if(net_init) netDeinitialize();
	
	if(module_http_loaded) sysModuleUnload(SYSMODULE_HTTP);
	if(module_https_loaded) sysModuleUnload(SYSMODULE_HTTPS);
	if(module_net_loaded) sysModuleUnload(SYSMODULE_NET);
	if(module_ssl_loaded) sysModuleUnload(SYSMODULE_SSL);
	
	
	if(uri_pool) free(uri_pool);
	if(http_pool) free(http_pool);
	if(ssl_pool) free(ssl_pool);
	if(cert_buffer) free(cert_buffer);
	
	return ret;
}

*/
