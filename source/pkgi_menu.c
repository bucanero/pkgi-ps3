#include "pkgi_menu.h"
#include "pkgi_config.h"
#include "pkgi_style.h"
#include "pkgi.h"

static int menu_search_clear;

static Config   menu_config;
static uint32_t menu_selected;
static int      menu_allow_refresh;

static MenuResult menu_result;

static int32_t menu_width;
static int32_t menu_delta;

typedef enum {
    MenuSearch,
    MenuSearchClear,
    MenuText,
    MenuSort,
    MenuFilter,
    MenuRefresh,
    MenuMode,
    MenuUpdate,
    MenuMusic,
    MenuContent
} MenuType;

typedef struct {
    MenuType type;
    const char* text;
    uint32_t value;
} MenuEntry;

static const MenuEntry menu_entries[] =
{
    { MenuSearch, "Search...", 0 },
    { MenuSearchClear, PKGI_UTF8_CLEAR " clear", 0 },

    { MenuText, "Sort by:", 0 },
    { MenuSort, "Title", SortByTitle },
    { MenuSort, "Region", SortByRegion },
    { MenuSort, "Name", SortByName },
    { MenuSort, "Size", SortBySize },

    { MenuText, "Content:", 0 },
    { MenuContent, "All", 0 },

    { MenuText, "Regions:", 0 },
    { MenuFilter, "Asia", DbFilterRegionASA },
    { MenuFilter, "Europe", DbFilterRegionEUR },
    { MenuFilter, "Japan", DbFilterRegionJPN },
    { MenuFilter, "USA", DbFilterRegionUSA },

    { MenuText, "Options:", 0 },
    { MenuMode, "Back. DL", 1 },
    { MenuMusic, "Music", 1 },
    { MenuUpdate, "Updates", 1 },

    { MenuRefresh, "Refresh...", 0 },
};

static const MenuEntry content_entries[] = 
{
    { MenuFilter, "All", DbFilterAllContent },
    { MenuFilter, "Games", DbFilterContentGame },
    { MenuFilter, "DLCs", DbFilterContentDLC },
    { MenuFilter, "Themes", DbFilterContentTheme },
    { MenuFilter, "Avatars", DbFilterContentAvatar },
    { MenuFilter, "Demos", DbFilterContentDemo },
    { MenuFilter, "Managers", DbFilterContentManager },
    { MenuFilter, "Emulators", DbFilterContentEmulator },
    { MenuFilter, "Apps", DbFilterContentApp },
    { MenuFilter, "Tools", DbFilterContentTool }
};

int pkgi_menu_is_open(void)
{
    return menu_width != 0;
}

MenuResult pkgi_menu_result()
{
    return menu_result;
}

void pkgi_menu_get(Config* config)
{
    *config = menu_config;
}

void pkgi_menu_start(int search_clear, const Config* config)
{
    menu_search_clear = search_clear;
    menu_width = 1;
    menu_delta = 1;
    menu_config = *config;
    menu_allow_refresh = config->allow_refresh;
}

int pkgi_do_menu(pkgi_input* input)
{
    if (menu_delta != 0)
    {
        menu_width += menu_delta * (int32_t)(input->delta * PKGI_ANIMATION_SPEED/ 3000);

        if (menu_delta < 0 && menu_width <= 0)
        {
            menu_width = 0;
            menu_delta = 0;
            return 0;
        }
        else if (menu_delta > 0 && menu_width >= PKGI_MENU_WIDTH)
        {
            menu_width = PKGI_MENU_WIDTH;
            menu_delta = 0;
        }
    }

    if (menu_width != 0)
    {
        pkgi_draw_fill_rect_z(VITA_WIDTH - menu_width, 25, PKGI_MENU_Z, menu_width, PKGI_MENU_HEIGHT, PKGI_COLOR_MENU_BACKGROUND);
        pkgi_draw_rect_z(VITA_WIDTH - menu_width, 25, PKGI_MENU_Z, menu_width, PKGI_MENU_HEIGHT, PKGI_COLOR_MENU_BORDER);
    }

    if (input->active & PKGI_BUTTON_UP)
    {
        do {
            if (menu_selected == 0)
            {
                menu_selected = PKGI_COUNTOF(menu_entries) - 1;
            }
            else
            {
                menu_selected--;
            }
        } while (menu_entries[menu_selected].type == MenuText
            || (menu_entries[menu_selected].type == MenuSearchClear && !menu_search_clear)
            || (menu_entries[menu_selected].type == MenuRefresh && !menu_allow_refresh));
    }

    if (input->active & PKGI_BUTTON_DOWN)
    {
        do {
            if (menu_selected == PKGI_COUNTOF(menu_entries) - 1)
            {
                menu_selected = 0;
            }
            else
            {
                menu_selected++;
            }
        } while (menu_entries[menu_selected].type == MenuText
            || (menu_entries[menu_selected].type == MenuSearchClear && !menu_search_clear)
            || (menu_entries[menu_selected].type == MenuRefresh && !menu_allow_refresh));
    }

    if (input->pressed & pkgi_cancel_button())
    {
        menu_result = MenuResultAccept;
        menu_delta = -1;
        return 1;
    }
    else if (input->pressed & pkgi_ok_button())
    {
        MenuType type = menu_entries[menu_selected].type;
        if (type == MenuSearch)
        {
            menu_result = MenuResultSearch;
            menu_delta = -1;
            return 1;
        }
        if (type == MenuSearchClear)
        {
            menu_selected--;
            menu_result = MenuResultSearchClear;
            menu_delta = -1;
            return 1;
        }
        else if (type == MenuRefresh)
        {
            menu_result = MenuResultRefresh;
            menu_delta = -1;
            return 1;
        }
        else if (type == MenuSort)
        {
            DbSort value = (DbSort)menu_entries[menu_selected].value;
            if (menu_config.sort == value)
            {
                menu_config.order = menu_config.order == SortAscending ? SortDescending : SortAscending;
            }
            else
            {
                menu_config.sort = value;
            }
        }
        else if (type == MenuFilter)
        {
            menu_config.filter ^= menu_entries[menu_selected].value;
        }
        else if (type == MenuMode)
        {
            menu_config.dl_mode_background ^= menu_entries[menu_selected].value;
        }
        else if (type == MenuMusic)
        {
            menu_config.music ^= menu_entries[menu_selected].value;
        }
        else if (type == MenuUpdate)
        {
            menu_config.version_check ^= menu_entries[menu_selected].value;
        }
        else if (type == MenuContent)
        {
            menu_config.filter ^= content_entries[menu_config.content].value;

            menu_config.content++;
            if (menu_config.content == MAX_CONTENT_TYPES)
                menu_config.content = 0;

            menu_config.filter ^= content_entries[menu_config.content].value;
        }
    }

    if (menu_width != PKGI_MENU_WIDTH)
    {
        return 1;
    }

    int font_height = pkgi_text_height("M");

    int y = PKGI_MENU_TOP_PADDING;
    for (uint32_t i = 0; i < PKGI_COUNTOF(menu_entries); i++)
    {
        const MenuEntry* entry = menu_entries + i;

        MenuType type = entry->type;
        if (type == MenuText)
        {
            y += font_height;
        }
        else if (type == MenuSearchClear && !menu_search_clear)
        {
            continue;
        }
        else if (type == MenuRefresh)
        {
            if (!menu_allow_refresh)
            {
                continue;
            }
            y += font_height;
        }

        uint32_t color = menu_selected == i ? PKGI_COLOR_TEXT_MENU_SELECTED : PKGI_COLOR_TEXT_MENU;

        int x = VITA_WIDTH - PKGI_MENU_WIDTH + PKGI_MENU_LEFT_PADDING;

        char text[64];
        if (type == MenuSearch || type == MenuSearchClear || type == MenuText || type == MenuRefresh)
        {
            pkgi_strncpy(text, sizeof(text), entry->text);
        }
        else if (type == MenuSort)
        {
            if (menu_config.sort == (DbSort)entry->value)
            {
                pkgi_snprintf(text, sizeof(text), "%s %s",
                    menu_config.order == SortAscending ? PKGI_UTF8_SORT_ASC : PKGI_UTF8_SORT_DESC,
                    entry->text);
            }
            else
            {
                x += pkgi_text_width(PKGI_UTF8_SORT_ASC " ");
                pkgi_strncpy(text, sizeof(text), entry->text);
            }
        }
        else if (type == MenuFilter)
        {
            pkgi_snprintf(text, sizeof(text), "%s %s",
                menu_config.filter & entry->value ? PKGI_UTF8_CHECK_ON : PKGI_UTF8_CHECK_OFF,
                entry->text);
        }
        else if (type == MenuMode)
        {
            pkgi_snprintf(text, sizeof(text), PKGI_UTF8_CLEAR " %s",
                menu_config.dl_mode_background == entry->value ? entry->text : "Direct DL");            
        }
        else if (type == MenuMusic)
        {
            pkgi_snprintf(text, sizeof(text), "%s %s",
                menu_config.music == entry->value ? PKGI_UTF8_CHECK_ON : PKGI_UTF8_CHECK_OFF, entry->text);            
        }
        else if (type == MenuUpdate)
        {
            pkgi_snprintf(text, sizeof(text), "%s %s",
                menu_config.version_check == entry->value ? PKGI_UTF8_CHECK_ON : PKGI_UTF8_CHECK_OFF, entry->text);            
        }
        else if (type == MenuContent)
        {
            pkgi_snprintf(text, sizeof(text), PKGI_UTF8_CLEAR " %s", content_entries[menu_config.content].text);
        }
        
        pkgi_draw_text_z(x, y, PKGI_MENU_TEXT_Z, color, text);

        y += font_height;
    }

    return 1;
}
