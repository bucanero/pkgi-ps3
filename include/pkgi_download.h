#pragma once

#include <stdint.h>

#define PKGI_RAP_SIZE 16

int pkgi_download(const char* content, const char* url, const uint8_t* rif, const uint8_t* digest);
