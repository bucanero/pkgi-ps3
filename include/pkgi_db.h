#pragma once

#include <stdint.h>

#define MAX_CONTENT_TYPES 10

typedef enum {
    PresenceUnknown,
    PresenceIncomplete,
    PresenceInstalled,
    PresenceMissing,
} DbPresence;

typedef enum {
    SortByTitle,
    SortByRegion,
    SortByName,
    SortBySize,
} DbSort;

typedef enum {
    SortAscending,
    SortDescending,
} DbSortOrder;

typedef enum {
    DbFilterRegionASA = 0x01,
    DbFilterRegionEUR = 0x02,
    DbFilterRegionJPN = 0x04,
    DbFilterRegionUSA = 0x08,

    // TODO: implement these two
    DbFilterInstalled = 0x10,
    DbFilterMissing   = 0x20,

    DbFilterContentGame     = 0x000100,
    DbFilterContentDLC      = 0x000200,
    DbFilterContentTheme    = 0x000400,
    DbFilterContentAvatar   = 0x000800,
    DbFilterContentDemo     = 0x001000,
    DbFilterContentManager  = 0x002000,
    DbFilterContentEmulator = 0x004000,
    DbFilterContentApp      = 0x008000,
    DbFilterContentTool     = 0x010000,

    DbFilterAllRegions = DbFilterRegionUSA | DbFilterRegionEUR | DbFilterRegionJPN | DbFilterRegionASA,
    DbFilterAllContent = DbFilterContentGame | DbFilterContentDLC | DbFilterContentTheme | DbFilterContentAvatar | 
                         DbFilterContentDemo | DbFilterContentManager | DbFilterContentEmulator | DbFilterContentApp | DbFilterContentTool,
    DbFilterAll = DbFilterAllRegions | DbFilterAllContent | DbFilterInstalled | DbFilterMissing,
} DbFilter;

typedef struct {
    DbPresence presence;
    const char* content;
    uint32_t type;
    const char* name;
    const char* description;
    const uint8_t* rap;
    const char* url;
    const uint8_t* digest;
    int64_t size;
} DbItem;

typedef enum {
    ContentUnknown,
    ContentGame,
    ContentDLC,
    ContentTheme,
    ContentAvatar,
    ContentDemo,
    ContentManager,
    ContentEmulator,
    ContentApp,
    ContentTool
} ContentType;

typedef enum {
    RegionASA,
    RegionEUR,
    RegionJPN,
    RegionUSA,
    RegionUnknown,
} GameRegion;

typedef struct Config Config;

int pkgi_db_reload(char* error, uint32_t error_size);
int pkgi_db_update(const char* update_url, uint32_t update_len, char* error, uint32_t error_size);
void pkgi_db_get_update_status(uint32_t* updated, uint32_t* total);

void pkgi_db_configure(const char* search, const Config* config);

uint32_t pkgi_db_count(void);
uint32_t pkgi_db_total(void);
DbItem* pkgi_db_get(uint32_t index);

GameRegion pkgi_get_region(const char* content);
ContentType pkgi_get_content_type(uint32_t content);
