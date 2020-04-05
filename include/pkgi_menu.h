#pragma once

#include "pkgi_db.h"
#include "pkgi_dialog.h"


typedef enum {
    MenuResultSearch,
    MenuResultSearchClear,
    MenuResultAccept,
    MenuResultCancel,
    MenuResultRefresh,
} MenuResult;

int pkgi_menu_is_open(void);
void pkgi_menu_get(Config* config);
MenuResult pkgi_menu_result(void);

void pkgi_menu_start(int search_clear, const Config* config);

int pkgi_do_menu(pkgi_input* input);
