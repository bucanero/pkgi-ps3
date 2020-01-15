#pragma once

#define MDIALOG_OK      0 
#define MDIALOG_YESNO   1 

typedef struct pkgi_input pkgi_input;

void pkgi_dialog_init(void);

int pkgi_dialog_is_open(void);
int pkgi_dialog_is_cancelled(void);
void pkgi_dialog_allow_close(int allow);
void pkgi_dialog_message(const char* title, const char* text);
void pkgi_dialog_error(const char* text);
void pkgi_dialog_details(const char* title, const char* text, const char* extra);

void pkgi_dialog_start_progress(const char* title, const char* text, float progress);
void pkgi_dialog_set_progress_title(const char* title);
void pkgi_dialog_update_progress(const char* text, const char* extra, const char* eta, float progress);

void pkgi_dialog_close(void);

void pkgi_do_dialog(pkgi_input* input);

int pkgi_msg_dialog(int tdialog, const char * str);
