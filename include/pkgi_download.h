#pragma once

#include <stdint.h>
#include "pkgi_db.h"

#define PKGI_RAP_SIZE 16

int pkgi_download(const DbItem* item, const int background_dl);
int pkgi_download_icon(const char* content);
char * pkgi_http_download_buffer(const char* url, uint32_t* buf_size);

int rap2rif(const uint8_t* rap, const char* content_id, const char *exdata_path);
