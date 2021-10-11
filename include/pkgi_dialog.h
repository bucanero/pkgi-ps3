#pragma once

#include <stdint.h>
#include "pkgi_db.h"

#define MDIALOG_OK      0 
#define MDIALOG_YESNO   1 

typedef struct pkgi_input {
    uint64_t delta;   // microseconds from previous frame
    uint32_t pressed; // button pressed in last frame
    uint32_t down;    // button is currently down
    uint32_t active;  // button is pressed in last frame, or held down for a long time (10 frames)
} pkgi_input;

typedef void (*pkgi_dialog_callback_t)(int);

void pkgi_dialog_init(void);

int pkgi_dialog_is_open(void);
int pkgi_dialog_is_cancelled(void);
void pkgi_dialog_allow_close(int allow);
void pkgi_dialog_message(const char* title, const char* text);
void pkgi_dialog_error(const char* text);
void pkgi_dialog_details(DbItem* item, const char* type);
void pkgi_dialog_ok_cancel(const char* title, const char* text, pkgi_dialog_callback_t callback);

void pkgi_dialog_start_progress(const char* title, const char* text, float progress);
void pkgi_dialog_set_progress_title(const char* title);
void pkgi_dialog_update_progress(const char* text, const char* extra, const char* eta, float progress);

void pkgi_dialog_close(void);

void pkgi_do_dialog(pkgi_input* input);

int pkgi_msg_dialog(int tdialog, const char * str);
