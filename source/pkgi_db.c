#include "pkgi_db.h"
#include "pkgi_config.h"
#include "pkgi_utils.h"
#include "pkgi_sha256.h"
#include "pkgi.h"
#include "pkgi_download.h"

#include <stddef.h>

#define MAX_DB_SIZE (4*1024*1024)
#define MAX_DB_ITEMS 8192

static char db_data[MAX_DB_SIZE];
static uint32_t db_total;
static uint32_t db_size;

static DbItem db[MAX_DB_ITEMS];
static uint32_t db_count;

static DbItem* db_item[MAX_DB_SIZE];
static uint32_t db_item_count;

static int64_t pkgi_strtoll(const char* str)
{
    int64_t res = 0;
    const char* s = str;
    if (*s && *s == '-')
    {
        s++;
    }
    while (*s)
    {
        res = res * 10 + (*s - '0');
        s++;
    }

    return str[0] == '-' ? -res : res;
}

static uint8_t hexvalue(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    else if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    else if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return 0;
}

static uint8_t* pkgi_hexbytes(const char* digest, uint32_t length)
{
    uint8_t* result = (uint8_t*)digest;

    for (uint32_t i = 0; i < length; i++)
    {
        char ch1 = digest[2 * i];
        char ch2 = digest[2 * i + 1];
        if (ch1 == 0 || ch2 == 0)
        {
            return NULL;
        }

        result[i] = hexvalue(ch1) * 16 + hexvalue(ch2);
    }

    return result;
}

int pkgi_db_update(const char* update_url, char* error, uint32_t error_size)
{
    db_total = 0;
    db_size = 0;
    db_count = 0;
    db_item_count = 0;

    char path[256];
    pkgi_snprintf(path, sizeof(path), "%s/pkgi.txt", pkgi_get_config_folder());  

    LOG("loading update from %s", path);
    
    int loaded = pkgi_load(path, db_data, sizeof(db_data) - 1);

    if (loaded > 0)
    {
        db_size = loaded;
    }
    else if (update_url[0] != 0)
    {
        LOG("loading update from %s", update_url);

        pkgi_http* http = pkgi_http_get(update_url, NULL, 0);
        if (!http)
        {
            pkgi_snprintf(error, error_size, "failed to download list");
            return 0;
        }
        else
        {
            int64_t length;
            if (!pkgi_http_response_length(http, &length))
            {
                pkgi_snprintf(error, error_size, "failed to download list");
            }
            else
            {
                if (length > (int64_t)sizeof(db_data) - 1)
                {
                    pkgi_snprintf(error, sizeof(error_size), "list is too large... check for newer pkgi version!");
                }
                else if (length != 0)
                {
                    db_total = (uint32_t)length;
                }

                error[0] = 0;

                for (;;)
                {
                    uint32_t want = (uint32_t)min64(1 << 16, sizeof(db_data) - 1 - db_size);
                    int read = pkgi_http_read(http, db_data + db_size, want);
                    if (read == 0)
                    {
                        break;
                    }
                    else if (read < 0)
                    {
                        pkgi_snprintf(error, sizeof(error_size), "HTTP error 0x%08x", read);
                        db_size = 0;
                        break;
                    }
                    db_size += read;
                }

                if (error[0] == 0 && db_size == 0)
                {
                    pkgi_snprintf(error, sizeof(error_size), "list is empty... check for newer pkgi version!");
                }
            }

            pkgi_http_close(http);

            if (db_size == 0)
            {
                return 0;
            }
        }
    }
    else
    {
        pkgi_snprintf(error, error_size, "ERROR: pkgi.txt file missing or bad config.txt file?");
        return 0;
    }

    LOG("parsing items");

    db_data[db_size] = '\n';
    char* ptr = db_data;
    char* end = db_data + db_size + 1;

    if (db_size > 3 && (uint8_t)ptr[0] == 0xef && (uint8_t)ptr[1] == 0xbb && (uint8_t)ptr[2] == 0xbf)
    {
        ptr += 3;
    }

    while (ptr < end && *ptr)
    {
        const char* content = ptr;
        while (ptr < end && *ptr != ',')
        {
            ptr++;
        }
        if (ptr == end)
        {
            break;
        }
        *ptr++ = 0;

        const char* flags = ptr;
        while (ptr < end && *ptr != ',')
        {
            ptr++;
        }
        if (ptr == end)
        {
            break;
        }
        *ptr++ = 0;

        const char* name = ptr;
        while (ptr < end && *ptr != ',')
        {
            ptr++;
        }
        if (ptr == end)
        {
            break;
        }
        *ptr++ = 0;

        const char* name_org = ptr;
        while (ptr < end && *ptr != ',')
        {
            ptr++;
        }
        if (ptr == end)
        {
            break;
        }
        *ptr++ = 0;

        const char* rap = ptr;
        while (ptr < end && *ptr != ',')
        {
            ptr++;
        }
        if (ptr == end)
        {
            break;
        }
        *ptr++ = 0;

        const char* url = ptr;
        while (ptr < end && *ptr != ',')
        {
            ptr++;
        }
        if (ptr == end)
        {
            break;
        }
        *ptr++ = 0;

        const char* size = ptr;
        while (ptr < end && *ptr != ',')
        {
            ptr++;
        }
        if (ptr == end)
        {
            break;
        }
        *ptr++ = 0;

        const char* digest = ptr;
        while (ptr < end && *ptr != '\n' && *ptr != '\r')
        {
            ptr++;
        }
        if (ptr == end)
        {
            break;
        }
        *ptr++ = 0;

        if (*ptr == '\n')
        {
            ptr++;
        }

        db[db_count].content = content;
        db[db_count].flags = (uint32_t)pkgi_strtoll(flags);
        db[db_count].name = name;
        db[db_count].name_org = (name_org[0] == 0 ? name : name_org);
        db[db_count].rap = (rap[0] == 0 ? NULL : pkgi_hexbytes(rap, PKGI_RAP_SIZE));
        db[db_count].url = url;
        db[db_count].size = pkgi_strtoll(size);
        db[db_count].digest = pkgi_hexbytes(digest, SHA256_DIGEST_SIZE);
        db_item[db_count] = db + db_count;
        db_count++;

        if (db_count == MAX_DB_ITEMS)
        {
            break;
        }

        if (ptr < end && *ptr == '\r')
        {
            ptr++;
        }
    }

    db_item_count = db_count;

    LOG("finished parsing, %u total items", db_count);
    return 1;
}

static void swap(uint32_t a, uint32_t b)
{
    DbItem* temp = db_item[a];
    db_item[a] = db_item[b];
    db_item[b] = temp;
}

static int matches(GameRegion region, uint32_t filter)
{
    return (region == RegionASA && (filter & DbFilterRegionASA))
        || (region == RegionEUR && (filter & DbFilterRegionEUR))
        || (region == RegionJPN && (filter & DbFilterRegionJPN))
        || (region == RegionUSA && (filter & DbFilterRegionUSA))
        || (region == RegionUnknown);
}

static int lower(const DbItem* a, const DbItem* b, DbSort sort, DbSortOrder order, uint32_t filter)
{
    GameRegion reg_a = pkgi_get_region(a->content);
    GameRegion reg_b = pkgi_get_region(b->content);

    int cmp = 0;
    if (sort == SortByTitle)
    {
        cmp = pkgi_stricmp(a->content + 7, b->content + 7) < 0;
    }
    else if (sort == SortByRegion)
    {
        cmp = reg_a == reg_b ? pkgi_stricmp(a->content + 7, b->content + 7) < 0 : reg_a < reg_b;
    }
    else if (sort == SortByName)
    {
        cmp = pkgi_stricmp(a->name, b->name) < 0;
    }
    else if (sort == SortBySize)
    {
        cmp = a->size < b->size;
    }

    int matches_a = matches(reg_a, filter);
    int matches_b = matches(reg_b, filter);

    if (matches_a == matches_b)
    {
        return order == SortAscending ? cmp : !cmp;
    }
    else if (matches_a)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

static void heapify(uint32_t n, uint32_t index, DbSort sort, DbSortOrder order, uint32_t filter)
{
    uint32_t largest = index;
    uint32_t left = 2 * index + 1;
    uint32_t right = 2 * index + 2;

    if (left < n && lower(db_item[largest], db_item[left], sort, order, filter))
    {
        largest = left;
    }

    if (right < n && lower(db_item[largest], db_item[right], sort, order, filter))
    {
        largest = right;
    }

    if (largest != index)
    {
        swap(index, largest);
        heapify(n, largest, sort, order, filter);
    }
}

void pkgi_db_configure(const char* search, const Config* config)
{
    uint32_t search_count;
    if (!search)
    {
        search_count = db_count;
    }
    else
    {
        uint32_t write = 0;
        for (uint32_t read = 0; read < db_count; read++)
        {
            if (pkgi_stricontains(db_item[read]->name, search))
            {
                if (write < read)
                {
                    swap(read, write);
                }
                write++;
            }
        }
        search_count = write;
    }

    if (search_count == 0)
    {
        db_item_count = 0;
        return;
    }

    for (int i = search_count / 2 - 1; i >= 0; i--)
    {
        heapify(search_count, i, config->sort, config->order, config->filter);
    }

    for (int i = search_count - 1; i >= 0; i--)
    {
        swap(i, 0);
        heapify(i, 0, config->sort, config->order, config->filter);
    }

    if (config->filter == DbFilterAll)
    {
        db_item_count = search_count;
    }
    else
    {
        uint32_t low = 0;
        uint32_t high = search_count - 1;
        while (low <= high)
        {
            // this never overflows because of MAX_DB_ITEMS
            uint32_t middle = (low + high) / 2;

            GameRegion region = pkgi_get_region(db_item[middle]->content);
            if (matches(region, config->filter))
            {
                low = middle + 1;
            }
            else
            {
                if (middle == 0)
                {
                    break;
                }
                high = middle - 1;
            }
        }
        db_item_count = low;
    }
}

void pkgi_db_get_update_status(uint32_t* updated, uint32_t* total)
{
    *updated = db_size;
    *total = db_total;
}

uint32_t pkgi_db_count(void)
{
    return db_item_count;
}

uint32_t pkgi_db_total(void)
{
    return db_count;
}

DbItem* pkgi_db_get(uint32_t index)
{
    return index < db_item_count ? db_item[index] : NULL;
}

GameRegion pkgi_get_region(const char* content)
{
    switch (content[0])
    {
    case 'H':
        return RegionASA;

    case 'E':
        return RegionEUR;

    case 'J':
        return RegionJPN;

    case 'U':
        return RegionUSA;

    default:
        return RegionUnknown;
    }
}
