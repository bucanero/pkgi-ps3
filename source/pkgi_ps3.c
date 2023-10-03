#include "pkgi.h"
#include "pkgi_style.h"

#include <sys/stat.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/memory.h>
#include <sys/process.h>
#include <sysutil/osk.h>

#include <io/pad.h>
#include <lv2/sysfs.h>
#include <lv2/process.h>
#include <net/net.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <ya2d/ya2d.h>
#include <curl/curl.h>

#include "ttf_render.h"

#include <mikmod.h>
#include "mikmod_loader.h"


#define OSKDIALOG_FINISHED          0x503
#define OSKDIALOG_UNLOADED          0x504
#define OSKDIALOG_INPUT_ENTERED     0x505
#define OSKDIALOG_INPUT_CANCELED    0x506

#define PKGI_OSK_INPUT_LENGTH 128

#define SCE_IME_DIALOG_MAX_TITLE_LENGTH	(128)
#define SCE_IME_DIALOG_MAX_TEXT_LENGTH	(512)

#define ANALOG_CENTER       0x78
#define ANALOG_THRESHOLD    0x68
#define ANALOG_MIN          (ANALOG_CENTER - ANALOG_THRESHOLD)
#define ANALOG_MAX          (ANALOG_CENTER + ANALOG_THRESHOLD)

#define PKGI_USER_AGENT "Mozilla/5.0 (PLAYSTATION 3; 1.00)"


struct pkgi_http
{
    int used;
    uint64_t size;
    uint64_t offset;
    CURL *curl;
};

typedef struct 
{
    pkgi_texture circle;
    pkgi_texture cross;
    pkgi_texture triangle;
    pkgi_texture square;
} t_tex_buttons;

typedef struct
{
    char *memory;
    size_t size;
} curl_memory_t;


static sys_mutex_t g_dialog_lock;
static uint32_t cpu_temp_c[2];

static int g_ok_button;
static int g_cancel_button;
static uint32_t g_button_frame_count;
static u64 g_time;

static int g_ime_active;
static int osk_action = 0;
static int osk_level = 0;

static sys_mem_container_t container_mem;
static oskCallbackReturnParam OutputReturnedParam;

volatile int osk_event = 0;
volatile int osk_unloaded = 0;

static uint16_t g_ime_title[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static uint16_t g_ime_text[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
static uint16_t g_ime_input[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

static pkgi_http g_http[4];
static t_tex_buttons tex_buttons;

static MREADER *mem_reader;
static MODULE *module;


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

uint32_t pkgi_strlen(const char *str)
{
    return strlen(str);
}

int64_t pkgi_strtoll(const char* str)
{
    int64_t res = 0;
    const char* s = str;
    if (*s && *s == '-')
    {
        s++;
    }
    while (*s)
    {
        res = res * 10 + (*s - '0');
        s++;
    }

    return str[0] == '-' ? -res : res;
}

void *pkgi_malloc(uint32_t size)
{
    return malloc(size);
}

void pkgi_free(void *ptr)
{
    free(ptr);
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
    dbglogger_init();
    LOG("PKGi PS3 logging initialized");

    dbglogger_failsafe("9999");
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

static void music_update_thread(void)
{
    while (module)
    {
        MikMod_Update();
        usleep(1000);
    }
    pkgi_thread_exit();
}

void init_music(void)
{
    MikMod_InitThreads();
    
    /* register the driver and S3M module loader */
    MikMod_RegisterDriver(&drv_psl1ght);    
    MikMod_RegisterLoader(&load_s3m);
    
    /* init the library */
    md_mode |= DMODE_SOFT_MUSIC | DMODE_STEREO | DMODE_HQMIXER | DMODE_16BITS;
    
    if (MikMod_Init("")) {
        LOG("Could not initialize sound: %s", MikMod_strerror(MikMod_errno));
        return;
    }
    
    LOG("Init %s", MikMod_InfoDriver());
    LOG("Loader %s", MikMod_InfoLoader());
    
    mem_reader = new_mikmod_mem_reader(haiku_s3m_bin, haiku_s3m_bin_size);
    module = Player_LoadGeneric(mem_reader, 64, 0);
    module->wrap = TRUE;

    pkgi_start_thread("music_thread", &music_update_thread);
}

void pkgi_start_music(void)
{
    if (module) {
        /* start module */
        LOG("Playing %s", module->songname);
        Player_Start(module);
    } else
        LOG("Could not load module: %s", MikMod_strerror(MikMod_errno));
}

void pkgi_stop_music(void)
{
    LOG("Stop music");
    Player_Stop();
}

void end_music(void)
{
    Player_Free(module);
    
    delete_mikmod_mem_reader(mem_reader);
    MikMod_Exit();
}

static int sys_game_get_temperature(int sel, u32 *temperature) 
{
    u32 temp;
  
    lv2syscall2(383, (u64) sel, (u64) &temp); 
    *temperature = (temp >> 24);
    return_to_user_prog(int);
}

int pkgi_dialog_lock(void)
{
    int res = sysMutexLock(g_dialog_lock, 0);
    if (res != 0)
    {
        LOG("dialog lock failed error=0x%08x", res);
    }
    return (res == 0);
}

int pkgi_dialog_unlock(void)
{
    int res = sysMutexUnlock(g_dialog_lock);
    if (res != 0)
    {
        LOG("dialog unlock failed error=0x%08x", res);
    }
    return (res == 0);
}

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
    utf16[count]=0;
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
    utf8[count]=0;
    return count;
}


static void osk_exit(void)
{
    if(osk_level == 2) {
        oskAbort();
        oskUnloadAsync(&OutputReturnedParam);
        
        osk_event = 0;
        osk_action=-1;
    }

    if(osk_level >= 1) {
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        sysMemContainerDestroy(container_mem);
    }

}

static void osk_event_handle(u64 status, u64 param, void * userdata)
{
    switch((u32) status) 
    {
	    case OSKDIALOG_INPUT_CANCELED:
		    osk_event = OSKDIALOG_INPUT_CANCELED;
		    break;

        case OSKDIALOG_UNLOADED:
		    osk_unloaded = 1;
		    break;

        case OSKDIALOG_INPUT_ENTERED:
	    	osk_event = OSKDIALOG_INPUT_ENTERED;
		    break;

	    case OSKDIALOG_FINISHED:
	    	osk_event = OSKDIALOG_FINISHED;
		    break;

        default:
            break;
    }
}


void pkgi_dialog_input_text(const char* title, const char* text)
{
    oskParam DialogOskParam;
    oskInputFieldInfo inputFieldInfo;
	int ret = 0;       
    osk_level = 0;
    
	if(sysMemContainerCreate(&container_mem, 8*1024*1024) < 0) {
	    ret = -1;
	    goto error_end;
    }

    osk_level = 1;

    convert_to_utf16(title, g_ime_title, PKGI_COUNTOF(g_ime_title) - 1);
    convert_to_utf16(text, g_ime_text, PKGI_COUNTOF(g_ime_text) - 1);
    
    inputFieldInfo.message =  g_ime_title;
    inputFieldInfo.startText = g_ime_text;
    inputFieldInfo.maxLength = PKGI_OSK_INPUT_LENGTH;
       
    OutputReturnedParam.res = OSK_NO_TEXT;
    OutputReturnedParam.len = PKGI_OSK_INPUT_LENGTH;
    OutputReturnedParam.str = g_ime_input;

    memset(g_ime_input, 0, sizeof(g_ime_input));

    if(oskSetKeyLayoutOption (OSK_10KEY_PANEL | OSK_FULLKEY_PANEL) < 0) {
        ret = -2; 
        goto error_end;
    }

    DialogOskParam.firstViewPanel = OSK_PANEL_TYPE_ALPHABET_FULL_WIDTH;
    DialogOskParam.allowedPanels = (OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_NUMERAL | OSK_PANEL_TYPE_ENGLISH);

    if(oskAddSupportLanguage (DialogOskParam.allowedPanels) < 0) {
        ret = -3; 
        goto error_end;
    }

    if(oskSetLayoutMode( OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER | OSK_LAYOUTMODE_VERTICAL_ALIGN_CENTER ) < 0) {
        ret = -4; 
        goto error_end;
    }

    oskPoint pos = {0.0, 0.0};

    DialogOskParam.controlPoint = pos;
    DialogOskParam.prohibitFlags = OSK_PROHIBIT_RETURN;
    if(oskSetInitialInputDevice(OSK_DEVICE_PAD) < 0) {
        ret = -5; 
        goto error_end;
    }
    
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, osk_event_handle, NULL);
    
    osk_action = 0;
    osk_unloaded = 0;
    
    if(oskLoadAsync(container_mem, (const void *) &DialogOskParam, (const void *) &inputFieldInfo) < 0) {
        ret= -6; 
        goto error_end;
    }

    osk_level = 2;

    if (ret == 0)
    {
        g_ime_active = 1;
        return;
    }

error_end:
    LOG("Keyboard Init failed, error 0x%08x", ret);

    osk_exit();
    osk_level = 0;
}

int pkgi_dialog_input_update(void)
{
    if (!g_ime_active)
    {
        return 0;
    }
    
    if (!osk_unloaded)
    {
        switch(osk_event) 
        {
            case OSKDIALOG_INPUT_ENTERED:
                oskGetInputText(&OutputReturnedParam);
                osk_event = 0;
                break;

            case OSKDIALOG_INPUT_CANCELED:
                oskAbort();
                oskUnloadAsync(&OutputReturnedParam);

                osk_event = 0;
                osk_action = -1;
                break;

            case OSKDIALOG_FINISHED:
                if (osk_action != -1) osk_action = 1;
                oskUnloadAsync(&OutputReturnedParam);
                osk_event = 0;
                break;

            default:    
                break;
        }
    }
    else
    {
        g_ime_active = 0;

        if ((OutputReturnedParam.res == OSK_OK) && (osk_action == 1))
        {
            osk_exit();
            return 1;
        } 
         
        osk_exit();
    }

    return 0;
}

void pkgi_dialog_input_get_text(char* text, uint32_t size)
{
    convert_from_utf16(g_ime_input, text, size - 1);
    LOG("input: %s", text);
}

void load_ttf_fonts()
{
	LOG("loading TTF fonts");
	
	TTFUnloadFont();
	TTFLoadFont(0, "/dev_flash/data/font/SCE-PS3-SR-R-LATIN2.TTF", NULL, 0);
	TTFLoadFont(1, "/dev_flash/data/font/SCE-PS3-DH-R-CGB.TTF", NULL, 0);
	TTFLoadFont(2, "/dev_flash/data/font/SCE-PS3-SR-R-JPN.TTF", NULL, 0);
	TTFLoadFont(3, "/dev_flash/data/font/SCE-PS3-YG-R-KOR.TTF", NULL, 0);
	
	ya2d_texturePointer = (u32*) init_ttf_table((u16*) ya2d_texturePointer);
}

static void sys_callback(uint64_t status, uint64_t param, void* userdata)
{
    switch (status) {
        case SYSUTIL_EXIT_GAME:
            pkgi_end();
            sysProcessExit(1);
            break;
        
        case SYSUTIL_MENU_OPEN:
        case SYSUTIL_MENU_CLOSE:
            break;

        default:
            break;
    }
}

void pkgi_start(void)
{
    pkgi_start_debug_log();
    
    netInitialize();

    LOG("initializing Network");
    sysModuleLoad(SYSMODULE_NET);
    curl_global_init(CURL_GLOBAL_ALL);

    sys_mutex_attr_t mutex_attr;
    mutex_attr.attr_protocol = SYS_MUTEX_PROTOCOL_FIFO;
    mutex_attr.attr_recursive = SYS_MUTEX_ATTR_NOT_RECURSIVE;
    mutex_attr.attr_pshared = SYS_MUTEX_ATTR_NOT_PSHARED;
    mutex_attr.attr_adaptive = SYS_MUTEX_ATTR_ADAPTIVE;
    strcpy(mutex_attr.name, "dialog");

    int ret = sysMutexCreate(&g_dialog_lock, &mutex_attr);
    if (ret != 0) {
        LOG("mutex create error (%x)", ret);
    }

    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_ENTER_BUTTON_ASSIGN, &ret);
    if (ret == 0)
    {
        g_ok_button = PKGI_BUTTON_O;
        g_cancel_button = PKGI_BUTTON_X;
    }
    else
    {
        g_ok_button = PKGI_BUTTON_X;
        g_cancel_button = PKGI_BUTTON_O;
    }
    
	ya2d_init();

	ya2d_paddata[0].ANA_L_H = ANALOG_CENTER;
	ya2d_paddata[0].ANA_L_V = ANALOG_CENTER;

    tex_buttons.circle   = pkgi_load_image_buffer(CIRCLE, png);
    tex_buttons.cross    = pkgi_load_image_buffer(CROSS, png);
    tex_buttons.triangle = pkgi_load_image_buffer(TRIANGLE, png);
    tex_buttons.square   = pkgi_load_image_buffer(SQUARE, png);

    SetFontSize(PKGI_FONT_WIDTH, PKGI_FONT_HEIGHT);
    SetFontZ(PKGI_FONT_Z);

    load_ttf_fonts();

    pkgi_mkdirs(PKGI_TMP_FOLDER);
    pkgi_mkdirs(PKGI_RAP_FOLDER);

    init_music();

    // register exit callback
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sys_callback, NULL);

    g_time = pkgi_time_msec();
}

int pkgi_update(pkgi_input* input)
{
	ya2d_controlsRead();
    
    uint32_t previous = input->down;
    memcpy(&input->down, &ya2d_paddata[0].button[2], sizeof(uint32_t));

    if (ya2d_paddata[0].ANA_L_V < ANALOG_MIN)
        input->down |= PKGI_BUTTON_UP;
        
    if (ya2d_paddata[0].ANA_L_V > ANALOG_MAX)
        input->down |= PKGI_BUTTON_DOWN;
        
    if (ya2d_paddata[0].ANA_L_H < ANALOG_MIN)
        input->down |= PKGI_BUTTON_LEFT;
        
    if (ya2d_paddata[0].ANA_L_H > ANALOG_MAX)
        input->down |= PKGI_BUTTON_RIGHT;

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

#ifdef PKGI_ENABLE_LOGGING
    if ((input->active & PKGI_BUTTON_RIGHT) && (input->active & PKGI_BUTTON_LEFT)) {
        LOG("screenshot");
        dbglogger_screenshot_tmp(0);
    }
#endif

	ya2d_screenClear();
	ya2d_screenBeginDrawing();
	reset_ttf_frame();

    uint64_t time = pkgi_time_msec();
    input->delta = time - g_time;
    g_time = time;

    return 1;
}

void pkgi_swap(void)
{
	ya2d_screenFlip();
}

void pkgi_end(void)
{
    if (module) end_music();

    curl_global_cleanup();
    pkgi_stop_debug_log();

    pkgi_free_texture(tex_buttons.circle);
    pkgi_free_texture(tex_buttons.cross);
    pkgi_free_texture(tex_buttons.triangle);
    pkgi_free_texture(tex_buttons.square);

	ya2d_deinit();

    sysMutexDestroy(g_dialog_lock);

#ifdef PKGI_ENABLE_LOGGING
    sysProcessExitSpawn2("/dev_hdd0/game/PSL145310/RELOAD.SELF", NULL, NULL, NULL, 0, 1001, SYS_PROCESS_SPAWN_STACK_SIZE_1M);
#endif

    sysProcessExit(0);
}

int pkgi_get_temperature(uint8_t cpu)
{
    static uint32_t t = 0;

    if (t++ % 0x100 == 0)
    {
        sys_game_get_temperature(0, &cpu_temp_c[0]);
        sys_game_get_temperature(1, &cpu_temp_c[1]);
    }

    return cpu_temp_c[cpu];
}

int pkgi_temperature_is_high(void)
{
    return ((cpu_temp_c[0] >= 70 || cpu_temp_c[1] >= 70));
}

uint64_t pkgi_get_free_space(void)
{
    u32 blockSize;
    static uint32_t t = 0;
    static uint64_t freeSize = 0;

    if (t++ % 0x200 == 0)
    {
        sysFsGetFreeSize("/dev_hdd0/", &blockSize, &freeSize);
        freeSize *= blockSize;
    }

    return (freeSize);
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
    return (res == 0);
}

int pkgi_dir_exists(const char* path)
{
    LOG("checking if folder %s exists", path);

    struct stat sb;
    if ((stat(path, &sb) == 0) && S_ISDIR(sb.st_mode)) {
        return 1;
    }
    return 0;
}

int pkgi_is_installed(const char* content)
{    
    char path[128];
    snprintf(path, sizeof(path), "/dev_hdd0/game/%.9s", content + 7);

    return (pkgi_dir_exists(path));
}

uint32_t pkgi_time_msec()
{
    return ya2d_millis();
}

void pkgi_thread_exit()
{
	sysThreadExit(0);
}

void pkgi_start_thread(const char* name, pkgi_thread_entry* start)
{
	s32 ret;
	sys_ppu_thread_t id;

	ret = sysThreadCreate(&id, (void (*)(void *))start, NULL, 1500, 1024*1024, THREAD_JOINABLE, (char*)name);
	LOG("sysThreadCreate: %s (0x%08x)",name, id);

    if (ret != 0)
    {
        LOG("failed to start %s thread", name);
    }
}

void pkgi_sleep(uint32_t msec)
{
    usleep(msec * 1000);
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

pkgi_texture pkgi_load_jpg_raw(const void* data, uint32_t size)
{
    ya2d_Texture *tex = ya2d_loadJPGfromBuffer(data, size);

    if (!tex)
    {
        LOG("failed to load texture");
    }
    return tex;
}

pkgi_texture pkgi_load_png_raw(const void* data, uint32_t size)
{
    ya2d_Texture *tex = ya2d_loadPNGfromBuffer(data, size);

    if (!tex)
    {
        LOG("failed to load texture");
    }
    return tex;
}

pkgi_texture pkgi_load_png_file(const char* filename)
{
    ya2d_Texture *tex = ya2d_loadPNGfromFile(filename);

    if (!tex)
    {
        LOG("failed to load texture file %s", filename);
    }
    return tex;
}

void pkgi_draw_texture(pkgi_texture texture, int x, int y)
{
    ya2d_drawTexture((ya2d_Texture*) texture, x, y);
}

void pkgi_draw_background(pkgi_texture texture)
{
    ya2d_drawTextureEx((ya2d_Texture*) texture, 0, 0, YA2D_DEFAULT_Z, VITA_WIDTH, VITA_HEIGHT);
}

void pkgi_draw_texture_z(pkgi_texture texture, int x, int y, int z, float scale)
{
    ya2d_drawTextureZ((ya2d_Texture*) texture, x, y, z, scale);
}

void pkgi_free_texture(pkgi_texture texture)
{
    ya2d_freeTexture((ya2d_Texture*) texture);
}


void pkgi_clip_set(int x, int y, int w, int h)
{
    set_ttf_window(x, y, w, h*2, 0);
}

void pkgi_clip_remove(void)
{
    set_ttf_window(0, 0, VITA_WIDTH, VITA_HEIGHT, 0);
}

void pkgi_draw_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    ya2d_drawFillRect(x, y, w, h, RGBA_COLOR(color, 255));
}

void pkgi_draw_fill_rect_z(int x, int y, int z, int w, int h, uint32_t color)
{
    ya2d_drawFillRectZ(x, y, z, w, h, RGBA_COLOR(color, 255));
}

void pkgi_draw_rect_z(int x, int y, int z, int w, int h, uint32_t color)
{
	ya2d_drawRectZ(x, y, z, w, h, RGBA_COLOR(color, 255));
}

void pkgi_draw_rect(int x, int y, int w, int h, uint32_t color)
{
	ya2d_drawRect(x, y, w, h, RGBA_COLOR(color, 255));
}

void pkgi_draw_text_z(int x, int y, int z, uint32_t color, const char* text)
{
    int i=x, j=y;
    SetFontColor(RGBA_COLOR(color, 255), 0);
    while (*text) {
        switch(*text) {
            case '\n':
                i = x;
                j += PKGI_FONT_HEIGHT;
                text++;
                continue;
            case '\xfa':
                pkgi_draw_texture_z(tex_buttons.circle, i, j, z, 0.5f);
                i += PKGI_FONT_WIDTH;
                text++;
                continue;
            case '\xfb':
                pkgi_draw_texture_z(tex_buttons.cross, i, j, z, 0.5f);
                i += PKGI_FONT_WIDTH;
                text++;
                continue;
            case '\xfc':
                pkgi_draw_texture_z(tex_buttons.triangle, i, j, z, 0.5f);
                i += PKGI_FONT_WIDTH;
                text++;
                continue;
            case '\xfd':
                pkgi_draw_texture_z(tex_buttons.square, i, j, z, 0.5f);
                i += PKGI_FONT_WIDTH;
                text++;
                continue;
        }
        
        DrawChar(i, j, z, (u8) *text);
        i += PKGI_FONT_WIDTH;
        text++; 
    }    
}


void pkgi_draw_text_ttf(int x, int y, int z, uint32_t color, const char* text)
{
    Z_ttf = z;
    display_ttf_string(x+PKGI_FONT_SHADOW, y+PKGI_FONT_SHADOW, text, RGBA_COLOR(PKGI_COLOR_TEXT_SHADOW, 128), 0, PKGI_FONT_WIDTH+6, PKGI_FONT_HEIGHT+2);
    display_ttf_string(x, y, text, RGBA_COLOR(color, 255), 0, PKGI_FONT_WIDTH+6, PKGI_FONT_HEIGHT+2);
}

int pkgi_text_width_ttf(const char* text)
{
    return (display_ttf_string(0, 0, text, 0, 0, PKGI_FONT_WIDTH+6, PKGI_FONT_HEIGHT+2));
}


void pkgi_draw_text(int x, int y, uint32_t color, const char* text)
{
    SetFontColor(RGBA_COLOR(PKGI_COLOR_TEXT_SHADOW, 128), 0);
    DrawString((float)x+PKGI_FONT_SHADOW, (float)y+PKGI_FONT_SHADOW, (char *)text);

    SetFontColor(RGBA_COLOR(color, 200), 0);
    DrawString((float)x, (float)y, (char *)text);
}


int pkgi_text_width(const char* text)
{
    return (strlen(text) * PKGI_FONT_WIDTH) + PKGI_FONT_SHADOW;
}

int pkgi_text_height(const char* text)
{
    return PKGI_FONT_HEIGHT + PKGI_FONT_SHADOW+1;
}

int pkgi_validate_url(const char* url)
{
    if (url[0] == 0)
    {
        return 0;
    }
    if ((pkgi_strstr(url, "http://") == url) || (pkgi_strstr(url, "https://") == url) ||
        (pkgi_strstr(url, "ftp://") == url)  || (pkgi_strstr(url, "ftps://") == url))
    {
        return 1;
    }
    return 0;
}

void pkgi_curl_init(CURL *curl)
{
    // Set user agent string
    curl_easy_setopt(curl, CURLOPT_USERAGENT, PKGI_USER_AGENT);
    // don't verify the certificate's name against host
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // don't verify the peer's SSL certificate
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    // Set SSL VERSION to TLS 1.2
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    // Set timeout for the connection to build
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // maximum number of redirects allowed
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 20L);
    // Fail the request if the HTTP code returned is equal to or larger than 400
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    // request using SSL for the FTP transfer if available
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
}

pkgi_http* pkgi_http_get(const char* url, const char* content, uint64_t offset)
{
    LOG("http get");

    if (!pkgi_validate_url(url))
    {
        LOG("unsupported URL (%s)", url);
        return NULL;
    }

    pkgi_http* http = NULL;
    for (size_t i = 0; i < 4; i++)
    {
        if (g_http[i].used == 0)
        {
            http = &g_http[i];
            break;
        }
    }

    if (!http)
    {
        LOG("too many simultaneous http requests");
        return NULL;
    }

    http->curl = curl_easy_init();
    if (!http->curl)
    {
        LOG("curl init error");
        return NULL;
    }

    pkgi_curl_init(http->curl);
    curl_easy_setopt(http->curl, CURLOPT_URL, url);

    LOG("starting http GET request for %s", url);

    if (offset != 0)
    {
        LOG("setting http offset %ld", offset);
        /* resuming upload at this position */
        curl_easy_setopt(http->curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t) offset);
    }

    http->used = 1;
    return(http);
}

int pkgi_http_response_length(pkgi_http* http, int64_t* length)
{
    CURLcode res;

    // do the download request without getting the body
    curl_easy_setopt(http->curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(http->curl, CURLOPT_NOPROGRESS, 1L);

    // Perform the request
    res = curl_easy_perform(http->curl);

    if(res != CURLE_OK)
    {
        LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        return 0;
    }

    long status = 0;
    curl_easy_getinfo(http->curl, CURLINFO_RESPONSE_CODE, &status);
    LOG("http status code = %d", status);

    curl_easy_getinfo(http->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, length);
    LOG("http response length = %llu", *length);
    http->size = *length;

    return 1;
}

int pkgi_http_read(pkgi_http* http, void* write_func, void* xferinfo_func)
{
    CURLcode res;

    curl_easy_setopt(http->curl, CURLOPT_NOBODY, 0L);
    // The function that will be used to write the data
    curl_easy_setopt(http->curl, CURLOPT_WRITEFUNCTION, write_func);
    // The data file descriptor which will be written to
    curl_easy_setopt(http->curl, CURLOPT_WRITEDATA, NULL);

    if (xferinfo_func)
    {
        /* pass the struct pointer into the xferinfo function */
        curl_easy_setopt(http->curl, CURLOPT_XFERINFOFUNCTION, xferinfo_func);
        curl_easy_setopt(http->curl, CURLOPT_XFERINFODATA, NULL);
        curl_easy_setopt(http->curl, CURLOPT_NOPROGRESS, 0L);
    }

    // Perform the request
    res = curl_easy_perform(http->curl);

    if(res != CURLE_OK)
    {
        LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        return 0;
    }

    return 1;
}

void pkgi_http_close(pkgi_http* http)
{
    LOG("http close");
    curl_easy_cleanup(http->curl);

    http->used = 0;
}

int pkgi_mkdirs(const char* dir)
{
    char path[256];
    pkgi_snprintf(path, sizeof(path), "%s", dir);
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

        if (!pkgi_dir_exists(path))
        {
            LOG("mkdir %s", path);
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

        int err = unlink(file);
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

void* pkgi_open(const char* path)
{
    LOG("fopen open rb on %s", path);
    FILE* fd = fopen(path, "rb");
    if (!fd)
    {
        LOG("cannot open %s, err=0x%08x", path, fd);
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
    size_t write = fwrite(buffer, size, 1, (FILE*)f);
    if (write < 0)
    {
        LOG("fwrite error 0x%08x", write);
    }
    else
    {
//        LOG("wrote %d bytes", write);
    }
    return (write == 1);
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

static size_t curl_write_memory(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    curl_memory_t *mem = (curl_memory_t *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr)
    {
        /* out of memory! */
        LOG("not enough memory (realloc)");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

char * pkgi_http_download_buffer(const char* url, uint32_t* buf_size)
{
    CURL *curl;
    CURLcode res;
    curl_memory_t chunk;

    curl = curl_easy_init();
    if(!curl)
    {
        LOG("cURL init error");
        return NULL;
    }
    
    chunk.memory = malloc(1);   /* will be grown as needed by the realloc above */
    chunk.size = 0;             /* no data at this point */

    pkgi_curl_init(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    // The function that will be used to write the data
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_memory);
    // The data file descriptor which will be written to
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    // Perform the request
    res = curl_easy_perform(curl);

    if(res != CURLE_OK)
    {
        LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(chunk.memory);
        return NULL;
    }

    LOG("%lu bytes retrieved", (unsigned long)chunk.size);
    // clean-up
    curl_easy_cleanup(curl);

    *buf_size = chunk.size;
    return (chunk.memory);
}

const char * pkgi_get_user_language()
{
    int language;

    if(sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_LANG, &language) < 0)
        return "en";

    switch (language)
    {
    case SYSUTIL_LANG_JAPANESE:             //  0   Japanese
        return "ja";

    case SYSUTIL_LANG_ENGLISH_US:           //  1   English (United States)
    case SYSUTIL_LANG_ENGLISH_GB:           // 18   English (United Kingdom)
        return "en";

    case SYSUTIL_LANG_FRENCH:               //  2   French
        return "fr";

    case SYSUTIL_LANG_SPANISH:              //  3   Spanish
        return "es";

    case SYSUTIL_LANG_GERMAN:               //  4   German
        return "de";

    case SYSUTIL_LANG_ITALIAN:              //  5   Italian
        return "it";

    case SYSUTIL_LANG_DUTCH:                //  6   Dutch
        return "nl";

    case SYSUTIL_LANG_RUSSIAN:              //  8   Russian
        return "ru";

    case SYSUTIL_LANG_KOREAN:               //  9   Korean
        return "ko";

    case SYSUTIL_LANG_CHINESE_T:            // 10   Chinese (traditional)
    case SYSUTIL_LANG_CHINESE_S:            // 11   Chinese (simplified)
        return "ch";

    case SYSUTIL_LANG_FINNISH:              // 12   Finnish
        return "fi";

    case SYSUTIL_LANG_SWEDISH:              // 13   Swedish
        return "sv";

    case SYSUTIL_LANG_DANISH:               // 14   Danish
        return "da";

    case SYSUTIL_LANG_NORWEGIAN:            // 15   Norwegian
        return "no";

    case SYSUTIL_LANG_POLISH:               // 16   Polish
        return "pl";

    case SYSUTIL_LANG_PORTUGUESE_PT:        //  7   Portuguese (Portugal)
    case SYSUTIL_LANG_PORTUGUESE_BR:        // 17   Portuguese (Brazil)
        return "pt";

    case SYSUTIL_LANG_TURKISH:              // 19   Turkish
        return "tr";

    default:
        break;
    }

    return "en";
}
