/* See LICENSE file for copyright and license details. */

#ifndef CONFIG_H
#define CONFIG_H

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <webkit2/webkit2.h>

/* Hint keys */
#define ENABLE_HINTS 1  // Set to 0 to disable hints
#define HINT_FOLLOW_KEY "f"
#define HINT_FOLLOW_NEW_WIN_KEY "F"

/* Adblocking */
#define ENABLE_ADBLOCK 1

/* General Configuration */
static const gchar *accepted_language[2] = { NULL, NULL };
static gint clients = 0, downloads = 0;
static gchar *download_dir = "/var/tmp"; /* Directory has to be static */
static gchar *fifo_suffix = "main";
static gdouble global_zoom = 1.0;
static gchar *history_file = NULL;
static gchar *home_uri = "https://html.duckduckgo.com/html/"; // about:blank
static gchar *search_text = NULL;
static gchar *search_engine = "https://duckduckgo.com/?q=%s";
static gchar *user_agent = NULL;

/* Cooperative Mode Settings */
static gboolean cooperative_alone = TRUE;
static gboolean cooperative_instances = TRUE;
static int cooperative_pipe_fp = 0;

/* UI Settings */
static GtkPositionType tab_pos = GTK_POS_TOP;
static gint tab_width_chars = 20;
static gboolean disable_smooth_scrolling = FALSE;
static gboolean disable_tab_thumbnails = TRUE;
static gboolean disable_site_icons = FALSE;
static gboolean disable_tooltips = TRUE;
static gboolean enable_resizable_text_areas = TRUE;

/* Web Content Settings */
static gboolean enable_javascript = TRUE;
static gboolean enable_images = TRUE;
static gboolean enable_webgl = TRUE;
static gboolean enable_page_cache = TRUE;
static gboolean enable_fullscreen = FALSE; // TRUE
static gboolean enable_media_stream = TRUE;
static gboolean print_backgrounds = TRUE;
static gboolean enable_mediasource = TRUE;
static gboolean enable_javascript_markup = TRUE;
static gboolean enable_html5_local_storage = TRUE;

/* Performance and Resource Usage Settings */
static gboolean enable_hardware_acceleration = TRUE;
static gboolean enable_site_specific_quirks = FALSE;
static gboolean enable_write_console_messages_to_stdout = FALSE;
static gboolean enable_media_capabilities = FALSE;
static gboolean enable_encrypted_media = FALSE;
static gboolean enable_back_forward_navigation_gestures = FALSE;
static gboolean enable_dns_prefetching = FALSE; // TRUE
static gboolean javascript_can_open_windows = TRUE;

/* Privacy and Security Settings */
static gboolean enable_hyperlink_auditing = FALSE;
static gboolean block_third_party_cookies = TRUE;
static gboolean enable_webrtc = TRUE;

/* Developer Settings */
static gboolean enable_developer_extras = FALSE; // TRUE
static gboolean enable_console_to_stdout = FALSE;

/* Font Settings */
static gchar *default_charset = "UTF-8";
static gint minimum_font_size = 0;
static gint default_font_size = 16;
static gint default_monospace_font_size = 13;
static gchar *sans_serif_font_family = "Roboto, Arial, Helvetica, sans-serif";
static gchar *serif_font_family = "Georgia, 'Times New Roman', Times, serif";
static gchar *monospace_font_family = "monospace, 'Roboto Mono', 'DejaVu Sans Mono', Consolas";

/* Spell Checking Settings */
static gboolean enable_spell_checking = FALSE; // TRUE
static const gchar * const spell_checking_languages[] = {"en_US", NULL};

struct Client;  // Forward declaration

typedef gboolean (*KeyBindingFunction)(struct Client*, const gchar*);

struct key {
    guint keyval;
    GdkModifierType mod;
    KeyBindingFunction func;
    const gchar *arg;
};

/* Function prototypes for keybindings */
gboolean close_tab(struct Client *c, const gchar *arg);
gboolean new_tab(struct Client *c, const gchar *arg);
gboolean reopen_closed_tab(struct Client *c, const gchar *arg);
gboolean reload_page(struct Client *c, const gchar *arg);
gboolean go_home(struct Client *c, const gchar *arg);
gboolean show_downloads(struct Client *c, const gchar *arg);
gboolean search_forward(struct Client *c, const gchar *arg);
gboolean search_backward(struct Client *c, const gchar *arg);
gboolean focus_location(struct Client *c, const gchar *arg);
gboolean init_search(struct Client *c, const gchar *arg);
gboolean reload_certs(struct Client *c, const gchar *arg);
gboolean prev_tab(struct Client *c, const gchar *arg);
gboolean next_tab(struct Client *c, const gchar *arg);
gboolean goto_tab(struct Client *c, const gchar *arg);
gboolean scroll_up(struct Client *c, const gchar *arg);
gboolean scroll_down(struct Client *c, const gchar *arg);
gboolean history_back(struct Client *c, const gchar *arg);
gboolean history_forward(struct Client *c, const gchar *arg);

/* Keybindings | GDK_MOD1_MASK is ALT | GDK_CONTROL_MASK is CTRL | GDK_SHIFT_MASK is SHIFT */
static struct key keys[] = {
    /* keyval,           modifier,         function,        argument */
    { GDK_KEY_w,         GDK_CONTROL_MASK, close_tab,       NULL },  // Ctrl+W (Close Tab)
    { GDK_KEY_t,         GDK_CONTROL_MASK, new_tab,         NULL },  // Ctrl+T (New Tab)
    { GDK_KEY_T, GDK_CONTROL_MASK | GDK_SHIFT_MASK, reopen_closed_tab, NULL }, // CTRL+Shift+T (Reopen Closed Tabs)
    { GDK_KEY_h,         GDK_MOD1_MASK,    go_home,         NULL },  // Alt+H (Home Page)
    { GDK_KEY_r,         GDK_CONTROL_MASK, reload_page,     NULL },  // Ctrl+R (Refresh/Reload Page)
    { GDK_KEY_j,         GDK_CONTROL_MASK, show_downloads,  NULL },  // Ctrl+J (Downloads Manager Popup)
    { GDK_KEY_f,         GDK_CONTROL_MASK, search_forward,  NULL },  // Ctrl+F (Hinting Open on a New Tab)
    { GDK_KEY_n,         GDK_MOD1_MASK,    search_forward,  NULL },  // Alt+N (IDK/Doesn't Work)
    { GDK_KEY_b,         GDK_MOD1_MASK,    search_backward, NULL },  // Alt+B (IDK/Doesn't Work)
    { GDK_KEY_l,         GDK_CONTROL_MASK, focus_location,  NULL },  // Ctrl+L (Focus on Search Bar)
    { GDK_KEY_k,         GDK_MOD1_MASK,    init_search,     NULL },  // Alt+K (Bar Search)
    { GDK_KEY_c,         GDK_MOD1_MASK,    reload_certs,    NULL },  // Alt+C (Reload Certificates)
    { GDK_KEY_Page_Up,   GDK_CONTROL_MASK, prev_tab,        NULL },  // Ctrl+PageUp (Next Tab)
    { GDK_KEY_Page_Down, GDK_CONTROL_MASK, next_tab,        NULL },  // Ctrl+PageDown (Back Tab)
    { GDK_KEY_k,         GDK_SHIFT_MASK,   scroll_up,       NULL },  // Shift+K (Vim up)
    { GDK_KEY_j,         GDK_SHIFT_MASK,   scroll_down,     NULL },  // Shift+J (Vim down)
    { GDK_KEY_h,         GDK_SHIFT_MASK,   history_back,    NULL },  // Shift+H (Vim left, back in history)
    { GDK_KEY_l,         GDK_SHIFT_MASK,   history_forward, NULL },  // Shift+L (Vim right, forward in history)
    { GDK_KEY_1,         GDK_MOD1_MASK,    goto_tab,        "0" },   // Alt+1
    { GDK_KEY_2,         GDK_MOD1_MASK,    goto_tab,        "1" },   // Alt+2
    { GDK_KEY_3,         GDK_MOD1_MASK,    goto_tab,        "2" },   // Alt+3
    { GDK_KEY_4,         GDK_MOD1_MASK,    goto_tab,        "3" },   // Alt+4
    { GDK_KEY_5,         GDK_MOD1_MASK,    goto_tab,        "4" },   // Alt+5
    { GDK_KEY_6,         GDK_MOD1_MASK,    goto_tab,        "5" },   // Alt+6
    { GDK_KEY_7,         GDK_MOD1_MASK,    goto_tab,        "6" },   // Alt+7
    { GDK_KEY_8,         GDK_MOD1_MASK,    goto_tab,        "7" },   // Alt+8
    { GDK_KEY_9,         GDK_MOD1_MASK,    goto_tab,        "8" },   // Alt+9
};

#define LENGTH(x) (sizeof(x) / sizeof(x[0]))

/* Client structure definition */
struct Client {
    gchar *external_handler_uri;
    gchar *hover_uri;
    gchar *feed_html;
    GtkWidget *location;
    GtkWidget *tabicon;
    GtkWidget *tablabel;
    GtkWidget *vbox;
    GtkWidget *web_view;
    gboolean focus_new_tab;
};

#endif // CONFIG_H
