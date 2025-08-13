#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4util/libxfce4util.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <xfconf/xfconf.h>
#include <libxfce4ui/libxfce4ui.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
// #define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#define PLUGIN_ID "launch-menu"
#define PLUGIN_VERSION "1.0.0" // Release version
#define PLUGIN_WEBSITE "https://github.com/James-Gryphon/Launch-Menu"
#define PLUGIN_AUTHORS "James Gooch"
#define CONFIG_CHANNEL "xfce4-panel"
#define CONFIG_PROPERTY_BASE "/plugins/" PLUGIN_ID

#define SPATIAL_MENU_DIR ".config/Launch Menu Items"
#define RECENT_APPS_DIR "Recent Applications"
#define RECENT_DOCS_DIR "Recent Documents"
#define TIMESTAMP_FORMAT "%Y%m%d_%H%M%S"

/* CLASSIC LIBRARY DEFINES */

gchar *classlib_get_process_name_from_pid(pid_t pid);
const gchar *classlib_get_application_display_name(WnckApplication *app);
gboolean classlib_looks_like_window_title(const gchar *name);
const gchar *classlib_get_application_display_name(WnckApplication *app);
const gchar *classlib_ensure_valid_utf8(const gchar *input);
gboolean classlib_is_file_manager(WnckApplication *app);
gboolean classlib_should_blacklist_application(xmlNode *bookmark_node);
gchar *classlib_get_default_file_manager(void);
gboolean classlib_is_desktop_manager(const gchar *process_name);

typedef enum 
{
    CLASSLIB_LOCALE_TYPE_C,      /* ASCII sorting */
    CLASSLIB_LOCALE_TYPE_UTF8    /* Smart sorting that ignores special chars */
} ClassicLocaleType;

typedef enum 
{
    CLASSLIB_SORT_STYLE_CAJA,     /* Locale-aware, ignores special chars in UTF-8 */
    CLASSLIB_SORT_STYLE_THUNAR,   /* Always uses natural sorting regardless of locale */
    CLASSLIB_SORT_STYLE_UNKNOWN   /* Fallback to Caja behavior */
} ClassicSortStyle;

gint classlib_file_manager_aware_compare(const gchar *a, const gchar *b, ClassicSortStyle sort_style, ClassicLocaleType locale_type);
gint classlib_get_special_char_priority(const gchar *str, ClassicSortStyle sort_style);
ClassicLocaleType classlib_detect_locale_type(void);
gint classlib_natural_compare_strings(const gchar *a, const gchar *b);
gchar *classlib_find_desktop_file(const gchar *app_name, WnckApplication *app);
gchar *classlib_search_desktop_directory(const gchar *dir_path, const gchar *app_name);

/* END CLASS LIBRARY DEFINES*/

typedef struct 
{
    XfcePanelPlugin *plugin;
    GtkWidget *button;
    GtkWidget *icon;
    GtkWidget *menu;
    WnckHandle *handle;
    gchar *launch_menu_path;
    gchar *icon_name;
    gboolean hardinfo_available;

    /* Configuration properties */
    XfconfChannel *channel;
    gchar *property_base;
    gboolean recents_on;
    gint recent_apps_count;
    gint recent_docs_count;
    gboolean classic_style_duplicates;

    /* Enhanced functionality fields */
    GFileMonitor *xbel_monitor;
    WnckScreen *wnck_screen;
    GHashTable *known_applications;
} LaunchMenuPlugin;

/* Structure to hold menu items with their display names for sorting */
typedef struct 
{
    gchar *filename;      /* Original filename */
    gchar *display_name;  /* Pretty name for display and sorting */
    gchar *full_path;     /* Full path to the item */
} MenuItemInfo;

/* Structure for recent items management */
typedef struct 
{
    gchar *name;
    gchar *path;
    gchar *timestamp_filename;
    time_t timestamp;
    gboolean is_application;
} RecentItem;

typedef struct 
{
    gchar *path;
    gchar *name;
    time_t timestamp;
} XbelEntry;

/* Function declarations */
static void launch_menu_construct(XfcePanelPlugin *plugin);
static void launch_menu_free(XfcePanelPlugin *plugin);
static gboolean on_button_pressed(GtkWidget *widget, GdkEventButton *event,
LaunchMenuPlugin *launch_plugin);
static void create_simple_menu(LaunchMenuPlugin *launch_plugin);
static GtkWidget *build_submenu_from_directory(const gchar *dir_path, gint
depth, LaunchMenuPlugin *launch_plugin);
static void on_about_computer_clicked(LaunchMenuPlugin
*launch_plugin);
static void on_launch_item_clicked(GtkMenuItem *item, gpointer user_data);
static GtkWidget *create_menu_item_with_icon(const gchar *label, const gchar
*item_path);
static void add_recent_application(LaunchMenuPlugin *launch_plugin, const gchar
*app_path);
static void cleanup_recent_folder(const gchar *folder_path, gint max_items);

/* Enhanced functionality - Phase 2 & 3 additions */
static gboolean parse_desktop_file_for_menu(const gchar *desktop_path, gchar
**display_name, gchar **icon_name);
static gchar *get_item_display_name(const gchar *item_name, const gchar
*item_path);
static gint compare_menu_items(gconstpointer a, gconstpointer b);
static void menu_item_info_free(MenuItemInfo *info);
static gchar *ensure_valid_utf8(const gchar *input);

/* Phase 3 - XBEL Document Tracking */
static void parse_xbel_file(LaunchMenuPlugin *launch_plugin);
static void on_xbel_file_changed(GFileMonitor *monitor, GFile *file, GFile
*other_file, GFileMonitorEvent event_type, gpointer user_data);
static gboolean is_document_file(const gchar *path);
static gchar *get_timestamp_string(time_t timestamp);
static gchar *create_safe_filename(const gchar *original_name, time_t
timestamp);
static void enforce_item_limit_with_timestamps(const gchar *folder_path, guint
max_items);
static void create_clean_links(const gchar *folder_path, LaunchMenuPlugin
*launch_plugin);
static void update_clean_links(LaunchMenuPlugin *launch_plugin);
static void recent_item_free(RecentItem *item);
static gint compare_recent_items(gconstpointer a, gconstpointer b);
static gint compare_paths_for_processing(gconstpointer a, gconstpointer b);

/* Phase 4 - Application Monitoring via wnck */
static void setup_application_monitoring(LaunchMenuPlugin *launch_plugin);
static void cleanup_application_monitoring(LaunchMenuPlugin *launch_plugin);
static void on_application_opened(WnckScreen *screen, WnckApplication *app,
gpointer user_data);
static void populate_initial_applications(LaunchMenuPlugin *launch_plugin);
static void add_recent_application_from_wnck(LaunchMenuPlugin *launch_plugin,
WnckApplication *app);
// static gchar *search_desktop_dir_for_app(const gchar *dir_path, const gchar *app_name);
// static void create_simple_recent_application_entry(LaunchMenuPlugin *launch_plugin, const gchar *app_name, WnckApplication *app);
static void create_recent_application_entry_with_desktop(LaunchMenuPlugin
*launch_plugin, const gchar *display_name, const gchar *desktop_path);

/* Configuration functions */
static void launch_menu_configure_plugin(XfcePanelPlugin *panel,
LaunchMenuPlugin *plugin);
static void launch_menu_about(XfcePanelPlugin *panel);
static void launch_menu_load_settings(LaunchMenuPlugin *plugin);
static void launch_menu_save_settings(LaunchMenuPlugin *plugin);
static gchar *launch_menu_get_property_name(LaunchMenuPlugin *plugin, const
gchar *property);

/* Disable checkboxes and tracking functions */
static void initialize_tracking(LaunchMenuPlugin *plugin);
static void sensitivity_changer(GtkToggleButton *button, gpointer user_data);
static void kill_tracking(LaunchMenuPlugin *plugin);


/* OPEN CLASSIC LIBRARY*/
/* Application display finder tools for PIDs */
gchar *classlib_get_process_name_from_pid(pid_t pid) 
{
    gchar *cmdline_path = g_strdup_printf("/proc/%d/cmdline", pid);
    gchar *cmdline = NULL;
    gsize cmdline_length = 0;
    gchar *result = NULL;

    if (g_file_get_contents(cmdline_path, &cmdline, &cmdline_length, NULL)) 
    {
        if (cmdline_length > 0) 
        {
            /* Extract program name (first null-terminated string) */
            gchar *program_name = g_strdup(cmdline);
            result = g_path_get_basename(program_name);
            g_free(program_name);
        }
        g_free(cmdline);
    }

    g_free(cmdline_path);
    return result ? result : g_strdup("unknown");
}

gboolean classlib_looks_like_window_title(const gchar *name)
{
    return (strlen(name) > 20 ||           // Too long for app name
    strstr(name, " — ") ||         // Contains document separator
    strstr(name, " - ") ||         // Alternate separator
    strchr(name, ':') ||           // Contains colons
    strchr(name, '/'));            // Contains paths
}

/* =============================================================================
 * APPLICATION NAME RESOLUTION SYSTEM
 * 6-tier resolution system extracted from both working projects
 * ============================================================================= */

/**
 * Validate that a string contains valid UTF-8 encoding.
 * Returns a safe fallback if the input is invalid.
 */
const gchar *classlib_ensure_valid_utf8(const gchar *input) 
{
    if (!input) 
    {
        return "Invalid App Name";
    }

    if (g_utf8_validate(input, -1, NULL)) 
    {
        return input; /* Input is valid UTF-8 */
    } 
    else 
    {
        return "Invalid App Name"; /* Safe fallback for invalid UTF-8 */
    }
}

/**
 * Get the proper display name for a WnckApplication using 6-tier resolution.
 *
 * This is the core function that both projects rely on for consistent
 * application naming. Extracted from macos9-menu.c get_app_display_name().
 */
const gchar *classlib_get_application_display_name(WnckApplication *app) 
{
    if (!app) 
    {
        return "Untitled Program";
    }

    const gchar *name = wnck_application_get_name(app);

    /* Handle applications with no name (like Python apps) */
    if (!name || strlen(name) == 0) 
    {
        /* Try to get name from first window as fallback */
        GList *windows = wnck_application_get_windows(app);
        if (windows) 
        {
            WnckWindow *first_window = WNCK_WINDOW(windows->data);
            const gchar *window_name = wnck_window_get_name(first_window);
            if (window_name && strlen(window_name) > 0) 
            {
                /* Validate UTF-8 before returning */
                return classlib_ensure_valid_utf8(window_name);
            }
        }
        /* Last resort fallback */
        return "Untitled Program";
    }

    /* Validate the application name before processing */
    name = classlib_ensure_valid_utf8(name);
    if (g_strcmp0(name, "Invalid App Name") == 0) 
    {
        return name; /* Return the safe fallback */
    }
    /* =========================================================================
     * TIER 0: Check if the name resembles a window title
     * ========================================================================= */
    /* TIER 0: PROCESS NAME FALLBACK - Handle window titles masquerading as app names */
    if (classlib_looks_like_window_title(name)) 
    {
        pid_t pid = wnck_application_get_pid(app);
        if (pid > 0)
        {
            gchar *process_name = classlib_get_process_name_from_pid(pid);
            if (process_name && strlen(process_name) > 0 && g_strcmp0(process_name, "unknown") != 0) 
                {
                /* Use process name instead and continue through existing tiers */
                name = g_intern_string(process_name);
            g_free(process_name);
                } 
                else 
                {
                    g_free(process_name);
                }
        }
    }

    /* =========================================================================
     * TIER 1: MANUAL MAPPING (Highest Priority)
     * Specific preferences and edge cases that need exact control
     * ========================================================================= */

    /* Using case-insensitive comparison for reliability */
    if (g_ascii_strcasecmp(name, "Org.mozilla.firefox") == 0) 
    {
        return "Firefox";
    } 
    else if (g_ascii_strcasecmp(name, "google-chrome") == 0) 
    {
        return "Google Chrome";
    }
     else if (g_ascii_strcasecmp(name, "code") == 0) {
        return "Visual Studio Code";
    }
     else if (g_ascii_strcasecmp(name, "vlc") == 0 || g_ascii_strcasecmp(name, "VLC media player") == 0) 
    {
        return "VLC Media Player";
    }
    else if (g_ascii_strcasecmp(name, "xfce4-about") == 0) 
    {
        return "About Xfce";
    } 
    else if (g_ascii_strcasecmp(name, "xfce4-appfinder") == 0) 
    {
        return "App Finder";
    } 
    else if (g_str_has_prefix(name, "Soffice") || g_str_has_prefix(name, "soffice"))
    {
        return "LibreOffice";
    } 
    else if (g_str_has_suffix(name, "- Audacious")) 
    {
        return "Audacious";
    }
    /* =========================================================================
        * TIER 2: XFCE SETTINGS PATTERN
        * Handle Xfce4-*-settings applications with proper capitalization
        * ========================================================================= */

    if (g_str_has_prefix(name, "Xfce4-") && g_str_has_suffix(name, "-settings")) 
    {
        /* Extract the middle part and capitalize it */
        const gchar *start = name + 6; /* Skip "Xfce4-" */
        const gchar *end = g_strrstr(name, "-settings");
        if (end && end > start) 
        {
            gsize len = end - start;
            gchar *middle = g_strndup(start, len);
            if (middle) 
            {
                /* Capitalize first letter */
                if (middle[0] >= 'a' && middle[0] <= 'z') 
                {
                    middle[0] = middle[0] - 'a' + 'A';
                }

                /* Apply Tier 5 logic to handle dashes in the middle part */
                if (strchr(middle, '-') != NULL) 
                {
                    /* Replace dashes with spaces and capitalize each word */
                    for (int i = 0; middle[i]; i++) 
                    {
                        if (middle[i] == '-') 
                        {
                            middle[i] = ' ';
                            /* Capitalize letter after space (if exists and is lowercase) */
                            if (middle[i + 1] >= 'a' && middle[i + 1] <= 'z') 
                            {
                                middle[i + 1] = middle[i + 1] - 'a' + 'A';
                            }
                        }
                    }
                }
                /* Use GLib intern string to avoid memory leaks */
                const gchar *result = g_intern_string(middle);
                g_free(middle);  /* Free our temporary string */
                return result;   /* Return the interned version (managed by GLib) */
            }
        }
    }

    /* =========================================================================
        * TIER 3: REVERSE DOMAIN PATTERN
        * Handle org.*.* and Org.*.* applications
        * ========================================================================= */

    if (g_str_has_prefix(name, "org.") || g_str_has_prefix(name, "Org.")) 
    {
        /* Find the last dot to get the app name */
        const gchar *last_dot = g_strrstr(name, ".");
        if (last_dot) 
        {
            const gchar *app_name = last_dot + 1; /* Skip the dot */
            if (strlen(app_name) > 0) 
            {
                /* Capitalize first letter only */
                gchar *capitalized = g_strdup(app_name);
                if (capitalized[0] >= 'a' && capitalized[0] <= 'z') 
                {
                    capitalized[0] = capitalized[0] - 'a' + 'A';
                }

                /* Apply Tier 5 logic if there are dashes */
                if (strchr(capitalized, '-') != NULL) 
                {
                    for (int i = 0; capitalized[i]; i++) {
                        if (capitalized[i] == '-') 
                        {
                            capitalized[i] = ' ';
                            /* Capitalize letter after space */
                            if (capitalized[i + 1] >= 'a' && capitalized[i + 1] <= 'z') 
                            {
                                capitalized[i + 1] = capitalized[i + 1] - 'a' + 'A';
                            }
                        }
                    }
                }
                const gchar *result = g_intern_string(capitalized);
                g_free(capitalized);
                return result;
            }
        }
    }

    /* =========================================================================
        * TIER 4: SIMPLE CAPITALIZATION
        * Single lowercase words only - capitalize first letter
        * ========================================================================= */

    /* Check if it's a simple single word (no spaces, dots, dashes) */
    if (!strchr(name, ' ') && !strchr(name, '.') && !strchr(name, '-')) 
    {
        /* Check if it's all lowercase */
        gboolean is_lowercase = TRUE;
        for (const gchar *p = name; *p; p++) 
        {
            if (*p >= 'A' && *p <= 'Z') 
            {
                is_lowercase = FALSE;
                break;
            }
        }

        if (is_lowercase && strlen(name) > 0) 
        {
            gchar *capitalized = g_strdup(name);
            capitalized[0] = g_ascii_toupper(capitalized[0]);
            const gchar *result = g_intern_string(capitalized);
            g_free(capitalized);
            return result;
        }
    }

    /* =========================================================================
        * TIER 5: DASH REPLACEMENT
        * Any name with dashes - replace with spaces and capitalize each word
        * ========================================================================= */

    if (strchr(name, '-') != NULL) 
    {
        gchar *processed = g_strdup(name);

        /* Replace dashes with spaces and capitalize each word */
        for (int i = 0; processed[i]; i++) 
        {
            if (processed[i] == '-') 
            {
                processed[i] = ' ';
                /* Capitalize letter after space (if exists and is lowercase) */
                if (processed[i + 1] >= 'a' && processed[i + 1] <= 'z') 
                {
                    processed[i + 1] = processed[i + 1] - 'a' + 'A';
                }
            }
        }

        /* Also capitalize the first letter if it's lowercase */
        if (processed[0] >= 'a' && processed[0] <= 'z') 
        {
            processed[0] = processed[0] - 'a' + 'A';
        }

        const gchar *result = g_intern_string(processed);
        g_free(processed);
        return result;
    }

    /* =========================================================================
        * TIER 6: FALLBACK
        * Return original name unchanged
        * ========================================================================= */

    return name;
}

/* =============================================================================
 * FILE MANAGER DETECTION AND BLACKLISTING SYSTEM
 * Extracted from switcher menu's file manager detection and spatial menu's blacklisting
 * ============================================================================= */

/**
 * Check if a process name corresponds to a desktop manager.
 * Extracted from switcher menu's desktop manager detection logic.
 */
gboolean classlib_is_desktop_manager(const gchar *process_name) 
{
    if (!process_name) 
    {
        return FALSE;
    }

    const gchar *desktop_managers[] = 
    {
        "xfdesktop",
        "caja",         /* caja -n --force-desktop */
        "nemo-desktop",
        "nautilus-desktop",
        "pcmanfm",      /* pcmanfm --desktop */
        NULL
    };

    for (int i = 0; desktop_managers[i]; i++) 
    {
        if (g_ascii_strcasecmp(process_name, desktop_managers[i]) == 0) 
        {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Check if an application should be considered a file manager.
 * Combines desktop manager detection and traditional file manager detection.
 */
gboolean classlib_is_file_manager(WnckApplication *app) 
{
    if (!app) 
    {
        return FALSE;
    }

    const gchar *app_name = wnck_application_get_name(app);
    if (!app_name) 
    {
        return FALSE;
    }

    /* Check for known file managers */
    const gchar *file_managers[] = 
    {
        "caja",
        "thunar",
        "nemo",
        "nautilus",
        "pcmanfm",
        "dolphin",
        "konqueror",
        NULL
    };

    for (int i = 0; file_managers[i]; i++) 
    {
        if (g_ascii_strcasecmp(app_name, file_managers[i]) == 0) 
        {
            return TRUE;
        }
    }

    /* Check for desktop managers */
    return classlib_is_desktop_manager(app_name);
}

/**
 * Check if an application should be blacklisted from recent document tracking.
 * Extracted from spatial menu's is_blacklisted_application() function.
 */
/* Check if application should be blacklisted from recent documents */
gboolean classlib_should_blacklist_application(xmlNode *bookmark_node) 
{
    /* Applications that primarily download/fetch files rather than edit documents */
    const gchar *blacklisted_apps[] = 
    {
        "Firefox",
        "firefox",
        "Mozilla Firefox",
        "Chrome",
        "Chromium",
        "Google Chrome",
        "chromium",
        "wget",
        "curl",
        "Thunderbird",
        "thunderbird",
        "Transmission",
        "qBittorrent",
        "aria2c",
        "yt-dlp",
        "youtube-dl",
        NULL
    };

    /* Look for application metadata in the bookmark */
    for (xmlNode *child = bookmark_node->children; child; child = child->next) 
    {
        if (child->type != XML_ELEMENT_NODE) 
        {
            continue;
        }

        /* Check for info/metadata structure */
        if (xmlStrcmp(child->name, (const xmlChar *)"info") == 0) 
        {
            for (xmlNode *info_child = child->children; info_child; info_child = info_child->next) 
            {
                if (info_child->type != XML_ELEMENT_NODE) 
                {
                    continue;
                }

                /* Look for metadata with applications */
                if (xmlStrcmp(info_child->name, (const xmlChar *)"metadata") == 0) 
                {
                    for (xmlNode *meta_child = info_child->children; meta_child; meta_child = meta_child->next) 
                    {
                        if (meta_child->type != XML_ELEMENT_NODE) 
                        {
                            continue;
                        }

                        /* Look for bookmark:applications */
                        if (xmlStrcmp(meta_child->name, (const xmlChar *)"applications") == 0) 
                        {
                            for (xmlNode *app_child = meta_child->children; app_child; app_child = app_child->next) 
                            {
                                if (app_child->type != XML_ELEMENT_NODE) 
                                {
                                    continue;
                                }

                                /* Check bookmark:application elements */
                                if (xmlStrcmp(app_child->name, (const xmlChar *)"application") == 0) 
                                {
                                    xmlChar *app_name = xmlGetProp(app_child, (const xmlChar *)"name");
                                    if (app_name) 
                                    {
                                        /* Check against blacklist */
                                        for (int i = 0; blacklisted_apps[i]; i++) 
                                        {
                                            if (g_ascii_strcasecmp((const gchar *)app_name, blacklisted_apps[i]) == 0) 
                                            {
                                                xmlFree(app_name);
                                                return TRUE; /* Blacklisted */
                                            }
                                        }
                                        xmlFree(app_name);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return FALSE; /* Not blacklisted */
}

/**
 * Get the default file manager for the system using xdg-mime.
 */
gchar *classlib_get_default_file_manager(void) 
{
    gchar *output = NULL;
    gchar *error = NULL;
    gint exit_status;

    /* Query the default application for inode/directory */
    if (g_spawn_command_line_sync("xdg-mime query default inode/directory",
        &output, &error, &exit_status, NULL)) 
        {
            if (exit_status == 0 && output && *output) 
            {
                /* Strip newline and .desktop extension */
                g_strstrip(output);
                if (g_str_has_suffix(output, ".desktop")) 
                {
                    gchar *basename = g_path_get_basename(output);
                    gchar *name = g_strndup(basename, strlen(basename) - 8); /* Remove .desktop */
                    g_free(basename);
                    g_free(output);
                    return name;
                }
                return output;
            }
        }
        g_free(output);
        g_free(error);

        /* Fallback detection */
        const gchar *fallback_managers[] = {"caja", "thunar", "nemo", "nautilus", NULL};
        for (int i = 0; fallback_managers[i]; i++) 
        {
            gchar *path = g_find_program_in_path(fallback_managers[i]);
            if (path) 
            {
                g_free(path);
                return g_strdup(fallback_managers[i]);
            }
        }
        return NULL;
}

/* =============================================================================
 * NATURAL SORTING SYSTEM
 * File manager aware string comparison extracted from switcher menu
 * ============================================================================= */

/**
 * Detect the current system locale type for sorting purposes.
 * Follows Linux locale hierarchy: LC_ALL -> LC_COLLATE -> LANG -> "C"
 */
ClassicLocaleType classlib_detect_locale_type(void) 
{
    const gchar *locale = NULL;

    /* Follow the canonical hierarchy */
    locale = getenv("LC_ALL");
    if (!locale || !*locale) 
    {
        locale = getenv("LC_COLLATE");
        if (!locale || !*locale) 
        {
            locale = getenv("LANG");
            if (!locale || !*locale) 
            {
                locale = "C";  /* Final fallback */
            }
        }
    }

    /* Check if it's C locale */
    if (g_strcmp0(locale, "C") == 0 || g_strcmp0(locale, "POSIX") == 0) 
    {
        return CLASSLIB_LOCALE_TYPE_C;
    }

    /* Everything else is treated as UTF-8 locale */
    return CLASSLIB_LOCALE_TYPE_UTF8;
}

/**
 * Get special character priority for different file managers.
 * Extracted from switcher menu's get_special_char_priority().
 */
gint classlib_get_special_char_priority(const gchar *str, ClassicSortStyle sort_style) 
{
    if (!str || !*str) return 1;  /* Default priority for empty strings */
    {
        switch (sort_style) 
        {
            case CLASSLIB_SORT_STYLE_CAJA:
            /* Caja: Both . and # files go to end */
            if (str[0] == '.' || str[0] == '#') 
            {
                return 1;   /* Special files last */
            } 
            else 
            {
                return 0;   /* Normal files first */
            }

            case CLASSLIB_SORT_STYLE_THUNAR:
            /* Thunar: Only . files get special treatment (go to beginning) */
            if (str[0] == '.') 
            {
                return 0;   /* Hidden files first */
            } 
            else 
            {
                return 1;   /* Everything else (including #) second */
            }

            case CLASSLIB_SORT_STYLE_UNKNOWN:
            default:
            /* Default to Caja behavior */
            if (str[0] == '.' || str[0] == '#') 
            {
                return 1;
            } 
            else 
            {
                return 0;
            }
        }
    }
}

/**
 * File manager aware string comparison.
 * Extracted from switcher menu's file_manager_aware_compare().
 */
gint classlib_file_manager_aware_compare(const gchar *a, const gchar *b, ClassicSortStyle sort_style, ClassicLocaleType locale_type) 
{
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;

    /* Phase 1: Special character priority (different for each file manager) */
    gint priority_a = classlib_get_special_char_priority(a, sort_style);
    gint priority_b = classlib_get_special_char_priority(b, sort_style);

    if (priority_a != priority_b) 
    {
        return priority_a - priority_b;
    }

    /* Phase 2: Locale-aware comparison */
    if (sort_style == CLASSLIB_SORT_STYLE_CAJA && locale_type == CLASSLIB_LOCALE_TYPE_C) 
    {
        /* Only Caja falls back to C locale sorting */
        return strcmp(a, b);
    } 
    else 
    {
        /* Thunar always uses UTF-8, Caja uses UTF-8 in UTF-8 locales */
        gchar *key_a = g_utf8_collate_key_for_filename(a, -1);
        gchar *key_b = g_utf8_collate_key_for_filename(b, -1);
        gint result = strcmp(key_a, key_b);
        g_free(key_a);
        g_free(key_b);
        return result;
    }
}
/* =============================================================================
 * DESKTOP FILE SEARCH SYSTEM
 * Extracted from spatial menu's desktop file search logic
 * ============================================================================= */

/**
 * Parse desktop file for display name and icon.
 * Helper function for desktop file searching.
 */
static gboolean parse_desktop_file_for_search(const gchar *desktop_path, gchar **display_name, gchar **icon_name) 
{
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;

    if (!g_key_file_load_from_file(key_file, desktop_path, G_KEY_FILE_NONE, &error)) 
    {
        g_key_file_free(key_file);
        if (error) g_error_free(error);
        return FALSE;
    }

    /* Get application name */
    gchar *name = g_key_file_get_string(key_file, "Desktop Entry", "Name", NULL);
    if (display_name) 
    {
        *display_name = name;
    } 
    else 
    {
        g_free(name);
    }

    /* Get icon name */
    gchar *icon = g_key_file_get_string(key_file, "Desktop Entry", "Icon", NULL);
    if (icon_name) 
    {
        *icon_name = icon;
    } 
    else 
    {
        g_free(icon);
    }

    g_key_file_free(key_file);
    return TRUE;
}

/**
 * Search a specific directory for desktop files matching an application name.
 * Extracted from spatial menu's search_desktop_dir_for_app().
 */
gchar *classlib_search_desktop_directory(const gchar *dir_path, const gchar *app_name) 
{
    if (!dir_path || !app_name) 
    {
        return NULL;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) 
    {
        return NULL;
    }

    struct dirent *entry;
    gchar *result = NULL;

    while ((entry = readdir(dir)) != NULL) 
    {
        if (!g_str_has_suffix(entry->d_name, ".desktop")) 
        {
            continue;
        }

        gchar *desktop_path = g_build_filename(dir_path, entry->d_name, NULL);
        gchar *display_name = NULL;
        gchar *icon_name = NULL;

        if (parse_desktop_file_for_search(desktop_path, &display_name, &icon_name)) 
        {
            if (display_name && g_ascii_strcasecmp(display_name, app_name) == 0) 
            {
                result = g_strdup(desktop_path);
                g_free(display_name);
                g_free(icon_name);
                g_free(desktop_path);
                break;
            }
            g_free(display_name);
            g_free(icon_name);
        }
        g_free(desktop_path);
    }

    closedir(dir);
    return result;
}

/*
 * Helper function to assist classlib_find_desktop_file in retrieving executables' desktop files.
 */
static gchar *search_desktop_by_executable(const gchar *dir_path, const gchar *exe_name) 
{
    DIR *dir = opendir(dir_path);
    if (!dir) return NULL;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) 
    {
        if (!g_str_has_suffix(entry->d_name, ".desktop")) continue;

        gchar *desktop_path = g_build_filename(dir_path, entry->d_name, NULL);
        GKeyFile *keyfile = g_key_file_new();

        if (g_key_file_load_from_file(keyfile, desktop_path, G_KEY_FILE_NONE, NULL)) 
        {
            gchar *exec = g_key_file_get_string(keyfile, "Desktop Entry", "Exec", NULL);
            if (exec && strstr(exec, exe_name)) 
            {
                g_free(exec);
                g_key_file_free(keyfile);
                closedir(dir);
                return desktop_path;
            }
            g_free(exec);
        }
        g_key_file_free(keyfile);
        g_free(desktop_path);
    }
    closedir(dir);
    return NULL;
}


/**
 * Find desktop file for an application by name.
 * Extracted from spatial menu's find_desktop_file_for_application().
 */
gchar *classlib_find_desktop_file(const gchar *app_name, WnckApplication *app) 
{
    if (!app_name) return NULL;

    const gchar *desktop_dirs[] = 
    {
        "/usr/share/applications",
        "/usr/local/share/applications",
        NULL
    };

    /* Try search variations */
    gchar *search_names[4];
    search_names[0] = g_strdup(app_name);
    search_names[1] = g_ascii_strdown(app_name, -1);
    search_names[2] = g_strdelimit(g_ascii_strdown(app_name, -1), " ", '-');
    search_names[3] = NULL;

    for (int i = 0; desktop_dirs[i]; i++) 
    {
        for (int j = 0; search_names[j]; j++) 
        {
            gchar *result = classlib_search_desktop_directory(desktop_dirs[i], search_names[j]);
            if (result) 
            {
                for (int k = 0; k < 3; k++) g_free(search_names[k]);
                return result;
            }
        }
    }

    for (int i = 0; i < 3; i++) g_free(search_names[i]);
    /* Fallback: search by executable name */
    pid_t pid = wnck_application_get_pid(app);
    if (pid > 0) 
    {
        gchar *exe_name = classlib_get_process_name_from_pid(pid);
        if (exe_name && g_strcmp0(exe_name, "unknown") != 0) 
        {
            for (int i = 0; desktop_dirs[i]; i++) 
            {
                gchar *result = search_desktop_by_executable(desktop_dirs[i], exe_name);
                if (result) 
                {
                    g_free(exe_name);
                    return result;
                }
            }
        }
        g_free(exe_name);
    }
    return NULL;
}
/**
* Natural string comparison with smart number handling.
* Simplified version focusing on natural numeric ordering.
*/
gint classlib_natural_compare_strings(const gchar *a, const gchar *b) 
{
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;

    /* Use GLib's filename-aware collation which handles natural sorting */
    gchar *key_a = g_utf8_collate_key_for_filename(a, -1);
    gchar *key_b = g_utf8_collate_key_for_filename(b, -1);
    gint result = strcmp(key_a, key_b);
    g_free(key_a);
    g_free(key_b);
    return result;
}

/* END CLASSIC LIBRARY */

/* Helper function to ensure valid UTF-8 strings */
static gchar *ensure_valid_utf8(const gchar *input) 
{
    if (!input) 
    {
        return g_strdup("");
    }

    /* Check if string is already valid UTF-8 */
    if (g_utf8_validate(input, -1, NULL)) 
    {
        return g_strdup(input);
    }

    /* If not valid, try to make it valid by escaping invalid sequences */
    gchar *escaped = g_uri_escape_string(input,
    G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, TRUE);
    if (escaped && g_utf8_validate(escaped, -1, NULL)) 
    {
        return escaped;
    }

    /* Last resort: return a safe fallback */
    g_free(escaped);
    return g_strdup("Invalid Name");
}

/* Generate timestamp string for filenames */
static gchar *get_timestamp_string(time_t timestamp) 
{
    struct tm *tm_info = gmtime(&timestamp);  /* Use GMT/UTC instead of
    localtime */
    gchar *time_str = g_malloc(20);
    strftime(time_str, 20, TIMESTAMP_FORMAT, tm_info);
    return time_str;
}

/* Create safe filename for recent item */
static gchar *create_safe_filename(const gchar *original_name, time_t timestamp) 
{
    gchar *time_str = get_timestamp_string(timestamp);
    gchar *safe_name = g_strdup(original_name);

    /* Replace unsafe characters */
    for (gchar *p = safe_name; *p; p++) 
    {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' ||
        *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') 
        {
            *p = '_';
        }
    }

    gchar *result = g_strdup_printf("%s_%s", time_str, safe_name);

    g_free(time_str);
    g_free(safe_name);

    return result;
}

/* Free a recent item */
static void recent_item_free(RecentItem *item) 
{
    if (item) 
    {
        g_free(item->name);
        g_free(item->path);
        g_free(item->timestamp_filename);
        g_free(item);
    }
}

/* Compare recent items by timestamp (newest first) */
static gint compare_recent_items(gconstpointer a, gconstpointer b) 
{
    const RecentItem *item_a = (const RecentItem *)a;
    const RecentItem *item_b = (const RecentItem *)b;

    if (item_a->timestamp > item_b->timestamp) return -1;
    if (item_a->timestamp < item_b->timestamp) return 1;
    return 0;
}

/* Check if a file should be considered a document (not a folder or
application) */
static gboolean is_document_file(const gchar *path) 
{
    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS)) 
    {
        return FALSE;
    }

    /* Skip directories */
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) 
    {
    return FALSE;
    }

    /* Skip executables */
    if (g_file_test(path, G_FILE_TEST_IS_EXECUTABLE) && !g_file_test(path, G_FILE_TEST_IS_DIR)) 
    {
        return FALSE;
    }

    /* Skip .desktop files */
    if (g_str_has_suffix(path, ".desktop")) 
    {
        return FALSE;
    }

    /* Only include regular files */
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) 
    {
        return TRUE;
    }

    return FALSE;
}

/* Load existing recent items from timestamps directory */
static GList *load_recent_items_from_timestamps_dir(const gchar *timestamps_dir) 
{
GList *items = NULL;
DIR *dir = opendir(timestamps_dir);

if (!dir) 
{
    return NULL;
}

struct dirent *entry;
while ((entry = readdir(dir)) != NULL) 
{
if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) 
{
    continue;
}

gchar *full_path = g_build_filename(timestamps_dir, entry->d_name, NULL);

/* Check if it's a symbolic link */
struct stat link_stat;
if (lstat(full_path, &link_stat) == 0 && S_ISLNK(link_stat.st_mode)) 
{
/* Parse timestamp from filename - expected format:
YYYYMMDD_HHMMSS_originalname */
if (strlen(entry->d_name) >= 15) 
{
gchar timestamp_str[16];
strncpy(timestamp_str, entry->d_name, 15);
timestamp_str[15] = '\0';

struct tm tm_info = {0};
if (strptime(timestamp_str, TIMESTAMP_FORMAT, &tm_info)) 
{
time_t timestamp = timegm(&tm_info);  
/* Use timegm for UTC */

/* Extract original name (skip timestamp prefix) */
const gchar *underscore_pos = strchr(entry->d_name, '_');
if (underscore_pos) 
{
underscore_pos = strchr(underscore_pos + 1, '_'); /*
Find second underscore */
if (underscore_pos) 
{
    gchar *original_name = g_strdup(underscore_pos + 1);

    /* Read the link target */
    gchar *target = g_file_read_link(full_path, NULL);
    if (target && g_file_test(target,
    G_FILE_TEST_EXISTS)) 
    {
        RecentItem *item = g_new0(RecentItem, 1);
        item->name = original_name; /* Transfer
        ownership */
        item->path = target; /* Transfer ownership */
        item->timestamp = timestamp;
        item->timestamp_filename = g_strdup(entry->d_name);
        items = g_list_insert_sorted(items, item,
        compare_recent_items);
    }
    else 
    {
        g_free(target);
        g_free(original_name);
        /* Remove broken link */
        unlink(full_path);
    }
}
}
}
}
}

g_free(full_path);
}

closedir(dir);
return items;
}

/* Enforce strict item limit in directory with timestamp system */
static void enforce_item_limit_with_timestamps(const gchar *folder_path, guint
max_items) {
/* Work with the .timestamps directory for ordering */
gchar *timestamps_dir = g_build_filename(folder_path, ".timestamps", NULL);

/* Make sure .timestamps directory exists */
g_mkdir_with_parents(timestamps_dir, 0755);

/* Load valid items and enforce count limit */
GList *items = load_recent_items_from_timestamps_dir(timestamps_dir);
guint item_count = g_list_length(items);

if (item_count <= max_items) {
g_list_free_full(items, (GDestroyNotify)recent_item_free);
g_free(timestamps_dir);
return;
}

/* Remove items beyond max_items from .timestamps directory */
GList *to_remove = g_list_nth(items, max_items);
while (to_remove) {
RecentItem *item = (RecentItem *)to_remove->data;

//      gchar *link_name = create_safe_filename(item->name, item->timestamp);
//    gchar *link_path = g_build_filename(timestamps_dir, link_name, NULL);

//      unlink(link_path);
gchar *old_link = g_build_filename(timestamps_dir,
item->timestamp_filename, NULL);
unlink(old_link);
g_free(old_link);
//     g_free(link_name);
//    g_free(link_path);
to_remove = to_remove->next;
}

g_list_free_full(items, (GDestroyNotify)recent_item_free);
g_free(timestamps_dir);
}

static gchar *calculate_reduced_path(const gchar *target_path, GList
*all_items) {
gchar **target_parts = g_strsplit(target_path, "/", -1);
gint target_len = g_strv_length(target_parts);

/* Find common prefix length among all paths */
gint common_prefix = 0;
for (gint i = 0; i < target_len - 1; i++) {
gboolean all_match = TRUE;
for (GList *l = all_items; l; l = l->next) {
RecentItem *item = l->data;
gchar **other_parts = g_strsplit(item->path, "/", -1);
gint other_len = g_strv_length(other_parts);

if (i >= other_len - 1 || g_strcmp0(target_parts[i],
other_parts[i]) != 0) {
all_match = FALSE;
g_strfreev(other_parts);
break;
}
g_strfreev(other_parts);
}

if (all_match) {
common_prefix = i + 1;
} else {
break;
}
}

/* Build reduced path from common prefix to end */
GString *result = g_string_new("");
for (gint i = common_prefix; i < target_len - 1; i++) {
if (result->len > 0) g_string_append_c(result, '/');
g_string_append(result, target_parts[i]);
}

/* Ensure at least one component */
if (result->len == 0 && target_len > 1) {
g_string_append(result, target_parts[target_len - 2]);
}

/* Replace slashes with safe separator */
for (gchar *p = result->str; *p; p++) {
if (*p == '/') *p = '|';
}

g_strfreev(target_parts);
return g_string_free(result, FALSE);
}

/* Create user-friendly main links from timestamp tracking links */
static void create_clean_links(const gchar *folder_path, LaunchMenuPlugin
*launch_plugin) {
g_print("CLEAN_LINKS: Called for %s\n", folder_path);
g_debug("CLEAN_LINKS: Function called for %s\n", folder_path);
if (!g_file_test(folder_path, G_FILE_TEST_IS_DIR)) {
return;
}

/* Create a ".timestamps" subdirectory for ordering */
gchar *timestamps_dir = g_build_filename(folder_path, ".timestamps", NULL);
g_mkdir_with_parents(timestamps_dir, 0755);

/* Load all timestamped items from .timestamps directory */
GList *items = load_recent_items_from_timestamps_dir(timestamps_dir);

if (!items) {
g_free(timestamps_dir);
return;
}

/* Remove old main directory links (except .timestamps) */
DIR *dir = opendir(folder_path);
if (dir) {
struct dirent *entry;
while ((entry = readdir(dir)) != NULL) {
if (strcmp(entry->d_name, ".") == 0 ||
strcmp(entry->d_name, "..") == 0 ||
strcmp(entry->d_name, ".timestamps") == 0) {
continue;
}
gchar *old_link = g_build_filename(folder_path, entry->d_name,
NULL);
unlink(old_link);
g_free(old_link);
}
closedir(dir);
}

/* Remove old duplicate folders when switching modes */
dir = opendir(folder_path);
if (dir) {
struct dirent *entry;
while ((entry = readdir(dir)) != NULL) {
if (g_str_has_suffix(entry->d_name, " folder")) {
gchar *folder_to_remove = g_build_filename(folder_path,
entry->d_name, NULL);
/* Recursively remove folder and contents */
if (g_file_test(folder_to_remove, G_FILE_TEST_IS_DIR)) {
gchar *rm_command = g_strdup_printf("rm -rf '%s'",
folder_to_remove);
g_spawn_command_line_sync(rm_command, NULL, NULL, NULL,
NULL);
g_free(rm_command);
}
g_free(folder_to_remove);            }
}
closedir(dir);
}
/* Create new clean links in main directory (up to MAX_RECENT_ITEMS) */
gchar *basename = g_path_get_basename(folder_path);
guint max_items = 20;
if (g_strcmp0(basename, "Recent Applications") == 0) {
max_items = launch_plugin->recent_apps_count;
} else if (g_strcmp0(basename, "Recent Documents") == 0){
max_items = launch_plugin->recent_docs_count;
}
g_print("CLEAN_LINKS: Starting, items=%d, max=%d\n", g_list_length(items),
max_items);

/* Group items by filename */
GHashTable *name_groups = g_hash_table_new_full(g_str_hash, g_str_equal,
g_free, (GDestroyNotify)g_list_free);
GHashTableIter iter;
gpointer key, value;
g_print("NAME GROUPS INITIALIZED\n");

/* Build groups */
for (GList *l = items; l; l = l->next) {
RecentItem *item = l->data;

GList *group = g_hash_table_lookup(name_groups, item->name);
group = g_list_append(group, item);

/* Use steal + insert to avoid double-free */
g_hash_table_steal(name_groups, item->name);
g_hash_table_insert(name_groups, g_strdup(item->name), group);
}
g_print("CLEAN_LINKS: Created %d groups\n", g_hash_table_size(name_groups));

/* Process groups */
g_hash_table_iter_init(&iter, name_groups);
while (g_hash_table_iter_next(&iter, &key, &value)) {
GList *group = (GList*)value;
group = g_list_sort(group, compare_paths_for_processing);

if (!launch_plugin->classic_style_duplicates && g_list_length(group) >1) {
/* Create folder for duplicates */
g_print("GROUP_DEBUG: Creating folder for %s\n", (gchar*)key);
gchar *folder_name = g_strdup_printf("%s folder", (gchar*)key);
gchar *dup_folder_path = g_build_filename(folder_path, folder_name,
NULL);
g_mkdir_with_parents(dup_folder_path, 0755);
/* Create symlinks inside with reduced path names */
for (GList *g = group; g; g = g->next) {
RecentItem *item = g->data;
/* Calculate reduced path by removing common prefix */
g_print("CLEAN_LINKS: Calculating reduced path for %s\n",
item->path);
gchar *reduced_path = calculate_reduced_path(item->path, group);
gchar *symlink_path = g_build_filename(dup_folder_path,
reduced_path, NULL);
g_print("SYMLINK_DEBUG: Creating %s -> %s\n", symlink_path,
item->path);

/* Create symlink */
if (symlink(item->path, symlink_path) != 0) {
g_warning("Failed to create symlink %s: %s\n",
symlink_path, strerror(errno));
}
g_print("CLEAN_LINKS: Cleaning up hash table\n");
g_free(reduced_path);
g_free(symlink_path);
}
g_free(folder_name);
g_free(dup_folder_path);
} else {
/* Create single symlink (use first/newest item) */
g_print("GROUP_DEBUG: Creating single link for %s\n", (gchar*)key);
RecentItem *item = (RecentItem*)group->data;
gchar *main_link = g_build_filename(folder_path, item->name, NULL);

/* Create symlink */
if (symlink(item->path, main_link) != 0) {
g_warning("Failed to create symlink: %s", strerror(errno));
}
g_free(main_link);
}
}

g_print("CLEAN_LINKS: About to destroy namegroups hash table\n");
g_hash_table_destroy(name_groups);
g_print("CLEAN_LINKS: About to free items list\n");
g_list_free_full(items, (GDestroyNotify)recent_item_free);
g_print("CLEAN_LINKS: About to free timestamps\n");
g_free(timestamps_dir);
g_print("Freed\n");
}

/* Update clean links after changes */
static void update_clean_links(LaunchMenuPlugin *launch_plugin) {
g_print("UPDATE_CLEAN_LINKS: Called\n");
g_debug("UPDATE_CLEAN_LINKS: Called\n");
if (!launch_plugin || !launch_plugin->launch_menu_path) {
return;
}

gchar *recent_apps_path = g_build_filename(launch_plugin->launch_menu_path, RECENT_APPS_DIR, NULL);
gchar *recent_docs_path = g_build_filename(launch_plugin->launch_menu_path, RECENT_DOCS_DIR, NULL);

/* Enforce limits first */
enforce_item_limit_with_timestamps(recent_apps_path,
launch_plugin->recent_apps_count);

/* Then create clean links */
create_clean_links(recent_apps_path, launch_plugin);
create_clean_links(recent_docs_path, launch_plugin);

g_free(recent_apps_path);
g_free(recent_docs_path);
}

static void xbel_entry_free(XbelEntry *entry) {
g_free(entry->path);
g_free(entry->name);
g_free(entry);
}

static gint compare_paths_for_processing(gconstpointer a, gconstpointer b) {
const RecentItem *item_a = (const RecentItem *)a;
const RecentItem *item_b = (const RecentItem *)b;
return strlen(item_a->path) - strlen(item_b->path);
}

static gint compare_xbel_entries(gconstpointer a, gconstpointer b) {
if (!a && !b) return 0;
if (!a) return 1;  // NULL sorts last
if (!b) return -1; // NULL sorts last
const XbelEntry *entry_a = (const XbelEntry *)a;
const XbelEntry *entry_b = (const XbelEntry *)b;
if (entry_a->timestamp > entry_b->timestamp) return -1;
if (entry_a->timestamp < entry_b->timestamp) return 1;
return 0;
}

/* Parse XBEL file and extract recent documents */
/* Complete fixed version of parse_xbel_file() function */
/* This replaces the entire function in cur-launch-menu.c */

static void parse_xbel_file(LaunchMenuPlugin *launch_plugin) {
if (!launch_plugin->recents_on) return;
g_print("XBEL_PARSE: Starting, max docs = %d\n",
launch_plugin->recent_docs_count);
gchar *xbel_path = g_build_filename(g_get_user_data_dir(),
"recently-used.xbel", NULL);

if (!g_file_test(xbel_path, G_FILE_TEST_EXISTS)) {
g_free(xbel_path);
return;
}

xmlDoc *doc = xmlReadFile(xbel_path, NULL, 0);
if (!doc) {
g_free(xbel_path);
return;
}

xmlNode *root = xmlDocGetRootElement(doc);
if (!root || xmlStrcmp(root->name, (const xmlChar *)"xbel") != 0) {
xmlFreeDoc(doc);
g_free(xbel_path);
return;
}

gchar *recent_docs_path = g_build_filename(launch_plugin->launch_menu_path, RECENT_DOCS_DIR, NULL);
gchar *timestamps_dir = g_build_filename(recent_docs_path, ".timestamps",
NULL);
g_mkdir_with_parents(timestamps_dir, 0755);
/* Track seen filenames for Mac-style deduplication */
GHashTable *seen_names = NULL;
if (launch_plugin->classic_style_duplicates) {
seen_names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
NULL);
}
/* Load existing timestamp entries */
GList *existing_timestamps = load_recent_items_from_timestamps_dir(timestamps_dir);

GList *xbel_entries = NULL;
/* Process bookmarks - improved XML parsing with application filtering */
for (xmlNode *current = root->children; current; current = current->next) {
/* Skip non-element nodes (whitespace, comments, etc.) */
if (current->type != XML_ELEMENT_NODE) {
continue;
}

/* Check if this is a bookmark element */
if (xmlStrcmp(current->name, (const xmlChar *)"bookmark") != 0) {
continue;
}
xmlChar *href = xmlGetProp(current, (const xmlChar *)"href");
g_print("XBEL_BOOKMARK: Found bookmark node, href='%s'\n", href ?
(char*)href : "NULL");        /* Check if this bookmark is from a blacklisted
application */
if (classlib_should_blacklist_application(current)) {
g_print("XBEL_BOOKMARK: Blacklisted\n");
if (href) xmlFree(href);
continue; /* Skip blacklisted applications */
}
g_print("XBEL_BOOKMARK: Passed blacklist\n");
g_print("XBEL_COLLECT: Found %d entries total\n",
g_list_length(xbel_entries));
if (xbel_entries) {
XbelEntry *first = (XbelEntry *)xbel_entries->data;
g_print("XBEL_COLLECT: First entry: '%s' timestamp=%ld\n",
first->name, first->timestamp);
}

/* Extract attributes from bookmark element */
href = xmlGetProp(current, (const xmlChar *)"href");
xmlChar *added = xmlGetProp(current, (const xmlChar *)"added");
xmlChar *modified = xmlGetProp(current, (const xmlChar *)"modified");

if (href && (modified || added)) {
gchar *file_uri = (gchar *)href;

if (g_str_has_prefix(file_uri, "file://")) {
gchar *local_path = g_filename_from_uri(file_uri, NULL, NULL);

if (local_path && g_file_test(local_path, G_FILE_TEST_EXISTS)
&& is_document_file(local_path)) {
gchar *timestamp_str = modified ? (gchar *)modified :
(gchar *)added;
GDateTime *datetime = g_date_time_new_from_iso8601(timestamp_str, NULL);

if (datetime) {
XbelEntry *entry = g_new0(XbelEntry, 1);
entry->path = g_strdup(local_path);
entry->name = g_path_get_basename(local_path);
entry->timestamp = g_date_time_to_unix(datetime);

/* Mac-style duplicate handling */
if (launch_plugin->classic_style_duplicates) {
if (g_hash_table_lookup(seen_names, entry->name)) {
/* Skip this entry - older duplicate */
xbel_entry_free(entry);
g_date_time_unref(datetime);
continue;
}
g_hash_table_insert(seen_names, g_strdup(entry->name),
GINT_TO_POINTER(1));
}
xbel_entries = g_list_insert_sorted(xbel_entries, entry,
compare_xbel_entries);
g_date_time_unref(datetime);
}
g_free(local_path);
}
}
/* Clean up XML attributes */
if (href) xmlFree(href);
if (added) xmlFree(added);
if (modified) xmlFree(modified);
}
}
g_print("XBEL_PARSE: Collected %d total entries\n",
g_list_length(xbel_entries));

// BEGIN TIMESTAMP CLEANING SECTION
/* Compare lists and remove matches */
GList *to_create = NULL;
gint count = 0;
for (GList *l = xbel_entries; l && count <
launch_plugin->recent_docs_count; l = l->next, count++){
XbelEntry *new_entry = l->data;
gboolean found_match = FALSE;

for (GList *e = existing_timestamps; e; e = e->next) {
RecentItem *existing = e->data;
if (g_strcmp0(new_entry->path, existing->path) == 0) {
found_match = TRUE;
/* Remove from both lists - these stay unchanged */
existing_timestamps = g_list_remove(existing_timestamps,
existing);
recent_item_free(existing);
break;
}
}

if (!found_match) {
to_create = g_list_append(to_create, new_entry);
}
}
/* Remaining existing_timestamps are obsolete */
// Deletion and cleaning sections
/* Debug: Print existing timestamps */
g_print("XBEL_DEBUG: Found %d existing timestamp files:\n",
g_list_length(existing_timestamps));
for (GList *l = existing_timestamps; l; l = l->next) {
RecentItem *item = l->data;
g_print("  - %s (target: %s)\n", item->name, item->path);
}

/* Debug: Print files to create */
g_print("XBEL_DEBUG: Will create %d new files:\n",
g_list_length(to_create));
for (GList *l = to_create; l; l = l->next) {
XbelEntry *entry = l->data;
g_print("  + %s (target: %s)\n", entry->name, entry->path);
}

/* Delete obsolete files */
g_print("XBEL_DEBUG: Deleting %d obsolete files:\n",
g_list_length(existing_timestamps));
for (GList *l = existing_timestamps; l; l = l->next) {
RecentItem *item = l->data;
gchar *old_link = g_build_filename(timestamps_dir,
item->timestamp_filename, NULL);
g_print("  Deleting: %s -> ", old_link);
if (unlink(old_link) == 0) {
g_print("SUCCESS\n");
} else {
g_print("FAILED (%s)\n", strerror(errno));
}
g_free(old_link);
}

/* Create new files - only process first N items */
count = 0;
for (GList *l = to_create; l && count <
launch_plugin->recent_docs_count; l = l->next, count++) {
if (!l->data) {
g_print("XBEL_ERROR: NULL entry at position %d\n", count);
continue;
}
XbelEntry *entry = (XbelEntry *)l->data;
g_print("XBEL_CREATE: [%d] '%s' timestamp=%ld\n", count,
entry->name ? entry->name : "NULL", entry->timestamp);
gchar *link_name = create_safe_filename(entry->name,
entry->timestamp);
gchar *link_path = g_build_filename(timestamps_dir, link_name,
NULL);

if (symlink(entry->path, link_path) != 0) {
g_warning("Failed to create symlink from %s to %s: %s",
link_path, entry->path, strerror(errno));
}
g_print("XBEL_PARSE: Created %d timestamp files\n", count);
g_free(link_name);
g_free(link_path);
}
/* Cleanup */
if (seen_names) {
g_hash_table_destroy(seen_names);
}
g_list_free_full(existing_timestamps, (GDestroyNotify)recent_item_free);
g_list_free(to_create);
// end timestamp processing

/* Clean up */
g_list_free_full(xbel_entries, (GDestroyNotify)xbel_entry_free);

/* Enforce strict limit on documents */
// enforce_item_limit_with_timestamps(recent_docs_path, launch_plugin->recent_docs_count);

xmlFreeDoc(doc);
g_free(timestamps_dir);
g_free(recent_docs_path);
g_free(xbel_path);
}



/* File monitor callback for XBEL changes */
static void on_xbel_file_changed(GFileMonitor *monitor G_GNUC_UNUSED,
GFile *file G_GNUC_UNUSED,
GFile *other_file G_GNUC_UNUSED,
GFileMonitorEvent event_type,
gpointer user_data) {
LaunchMenuPlugin *launch_plugin = (LaunchMenuPlugin *)user_data;
if (!launch_plugin->recents_on) return;

if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
event_type == G_FILE_MONITOR_EVENT_CREATED) {
parse_xbel_file(launch_plugin);

/* Update clean links after XBEL changes */
update_clean_links(launch_plugin);
}
}

/* Phase 4: Application Monitoring via libwnck
(Revised Implementation) */

/* Setup wnck-based application monitoring */
static void
setup_application_monitoring(LaunchMenuPlugin *launch_plugin) {
/* Initialize hash table for tracking
known applications */
launch_plugin->known_applications = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

/* Get the default screen */
launch_plugin->handle = wnck_handle_new(WNCK_CLIENT_TYPE_PAGER);
launch_plugin->wnck_screen = wnck_handle_get_default_screen(launch_plugin->handle);
if (!launch_plugin->wnck_screen) {
g_warning("Failed to get default wnck screen");
return;
}

/* Connect to application events */

g_signal_connect(launch_plugin->wnck_screen, "application-opened",

G_CALLBACK(on_application_opened), launch_plugin);

/* Force initial update and populate known
applications */

wnck_screen_force_update(launch_plugin->wnck_screen);

populate_initial_applications(launch_plugin);
}

/* Populate initial applications list to avoid
spurious "new" detections */
static void
populate_initial_applications(LaunchMenuPlugin *launch_plugin) {
GList *windows = wnck_screen_get_windows(launch_plugin->wnck_screen);
GHashTable *seen_apps = g_hash_table_new(g_direct_hash, g_direct_equal);

for (GList *window_list = windows;
window_list; window_list = window_list->next) {
WnckWindow *window = WNCK_WINDOW(window_list->data);
if (!window) continue;

WnckApplication *app = wnck_window_get_application(window);
if (!app) continue;

/* Skip if we've already processed
this application */
if (g_hash_table_lookup(seen_apps,
app)) {
continue;
}
g_hash_table_insert(seen_apps, app,
GINT_TO_POINTER(1));

const gchar *app_name = wnck_application_get_name(app);
if (app_name && strlen(app_name) > 0) {
/* Mark as known application */
g_hash_table_insert(launch_plugin->known_applications, g_strdup(app_name), GINT_TO_POINTER(1));
}
}

g_hash_table_destroy(seen_apps);
}
/* Handle new application opened events */
static void on_application_opened(WnckScreen
*screen, WnckApplication *app, gpointer user_data) {
(void)screen;
LaunchMenuPlugin *launch_plugin = (LaunchMenuPlugin *)user_data;
if (!launch_plugin->recents_on) return;
const gchar *app_name = wnck_application_get_name(app);
if (!app_name || strlen(app_name) == 0 ||
classlib_is_file_manager(app)) {
return;
}
/* Skip system applications */
const gchar *blacklisted_apps[] = {
"xfce4-panel", "Xfce4-panel", "Panel",
"Wrapper", "Wrapper-2.0",
"Xfce4-notifyd",
NULL
};

for (int i = 0; blacklisted_apps[i]; i++) {
if (g_ascii_strcasecmp(app_name,
blacklisted_apps[i]) == 0) {
return;
}
}
/* Add to recent applications */

add_recent_application_from_wnck(launch_plugin, app);
}

/* Add application to recent applications from wnck data */
static void add_recent_application_from_wnck(LaunchMenuPlugin *launch_plugin, WnckApplication *app)
{
    /* SHARED CODE USAGE: Use name resolution
    instead of raw wnck name */
    const gchar *app_name = classlib_get_application_display_name(app);

    if (!app_name || strlen(app_name) == 0) 
    {
        return;
    }

    /* Try to find corresponding desktop file */
    gchar *desktop_path = classlib_find_desktop_file(app_name, app);

    if (desktop_path) 
    {
        /* Get display name from desktop file */
        gchar *display_name = NULL;
        gchar *icon_name = NULL;
        if(parse_desktop_file_for_menu(desktop_path, &display_name, &icon_name)) 
        {
            create_recent_application_entry_with_desktop(launch_plugin, display_name ?
            display_name : app_name, desktop_path);
            g_free(display_name);
            g_free(icon_name);
        }
        g_free(desktop_path);
    }
}

/* Create recent application entry using
desktop file */
static void
create_recent_application_entry_with_desktop(LaunchMenuPlugin *launch_plugin,
const gchar *display_name, const gchar *desktop_path) {
gchar *recent_apps_path = g_build_filename(launch_plugin->launch_menu_path, RECENT_APPS_DIR, NULL);
gchar *timestamps_dir = g_build_filename(recent_apps_path, ".timestamps", NULL);
g_mkdir_with_parents(timestamps_dir, 0755);
/* Remove any existing timestamp entries
for this app */
DIR *ts_dir = opendir(timestamps_dir);
if (ts_dir) {
struct dirent *entry;
while ((entry = readdir(ts_dir)) != NULL) {
if (strstr(entry->d_name,
display_name)) {
gchar *old_ts = g_build_filename(timestamps_dir, entry->d_name, NULL);
unlink(old_ts);
g_free(old_ts);
}
}
closedir(ts_dir);
}
/* Create timestamp filename */

time_t now = time(NULL);
gchar *safe_filename = create_safe_filename(display_name, now);
gchar *timestamp_path = g_build_filename(timestamps_dir, safe_filename, NULL);

/* Create timestamp file */
FILE *ts_file = fopen(timestamp_path, "w");
if (ts_file) {
fprintf(ts_file, "%s\n", desktop_path);
fclose(ts_file);
}

/* Create clean symlink with .desktop
extension */
gchar *link_name = g_strdup_printf("%s.desktop", display_name);
gchar *existing_link = g_build_filename(recent_apps_path, link_name, NULL);


/* Remove existing entry if present */

if (g_file_test(existing_link,
G_FILE_TEST_EXISTS)) {
unlink(existing_link);
}
g_free(link_name);

if (symlink(desktop_path, existing_link)
!= 0) {
g_warning("Failed to create symlink: %s -> %s", existing_link, desktop_path);
}

/* Enforce limits */

enforce_item_limit_with_timestamps(recent_apps_path,
launch_plugin->recent_apps_count);

g_free(existing_link);
g_free(safe_filename);
g_free(timestamp_path);
g_free(recent_apps_path);
g_free(timestamps_dir);
}

/* Search a directory for desktop file matching application name */
// Unused, but preserved in comments just in case it's ever useful...
/*
static gchar *search_desktop_dir_for_app(const
gchar *dir_path, const gchar *app_name) 
{
    DIR *dir = opendir(dir_path);
    if (!dir) 
    {
        return NULL;
    }

    struct dirent *entry;
    gchar *result = NULL;

    while ((entry = readdir(dir)) != NULL) {
    if (!g_str_has_suffix(entry->d_name,
    ".desktop")) {
    continue;
    }

    gchar *desktop_path = g_build_filename(dir_path, entry->d_name, NULL);
    gchar *display_name = NULL;
    gchar *icon_name = NULL;

    if
    (parse_desktop_file_for_menu(desktop_path, &display_name, &icon_name)) {
    if (display_name &&
    g_ascii_strcasecmp(display_name, app_name) == 0) {
    result = g_strdup(desktop_path);
    g_free(display_name);
    g_free(icon_name);
    g_free(desktop_path);
    break;
    }
    g_free(display_name);
    g_free(icon_name);
    }
    g_free(desktop_path);
    }

    closedir(dir);
    return result;
} */

/* Create simple recent application entry without desktop file
We don't need this now but just in case the pieces are ever useful for something else
static void create_simple_recent_application_entry(LaunchMenuPlugin *launch_plugin, const
gchar *app_name, WnckApplication *app) {
// Check if we already created a desktop file for this app /
gchar *desktop_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
gchar *desktop_file = g_strdup_printf("%s.desktop", app_name);
gchar *desktop_path = g_build_filename(desktop_dir, desktop_file, NULL);

if (g_file_test(desktop_path,
G_FILE_TEST_EXISTS)) {
// Desktop file exists, use it directly /

create_recent_application_entry_with_desktop(launch_plugin, app_name,
desktop_path);
goto cleanup;
}
// Get executable path from PID /
pid_t pid = wnck_application_get_pid(app);
gchar *exec_path = classlib_get_process_name_from_pid(pid);

if (!exec_path || g_strcmp0(exec_path,
"unknown") == 0) 
{
g_free(exec_path);
return;
}

// Create desktop file in user directory 
g_mkdir_with_parents(desktop_dir, 0755);

gchar *content = g_strdup_printf(
"[Desktop Entry]\n"
"Type=Application\n"
"Name=%s\n"
"Exec=%s\n"
"Categories=Utility;\n",
app_name, exec_path);

g_file_set_contents(desktop_path, content,
-1, NULL);

/ Now create recent entry with this
desktop file /

create_recent_application_entry_with_desktop(launch_plugin, app_name,
desktop_path);

g_free(exec_path);
g_free(content);
cleanup:
g_free(desktop_dir);
g_free(desktop_file);
g_free(desktop_path);

}
*/

/* Cleanup application monitoring */

static void cleanup_application_monitoring(LaunchMenuPlugin *launch_plugin)
{

if (launch_plugin->wnck_screen) {

g_signal_handlers_disconnect_by_data(launch_plugin->wnck_screen,
launch_plugin);

}


if (launch_plugin->known_applications) {

g_hash_table_destroy(launch_plugin->known_applications);

}

}

/* Compare MenuItemInfo structures by display name using natural sorting */
static gint compare_menu_items(gconstpointer a, gconstpointer b) 
{
const MenuItemInfo *item_a = (const MenuItemInfo *)a;
const MenuItemInfo *item_b = (const MenuItemInfo *)b;
return classlib_natural_compare_strings(item_a->display_name, item_b->display_name);
}

/* Free a MenuItemInfo structure */
static void menu_item_info_free(MenuItemInfo *info) 
{
if (info) 
{
g_free(info->filename);
g_free(info->display_name);
g_free(info->full_path);
g_free(info);
}
}

/* Helper function to parse desktop file for menu display */
static gboolean parse_desktop_file_for_menu(const gchar *desktop_path, gchar **display_name, gchar **icon_name) 
{
GKeyFile *key_file = g_key_file_new();
GError *error = NULL;

if (!g_key_file_load_from_file(key_file, desktop_path, G_KEY_FILE_NONE, &error)) 
{
if (error) g_error_free(error);
g_key_file_free(key_file);
return FALSE;
}

/* Get application name */
gchar *name = g_key_file_get_string(key_file, "Desktop Entry", "Name", NULL);
if (!name) {    
name = g_path_get_basename(desktop_path);
/* Remove .desktop extension */
gchar *dot = strrchr(name, '.');
if (dot) *dot = '\0';
}

/* Get icon name */
gchar *icon = g_key_file_get_string(key_file, "Desktop Entry", "Icon", NULL);

/* Check if desktop entry should be shown
*/
gboolean no_display = g_key_file_get_boolean(key_file, "Desktop Entry", "NoDisplay", NULL);
gboolean hidden = g_key_file_get_boolean(key_file, "Desktop Entry", "Hidden", NULL);

g_key_file_free(key_file);

if (no_display || hidden) {
g_free(name);
g_free(icon);
return FALSE;
}

if (name) {
*display_name = ensure_valid_utf8(name);
*icon_name = icon; /* Can be NULL */
g_free(name);
return TRUE;
}

g_free(name);
g_free(icon);
return FALSE;
}

/* Get appropriate display name for a menu
item */
static gchar *get_item_display_name(const
gchar *item_name, const gchar *item_path) {
gchar *display_name = NULL;

/* Check if it's a desktop file or symlink
to a desktop file */
if (g_str_has_suffix(item_name,
".desktop")) {
gchar *temp_display = NULL;
gchar *temp_icon = NULL;
if
(parse_desktop_file_for_menu(item_path, &temp_display, &temp_icon)) {
display_name = temp_display; /*
Transfer ownership */
g_free(temp_icon); /* We don't
need icon here */
}
} else if (g_file_test(item_path,
G_FILE_TEST_IS_SYMLINK)) {
gchar *target = g_file_read_link(item_path, NULL);
if (target && g_str_has_suffix(target,
".desktop")) {
gchar *temp_display = NULL;
gchar *temp_icon = NULL;
if
(parse_desktop_file_for_menu(target, &temp_display, &temp_icon)) {
display_name = temp_display;
/* Transfer ownership */
g_free(temp_icon); /* We don't
need icon here */
}
}
g_free(target);
}

/* Fall back to original filename if no
display name found */
if (!display_name) {
display_name = ensure_valid_utf8(item_name);
}

return display_name;
}

/* Find icon function */
static gchar *launch_menu_find_icon(void) {
const gchar *icon_candidates[] = {
"xfce_xicon1",
"system-icon-white",
"org.xfce.panel.applicationsmenu",
NULL
};

GtkIconTheme *icon_theme = gtk_icon_theme_get_default();

for (int i = 0; icon_candidates[i]; i++) {
if
(gtk_icon_theme_has_icon(icon_theme, icon_candidates[i])) {
return
g_strdup(icon_candidates[i]);
}
}

return
g_strdup("org.xfce.panel.applicationsmenu");
}

/* Enhanced create menu item with icon -
desktop file support */
static GtkWidget
*create_menu_item_with_icon(const gchar *label, const gchar *item_path) {
GtkWidget *menu_item = gtk_menu_item_new();
GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
GtkWidget *icon = NULL;
GtkWidget *label_widget = NULL;

gchar *display_name = NULL;
gchar *icon_name = NULL;

/* Check if this is a desktop file symlink
*/
if (item_path && g_file_test(item_path,
G_FILE_TEST_IS_SYMLINK)) {
gchar *target = g_file_read_link(item_path, NULL);
if (target && g_str_has_suffix(target,
".desktop")) {
/* It's a symlink to a desktop
file - parse it for pretty name and icon */

parse_desktop_file_for_menu(target, &display_name, &icon_name);
}
g_free(target);
} else if (item_path &&
g_str_has_suffix(item_path, ".desktop")) {
/* It's a direct desktop file - parse
it */
parse_desktop_file_for_menu(item_path,
&display_name, &icon_name);
}

/* Use parsed display name if available,
otherwise use provided label */
const gchar *final_label = display_name ?
display_name : label;
label_widget = gtk_label_new(final_label);

/* Set label alignment */
gtk_widget_set_halign(label_widget,
GTK_ALIGN_START);

gtk_label_set_xalign(GTK_LABEL(label_widget), 0.0);

/* Try to get appropriate icon */
if (icon_name) {
/* Use icon from desktop file */
icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
} else if (g_strcmp0(final_label, "About This Computer") == 0) {
/* Special case for About This Computer */
icon = gtk_image_new_from_icon_name("computer", GTK_ICON_SIZE_MENU);
} else if (item_path) {
if (g_file_test(item_path,
G_FILE_TEST_IS_DIR)) {
/* Check for custom folder icon
via GIO metadata */
GFile *folder_file = g_file_new_for_path(item_path);
GFileInfo *info = g_file_query_info(folder_file, "metadata::custom-icon",

G_FILE_QUERY_INFO_NONE, NULL, NULL);
const gchar *custom_icon = NULL;
if (info) {
custom_icon = g_file_info_get_attribute_string(info, "metadata::custom-icon");
}
if (custom_icon &&
strlen(custom_icon) > 0) {
    icon = gtk_image_new_from_icon_name(custom_icon, GTK_ICON_SIZE_MENU);

} else {
/* Fallback to folder icon */

icon = gtk_image_new_from_icon_name("folder", GTK_ICON_SIZE_MENU);
}
g_object_unref(info);
g_object_unref(folder_file);
} else if (g_file_test(item_path,
G_FILE_TEST_IS_EXECUTABLE) ||
g_str_has_suffix(item_path,
".desktop")) {
/* It's executable or desktop file
- try to get application icon */
GFile *file = g_file_new_for_path(item_path);
GFileInfo *file_info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_ICON,

G_FILE_QUERY_INFO_NONE, NULL, NULL);
if (file_info) {
GIcon *gicon = g_file_info_get_icon(file_info);
if (gicon) {
icon = gtk_image_new_from_gicon(gicon, GTK_ICON_SIZE_MENU);
}
g_object_unref(file_info);
}
g_object_unref(file);

/* Fallback to generic application
icon */
if (!icon) {
icon = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_MENU);
}
} else {
/* It's a regular file - try
to get file type icon */
GFile *file = g_file_new_for_path(item_path);
GFileInfo *file_info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_ICON,

G_FILE_QUERY_INFO_NONE, NULL, NULL);
if (file_info) {
    GIcon *gicon = g_file_info_get_icon(file_info);
    if (gicon) {
        icon = gtk_image_new_from_gicon(gicon, GTK_ICON_SIZE_MENU);
    }
    g_object_unref(file_info);
}
g_object_unref(file);

/* Fallback to generic
document icon */
if (!icon) {
    icon = gtk_image_new_from_icon_name("text-x-generic", GTK_ICON_SIZE_MENU);
}
}
}

/* If we still don't have an icon, use a
generic one */
if (!icon) {
icon = gtk_image_new_from_icon_name("application-x-generic", GTK_ICON_SIZE_MENU);
}

/* Pack icon and label into box */
gtk_box_pack_start(GTK_BOX(box), icon,
FALSE, FALSE, 0);
gtk_box_pack_start(GTK_BOX(box),
label_widget, TRUE, TRUE, 0);

/* Add box to menu item */

gtk_container_add(GTK_CONTAINER(menu_item), box);

/* Clean up */
g_free(display_name);
g_free(icon_name);

return menu_item;
}

/* Comparison function for recent items - old
style for compatibility */
typedef struct {
gchar *name;
time_t mtime;
} RecentItemOld;

static gint
compare_recent_items_old(gconstpointer a, gconstpointer b) {
const RecentItemOld *item_a = (const
RecentItemOld *)a;
const RecentItemOld *item_b = (const
RecentItemOld *)b;
if (item_a->mtime > item_b->mtime) return
-1;
if (item_a->mtime < item_b->mtime) return
1;
return 0;
}

/* Cleanup recent folder - enforce strict item
limit (legacy function for compatibility) */
static void cleanup_recent_folder(const gchar
*folder_path, gint max_items) {
if (!g_file_test(folder_path,
G_FILE_TEST_IS_DIR)) {
return;
}

DIR *dir = opendir(folder_path);
if (!dir) {
return;
}

GList *items = NULL;
struct dirent *entry;

/* Collect all items with their
modification times */
while ((entry = readdir(dir)) != NULL) {
if (strcmp(entry->d_name, ".") == 0 ||
strcmp(entry->d_name, "..") == 0) {
continue;
}

gchar *item_path = g_build_filename(folder_path, entry->d_name, NULL);
struct stat st;

if (lstat(item_path, &st) == 0) {
RecentItemOld *item = g_new(RecentItemOld, 1);
item->name = g_strdup(entry->d_name);
item->mtime = st.st_mtime;

items = g_list_prepend(items,
item);
}

g_free(item_path);
}
closedir(dir);

/* Sort by modification time (newest
first) */
items = g_list_sort(items,
compare_recent_items_old);

gint count = g_list_length(items);

/* Remove items beyond the limit */
if (count > max_items) {
GList *to_remove = g_list_nth(items,
max_items);
for (GList *l = to_remove; l; l = l->next) {
RecentItemOld *item = (RecentItemOld *)l->data;
gchar *item_path = g_build_filename(folder_path, item->name, NULL);

unlink(item_path);
g_free(item_path);
}
}

/* Free the list */
for (GList *l = items; l; l = l->next) {
RecentItemOld *item = (RecentItemOld
*)l->data;
g_free(item->name);
g_free(item);
}
g_list_free(items);
}

/* Add recent application - basic
implementation */
static void
add_recent_application(LaunchMenuPlugin *launch_plugin, const gchar *app_path)
{
if (!launch_plugin || !app_path ||
!g_file_test(app_path, G_FILE_TEST_EXISTS)) {
return;
}

gchar *recent_apps_path = g_build_filename(launch_plugin->launch_menu_path, RECENT_APPS_DIR, NULL);
gchar *app_name = g_path_get_basename(app_path);

/* Create unique filename with timestamp */
time_t now = time(NULL);
struct tm *tm_info = localtime(&now);
gchar time_str[20];
strftime(time_str, sizeof(time_str),
"%Y%m%d_%H%M%S", tm_info);

gchar *safe_name = g_strdup(app_name);
/* Replace unsafe characters */
for (gchar *p = safe_name; *p; p++) {
if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' ||
*p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
*p = '_';
}
}

gchar *link_name = g_strdup_printf("%s_%s", time_str, safe_name);
gchar *link_path = g_build_filename(recent_apps_path, link_name, NULL);

/* Remove existing link if it exists */
if (g_file_test(link_path,
G_FILE_TEST_EXISTS)) {
unlink(link_path);
}

/* Create symbolic link */
if (symlink(app_path, link_path) == 0) {
g_debug("ADD RECENT APP1: %s -> %s\n",
app_name, link_path);
/* Cleanup old items - enforce strict
limit */

cleanup_recent_folder(recent_apps_path, launch_plugin->recent_apps_count);
}

g_free(app_name);
g_free(safe_name);
g_free(link_name);
g_free(link_path);
g_free(recent_apps_path);
}

/* About Computer click handler */
static void on_about_computer_clicked(G_GNUC_UNUSED LaunchMenuPlugin *launch_plugin) 
{
    gchar *hardinfo_path = g_find_program_in_path("hardinfo2");
    if (!hardinfo_path) 
    {
        hardinfo_path = g_find_program_in_path("hardinfo");
    }

    if (hardinfo_path) 
    {
        GError *error = NULL;

        g_spawn_command_line_async(hardinfo_path, &error);
        if (error) 
        {
            g_error_free(error);
        }
        g_free(hardinfo_path);
    }
}

/* Enhanced launch item click handler with
desktop file support */
static void on_launch_item_clicked(GtkMenuItem *item, gpointer user_data) {
gchar *item_path = (gchar *)user_data;

if (!item_path || !g_file_test(item_path,
G_FILE_TEST_EXISTS)) {
return;
}

/* Resolve symlinks to their actual
targets */
if (g_file_test(item_path,
G_FILE_TEST_IS_SYMLINK)) {
gchar *target = g_file_read_link(item_path, NULL);
if (target && g_file_test(target,
G_FILE_TEST_EXISTS)) {
item_path = target;  /* Use
resolved target for all operations */
} else {
g_free(target);
}
}

GError *error = NULL;
gboolean is_desktop_file = FALSE;

/* Check if it's a desktop file */
if (g_str_has_suffix(item_path,
".desktop")) {
is_desktop_file = TRUE;
}
if (is_desktop_file) {
gchar *desktop_basename = g_path_get_basename(item_path);

/* Remove .desktop extension for
gtk-launch */
gchar *dot = strrchr(desktop_basename,
'.');
if (dot && strcmp(dot, ".desktop") == 0) {
*dot = '\0';
}

gchar *command = g_strdup_printf("gtk-launch %s", desktop_basename);
if
(!g_spawn_command_line_async(command, &error)) {
g_print("Launch failed: %s\n",
error ? error->message : "unknown error");
if (error) g_clear_error(&error);
}

g_free(command);
g_free(desktop_basename);

/* Track as recent application */
LaunchMenuPlugin *launch_plugin = g_object_get_data(G_OBJECT(item), "launch-menu-plugin");
if (launch_plugin) {
// add_recent_application(launch_plugin, resolved_path);
}

} else if (g_file_test(item_path,
G_FILE_TEST_IS_DIR)) {
/* It's a directory - open in file
manager */
gchar *uri = g_filename_to_uri(item_path, NULL, NULL);
if (uri) {
gtk_show_uri_on_window(NULL, uri,
GDK_CURRENT_TIME, &error);
g_free(uri);
}
} else if (g_file_test(item_path,
G_FILE_TEST_IS_EXECUTABLE)) {
/* It's a regular executable - run it
and track as recent application */
g_spawn_command_line_async(item_path,
&error);

/* Track as recent application */
LaunchMenuPlugin *launch_plugin = g_object_get_data(G_OBJECT(item), "launch-menu-plugin");
if (launch_plugin) {
//
add_recent_application(launch_plugin, item_path);
}
} else {
/* It's a regular file - open with
default application */
gchar *uri = g_filename_to_uri(item_path, NULL, NULL);
if (uri) {
gtk_show_uri_on_window(NULL, uri,
GDK_CURRENT_TIME, &error);
g_free(uri);
}
}

if (error) {
g_error_free(error);
}
}

/* Build submenu from directory - enhanced
with natural sorting */
static GtkWidget
*build_submenu_from_directory(const gchar *dir_path, gint depth,
LaunchMenuPlugin *launch_plugin) {
/* Limit recursion depth to prevent overly
deep menus */
if (depth >= 3) {
return NULL;
}

if (!g_file_test(dir_path,
G_FILE_TEST_IS_DIR)) {
return NULL;
}

GtkWidget *submenu = gtk_menu_new();

/* Add "Open This Folder" item at the top
*/
GtkWidget *open_folder_item = create_menu_item_with_icon("Open This Folder", dir_path);
g_signal_connect(open_folder_item,
"activate", G_CALLBACK(on_launch_item_clicked), g_strdup(dir_path));

gtk_menu_shell_append(GTK_MENU_SHELL(submenu), open_folder_item);

/* Add separator */
GtkWidget *separator = gtk_separator_menu_item_new();

gtk_menu_shell_append(GTK_MENU_SHELL(submenu), separator);

DIR *dir = opendir(dir_path);
if (!dir) {
return submenu; /* Return menu with
just "Open This Folder" */
}

struct dirent *entry;
GList *menu_items = NULL;

/* Collect items with display names for
sorting */
while ((entry = readdir(dir)) != NULL) {
if (strcmp(entry->d_name, ".") == 0 ||
strcmp(entry->d_name, "..") == 0) {
continue;
}

/* Skip hidden files/folders */
if (entry->d_name[0] == '.') {
continue;
}

gchar *item_path = g_build_filename(dir_path, entry->d_name, NULL);
gchar *display_name = get_item_display_name(entry->d_name, item_path);

MenuItemInfo *item_info = g_new0(MenuItemInfo, 1);
item_info->filename = g_strdup(entry->d_name);
item_info->display_name = display_name;
item_info->full_path = item_path;

menu_items = g_list_insert_sorted(menu_items, item_info, compare_menu_items);
}
closedir(dir);

/* Create menu items using sorted list */
for (GList *l = menu_items; l; l = l->next) {
MenuItemInfo *item_info = (MenuItemInfo *)l->data;

GtkWidget *menu_item = create_menu_item_with_icon(item_info->display_name, item_info->full_path);

if (g_file_test(item_info->full_path,
G_FILE_TEST_IS_DIR)) {
/* It's a directory - create
submenu */
GtkWidget *nested_submenu = build_submenu_from_directory(item_info->full_path, depth + 1, launch_plugin);
if (nested_submenu) {
/* Check if the submenu has
content beyond "Open This Folder" */
GList *submenu_children = gtk_container_get_children(GTK_CONTAINER(nested_submenu));
gint child_count = g_list_length(submenu_children);

if (submenu_children &&
child_count > 2) {
    /* Submenu has items
beyond "Open This Folder" + separator */

gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), nested_submenu);
} else {
    /* Submenu only has "Open
This Folder" - destroy it and connect folder click instead */

gtk_widget_destroy(nested_submenu);

g_signal_connect(menu_item, "activate", G_CALLBACK(on_launch_item_clicked),
g_strdup(item_info->full_path));
}
g_list_free(submenu_children);
} else {
/* No submenu created -
connect folder click */
g_signal_connect(menu_item,
"activate", G_CALLBACK(on_launch_item_clicked),
g_strdup(item_info->full_path));
}
} else {
/* It's a file - connect the click
handler and set plugin data for recent tracking */

g_object_set_data(G_OBJECT(menu_item), "launch-menu-plugin", launch_plugin);
g_signal_connect(menu_item,
"activate", G_CALLBACK(on_launch_item_clicked),
g_strdup(item_info->full_path));
}


gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menu_item);
}

g_list_free_full(menu_items,
(GDestroyNotify)menu_item_info_free);
return submenu;
}

/* Check hardinfo availability */
static gboolean
launch_menu_check_hardinfo(void) {
gchar *hardinfo_path = g_find_program_in_path("hardinfo2");
if (hardinfo_path) {
g_free(hardinfo_path);
return TRUE;
}

hardinfo_path = g_find_program_in_path("hardinfo");
if (hardinfo_path) {
g_free(hardinfo_path);
return TRUE;
}

return FALSE;
}

/* Create directories */
static void
create_launch_directories(LaunchMenuPlugin *launch_plugin) {
if (!launch_plugin ||
!launch_plugin->launch_menu_path) {
return;
}


g_mkdir_with_parents(launch_plugin->launch_menu_path, 0755);

gchar *recent_apps = g_build_filename(launch_plugin->launch_menu_path, RECENT_APPS_DIR, NULL);
gchar *recent_docs = g_build_filename(launch_plugin->launch_menu_path, RECENT_DOCS_DIR, NULL);

g_mkdir_with_parents(recent_apps, 0755);
g_mkdir_with_parents(recent_docs, 0755);

g_free(recent_apps);
g_free(recent_docs);

/* Create Settings Manager symlink */
gchar *settings_symlink = g_build_filename(launch_plugin->launch_menu_path, "Settings Manager.desktop",
NULL);
if (!g_file_test(settings_symlink,
G_FILE_TEST_EXISTS)) {

symlink("/usr/share/applications/xfce-settings-manager.desktop",
settings_symlink);
}
g_free(settings_symlink);
}

/* Enhanced menu creation with natural sorting
*/
static void
create_simple_menu(LaunchMenuPlugin *launch_plugin) {
if (!launch_plugin) {
return;
}

/* Destroy existing menu */
if (launch_plugin->menu) {

gtk_widget_destroy(launch_plugin->menu);
}

launch_plugin->menu = gtk_menu_new();

/* Add "About This Computer" if available
*/
if (launch_plugin->hardinfo_available) {
GtkWidget *about_item = create_menu_item_with_icon("About This Computer", NULL);
g_signal_connect(about_item,
"activate", G_CALLBACK(on_about_computer_clicked), NULL);

gtk_menu_shell_append(GTK_MENU_SHELL(launch_plugin->menu), about_item);

GtkWidget *separator = gtk_separator_menu_item_new();

gtk_menu_shell_append(GTK_MENU_SHELL(launch_plugin->menu), separator);
}

/* Add launch menu items from directory
with enhanced sorting */
if
(g_file_test(launch_plugin->launch_menu_path, G_FILE_TEST_IS_DIR)) {
DIR *dir = opendir(launch_plugin->launch_menu_path);
if (dir) {
struct dirent *entry;
GList *menu_items = NULL;

/* Collect items with display
names */
while ((entry = readdir(dir)) != NULL) {
if (strcmp(entry->d_name, ".")
== 0 || strcmp(entry->d_name, "..") == 0) {
    continue;
}

gchar *item_path = g_build_filename(launch_plugin->launch_menu_path, entry->d_name, NULL);
gchar *display_name = get_item_display_name(entry->d_name, item_path);

MenuItemInfo *item_info = g_new0(MenuItemInfo, 1);
item_info->filename = g_strdup(entry->d_name);
if (!launch_plugin->recents_on
&&

(g_strcmp0(item_info->filename, RECENT_APPS_DIR) == 0 ||

g_strcmp0(item_info->filename, RECENT_DOCS_DIR) == 0)) {
    continue;
    }
item_info->display_name = display_name;
item_info->full_path = item_path;

menu_items = g_list_insert_sorted(menu_items, item_info, compare_menu_items);
}
closedir(dir);

/* Create menu items with submenu
support using sorted list */
for (GList *l = menu_items; l; l = l->next) {
MenuItemInfo *item_info = (MenuItemInfo *)l->data;

GtkWidget *menu_item = create_menu_item_with_icon(item_info->display_name, item_info->full_path);

if
(g_file_test(item_info->full_path, G_FILE_TEST_IS_DIR)) {
    /* It's a directory -
create submenu */
    GtkWidget *submenu = build_submenu_from_directory(item_info->full_path, 1, launch_plugin);
    if (submenu) {
        /* Check if the
submenu has content beyond "Open This Folder" */
        GList
*submenu_children = gtk_container_get_children(GTK_CONTAINER(submenu));
        gint child_count = g_list_length(submenu_children);

        if (submenu_children
&& child_count > 2) {
            /* Submenu has
items beyond "Open This Folder" + separator */

gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), submenu);
        } else {
            /* Submenu only
has "Open This Folder" - destroy it and connect folder click instead */

gtk_widget_destroy(submenu);

g_signal_connect(menu_item, "activate", G_CALLBACK(on_launch_item_clicked),
g_strdup(item_info->full_path));
        }

g_list_free(submenu_children);
    } else {
        /* No submenu created
- connect folder click */

g_signal_connect(menu_item, "activate", G_CALLBACK(on_launch_item_clicked),
g_strdup(item_info->full_path));
    }
} else {
    /* It's a file - connect
click handler and set plugin data for recent tracking */

g_object_set_data(G_OBJECT(menu_item), "launch-menu-plugin", launch_plugin);

g_signal_connect(menu_item, "activate", G_CALLBACK(on_launch_item_clicked),
g_strdup(item_info->full_path));
}


gtk_menu_shell_append(GTK_MENU_SHELL(launch_plugin->menu), menu_item);
}

g_list_free_full(menu_items,
(GDestroyNotify)menu_item_info_free);
}
} else {
/* Add fallback message */
GtkWidget *no_items = gtk_menu_item_new_with_label("No items found");
gtk_widget_set_sensitive(no_items,
FALSE);

gtk_menu_shell_append(GTK_MENU_SHELL(launch_plugin->menu), no_items);
}

#ifdef DEBUG
/* Add debug version separator and info */
GtkWidget *debug_separator = gtk_separator_menu_item_new();

gtk_menu_shell_append(GTK_MENU_SHELL(launch_plugin->menu), debug_separator);

GtkWidget *version_item = gtk_menu_item_new_with_label(PLUGIN_VERSION);
if (version_item) {
gtk_widget_set_sensitive(version_item,
FALSE);

gtk_menu_shell_append(GTK_MENU_SHELL(launch_plugin->menu), version_item);
}
#endif

gtk_widget_show_all(launch_plugin->menu);
}

/* Button press handler */
static gboolean on_button_pressed(GtkWidget
*widget, GdkEventButton *event, LaunchMenuPlugin *launch_plugin) {
if (event->button == 1) { /* Left mouse
button */
create_simple_menu(launch_plugin);

if (launch_plugin->menu) {

gtk_menu_popup_at_widget(GTK_MENU(launch_plugin->menu), widget,

GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST,

(GdkEvent*)event);
}

return TRUE; /* Event handled */
}

return FALSE;
}

/* Configuration functions */
/* PROPERTIES DIALOG AND CONFIG FUNCTIONS */
static gchar
*launch_menu_get_property_name(LaunchMenuPlugin *plugin, const gchar *property)
{
return g_strconcat(plugin->property_base,
"/", property, NULL);
}

static void
launch_menu_load_settings(LaunchMenuPlugin *plugin) {
if (!plugin || !plugin->channel) return;

/* Load recent items setting */
gchar *prop_name = launch_menu_get_property_name(plugin, "recents-on");
plugin->recents_on = xfconf_channel_get_bool(plugin->channel, prop_name, FALSE); /* Default: FALSE */
g_free(prop_name);

/* Load Mac duplicates setting */
prop_name = launch_menu_get_property_name(plugin, "classical-duplicates");
plugin->classic_style_duplicates = xfconf_channel_get_bool(plugin->channel, prop_name, TRUE); /* Default: TRUE */
g_free(prop_name);

/* Load recent apps count setting */
prop_name = launch_menu_get_property_name(plugin, "recent-apps-count");
plugin->recent_apps_count = xfconf_channel_get_int(plugin->channel, prop_name, 20); /* Default: 20 */
g_free(prop_name);

/* Load recent docs count setting */
prop_name = launch_menu_get_property_name(plugin, "recent-docs-count");
plugin->recent_docs_count = xfconf_channel_get_int(plugin->channel, prop_name, 20); /* Default: 20 */
g_free(prop_name);
}

static void
launch_menu_save_settings(LaunchMenuPlugin *plugin) {
if (!plugin || !plugin->channel) return;

gchar *prop_name = launch_menu_get_property_name(plugin, "recents-on");
xfconf_channel_set_bool(plugin->channel,
prop_name, plugin->recents_on);
g_free(prop_name);

prop_name = launch_menu_get_property_name(plugin, "classical-duplicates");
xfconf_channel_set_bool(plugin->channel,
prop_name, plugin->classic_style_duplicates);
g_free(prop_name);

/* Save checkmarks setting */
prop_name = launch_menu_get_property_name(plugin, "recent-apps-count");
xfconf_channel_set_int(plugin->channel,
prop_name, plugin->recent_apps_count);
g_free(prop_name);

/* Save submenus setting */
prop_name = launch_menu_get_property_name(plugin, "recent-docs-count");
xfconf_channel_set_int(plugin->channel,
prop_name, plugin->recent_docs_count);
g_free(prop_name);
}

static void on_recents_toggled(GtkToggleButton *button, LaunchMenuPlugin *plugin) {
plugin->recents_on = gtk_toggle_button_get_active(button);
launch_menu_save_settings(plugin);
if(plugin->recents_on)
{
    initialize_tracking(plugin);
}
else
{
    kill_tracking(plugin);
}
}

static void sensitivity_changer(GtkToggleButton *button, gpointer user_data)
{
    /* Enables (or in killing, disables) other widgets, since
    they by default are set to auto-update tracking things when
    they're clicked. Besides, users might be confused by options
    to configure something that shouldn't be online.
    */
    gboolean recents_state = gtk_toggle_button_get_active(button);
    if(recents_state)
    {
        gtk_widget_set_sensitive(user_data, TRUE);
    }
    else
    {
        gtk_widget_set_sensitive(user_data, FALSE);
    }
}

static void initialize_tracking(LaunchMenuPlugin *plugin)
{ /* This boots up the different tracking systems. 
     We don't want them to run automatically, or the
     recents-on setting wouldn't stop tracking, it
     would just stop the program from doing anything
     with that tracking: not what I've advertised.
     */
    gchar *xbel_path = g_build_filename(g_get_user_data_dir(), "recently-used.xbel", NULL);
    GFile *xbel_file = g_file_new_for_path(xbel_path);

    plugin->xbel_monitor = g_file_monitor_file(xbel_file, G_FILE_MONITOR_NONE, NULL, NULL);
    if (plugin->xbel_monitor) 
    {
        g_signal_connect(plugin->xbel_monitor, "changed",
        G_CALLBACK(on_xbel_file_changed), plugin);
    }

    g_object_unref(xbel_file);
    g_free(xbel_path);

    /* Phase 4: Set up application monitoring via wnck */

    setup_application_monitoring(plugin);

    /* Initial XBEL parse and link creation */
    parse_xbel_file(plugin);
    update_clean_links(plugin);
}

static void kill_tracking(LaunchMenuPlugin *plugin)
{
    // This attempts to forcibly shut down any tracking that may be going on, just in case anyone cares.
    g_signal_handlers_disconnect_by_func(plugin->xbel_monitor, G_CALLBACK(on_xbel_file_changed), plugin);
    g_signal_handlers_disconnect_by_func(plugin->wnck_screen, G_CALLBACK(on_application_opened), plugin);
    plugin->xbel_monitor = NULL;
    plugin->handle = NULL;
    plugin->wnck_screen = NULL;
}

static void on_classic_dups_toggled(GtkToggleButton *button, LaunchMenuPlugin *plugin) 
{
    plugin->classic_style_duplicates = gtk_toggle_button_get_active(button);
    launch_menu_save_settings(plugin);
    parse_xbel_file(plugin);
    update_clean_links(plugin);
}

static void on_recent_apps_count_spun(GtkSpinButton *spin, LaunchMenuPlugin *plugin) 
{
    plugin->recent_apps_count = gtk_spin_button_get_value_as_int(spin);
    launch_menu_save_settings(plugin);
    update_clean_links(plugin);
}

static void on_recent_docs_count_spun(GtkSpinButton *spin, LaunchMenuPlugin *plugin) 
{
    plugin->recent_docs_count = gtk_spin_button_get_value_as_int(spin);
    launch_menu_save_settings(plugin);
    parse_xbel_file(plugin);
    update_clean_links(plugin);
}

static void launch_menu_configure_plugin(XfcePanelPlugin *panel, LaunchMenuPlugin *plugin)
{
GtkWidget *dialog;
GtkWidget *content_area;
GtkWidget *vbox;
GtkWidget *recents_on_check;
GtkWidget *classic_dups_check;
GtkWidget *recent_apps_count_spin;
GtkWidget *recent_docs_count_spin;

/* Create dialog */
dialog = xfce_titled_dialog_new_with_mixed_buttons(
_("Launch Menu Properties"),

GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(panel))),
GTK_DIALOG_DESTROY_WITH_PARENT,
"window-close-symbolic", _("_Close"),
GTK_RESPONSE_CLOSE,
NULL);


gtk_window_set_icon_name(GTK_WINDOW(dialog), "org.xfce.panel.applicationsmenu");

gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 200);

/* Get content area */
content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

/* Create main vbox */
vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
gtk_box_pack_start(GTK_BOX(content_area),
vbox, TRUE, TRUE, 0);

/* Recent mode checkbox - most important setting first */
recents_on_check = gtk_check_button_new_with_label(_("Enable Recent Applications and Documents"));

gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(recents_on_check),
plugin->recents_on);
gtk_box_pack_start(GTK_BOX(vbox),
recents_on_check, FALSE, FALSE, 0);

/* Mac dups checkbox */
classic_dups_check = gtk_check_button_new_with_label(_("Classical Duplicate Handling"));

gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(classic_dups_check),
plugin->classic_style_duplicates);
if(!plugin->recents_on)
{
    gtk_widget_set_sensitive(classic_dups_check, FALSE);
}
gtk_box_pack_start(GTK_BOX(vbox),
classic_dups_check, FALSE, FALSE, 0);

/* Recent apps field
* Create spin button for max recent items
(10-99)
*/
recent_apps_count_spin = gtk_spin_button_new_with_range(10.0, 99.0, 1.0);

gtk_spin_button_set_value(GTK_SPIN_BUTTON(recent_apps_count_spin),
plugin->recent_apps_count);
/* Add label */
GtkWidget *recent_apps_label = gtk_label_new(_("Max recent applications:"));
GtkWidget *recent_apps_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
if(!plugin->recents_on)
{
    gtk_widget_set_sensitive(recent_apps_count_spin, FALSE);
}
gtk_box_pack_start(GTK_BOX(recent_apps_hbox), recent_apps_label, FALSE, FALSE,
0);

gtk_box_pack_start(GTK_BOX(recent_apps_hbox), recent_apps_count_spin, FALSE,
FALSE, 0);
gtk_box_pack_start(GTK_BOX(vbox),
recent_apps_hbox, FALSE, FALSE, 0);

/* Recent docs field
* Create spin button for max recent items
(10-99)
*/
recent_docs_count_spin = gtk_spin_button_new_with_range(10.0, 99.0, 1.0);

gtk_spin_button_set_value(GTK_SPIN_BUTTON(recent_docs_count_spin),
plugin->recent_docs_count);
/* Add label */
GtkWidget *recent_docs_label = gtk_label_new(_("Max recent documents:"));
GtkWidget *recent_docs_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

if(!plugin->recents_on)
{
    gtk_widget_set_sensitive(recent_docs_count_spin, FALSE);
}
gtk_box_pack_start(GTK_BOX(recent_docs_hbox), recent_docs_label, FALSE, FALSE,
0);

gtk_box_pack_start(GTK_BOX(recent_docs_hbox), recent_docs_count_spin, FALSE,
FALSE, 0);
gtk_box_pack_start(GTK_BOX(vbox),
recent_docs_hbox, FALSE, FALSE, 0);

/* Connect signals */
g_signal_connect(recents_on_check,
"toggled", G_CALLBACK(on_recents_toggled), plugin);
g_signal_connect(classic_dups_check,
"toggled", G_CALLBACK(on_classic_dups_toggled), plugin);
g_signal_connect(recent_apps_count_spin,
"value-changed", G_CALLBACK(on_recent_apps_count_spun), plugin);
g_signal_connect(recent_docs_count_spin,
"value-changed", G_CALLBACK(on_recent_docs_count_spun), plugin);
/* Sensitivity controls*/
g_signal_connect(recents_on_check,
     "toggled", G_CALLBACK(sensitivity_changer), classic_dups_check);
g_signal_connect(recents_on_check,
     "toggled", G_CALLBACK(sensitivity_changer), recent_apps_count_spin);
g_signal_connect(recents_on_check,
     "toggled", G_CALLBACK(sensitivity_changer), recent_docs_count_spin);

/* Show all widgets */
gtk_widget_show_all(dialog);

/* Run dialog */
gtk_dialog_run(GTK_DIALOG(dialog));
gtk_widget_destroy(dialog);
}

static void launch_menu_about(XfcePanelPlugin *panel) {
const gchar *authors[] = { PLUGIN_AUTHORS,
NULL };

gtk_show_about_dialog(

GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(panel))), "program-name",_("Launch Menu"), "version", PLUGIN_VERSION, "comments", _("A program and document launching menu based on classical standards"), "website",
PLUGIN_WEBSITE, "authors", authors,"license",_("This program is free software; you can redistribute it and/or modify it\n under the terms of the GNU General Public License as published by the Free\n Software Foundation; either version 2 of the License, or (at your option)\n any later version.\n\nThis program is distributed in the hope that it will be useful, but WITHOUT\n ANY WARRANTY; without even the implied warranty of MERCHANTABILITY\n or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for\n more details.\n\nYou should have received a copy of the GNU General Public License along with\n this program; if not, write to the Free Software Foundation, Inc., 51 Franklin\n Street, Fifth Floor, Boston, MA 02110-1301, USA."), "logo-icon-name", "xfce4_xicon1", NULL);
}


/* Main construction function */
static void
launch_menu_construct(XfcePanelPlugin *plugin) {

LaunchMenuPlugin *launch_plugin = g_new0(LaunchMenuPlugin, 1);
launch_plugin->plugin = plugin;

/* Initialize configuration properties */
launch_plugin->channel = NULL;
launch_plugin->property_base = NULL;
launch_plugin->recents_on = FALSE;  /*
Default value */
launch_plugin->classic_style_duplicates = TRUE;
launch_plugin->recent_apps_count = 20;  /*
Default value */
launch_plugin->recent_docs_count =  20;
/* Default value */

/* Set up paths - using new folder names */
launch_plugin->launch_menu_path = g_build_filename(g_get_home_dir(), SPATIAL_MENU_DIR, NULL);

/* Find icon */
launch_plugin->icon_name = launch_menu_find_icon();

/* Check hardinfo */
launch_plugin->hardinfo_available = launch_menu_check_hardinfo();

/* Initialize xfconf and load settings */
GError *error = NULL;
if (!xfconf_init(&error)) {
g_warning("Failed to initialize xfconf: %s", error ? error->message : "Unknown error");
if (error) g_error_free(error);
/* Continue without configuration
support */
launch_plugin->channel = NULL;
} else {
launch_plugin->channel = xfconf_channel_get(CONFIG_CHANNEL);
}

/* Set up property base path */
launch_plugin->property_base = g_strdup_printf("%s/plugin-%d",

CONFIG_PROPERTY_BASE,

xfce_panel_plugin_get_unique_id(launch_plugin->plugin));

/* Load settings */
launch_menu_load_settings(launch_plugin);

/* Initialize new fields for current and
future phases */
launch_plugin->xbel_monitor = NULL;
launch_plugin->wnck_screen = NULL;
launch_plugin->known_applications = NULL;

/* Create directories */
create_launch_directories(launch_plugin);

if (launch_plugin->recents_on) 
{
    initialize_tracking(launch_plugin);
}

/* Create button */
launch_plugin->button = gtk_button_new();

gtk_button_set_relief(GTK_BUTTON(launch_plugin->button), GTK_RELIEF_NONE);

/* Make button focusable for automation
tools */

gtk_widget_set_can_focus(GTK_WIDGET(launch_plugin->button), TRUE);

gtk_widget_set_focus_on_click(GTK_WIDGET(launch_plugin->button), TRUE);

/* Create icon */
launch_plugin->icon = gtk_image_new_from_icon_name(launch_plugin->icon_name, GTK_ICON_SIZE_MENU);

gtk_container_add(GTK_CONTAINER(launch_plugin->button), launch_plugin->icon);

/* Connect signal */
g_signal_connect(launch_plugin->button,
"button-press-event",

G_CALLBACK(on_button_pressed), launch_plugin);

/* Connect cleanup */
g_signal_connect(plugin, "free-data",
G_CALLBACK(launch_menu_free), NULL);

/* Set up plugin menu and connect
configuration signals */

xfce_panel_plugin_menu_show_configure(plugin);
xfce_panel_plugin_menu_show_about(plugin);
g_signal_connect(plugin,
"configure-plugin", G_CALLBACK(launch_menu_configure_plugin), launch_plugin);
g_signal_connect(plugin, "about",
G_CALLBACK(launch_menu_about), NULL);

/* Add to panel */
gtk_container_add(GTK_CONTAINER(plugin),
launch_plugin->button);

xfce_panel_plugin_add_action_widget(plugin, launch_plugin->button);

/* Store plugin data */
g_object_set_data(G_OBJECT(plugin),
"launch-plugin", launch_plugin);

gtk_widget_show_all(GTK_WIDGET(plugin));
}

/* Cleanup function */
static void launch_menu_free(XfcePanelPlugin
*plugin) {
LaunchMenuPlugin *launch_plugin = g_object_get_data(G_OBJECT(plugin), "launch-plugin");

if (launch_plugin) {
if (launch_plugin->menu) {

gtk_widget_destroy(launch_plugin->menu);
}

/* Clean up configuration */
if (launch_plugin->property_base) {

g_free(launch_plugin->property_base);
}

if (launch_plugin->channel) {
/* Channel is managed by xfconf,
don't unref it */
launch_plugin->channel = NULL;
}

/* Clean up XBEL monitor */
if (launch_plugin->xbel_monitor) {

g_signal_handlers_disconnect_by_data(launch_plugin->xbel_monitor,
launch_plugin);

g_object_unref(launch_plugin->xbel_monitor);
}

/* Clean up application monitoring */

cleanup_application_monitoring(launch_plugin);
if (launch_plugin->handle) {

g_object_unref(launch_plugin->handle);
}

g_free(launch_plugin->launch_menu_path);
g_free(launch_plugin->icon_name);
g_free(launch_plugin);
}
}

/* Plugin registration */

XFCE_PANEL_PLUGIN_REGISTER(launch_menu_construct);
