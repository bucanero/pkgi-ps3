#pragma once

#include "pkgi_db.h"

typedef struct Config {
    DbSort sort;
    DbSortOrder order;
    uint8_t content;
    uint32_t filter;
    uint8_t version_check;
    uint8_t dl_mode_background;
    uint8_t music;
    uint8_t allow_refresh;
} Config;

void pkgi_load_config(Config* config, char* update_url, uint32_t update_len);
void pkgi_save_config(const Config* config, const char* update_url, uint32_t update_len);

const char* pkgi_content_tag(ContentType content);
