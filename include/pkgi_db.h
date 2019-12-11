#pragma once

#include <stdint.h>

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

    DbFilterAllRegions = DbFilterRegionUSA | DbFilterRegionEUR | DbFilterRegionJPN | DbFilterRegionASA,
    DbFilterAll = DbFilterAllRegions | DbFilterInstalled | DbFilterMissing,
} DbFilter;

typedef struct {
    DbPresence presence;
    const char* content;
    uint32_t flags;
    const char* name;
    const char* description;
    const uint8_t* rap;
    const char* url;
    const uint8_t* digest;
    int64_t size;
} DbItem;


typedef enum {
    RegionASA,
    RegionEUR,
    RegionJPN,
    RegionUSA,
    RegionUnknown,
} GameRegion;

typedef struct Config Config;

int pkgi_db_update(const char* update_url, char* error, uint32_t error_size);
void pkgi_db_get_update_status(uint32_t* updated, uint32_t* total);

void pkgi_db_configure(const char* search, const Config* config);

uint32_t pkgi_db_count(void);
uint32_t pkgi_db_total(void);
DbItem* pkgi_db_get(uint32_t index);

GameRegion pkgi_get_region(const char* content);
