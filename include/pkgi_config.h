#pragma once

#include "pkgi_db.h"

void pkgi_load_config(Config* config, char* update_url, uint32_t update_len);
void pkgi_save_config(const Config* config, const char* update_url, uint32_t update_len);

const char* pkgi_content_tag(ContentType content);
const char* pkgi_get_user_language();
