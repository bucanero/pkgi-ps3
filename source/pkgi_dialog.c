#include "pkgi_dialog.h"
#include "pkgi_style.h"
#include "pkgi_utils.h"
#include "pkgi.h"

#include <sysutil/msg.h>
#include <mini18n.h>

typedef enum {
    DialogNone,
    DialogMessage,
    DialogError,
    DialogProgress,
    DialogOkCancel,
    DialogDetails
} DialogType;

static DialogType dialog_type;
static char dialog_title[256];
static char dialog_text[256];
static char dialog_extra[256];
static char dialog_eta[256];
static float dialog_progress;
static int dialog_allow_close;
static int dialog_cancelled;
static pkgi_texture pkg_icon = NULL;
static DbItem* db_item = NULL;
static pkgi_dialog_callback_t dialog_callback = NULL;

static int32_t dialog_width;
static int32_t dialog_height;
static int32_t dialog_delta;

volatile int msg_dialog_action = 0;


void pkgi_dialog_init(void)
{
    dialog_type = DialogNone;
    dialog_allow_close = 1;
}

int pkgi_dialog_is_open(void)
{
    return dialog_type != DialogNone;
}

int pkgi_dialog_is_cancelled(void)
{
    return dialog_cancelled;
}

void pkgi_dialog_allow_close(int allow)
{
    pkgi_dialog_lock();
    dialog_allow_close = allow;
    pkgi_dialog_unlock();
}

void pkgi_dialog_data_init(DialogType type, const char* title, const char* text)
{
    pkgi_strncpy(dialog_title, sizeof(dialog_title), title);
    pkgi_strncpy(dialog_text, sizeof(dialog_text), text);
    dialog_extra[0] = 0;
    dialog_eta[0] = 0;

    dialog_cancelled = 0;
    dialog_type = type;
    dialog_delta = 1;
}

void pkgi_dialog_details(DbItem *item, const char* content_type)
{
    pkgi_dialog_lock();

    pkgi_snprintf(dialog_extra, sizeof(dialog_extra), PKGI_TMP_FOLDER "/%.9s.PNG", item->content + 7);
    if (!pkg_icon && pkgi_get_size(dialog_extra)) 
        pkg_icon = pkgi_load_png_file(dialog_extra);

    pkgi_snprintf(dialog_extra, sizeof(dialog_extra), "ID: %s\n\n%s: %s - RAP(%s) SHA256(%s)", 
        item->content, _("Content"), content_type,
        (item->rap ? PKGI_UTF8_CHECK_ON : PKGI_UTF8_CHECK_OFF),
        (item->digest ? PKGI_UTF8_CHECK_ON : PKGI_UTF8_CHECK_OFF));

    pkgi_dialog_data_init(DialogDetails, item->name, dialog_extra);
    pkgi_strncpy(dialog_extra, sizeof(dialog_extra), item->description);

    db_item = item;
    pkgi_dialog_unlock();
}

void pkgi_dialog_message(const char* title, const char* text)
{
    pkgi_dialog_lock();
    pkgi_dialog_data_init(DialogMessage, title, text);
    pkgi_dialog_unlock();
}

void pkgi_dialog_ok_cancel(const char* title, const char* text, pkgi_dialog_callback_t callback)
{
    pkgi_dialog_lock();
    pkgi_dialog_data_init(DialogOkCancel, title, text);
    dialog_callback = callback;
    pkgi_dialog_unlock();
}

void pkgi_dialog_error(const char* text)
{
    pkgi_dialog_lock();
    pkgi_dialog_data_init(DialogError, _("ERROR"), text);
    pkgi_dialog_unlock();
}

void pkgi_dialog_start_progress(const char* title, const char* text, float progress)
{
    pkgi_dialog_lock();
    pkgi_dialog_data_init(DialogProgress, title, text);
    dialog_progress = progress;
    pkgi_dialog_unlock();
}

void pkgi_dialog_set_progress_title(const char* title)
{
    pkgi_dialog_lock();
    pkgi_strncpy(dialog_title, sizeof(dialog_title), title);
    pkgi_dialog_unlock();
}

void pkgi_dialog_update_progress(const char* text, const char* extra, const char* eta, float progress)
{
    pkgi_dialog_lock();

    pkgi_strncpy(dialog_text, sizeof(dialog_text), text);
    pkgi_strncpy(dialog_extra, sizeof(dialog_extra), extra ? extra : "");
    pkgi_strncpy(dialog_eta, sizeof(dialog_eta), eta ? eta : "");

    dialog_progress = (progress > 1.0f) ? 1.0f : progress;

    pkgi_dialog_unlock();
}

void pkgi_dialog_close(void)
{
    dialog_delta = -1;
}

void pkgi_do_dialog(pkgi_input* input)
{
    pkgi_dialog_lock();

    if (dialog_allow_close)
    {
        if ((dialog_type == DialogMessage || dialog_type == DialogError || dialog_type == DialogDetails) && (input->pressed & pkgi_ok_button()))
        {
            dialog_delta = -1;
        }
        else if ((dialog_type == DialogProgress || dialog_type == DialogOkCancel) && (input->pressed & pkgi_cancel_button()))
        {
            dialog_cancelled = 1;
        }
        else if (dialog_type == DialogOkCancel && (input->pressed & pkgi_ok_button()))
        {
            dialog_delta = -1;
            if (dialog_callback)
            {
                dialog_callback(MDIALOG_OK);
                dialog_callback = NULL;
            }
        }
        else if (dialog_type == DialogDetails && (input->pressed & PKGI_BUTTON_S))
        {
            int updates = pkgi_db_load_xml_updates(db_item->content, db_item->name);
            if (updates < 0)
            {
                pkgi_strncpy(dialog_text, sizeof(dialog_text), _("Failed to download the update list"));
                dialog_type = DialogError;
            }
            else
            {
                pkgi_snprintf(dialog_text, sizeof(dialog_text), "%d %s", updates, _("update(s) loaded"));
                dialog_type = DialogMessage;
            }
        }
    }

    if (dialog_delta != 0)
    {
        dialog_width += dialog_delta * (int32_t)(input->delta * PKGI_ANIMATION_SPEED / 1000);
        dialog_height += dialog_delta * (int32_t)(input->delta * PKGI_ANIMATION_SPEED / 500);

        if (dialog_delta < 0 && (dialog_width <= 0 || dialog_height <= 0))
        {
            dialog_type = DialogNone;
            dialog_text[0] = 0;
            dialog_extra[0] = 0;
            dialog_eta[0] = 0;

            dialog_width = 0;
            dialog_height = 0;
            dialog_delta = 0;

            if (pkg_icon)
            {
                pkgi_free_texture(pkg_icon);
                pkg_icon = NULL;
            }

            pkgi_dialog_unlock();
            return;
        }
        else if (dialog_delta > 0)
        {
            if (dialog_width >= PKGI_DIALOG_WIDTH && dialog_height >= PKGI_DIALOG_HEIGHT)
            {
                dialog_delta = 0;
            }
            dialog_width = min32(dialog_width, PKGI_DIALOG_WIDTH);
            dialog_height = min32(dialog_height, PKGI_DIALOG_HEIGHT);
        }
    }

    DialogType local_type = dialog_type;
    char local_title[256];
    char local_text[256];
    char local_extra[256];
    char local_eta[256];
    float local_progress = dialog_progress;
    int local_allow_close = dialog_allow_close;
    int32_t local_width = dialog_width;
    int32_t local_height = dialog_height;

    pkgi_strncpy(local_title, sizeof(local_title), dialog_title);
    pkgi_strncpy(local_text, sizeof(local_text), dialog_text);
    pkgi_strncpy(local_extra, sizeof(local_extra), dialog_extra);
    pkgi_strncpy(local_eta, sizeof(local_eta), dialog_eta);

    pkgi_dialog_unlock();

    if (local_width != 0 && local_height != 0)
    {
        pkgi_draw_fill_rect_z((VITA_WIDTH - local_width) / 2, (VITA_HEIGHT - local_height) / 2, PKGI_MENU_Z, local_width, local_height, PKGI_COLOR_MENU_BACKGROUND);
        pkgi_draw_rect_z((VITA_WIDTH - local_width) / 2, (VITA_HEIGHT - local_height) / 2, PKGI_MENU_Z, local_width, local_height, PKGI_COLOR_MENU_BORDER);
    }

    if (local_width != PKGI_DIALOG_WIDTH || local_height != PKGI_DIALOG_HEIGHT)
    {
        return;
    }

    int font_height = pkgi_text_height("M");

    int w = VITA_WIDTH - 2 * PKGI_DIALOG_HMARGIN;
    int h = VITA_HEIGHT - 2 * PKGI_DIALOG_VMARGIN;

    if (local_title[0])
    {
        uint32_t color;
        if (local_type == DialogError)
        {
            color = PKGI_COLOR_TEXT_ERROR;
        }
        else
        {
            color = PKGI_COLOR_TEXT_DIALOG;
        }

        int width = pkgi_text_width_ttf(local_title);
        if (width > w + 2 * PKGI_DIALOG_PADDING)
        {
            pkgi_clip_set(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, PKGI_DIALOG_VMARGIN + font_height, w - 2 * PKGI_DIALOG_PADDING, h - 2 * PKGI_DIALOG_PADDING);
            pkgi_draw_text_ttf(0, 0, PKGI_DIALOG_TEXT_Z, color, local_title);
            pkgi_clip_remove();
        }
        else
        {
            pkgi_draw_text_ttf((VITA_WIDTH - width) / 2, PKGI_DIALOG_VMARGIN + font_height, PKGI_DIALOG_TEXT_Z, color, local_title);
        }
    }

    if (local_type == DialogProgress)
    {
        int extraw = pkgi_text_width(local_extra);

        int availw = VITA_WIDTH - 2 * (PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING) - (extraw ? extraw + 10 : 10);
        pkgi_clip_set(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, VITA_HEIGHT / 2 - font_height - PKGI_DIALOG_PROCESS_BAR_PADDING, availw, font_height + 2);
        pkgi_draw_text_z(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, VITA_HEIGHT / 2 - font_height - PKGI_DIALOG_PROCESS_BAR_PADDING, PKGI_DIALOG_TEXT_Z, PKGI_COLOR_TEXT_DIALOG, local_text);
        pkgi_clip_remove();

        if (local_extra[0])
        {
            pkgi_draw_text_z(PKGI_DIALOG_HMARGIN + w - (PKGI_DIALOG_PADDING + extraw), VITA_HEIGHT / 2 - font_height - PKGI_DIALOG_PROCESS_BAR_PADDING, PKGI_DIALOG_TEXT_Z, PKGI_COLOR_TEXT_DIALOG, local_extra);
        }

        if (local_progress < 0)
        {
            uint32_t avail = w - 2 * PKGI_DIALOG_PADDING;

            uint32_t start = (pkgi_time_msec() / 2) % (avail + PKGI_DIALOG_PROCESS_BAR_CHUNK);
            uint32_t end = start < PKGI_DIALOG_PROCESS_BAR_CHUNK ? start : start + PKGI_DIALOG_PROCESS_BAR_CHUNK > avail + PKGI_DIALOG_PROCESS_BAR_CHUNK ? avail : start;
            start = start < PKGI_DIALOG_PROCESS_BAR_CHUNK ? 0 : start - PKGI_DIALOG_PROCESS_BAR_CHUNK;

            pkgi_draw_fill_rect_z(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, VITA_HEIGHT / 2, PKGI_MENU_Z, avail, PKGI_DIALOG_PROCESS_BAR_HEIGHT, PKGI_COLOR_PROGRESS_BACKGROUND);
            pkgi_draw_fill_rect_z(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING + start, VITA_HEIGHT / 2, PKGI_MENU_Z, end - start, PKGI_DIALOG_PROCESS_BAR_HEIGHT, PKGI_COLOR_PROGRESS_BAR);
        }
        else
        {
            pkgi_draw_fill_rect_z(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, VITA_HEIGHT / 2, PKGI_MENU_Z, w - 2 * PKGI_DIALOG_PADDING, PKGI_DIALOG_PROCESS_BAR_HEIGHT, PKGI_COLOR_PROGRESS_BACKGROUND);
            pkgi_draw_fill_rect_z(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, VITA_HEIGHT / 2, PKGI_MENU_Z, (int)((w - 2 * PKGI_DIALOG_PADDING) * local_progress), PKGI_DIALOG_PROCESS_BAR_HEIGHT, PKGI_COLOR_PROGRESS_BAR);

            char percent[256];
            pkgi_snprintf(percent, sizeof(percent), "%.0f%%", local_progress * 100.f);

            int percentw = pkgi_text_width(percent);
            pkgi_draw_text_z((VITA_WIDTH - percentw) / 2, VITA_HEIGHT / 2 + PKGI_DIALOG_PROCESS_BAR_HEIGHT + PKGI_DIALOG_PROCESS_BAR_PADDING, PKGI_DIALOG_TEXT_Z, PKGI_COLOR_TEXT_DIALOG, percent);
        }

        if (local_eta[0])
        {
            pkgi_draw_text_z(PKGI_DIALOG_HMARGIN + w - (PKGI_DIALOG_PADDING + pkgi_text_width(local_eta)), VITA_HEIGHT / 2 + PKGI_DIALOG_PROCESS_BAR_HEIGHT + PKGI_DIALOG_PROCESS_BAR_PADDING, PKGI_DIALOG_TEXT_Z, PKGI_COLOR_TEXT_DIALOG, local_eta);
        }

        if (local_allow_close)
        {
            char text[256];
            pkgi_snprintf(text, sizeof(text), _("press %s to cancel"), pkgi_ok_button() == PKGI_BUTTON_X ? PKGI_UTF8_O : PKGI_UTF8_X);
            pkgi_draw_text_z((VITA_WIDTH - pkgi_text_width(text)) / 2, PKGI_DIALOG_VMARGIN + h - 2 * font_height, PKGI_DIALOG_TEXT_Z, PKGI_COLOR_TEXT_DIALOG, text);
        }
    }
    else if (local_type == DialogDetails)
    {
        pkgi_draw_texture_z(pkg_icon, PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING + 425, PKGI_DIALOG_VMARGIN + PKGI_DIALOG_PADDING + 25, PKGI_DIALOG_TEXT_Z, 0.5);

        pkgi_draw_text_z(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, PKGI_DIALOG_VMARGIN + PKGI_DIALOG_PADDING + font_height*2, PKGI_DIALOG_TEXT_Z, PKGI_COLOR_TEXT_DIALOG, local_text);
        pkgi_draw_text_z(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, PKGI_DIALOG_VMARGIN + PKGI_DIALOG_PADDING + font_height*5, PKGI_DIALOG_TEXT_Z, PKGI_COLOR_TEXT_DIALOG, local_extra);

        if (local_allow_close)
        {
            char text[256];
            pkgi_snprintf(text, sizeof(text), _("press %s to close - %s to scan updates"), pkgi_ok_button() == PKGI_BUTTON_X ? PKGI_UTF8_X : PKGI_UTF8_O, PKGI_UTF8_S);
            pkgi_draw_text_z((VITA_WIDTH - pkgi_text_width(text)) / 2, PKGI_DIALOG_VMARGIN + h - 2 * font_height, PKGI_DIALOG_TEXT_Z, PKGI_COLOR_TEXT_DIALOG, text);
        }
    }
    else
    {
        uint32_t color;
        if (local_type == DialogMessage || local_type == DialogOkCancel)
        {
            color = PKGI_COLOR_TEXT_DIALOG;
        }
        else // local_type == DialogError
        {
            color = PKGI_COLOR_TEXT_ERROR;
        }

        int textw = pkgi_text_width(local_text);
        if (textw > w + 2 * PKGI_DIALOG_PADDING)
        {
            pkgi_clip_set(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, PKGI_DIALOG_VMARGIN + PKGI_DIALOG_PADDING, w - 2 * PKGI_DIALOG_PADDING, h - 2 * PKGI_DIALOG_PADDING);
            pkgi_draw_text_z(PKGI_DIALOG_HMARGIN + PKGI_DIALOG_PADDING, VITA_HEIGHT / 2 - font_height / 2, PKGI_DIALOG_TEXT_Z, color, local_text);
            pkgi_clip_remove();
        }
        else
        {
            pkgi_draw_text_z((VITA_WIDTH - textw) / 2, VITA_HEIGHT / 2 - font_height / 2, PKGI_DIALOG_TEXT_Z, color, local_text);
        }

        if (local_allow_close)
        {
            char text[256];
            if (local_type == DialogOkCancel)
                pkgi_snprintf(text, sizeof(text), "%s %s  %s %s", pkgi_ok_button() == PKGI_BUTTON_X ? PKGI_UTF8_X : PKGI_UTF8_O, _("Enter"), pkgi_cancel_button() == PKGI_BUTTON_O ? PKGI_UTF8_O : PKGI_UTF8_X, _("Back"));
            else
                pkgi_snprintf(text, sizeof(text), _("press %s to close"), pkgi_ok_button() == PKGI_BUTTON_X ? PKGI_UTF8_X : PKGI_UTF8_O);

            pkgi_draw_text_z((VITA_WIDTH - pkgi_text_width(text)) / 2, PKGI_DIALOG_VMARGIN + h - 2 * font_height, PKGI_DIALOG_TEXT_Z, PKGI_COLOR_TEXT_DIALOG, text);
        }
    }
}

void msg_dialog_event(msgButton button, void *userdata)
{
    switch(button) {

        case MSG_DIALOG_BTN_YES:
            msg_dialog_action = 1;
            break;
        case MSG_DIALOG_BTN_NO:
        case MSG_DIALOG_BTN_ESCAPE:
        case MSG_DIALOG_BTN_NONE:
            msg_dialog_action = 2;
            break;
        default:
		    break;
    }
}

int pkgi_msg_dialog(int tdialog, const char * str)
{
    msg_dialog_action = 0;

    msgType mtype = MSG_DIALOG_NORMAL;
    mtype |= (tdialog ? (MSG_DIALOG_BTN_TYPE_YESNO  | MSG_DIALOG_DEFAULT_CURSOR_NO) : MSG_DIALOG_BTN_TYPE_OK);

    msgDialogOpen2(mtype, str, msg_dialog_event, NULL, NULL);

    while(!msg_dialog_action)
    {
        pkgi_swap();
    }

    msgDialogAbort();
    pkgi_sleep(100);

    return (msg_dialog_action == 1);
}
