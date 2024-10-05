// Standard C libraries
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// POSIX system headers
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// GTK and related libraries
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>

// WebKit and JavaScript libraries
#include <webkit2/webkit2.h>
#include <JavaScriptCore/JavaScript.h>

// Cleanup cache and Connection
#include <sys/resource.h>

// Local configuration
#include "config.h"

// Client Management
void client_destroy(GtkWidget *, gpointer);
WebKitWebView *client_new(const gchar *, WebKitWebView *, gboolean, gboolean);
WebKitWebView *client_new_request(WebKitWebView *, WebKitNavigationAction *, gpointer);

// UI and Window Management
void mainwindow_setup(void);
void mainwindow_title(gint);
void notebook_switch_page(GtkNotebook *, GtkWidget *, guint, gpointer);
void downloadmanager_setup(void);
gboolean downloadmanager_delete(GtkWidget *, gpointer);
void preload_resources(void);

// Event Handlers
gboolean key_common(GtkWidget *, GdkEvent *, gpointer);
gboolean key_downloadmanager(GtkWidget *, GdkEvent *, gpointer);
gboolean key_location(GtkWidget *, GdkEvent *, gpointer);
gboolean key_tablabel(GtkWidget *, GdkEvent *, gpointer);
gboolean key_web_view(GtkWidget *, GdkEvent *, gpointer);
void hover_web_view(WebKitWebView *, WebKitHitTestResult *, guint, gpointer);
void icon_location(GtkEntry *, GtkEntryIconPosition, GdkEvent *, gpointer);
gboolean cleanup_resources(gpointer user_data);

// WebKit Callbacks
void changed_download_progress(GObject *, GParamSpec *, gpointer);
void changed_load_progress(GObject *, GParamSpec *, gpointer);
void changed_favicon(GObject *, GParamSpec *, gpointer);
void changed_title(GObject *, GParamSpec *, gpointer);
void changed_uri(GObject *, GParamSpec *, gpointer);
gboolean crashed_web_view(WebKitWebView *, gpointer);
gboolean decide_policy(WebKitWebView *, WebKitPolicyDecision *, WebKitPolicyDecisionType, gpointer);
void web_view_load_changed(WebKitWebView *web_view, WebKitLoadEvent load_event, gpointer user_data);

// Download Handling
gboolean download_handle(WebKitDownload *, gchar *, gpointer);
void download_handle_start(WebKitWebView *, WebKitDownload *, gpointer);
void downloadmanager_cancel(GtkToolButton *, gpointer);

// Navigation and Tab Management
gboolean goto_tab(struct Client *c, const gchar *arg);
void search(gpointer, gint);

// Initialization and Configuration
void cooperation_setup(void);
void grab_environment_configuration(void);
void init_default_web_context(void);
void trust_user_certs(WebKitWebContext *);

// Utility Functions
gchar *ensure_uri_scheme(const gchar *);
void grab_feeds_finished(GObject *, GAsyncResult *, gpointer);
void inject_hints_script(WebKitWebView *web_view);
gboolean quit_if_nothing_active(void);
gboolean remote_msg(GIOChannel *, GIOCondition, gpointer);
void run_user_scripts(WebKitWebView *);
void show_web_view(WebKitWebView *, gpointer);
ssize_t write_full(int, char *, size_t);

// Reopen Closed Tab Stuff
#define MAX_CLOSED_TABS 10
GQueue *closed_tabs;

gboolean reopen_closed_tab(struct Client *c, const gchar *arg) {
    (void)c;
    (void)arg;
    if (!g_queue_is_empty(closed_tabs)) {
        gchar *uri = g_queue_pop_head(closed_tabs);
        if (uri) {
            client_new(uri, NULL, TRUE, TRUE);
            g_free(uri);
            return TRUE;
        }
    }
    return FALSE;
}

// Main Window Structure
struct MainWindow
{
    GtkWidget *win;      // Main window widget
    GtkWidget *notebook; // Notebook widget for tabs
} mw;

// Download Manager Structure
struct DownloadManager
{
    GtkWidget *win;     // Download manager window widget
    GtkWidget *scroll;  // Scrolled window widget
    GtkWidget *toolbar; // Toolbar widget
} dm;

void
client_destroy(GtkWidget *widget, gpointer data)
{
    struct Client *c = (struct Client *)data;
    gint idx;
    const gchar *uri;

    g_signal_handlers_disconnect_by_func(G_OBJECT(c->web_view),
                                         changed_load_progress, c);

    idx = gtk_notebook_page_num(GTK_NOTEBOOK(mw.notebook), c->vbox);
    if (idx == -1)
        fprintf(stderr, NAME": Tab index was -1, bamboozled\n");
    else {
        // Save the URI of the closed tab
        uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
        if (uri) {
            g_queue_push_head(closed_tabs, g_strdup(uri));
            if (g_queue_get_length(closed_tabs) > MAX_CLOSED_TABS) {
                g_free(g_queue_pop_tail(closed_tabs));
            }
        }
        gtk_notebook_remove_page(GTK_NOTEBOOK(mw.notebook), idx);
    }

    free(c);
    clients--;

    quit_if_nothing_active();
}

WebKitWebView *
client_new(const gchar *uri, WebKitWebView *related_wv, gboolean show,
           gboolean focus_tab)
{
    struct Client *c;
    gchar *f;
    GtkWidget *evbox, *tabbox;
    WebKitWebContext *wc;
    WebKitSettings *settings;

    if (uri != NULL && cooperative_instances && !cooperative_alone)
    {
        f = ensure_uri_scheme(uri);
        if (write_full(cooperative_pipe_fp, f, strlen(f)) <= 0 ||
            write_full(cooperative_pipe_fp, "\n", 1) <= 0)
        {
            fprintf(stderr, NAME": Could not write command '%s'\n", f);
        }
        g_free(f);
        return NULL;
    }

    c = g_slice_new0(struct Client);
    if (!c)
    {
        fprintf(stderr, NAME": fatal: memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    c->focus_new_tab = focus_tab;

    if (related_wv == NULL)
        c->web_view = GTK_WIDGET(webkit_web_view_new());
    else
        c->web_view = GTK_WIDGET(webkit_web_view_new_with_related_view(related_wv));

    if (accepted_language[0] != NULL)
    {
        wc = webkit_web_view_get_context(WEBKIT_WEB_VIEW(c->web_view));
        webkit_web_context_set_preferred_languages(wc, accepted_language);
    }

    settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(c->web_view));
    
    if (user_agent != NULL) {
        webkit_settings_set_user_agent(settings, user_agent);
    }

    // Apply WebKit settings
    webkit_settings_set_enable_javascript(settings, enable_javascript);
    webkit_settings_set_auto_load_images(settings, enable_images);
    webkit_settings_set_enable_webgl(settings, enable_webgl);
    webkit_settings_set_hardware_acceleration_policy(settings, 
        enable_hardware_acceleration ? WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS : WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

    // Apply additional WebKit settings
    webkit_settings_set_enable_page_cache(settings, enable_page_cache);
    webkit_settings_set_enable_developer_extras(settings, enable_developer_extras);
    webkit_settings_set_enable_fullscreen(settings, enable_fullscreen);
    webkit_settings_set_enable_dns_prefetching(settings, enable_dns_prefetching);
    webkit_settings_set_enable_hyperlink_auditing(settings, enable_hyperlink_auditing);
    webkit_settings_set_media_playback_requires_user_gesture(settings, !enable_media_stream);
    webkit_settings_set_print_backgrounds(settings, print_backgrounds);
 
    // Font Settings   
    webkit_settings_set_default_charset(settings, default_charset);
    webkit_settings_set_default_font_family(settings, sans_serif_font_family);
    webkit_settings_set_serif_font_family(settings, serif_font_family);
    webkit_settings_set_monospace_font_family(settings, monospace_font_family);
    webkit_settings_set_minimum_font_size(settings, minimum_font_size);
    webkit_settings_set_default_font_size(settings, default_font_size);
    webkit_settings_set_default_monospace_font_size(settings, default_monospace_font_size);

    webkit_settings_set_javascript_can_open_windows_automatically(settings, javascript_can_open_windows);
    webkit_settings_set_enable_webrtc(settings, enable_webrtc);
    webkit_settings_set_enable_mediasource(settings, enable_mediasource);
    webkit_settings_set_enable_javascript_markup(settings, enable_javascript_markup);
    webkit_settings_set_enable_resizable_text_areas(settings, enable_resizable_text_areas);
    webkit_settings_set_enable_html5_local_storage(settings, enable_html5_local_storage);

    // Apply new performance and resource usage settings
    webkit_settings_set_enable_site_specific_quirks(settings, enable_site_specific_quirks);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, enable_write_console_messages_to_stdout);
    webkit_settings_set_enable_media_capabilities(settings, enable_media_capabilities);
    webkit_settings_set_enable_encrypted_media(settings, enable_encrypted_media);

    // Set spell checking languages on the WebKit context
    WebKitWebContext *context = webkit_web_view_get_context(WEBKIT_WEB_VIEW(c->web_view));
    webkit_web_context_set_spell_checking_languages(context, spell_checking_languages);
    webkit_web_context_set_spell_checking_enabled(context, enable_spell_checking);
    
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(c->web_view), global_zoom);
    g_signal_connect_after(G_OBJECT(c->web_view), "notify::favicon",
                     G_CALLBACK(changed_favicon), c);
    g_signal_connect_after(G_OBJECT(c->web_view), "notify::title",
                     G_CALLBACK(changed_title), c);
    g_signal_connect(G_OBJECT(c->web_view), "notify::uri",
                     G_CALLBACK(changed_uri), c);
    g_signal_connect(G_OBJECT(c->web_view), "notify::estimated-load-progress",
                     G_CALLBACK(changed_load_progress), c);
    g_signal_connect(G_OBJECT(c->web_view), "create",
                     G_CALLBACK(client_new_request), NULL);
    g_signal_connect(G_OBJECT(c->web_view), "close",
                     G_CALLBACK(client_destroy), c);
    g_signal_connect(G_OBJECT(c->web_view), "decide-policy",
                     G_CALLBACK(decide_policy), NULL);
    g_signal_connect(G_OBJECT(c->web_view), "key-press-event",
                     G_CALLBACK(key_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "button-release-event",
                     G_CALLBACK(key_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "scroll-event",
                     G_CALLBACK(key_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "mouse-target-changed",
                     G_CALLBACK(hover_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "web-process-crashed",
                     G_CALLBACK(crashed_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "load-changed",
                     G_CALLBACK(web_view_load_changed), c);

    webkit_settings_set_enable_smooth_scrolling(settings, !disable_smooth_scrolling);

    c->location = gtk_entry_new();
    g_signal_connect(G_OBJECT(c->location), "key-press-event",
                     G_CALLBACK(key_location), c);
    g_signal_connect(G_OBJECT(c->location), "icon-release",
                     G_CALLBACK(icon_location), c);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(c->location),
                                      GTK_ENTRY_ICON_PRIMARY,
                                      NULL);

    c->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(c->vbox), c->location, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(c->vbox), c->web_view, TRUE, TRUE, 0);
    gtk_container_set_focus_child(GTK_CONTAINER(c->vbox), c->web_view);

    c->tabicon = gtk_image_new_from_icon_name("text-html", GTK_ICON_SIZE_SMALL_TOOLBAR);

    c->tablabel = gtk_label_new(NAME);
    gtk_label_set_ellipsize(GTK_LABEL(c->tablabel), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(c->tablabel), tab_width_chars);
    gtk_widget_set_has_tooltip(c->tablabel, !disable_tooltips);

    tabbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,
                         5 * gtk_widget_get_scale_factor(mw.win));
    gtk_box_pack_start(GTK_BOX(tabbox), c->tabicon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tabbox), c->tablabel, TRUE, TRUE, 0);

    evbox = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(evbox), tabbox);
    g_signal_connect(G_OBJECT(evbox), "button-release-event",
                     G_CALLBACK(key_tablabel), c);

    gtk_widget_add_events(evbox, GDK_SCROLL_MASK);
    g_signal_connect(G_OBJECT(evbox), "scroll-event",
                     G_CALLBACK(key_tablabel), c);

    g_object_set_data(G_OBJECT(evbox), "lariza-tab-label", c->tablabel);

    gtk_widget_show_all(evbox);

    gtk_notebook_insert_page(GTK_NOTEBOOK(mw.notebook), c->vbox, evbox,
                             gtk_notebook_get_current_page(GTK_NOTEBOOK(mw.notebook)) + 1);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(mw.notebook), c->vbox, TRUE);

    if (disable_tab_thumbnails) {
        webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(c->web_view), &(GdkRGBA){0, 0, 0, 0});
    }

    if (show)
        show_web_view(NULL, c);
    else
        g_signal_connect(G_OBJECT(c->web_view), "ready-to-show",
                         G_CALLBACK(show_web_view), c);

    if (uri != NULL)
    {
        f = ensure_uri_scheme(uri);
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), f);
        g_free(f);
    }

    clients++;

    return WEBKIT_WEB_VIEW(c->web_view);
}

WebKitWebView *
client_new_request(WebKitWebView *web_view,
                   WebKitNavigationAction *navigation_action, gpointer data)
{
    return client_new(NULL, web_view, FALSE, FALSE);
}

void
web_view_load_changed(WebKitWebView *web_view, WebKitLoadEvent load_event, gpointer user_data)
{
    if (load_event == WEBKIT_LOAD_FINISHED) {
        fprintf(stderr, "Page load finished, injecting hints script\n");
        inject_hints_script(web_view);
    }
}

void inject_hints_script(WebKitWebView *web_view)
{
    if (ENABLE_HINTS) {
        fprintf(stderr, "Injecting hints script\n");
        const char *hints_script = 
        "// Anonymous function to get private namespace.\n"
        "(function() {\n"
        "    console.log('[hints] Script injected and executing');\n"
        "\n"
        "    var charset = \"sdfghjklertzuivbn\".split(\"\");\n"
        "    var key_follow = \"f\";\n"
        "    var key_follow_new_win = \"F\";\n"
        "\n"
        "    function update_highlights_or_abort()\n"
        "    {\n"
        "        console.log('[hints] update_highlights_or_abort called');\n"
        "        var submatch;\n"
        "        var col_sel, col_unsel;\n"
        "        var longest_id = 0;\n"
        "\n"
        "        if (document.lariza_hints.state === \"follow_new\")\n"
        "        {\n"
        "            col_unsel = \"#DAFFAD\";\n"
        "            col_sel = \"#FF5D00\";\n"
        "        }\n"
        "        else\n"
        "        {\n"
        "            col_unsel = \"#A7FFF5\";\n"
        "            col_sel = \"#33FF00\";\n"
        "        }\n"
        "\n"
        "        for (var id in document.lariza_hints.labels)\n"
        "        {\n"
        "            var label = document.lariza_hints.labels[id];\n"
        "            var bgcol = col_unsel;\n"
        "\n"
        "            longest_id = Math.max(longest_id, id.length);\n"
        "\n"
        "            if (document.lariza_hints.box.value !== \"\")\n"
        "            {\n"
        "                submatch = id.match(\"^\" + document.lariza_hints.box.value);\n"
        "                if (submatch !== null)\n"
        "                {\n"
        "                    var href_suffix = \"\";\n"
        "                    var box_shadow_inner = \"#B00000\";\n"
        "                    if (id === document.lariza_hints.box.value)\n"
        "                    {\n"
        "                        bgcol = col_sel;\n"
        "                        box_shadow_inner = \"red\";\n"
        "                        if (label.elem.tagName.toLowerCase() === \"a\")\n"
        "                            href_suffix = \": <span style='font-size: 75%'>\" +\n"
        "                                          label.elem.href + \"</span>\";\n"
        "                    }\n"
        "\n"
        "                    var len = submatch[0].length;\n"
        "                    label.span.innerHTML = \"<b>\" + submatch[0] + \"</b>\" +\n"
        "                                           id.substring(len, id.length) +\n"
        "                                           href_suffix;\n"
        "                    label.span.style.visibility = \"visible\";\n"
        "\n"
        "                    save_parent_style(label);\n"
        "                    label.elem.style.boxShadow = \"0 0 5pt 2pt black, 0 0 0 2pt \" +\n"
        "                                                 box_shadow_inner + \" inset\";\n"
        "                }\n"
        "                else\n"
        "                {\n"
        "                    label.span.style.visibility = \"hidden\";\n"
        "                    reset_parent_style(label);\n"
        "                }\n"
        "            }\n"
        "            else\n"
        "            {\n"
        "                label.span.style.visibility = \"visible\";\n"
        "                label.span.innerHTML = id;\n"
        "                reset_parent_style(label);\n"
        "            }\n"
        "            label.span.style.backgroundColor = bgcol;\n"
        "        }\n"
        "\n"
        "        if (document.lariza_hints.box.value.length > longest_id)\n"
        "            set_state(\"inactive\");\n"
        "    }\n"
        "\n"
        "    function open_match()\n"
        "    {\n"
        "        console.log('[hints] open_match called');\n"
        "        var choice = document.lariza_hints.box.value;\n"
        "        var was_state = document.lariza_hints.state;\n"
        "\n"
        "        var elem = document.lariza_hints.labels[choice].elem;\n"
        "        set_state(\"inactive\");  /* Nukes labels. */\n"
        "\n"
        "        if (elem)\n"
        "        {\n"
        "            var tag_name = elem.tagName.toLowerCase();\n"
        "            var type = elem.type ? elem.type.toLowerCase() : \"\";\n"
        "\n"
        "            console.log(\"[hints] Selected elem [\" + elem + \"] [\" + tag_name +\n"
        "                        \"] [\" + type + \"]\");\n"
        "\n"
        "            if (was_state === \"follow_new\" && tag_name === \"a\")\n"
        "                window.open(elem.href);\n"
        "            else if (\n"
        "                (\n"
        "                    tag_name === \"input\" &&\n"
        "                    type !== \"button\" &&\n"
        "                    type !== \"color\" &&\n"
        "                    type !== \"checkbox\" &&\n"
        "                    type !== \"file\" &&\n"
        "                    type !== \"radio\" &&\n"
        "                    type !== \"reset\" &&\n"
        "                    type !== \"submit\"\n"
        "                ) ||\n"
        "                tag_name === \"textarea\" ||\n"
        "                tag_name === \"select\"\n"
        "            )\n"
        "                elem.focus();\n"
        "            else\n"
        "                elem.click();\n"
        "        }\n"
        "    }\n"
        "\n"
        "    function set_state(new_state)\n"
        "    {\n"
        "        console.log('[hints] set_state called with state: ' + new_state);\n"
        "        document.lariza_hints.state = new_state;\n"
        "\n"
        "        if (document.lariza_hints.state === \"inactive\")\n"
        "        {\n"
        "            nuke_labels();\n"
        "\n"
        "            // Removing our box causes unwanted scrolling. Just hide it.\n"
        "            document.lariza_hints.box.blur();\n"
        "            document.lariza_hints.box.value = \"\";\n"
        "            document.lariza_hints.box.style.visibility = \"hidden\";\n"
        "        }\n"
        "        else\n"
        "        {\n"
        "            if (document.lariza_hints.labels === null)\n"
        "                create_labels();\n"
        "\n"
        "            var box = document.lariza_hints.box;\n"
        "            if (box === null)\n"
        "            {\n"
        "                document.lariza_hints.box = document.createElement(\"input\");\n"
        "                box = document.lariza_hints.box;\n"
        "\n"
        "                box.addEventListener(\"keydown\", on_box_key);\n"
        "                box.addEventListener(\"input\", on_box_input);\n"
        "                box.style.opacity = \"0\";\n"
        "                box.style.position = \"fixed\";\n"
        "                box.style.left = \"0px\";\n"
        "                box.style.top = \"0px\";\n"
        "                box.type = \"text\";\n"
        "\n"
        "                box.setAttribute(\"lariza_input_box\", \"yes\");\n"
        "\n"
        "                document.body.appendChild(box);\n"
        "            }\n"
        "\n"
        "            box.style.visibility = \"visible\";\n"
        "            box.focus();\n"
        "        }\n"
        "\n"
        "        update_highlights_or_abort();\n"
        "    }\n"
        "\n"
        "    function create_labels()\n"
        "    {\n"
        "        console.log('[hints] create_labels called');\n"
        "        document.lariza_hints.labels = new Object();\n"
        "\n"
        "        var selector = \"a[href]:not([href=''])\";\n"
        "        if (document.lariza_hints.state !== \"follow_new\")\n"
        "        {\n"
        "            selector += \", input:not([type=hidden]):not([lariza_input_box=yes])\";\n"
        "            selector += \", textarea, select, button\";\n"
        "        }\n"
        "\n"
        "        var elements = document.body.querySelectorAll(selector);\n"
        "\n"
        "        for (var i = 0; i < elements.length; i++)\n"
        "        {\n"
        "            var elem = elements[i];\n"
        "\n"
        "            var label_id = \"\";\n"
        "            var n = i;\n"
        "            do\n"
        "            {\n"
        "                label_id += charset[n % charset.length];\n"
        "                n = Math.floor(n / charset.length);\n"
        "            } while (n !== 0);\n"
        "\n"
        "            var span = document.createElement(\"span\");\n"
        "            span.style.border = \"black 1pt solid\";\n"
        "            span.style.color = \"black\";\n"
        "            span.style.fontFamily = \"monospace\";\n"
        "            span.style.fontSize = \"10pt\";\n"
        "            span.style.fontWeight = \"normal\";\n"
        "            span.style.margin = \"0px 2pt\";\n"
        "            span.style.position = \"absolute\";\n"
        "            span.style.textTransform = \"lowercase\";\n"
        "            span.style.visibility = \"hidden\";\n"
        "            span.style.zIndex = \"2147483647\";\n"
        "\n"
        "            document.lariza_hints.labels[label_id] = {\n"
        "                \"elem\": elem,\n"
        "                \"span\": span,\n"
        "                \"parent_style\": null,\n"
        "            };\n"
        "\n"
        "            var tag_name = elem.tagName.toLowerCase();\n"
        "            if (tag_name === \"a\")\n"
        "            {\n"
        "                span.style.borderTopLeftRadius = \"10pt\";\n"
        "                span.style.borderBottomLeftRadius = \"10pt\";\n"
        "                span.style.padding = \"0px 2pt 0px 5pt\";\n"
        "                elem.appendChild(span);\n"
        "            }\n"
        "            else\n"
        "            {\n"
        "                span.style.borderRadius = \"10pt\";\n"
        "                span.style.padding = \"0px 5pt\";\n"
        "                elem.parentNode.insertBefore(span, elem);\n"
        "            }\n"
        "\n"
        "            console.log(\"[hints] Label ID \" + label_id + \", \" + i +\n"
        "                        \" for elem [\" + elem + \"]\");\n"
        "        }\n"
        "    }\n"
        "\n"
        "    function nuke_labels()\n"
        "    {\n"
        "        console.log('[hints] nuke_labels called');\n"
        "        for (var id in document.lariza_hints.labels)\n"
        "        {\n"
        "            var label = document.lariza_hints.labels[id];\n"
        "\n"
        "            reset_parent_style(label);\n"
        "\n"
        "            var tag_name = label.elem.tagName.toLowerCase();\n"
        "            if (tag_name === \"a\")\n"
        "                label.elem.removeChild(label.span);\n"
        "            else\n"
        "                label.elem.parentNode.removeChild(label.span);\n"
        "        }\n"
        "\n"
        "        document.lariza_hints.labels = null;\n"
        "    }\n"
        "\n"
        "    function reset_parent_style(label)\n"
        "    {\n"
        "        if (label.parent_style !== null)\n"
        "            label.elem.style.boxShadow = label.parent_style.boxShadow;\n"
        "    }\n"
        "\n"
        "    function save_parent_style(label)\n"
        "    {\n"
        "        if (label.parent_style === null)\n"
        "        {\n"
        "            var style = window.getComputedStyle(label.elem);\n"
        "            label.parent_style = new Object();\n"
        "            label.parent_style.boxShadow = style.getPropertyValue(\"boxShadow\");\n"
        "        }\n"
        "    }\n"
        "\n"
        "    function on_box_input(e)\n"
        "    {\n"
        "        update_highlights_or_abort();\n"
        "    }\n"
        "\n"
        "    function on_box_key(e)\n"
        "    {\n"
        "        if (e.key === \"Escape\")\n"
        "        {\n"
        "            e.preventDefault();\n"
        "            e.stopPropagation();\n"
        "            set_state(\"inactive\");\n"
        "        }\n"
        "        else if (e.key === \"Enter\")\n"
        "        {\n"
        "            e.preventDefault();\n"
        "            e.stopPropagation();\n"
        "            open_match();\n"
        "        }\n"
        "    }\n"
        "\n"
        "    function on_window_key(e)\n"
        "    {\n"
        "        console.log('[hints] on_window_key called with key: ' + e.key);\n"
        "        if (e.target.nodeName.toLowerCase() === \"textarea\" ||\n"
        "            e.target.nodeName.toLowerCase() === \"input\" ||\n"
        "            document.designMode === \"on\" ||\n"
        "            e.target.contentEditable === \"true\")\n"
        "        {\n"
        "            return;\n"
        "        }\n"
        "\n"
        "        if (document.lariza_hints.state === \"inactive\")\n"
        "        {\n"
        "            if (e.key === key_follow)\n"
        "                set_state(\"follow\");\n"
        "            else if (e.key === key_follow_new_win)\n"
        "                set_state(\"follow_new\");\n"
        "        }\n"
        "    }\n"
        "\n"
        "    if (document.lariza_hints === undefined)\n"
        "    {\n"
        "        document.lariza_hints = new Object();\n"
        "        document.lariza_hints.box = null;\n"
        "        document.lariza_hints.labels = null;\n"
        "        document.lariza_hints.state = \"inactive\";\n"
        "\n"
        "        document.addEventListener(\"keyup\", on_window_key);\n"
        "\n"
        "        console.log(\"[hints] Initialized.\");\n"
        "    }\n"
        "    else\n"
        "        console.log(\"[hints] ALREADY INSTALLED\");\n"
        "\n"
        "})();\n"
        "\n";

        webkit_web_view_evaluate_javascript(web_view, hints_script, -1, NULL, NULL, NULL, NULL, NULL);
    } else {
        fprintf(stderr, "Hints are disabled\n");
    }
}

void
cooperation_setup(void)
{
    GIOChannel *towatch;
    gchar *fifofilename, *fifopath;

    fifofilename = g_strdup_printf("%s-%s", NAME".fifo", fifo_suffix);
    fifopath = g_build_filename(g_get_user_runtime_dir(), fifofilename, NULL);
    g_free(fifofilename);

    if (!g_file_test(fifopath, G_FILE_TEST_EXISTS))
        mkfifo(fifopath, 0600);

    cooperative_pipe_fp = open(fifopath, O_WRONLY | O_NONBLOCK);
    if (!cooperative_pipe_fp)
    {
        fprintf(stderr, NAME": Can't open FIFO at all.\n");
    }
    else
    {
        if (write(cooperative_pipe_fp, "", 0) == -1)
        {
            /* Could not do an empty write to the FIFO which means there's
             * no one listening. */
            close(cooperative_pipe_fp);
            towatch = g_io_channel_new_file(fifopath, "r+", NULL);
            g_io_add_watch(towatch, G_IO_IN, (GIOFunc)remote_msg, NULL);
        }
        else
            cooperative_alone = FALSE;
    }

    g_free(fifopath);
}

void
changed_download_progress(GObject *obj, GParamSpec *pspec, gpointer data)
{
    WebKitDownload *download = WEBKIT_DOWNLOAD(obj);
    WebKitURIResponse *resp;
    GtkToolItem *tb = GTK_TOOL_ITEM(data);
    gdouble p, size_mb;
    const gchar *uri;
    gchar *t, *filename, *base;
    guint64 content_length;

    p = webkit_download_get_estimated_progress(download);
    p = p > 1 ? 1 : p;
    p = p < 0 ? 0 : p;
    p *= 100;
    resp = webkit_download_get_response(download);
    content_length = webkit_uri_response_get_content_length(resp);
    size_mb = content_length / 1e6;

    uri = webkit_download_get_destination(download);
    filename = g_filename_from_uri(uri, NULL, NULL);
    if (filename == NULL)
    {
        fprintf(stderr, NAME": Could not construct file name from URI!\n");
        t = g_strdup_printf("%s (%.0f%% of %.1f MB)",
                            webkit_uri_response_get_uri(resp), p, size_mb);
    }
    else
    {
        base = g_path_get_basename(filename);
        t = g_strdup_printf("%s (%.0f%% of %.1f MB)", base, p, size_mb);
        g_free(filename);
        g_free(base);
    }
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(tb), t);
    g_free(t);

    // If the download is complete, remove it from the active downloads
    if (p >= 100) {
        downloads--;
        gtk_widget_destroy(GTK_WIDGET(tb));
    }
}

void changed_load_progress(GObject *obj, GParamSpec *pspec, gpointer data)
{
    struct Client *c = (struct Client *)data;
    gdouble p;
    gchar *grab_feeds =
        "a = document.querySelectorAll('"
        "    html > head > link[rel=\"alternate\"][href][type=\"application/atom+xml\"],"
        "    html > head > link[rel=\"alternate\"][href][type=\"application/rss+xml\"]"
        "');"
        "if (a.length == 0)"
        "    null;"
        "else"
        "{"
        "    out = '';"
        "    for (i = 0; i < a.length; i++)"
        "    {"
        "        url = encodeURIComponent(a[i].href);"
        "        if ('title' in a[i] && a[i].title != '')"
        "            title = encodeURIComponent(a[i].title);"
        "        else"
        "            title = url;"
        "        out += '<li><a href=\"' + url + '\">' + title + '</a></li>';"
        "    }"
        "    out;"
        "}";

    p = webkit_web_view_get_estimated_load_progress(WEBKIT_WEB_VIEW(c->web_view));
    if (p == 1)
    {
        p = 0;

        /* The page has loaded fully. We now run the short JavaScript
         * snippet above that operates on the DOM. It tries to grab all
         * occurences of <link rel="alternate" ...>, i.e. RSS/Atom feed
         * references. */
        webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(c->web_view),
                                            grab_feeds, -1, NULL, NULL,
                                            NULL, grab_feeds_finished, c);

        run_user_scripts(WEBKIT_WEB_VIEW(c->web_view));
    }
    gtk_entry_set_progress_fraction(GTK_ENTRY(c->location), p);
}

void
changed_favicon(GObject *obj, GParamSpec *pspec, gpointer data)
{
    struct Client *c = (struct Client *)data;
    cairo_surface_t *f;
    int w, h, w_should, h_should;
    GdkPixbuf *pb, *pb_scaled;

    f = webkit_web_view_get_favicon(WEBKIT_WEB_VIEW(c->web_view));
    if (f == NULL)
    {
        gtk_image_set_from_icon_name(GTK_IMAGE(c->tabicon), "text-html",
                                     GTK_ICON_SIZE_SMALL_TOOLBAR);
    }
    else
    {
        w = cairo_image_surface_get_width(f);
        h = cairo_image_surface_get_height(f);
        pb = gdk_pixbuf_get_from_surface(f, 0, 0, w, h);
        if (pb != NULL)
        {
            w_should = 16 * gtk_widget_get_scale_factor(c->tabicon);
            h_should = 16 * gtk_widget_get_scale_factor(c->tabicon);
            pb_scaled = gdk_pixbuf_scale_simple(pb, w_should, h_should,
                                                GDK_INTERP_BILINEAR);
            gtk_image_set_from_pixbuf(GTK_IMAGE(c->tabicon), pb_scaled);

            g_object_unref(pb_scaled);
            g_object_unref(pb);
        }
    }
}

void
changed_title(GObject *obj, GParamSpec *pspec, gpointer data)
{
    const gchar *t, *u;
    struct Client *c = (struct Client *)data;

    u = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
    t = webkit_web_view_get_title(WEBKIT_WEB_VIEW(c->web_view));

    u = u == NULL ? NAME : u;
    u = u[0] == 0 ? NAME : u;

    t = t == NULL ? u : t;
    t = t[0] == 0 ? u : t;

    gtk_label_set_text(GTK_LABEL(c->tablabel), t);
    gtk_widget_set_tooltip_text(c->tablabel, t);
    mainwindow_title(gtk_notebook_get_current_page(GTK_NOTEBOOK(mw.notebook)));
}

void
changed_uri(GObject *obj, GParamSpec *pspec, gpointer data)
{
    const gchar *t;
    struct Client *c = (struct Client *)data;
    FILE *fp;

    t = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));

    /* When a web process crashes, we get a "notify::uri" signal, but we
     * can no longer read a meaningful URI. It's just an empty string
     * now. Not updating the location bar in this scenario is important,
     * because we would override the "WEB PROCESS CRASHED" message. */
    if (t != NULL && strlen(t) > 0)
    {
        gtk_entry_set_text(GTK_ENTRY(c->location), t);

        if (history_file != NULL)
        {
            fp = fopen(history_file, "a");
            if (fp != NULL)
            {
                fprintf(fp, "%s\n", t);
                fclose(fp);
            }
            else
                perror(NAME": Error opening history file");
        }
    }
}

gboolean
crashed_web_view(WebKitWebView *web_view, gpointer data)
{
    gchar *t;
    struct Client *c = (struct Client *)data;

    t = g_strdup_printf("WEB PROCESS CRASHED: %s",
                        webkit_web_view_get_uri(WEBKIT_WEB_VIEW(web_view)));
    gtk_entry_set_text(GTK_ENTRY(c->location), t);
    g_free(t);

    return TRUE;
}

gboolean
decide_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision,
              WebKitPolicyDecisionType type, gpointer data)
{
    WebKitResponsePolicyDecision *r;

    switch (type)
    {
        case WEBKIT_POLICY_DECISION_TYPE_RESPONSE:
            r = WEBKIT_RESPONSE_POLICY_DECISION(decision);
            if (!webkit_response_policy_decision_is_mime_type_supported(r))
                webkit_policy_decision_download(decision);
            else
                webkit_policy_decision_use(decision);
            break;
        default:
            /* Use whatever default there is. */
            return FALSE;
    }
    return TRUE;
}

void
download_handle_finished(WebKitDownload *download, gpointer data)
{
    downloads--;
    if (downloads == 0 && gtk_widget_get_visible(dm.win)) {
        gtk_widget_hide(dm.win);
    }
}

void
download_handle_start(WebKitWebView *web_view, WebKitDownload *download,
                      gpointer data)
{
    // Check if the signal handler is already connected
    if (!g_signal_handler_find(G_OBJECT(download), G_SIGNAL_MATCH_FUNC, 0, 0, NULL, G_CALLBACK(download_handle), NULL)) {
        g_signal_connect(G_OBJECT(download), "decide-destination",
                         G_CALLBACK(download_handle), data);
    }
}

gboolean
download_handle(WebKitDownload *download, gchar *suggested_filename, gpointer data)
{
    gchar *sug_clean, *path, *path2 = NULL, *uri;
    GtkToolItem *tb;
    int suffix = 1;
    size_t i;
    guint64 content_length;
    const gchar *mime_type;

    // Get the content length of the download
    content_length = webkit_uri_response_get_content_length(
        webkit_download_get_response(download));

    // If the content length is 0, cancel the download
    if (content_length == 0) {
        fprintf(stderr, NAME": Download cancelled due to zero content length\n");
        webkit_download_cancel(download);
        return FALSE;
    }

    // Get the MIME type of the download
    mime_type = webkit_uri_response_get_mime_type(webkit_download_get_response(download));

    // Check if it's an image
    gboolean is_image = g_str_has_prefix(mime_type, "image/");

    sug_clean = g_strdup(suggested_filename);
    for (i = 0; i < strlen(sug_clean); i++)
        if (sug_clean[i] == G_DIR_SEPARATOR)
            sug_clean[i] = '_';

    path = g_build_filename(download_dir, sug_clean, NULL);
    path2 = g_strdup(path);
    while (g_file_test(path2, G_FILE_TEST_EXISTS) && suffix < 1000)
    {
        g_free(path2);

        path2 = g_strdup_printf("%s.%d", path, suffix);
        suffix++;
    }

    if (suffix == 1000)
    {
        fprintf(stderr, NAME": Suffix reached limit for download.\n");
        webkit_download_cancel(download);
    }
    else
    {
        uri = g_filename_to_uri(path2, NULL, NULL);
        webkit_download_set_destination(download, uri);
        g_free(uri);

        tb = gtk_tool_button_new(NULL, NULL);
        gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(tb), "gtk-delete");
        gtk_tool_button_set_label(GTK_TOOL_BUTTON(tb), sug_clean);
        gtk_toolbar_insert(GTK_TOOLBAR(dm.toolbar), tb, 0);

        if (!is_image) {
            gtk_widget_show_all(dm.win);
        }

        g_signal_connect(G_OBJECT(download), "notify::estimated-progress",
                         G_CALLBACK(changed_download_progress), tb);

        downloads++;
        g_signal_connect(G_OBJECT(download), "finished",
                         G_CALLBACK(download_handle_finished), NULL);

        g_object_ref(download);
        g_signal_connect(G_OBJECT(tb), "clicked",
                         G_CALLBACK(downloadmanager_cancel), download);
    }

    g_free(sug_clean);
    g_free(path);
    g_free(path2);

    // Propagate -- to whom it may concern.
    return FALSE;
}

void
downloadmanager_cancel(GtkToolButton *tb, gpointer data)
{
    WebKitDownload *download = WEBKIT_DOWNLOAD(data);

    webkit_download_cancel(download);
    g_object_unref(download);

    gtk_widget_destroy(GTK_WIDGET(tb));
}

gboolean
downloadmanager_delete(GtkWidget *obj, gpointer data)
{
    if (!quit_if_nothing_active())
        gtk_widget_hide(dm.win);

    return TRUE;
}

void
downloadmanager_setup(void)
{
    dm.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_type_hint(GTK_WINDOW(dm.win), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_default_size(GTK_WINDOW(dm.win), 500, 250);
    gtk_window_set_title(GTK_WINDOW(dm.win), NAME" - Download Manager");
    g_signal_connect(G_OBJECT(dm.win), "delete-event",
                     G_CALLBACK(downloadmanager_delete), NULL);
    g_signal_connect(G_OBJECT(dm.win), "key-press-event",
                     G_CALLBACK(key_downloadmanager), NULL);

    dm.toolbar = gtk_toolbar_new();
    gtk_orientable_set_orientation(GTK_ORIENTABLE(dm.toolbar),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_toolbar_set_style(GTK_TOOLBAR(dm.toolbar), GTK_TOOLBAR_BOTH_HORIZ);
    gtk_toolbar_set_show_arrow(GTK_TOOLBAR(dm.toolbar), FALSE);

    dm.scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(dm.scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(dm.scroll), dm.toolbar);

    gtk_container_add(GTK_CONTAINER(dm.win), dm.scroll);
}

gchar *
ensure_uri_scheme(const gchar *t)
{
    static GRegex *url_regex = NULL;
    gchar *f, *fabs;

    if (!url_regex) {
        url_regex = g_regex_new("^[a-zA-Z0-9-]+\\.[a-zA-Z]{2,}(\\S*)?$", 0, 0, NULL);
    }

    if (g_regex_match(url_regex, t, 0, NULL)) {
        return g_strdup_printf("http://%s", t);
    }

    f = g_ascii_strdown(t, -1);
    if (!g_str_has_prefix(f, "http:") &&
        !g_str_has_prefix(f, "https:") &&
        !g_str_has_prefix(f, "file:") &&
        !g_str_has_prefix(f, "about:") &&
        !g_str_has_prefix(f, "data:") &&
        !g_str_has_prefix(f, "webkit:"))
    {
        g_free(f);
        fabs = realpath(t, NULL);
        if (fabs != NULL) {
            f = g_strdup_printf("file://%s", fabs);
            free(fabs);
        } else {
            return NULL;
        }
        return f;
    } else {
        g_free(f);
        return g_strdup(t);
    }
}

void
grab_environment_configuration(void)
{
    const gchar *e;

    e = g_getenv(NAME_UPPERCASE"_ACCEPTED_LANGUAGE");
    if (e != NULL)
        accepted_language[0] = g_strdup(e);

    e = g_getenv(NAME_UPPERCASE"_DISABLE_SMOOTH_SCROLLING");
    if (e != NULL)
        disable_smooth_scrolling = (g_ascii_strcasecmp(e, "true") == 0 || g_ascii_strcasecmp(e, "1") == 0);

    e = g_getenv(NAME_UPPERCASE"_DOWNLOAD_DIR");
    if (e != NULL)
        download_dir = g_strdup(e);

    e = g_getenv(NAME_UPPERCASE"_ENABLE_CONSOLE_TO_STDOUT");
    if (e != NULL)
        enable_console_to_stdout = (g_ascii_strcasecmp(e, "true") == 0 || g_ascii_strcasecmp(e, "1") == 0);

    e = g_getenv(NAME_UPPERCASE"_FIFO_SUFFIX");
    if (e != NULL)
        fifo_suffix = g_strdup(e);

    e = g_getenv(NAME_UPPERCASE"_HISTORY_FILE");
    if (e != NULL)
        history_file = g_strdup(e);

    e = g_getenv(NAME_UPPERCASE"_HOME_URI");
    if (e != NULL)
        home_uri = g_strdup(e);

    e = g_getenv(NAME_UPPERCASE"_TAB_POS");
    if (e != NULL)
    {
        if (strcmp(e, "top") == 0)
            tab_pos = GTK_POS_TOP;
        if (strcmp(e, "right") == 0)
            tab_pos = GTK_POS_RIGHT;
        if (strcmp(e, "bottom") == 0)
            tab_pos = GTK_POS_BOTTOM;
        if (strcmp(e, "left") == 0)
            tab_pos = GTK_POS_LEFT;
    }

    e = g_getenv(NAME_UPPERCASE"_TAB_WIDTH_CHARS");
    if (e != NULL)
        tab_width_chars = atoi(e);

    e = g_getenv(NAME_UPPERCASE"_USER_AGENT");
    if (e != NULL)
        user_agent = g_strdup(e);

    e = g_getenv(NAME_UPPERCASE"_ZOOM");
    if (e != NULL)
        global_zoom = atof(e);

    e = g_getenv(NAME_UPPERCASE"_ENABLE_JAVASCRIPT");
    if (e != NULL)
        enable_javascript = (g_ascii_strcasecmp(e, "true") == 0 || g_ascii_strcasecmp(e, "1") == 0);

    e = g_getenv(NAME_UPPERCASE"_ENABLE_IMAGES");
    if (e != NULL)
        enable_images = (g_ascii_strcasecmp(e, "true") == 0 || g_ascii_strcasecmp(e, "1") == 0);

    e = g_getenv(NAME_UPPERCASE"_ENABLE_HARDWARE_ACCELERATION");
    if (e != NULL)
        enable_hardware_acceleration = (g_ascii_strcasecmp(e, "true") == 0 || g_ascii_strcasecmp(e, "1") == 0);

    e = g_getenv(NAME_UPPERCASE"_ENABLE_WEBGL");
    if (e != NULL)
        enable_webgl = (g_ascii_strcasecmp(e, "true") == 0 || g_ascii_strcasecmp(e, "1") == 0);
}

void grab_feeds_finished(GObject *object, GAsyncResult *result, gpointer data)
{
    struct Client *c = (struct Client *)data;
    GError *err = NULL;
    JSCValue *js_value;
    gchar *str_value;

    g_free(c->feed_html);
    c->feed_html = NULL;

    js_value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(object),
                                                          result, &err);
    if (!js_value)
    {
        fprintf(stderr, NAME": Error running javascript: %s\n", err->message);
        g_error_free(err);
        return;
    }

    if (jsc_value_is_string(js_value))
    {
        str_value = jsc_value_to_string(js_value);
        JSCException *exception = jsc_context_get_exception(jsc_value_get_context(js_value));
        if (exception != NULL)
        {
            fprintf(stderr, NAME": Error running javascript: %s\n",
                    jsc_exception_get_message(exception));
            g_free(str_value);
        }
        else
        {
            c->feed_html = str_value;
            gtk_entry_set_icon_from_icon_name(GTK_ENTRY(c->location),
                                              GTK_ENTRY_ICON_PRIMARY,
                                              "application-rss+xml-symbolic");
            gtk_entry_set_icon_activatable(GTK_ENTRY(c->location),
                                           GTK_ENTRY_ICON_PRIMARY,
                                           TRUE);
        }
    }
    else
    {
        gtk_entry_set_icon_from_icon_name(GTK_ENTRY(c->location),
                                          GTK_ENTRY_ICON_PRIMARY,
                                          NULL);
    }

    g_object_unref(js_value);
}

void
hover_web_view(WebKitWebView *web_view, WebKitHitTestResult *ht, guint modifiers,
               gpointer data)
{
    struct Client *c = (struct Client *)data;
    const char *to_show;

    g_free(c->hover_uri);

    if (webkit_hit_test_result_context_is_link(ht))
    {
        to_show = webkit_hit_test_result_get_link_uri(ht);
        c->hover_uri = g_strdup(to_show);
    }
    else
    {
        to_show = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
        c->hover_uri = NULL;
    }

    if (!gtk_widget_is_focus(c->location))
        gtk_entry_set_text(GTK_ENTRY(c->location), to_show);
}

void
icon_location(GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEvent *event,
              gpointer data)
{
    struct Client *c = (struct Client *)data;
    gchar *d;
    gchar *data_template =
        "data:text/html,"
        "<!DOCTYPE html>"
        "<html>"
        "    <head>"
        "        <meta charset=\"UTF-8\">"
        "        <title>Feeds</title>"
        "    </head>"
        "    <body>"
        "        <p>Feeds found on this page:</p>"
        "        <ul>"
        "        %s"
        "        </ul>"
        "    </body>"
        "</html>";

    if (c->feed_html != NULL)
    {
        /* What we're actually trying to do is show a simple HTML page
         * that lists all the feeds on the current page. The function
         * webkit_web_view_load_html() looks like the proper way to do
         * that. Sad thing is, it doesn't create a history entry, but
         * instead simply replaces the content of the current page. This
         * is not what we want.
         *
         * RFC 2397 [0] defines the data URI scheme [1]. We abuse this
         * mechanism to show my custom HTML snippet *and* create a
         * history entry.
         *
         * [0]: https://tools.ietf.org/html/rfc2397
         * [1]: https://en.wikipedia.org/wiki/Data_URI_scheme */
        d = g_strdup_printf(data_template, c->feed_html);
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), d);
        g_free(d);
    }
}

gboolean
key_common(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    struct Client *c = (struct Client *)data;

    if (event->type == GDK_KEY_PRESS)
    {
        GdkEventKey *key_event = (GdkEventKey *)event;
        for (unsigned int i = 0; i < LENGTH(keys); i++) {
            if (key_event->keyval == keys[i].keyval && 
                (key_event->state & (GDK_MOD1_MASK | GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == keys[i].mod) {
                return keys[i].func(c, keys[i].arg);
            }
        }

        // Handle Shift+H and Shift+L separately For History Navigation
        if (key_event->state & GDK_SHIFT_MASK) {
            switch (key_event->keyval) {
                case GDK_KEY_H:
                    return history_back(c, NULL);
                case GDK_KEY_L:
                    return history_forward(c, NULL);
            }
        }

        // Handle Shift+K and Shift+J separately For Up and Down Scrolling
        if (key_event->state & GDK_SHIFT_MASK) {
            switch (key_event->keyval) {
                case GDK_KEY_K:
                    return scroll_down(c, NULL);
                case GDK_KEY_J:
                    return scroll_up(c, NULL);
            }
        }
        
        // Existing F2 and F3 functionality
        if (key_event->keyval == GDK_KEY_F2)
        {
            webkit_web_view_go_back(WEBKIT_WEB_VIEW(c->web_view));
            return TRUE;
        }
        else if (key_event->keyval == GDK_KEY_F3)
        {
            webkit_web_view_go_forward(WEBKIT_WEB_VIEW(c->web_view));
            return TRUE;
        }
    }

    return FALSE;
}

gboolean
key_downloadmanager(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    if (event->type == GDK_KEY_PRESS)
    {
        if (((GdkEventKey *)event)->state & GDK_MOD1_MASK)
        {
            switch (((GdkEventKey *)event)->keyval)
            {
                case GDK_KEY_d:  /* close window (left hand) */
                case GDK_KEY_q:
                    downloadmanager_delete(dm.win, NULL);
                    return TRUE;
            }
        }
    }

    return FALSE;
}

gboolean
key_location(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    struct Client *c = (struct Client *)data;
    const gchar *t;
    gchar *f;

    if (key_common(widget, event, data))
        return TRUE;

    if (event->type == GDK_KEY_PRESS)
    {
        switch (((GdkEventKey *)event)->keyval)
        {
            case GDK_KEY_KP_Enter:
            case GDK_KEY_Return:
                gtk_widget_grab_focus(c->web_view);
                t = gtk_entry_get_text(GTK_ENTRY(c->location));
                if (t != NULL && t[0] == ':' && t[1] == '/')
                {
                    if (search_text != NULL)
                        g_free(search_text);
                    search_text = g_strdup(t + 2);
                    search(c, 0);
                }
                else
                {
                    f = ensure_uri_scheme(t);
                    if (f != NULL)
                    {
                        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), f);
                        g_free(f);
                    }
                    else
                    {
                        // If ensure_uri_scheme returns NULL, treat it as a search query
                        gchar *search_url = g_strdup_printf(search_engine, g_uri_escape_string(t, NULL, FALSE));
                        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), search_url);
                        g_free(search_url);
                    }
                }
                return TRUE;
            case GDK_KEY_Escape:
                t = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
                gtk_entry_set_text(GTK_ENTRY(c->location),
                                   (t == NULL ? NAME : t));
                return TRUE;
        }
    }

    return FALSE;
}

gboolean
key_tablabel(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    GdkScrollDirection direction;

    if (event->type == GDK_BUTTON_RELEASE)
    {
        switch (((GdkEventButton *)event)->button)
        {
            case 2:
                client_destroy(NULL, data);
                return TRUE;
        }
    }
    else if (event->type == GDK_SCROLL)
    {
        gdk_event_get_scroll_direction(event, &direction);
        switch (direction)
        {
            case GDK_SCROLL_UP:
                gtk_notebook_prev_page(GTK_NOTEBOOK(mw.notebook));
                break;
            case GDK_SCROLL_DOWN:
                gtk_notebook_next_page(GTK_NOTEBOOK(mw.notebook));
                break;
            default:
                break;
        }
        return TRUE;
    }
    return FALSE;
}

gboolean
key_web_view(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    struct Client *c = (struct Client *)data;
    gdouble dx, dy;
    gfloat z;

    if (key_common(widget, event, data))
        return TRUE;

    if (event->type == GDK_KEY_PRESS)
    {
        if (((GdkEventKey *)event)->keyval == GDK_KEY_Escape)
        {
            webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(c->web_view));
            gtk_entry_set_progress_fraction(GTK_ENTRY(c->location), 0);
        }
    }
    else if (event->type == GDK_BUTTON_RELEASE)
    {
        GdkEventButton *button_event = (GdkEventButton *)event;
        switch (button_event->button)
        {
            case 1: // Left mouse button
                if (button_event->state & GDK_CONTROL_MASK)
                {
                    if (c->hover_uri != NULL)
                    {
                        client_new(c->hover_uri, NULL, TRUE, FALSE);
                        return TRUE;
                    }
                }
                break;
            case 2:
                if (c->hover_uri != NULL)
                {
                    client_new(c->hover_uri, NULL, TRUE, FALSE);
                    return TRUE;
                }
                break;
            case 8:
                webkit_web_view_go_back(WEBKIT_WEB_VIEW(c->web_view));
                return TRUE;
            case 9:
                webkit_web_view_go_forward(WEBKIT_WEB_VIEW(c->web_view));
                return TRUE;
        }
    }
    else if (event->type == GDK_SCROLL)
    {
        if (((GdkEventScroll *)event)->state & GDK_MOD1_MASK ||
            ((GdkEventScroll *)event)->state & GDK_CONTROL_MASK)
        {
            gdk_event_get_scroll_deltas(event, &dx, &dy);
            z = webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(c->web_view));
            z += -dy * 0.1;
            z = dx != 0 ? global_zoom : z;
            webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(c->web_view), z);
            return TRUE;
        }
    }

    return FALSE;
}

void 
init_default_web_context(void)
{
    gchar *p;
    WebKitWebContext *wc;

    wc = webkit_web_context_get_default();

    p = g_build_filename(g_get_user_config_dir(), NAME, "web_extensions", NULL);
    webkit_web_context_set_web_extensions_directory(wc, p);
    g_free(p);

    g_signal_connect(G_OBJECT(wc), "download-started",
                     G_CALLBACK(download_handle_start), NULL);

    trust_user_certs(wc);

    WebKitSettings *settings = webkit_settings_new();
    
    // Apply performance and resource usage settings
    webkit_settings_set_enable_site_specific_quirks(settings, enable_site_specific_quirks);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, enable_write_console_messages_to_stdout);
    webkit_settings_set_enable_media_capabilities(settings, enable_media_capabilities);
    webkit_settings_set_enable_page_cache(settings, enable_page_cache);
    webkit_settings_set_enable_encrypted_media(settings, enable_encrypted_media);
    
    // Set a more conservative cache model
    webkit_web_context_set_cache_model(wc, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    
    // Apply minimalistic UI settings
    webkit_settings_set_enable_site_specific_quirks(settings, !disable_site_icons);
    
    // Apply additional privacy settings
    webkit_cookie_manager_set_accept_policy(
        webkit_web_context_get_cookie_manager(wc),
        block_third_party_cookies ? WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY : WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS
    );

    webkit_web_context_set_web_extensions_initialization_user_data(wc, 
        g_variant_new_boolean(enable_back_forward_navigation_gestures));

    // Set favicon cache directory
    gchar *favicon_cache_dir = g_build_filename(g_get_user_cache_dir(), NAME, "favicons", NULL);
    webkit_web_context_set_favicon_database_directory(wc, disable_site_icons ? NULL : favicon_cache_dir);
    g_free(favicon_cache_dir);

    // Set the settings for the default web view
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    webkit_web_view_set_settings(web_view, settings);
    
    // Ensure proper reference management
    g_object_ref_sink(web_view);
    g_object_unref(web_view);

    // Clean up
    g_object_unref(settings);

    // Print a note about the GStreamer FDK AAC plugin
    g_print("Note: If you encounter issues with AAC playback, you may need to install the GStreamer FDK AAC plugin.\n");
}

void
mainwindow_setup(void)
{
    mw.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(mw.win), 800, 600);
    g_signal_connect(G_OBJECT(mw.win), "destroy", gtk_main_quit, NULL);
    gtk_window_set_title(GTK_WINDOW(mw.win), NAME);

    mw.notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(mw.notebook), TRUE);
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(mw.notebook), tab_pos);
    gtk_container_add(GTK_CONTAINER(mw.win), mw.notebook);
    g_signal_connect(G_OBJECT(mw.notebook), "switch-page",
                     G_CALLBACK(notebook_switch_page), NULL);
}

void
mainwindow_title(gint idx)
{
    GtkWidget *child, *widg, *tablabel;
    const gchar *text;

    child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(mw.notebook), idx);
    if (child == NULL)
        return;

    widg = gtk_notebook_get_tab_label(GTK_NOTEBOOK(mw.notebook), child);
    tablabel = (GtkWidget *)g_object_get_data(G_OBJECT(widg), "lariza-tab-label");
    text = gtk_label_get_text(GTK_LABEL(tablabel));
    gtk_window_set_title(GTK_WINDOW(mw.win), text);
}

void
notebook_switch_page(GtkNotebook *nb, GtkWidget *p, guint idx, gpointer data)
{
    mainwindow_title(idx);
}

gboolean
quit_if_nothing_active(void)
{
    if (clients == 0)
    {
        if (downloads == 0)
        {
            gtk_main_quit();
            return TRUE;
        }
        else
            gtk_widget_show_all(dm.win);
    }

    return FALSE;
}

gboolean
remote_msg(GIOChannel *channel, GIOCondition condition, gpointer data)
{
    gchar *uri = NULL;

    g_io_channel_read_line(channel, &uri, NULL, NULL, NULL);
    if (uri)
    {
        g_strstrip(uri);
        client_new(uri, NULL, TRUE, TRUE);
        g_free(uri);
    }
    return TRUE;
}

void run_user_scripts(WebKitWebView *web_view)
{
    static GHashTable *script_cache = NULL;
    gchar *base = NULL, *path = NULL, *contents = NULL;
    const gchar *entry = NULL;
    GDir *scriptdir = NULL;

    if (!script_cache) {
        script_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    }

    base = g_build_filename(g_get_user_config_dir(), NAME, "user-scripts", NULL);
    scriptdir = g_dir_open(base, 0, NULL);
    if (scriptdir != NULL) {
        while ((entry = g_dir_read_name(scriptdir)) != NULL) {
            if (g_str_has_suffix(entry, ".js")) {
                path = g_build_filename(base, entry, NULL);
                contents = g_hash_table_lookup(script_cache, path);
                if (!contents) {
                    if (g_file_get_contents(path, &contents, NULL, NULL)) {
                        g_hash_table_insert(script_cache, g_strdup(path), contents);
                    }
                }
                if (contents) {
                    webkit_web_view_evaluate_javascript(web_view, contents, -1, NULL, NULL, NULL, NULL, NULL);
                }
                g_free(path);
            }
        }
        g_dir_close(scriptdir);
    }

    g_free(base);
}

void
search(gpointer data, gint direction)
{
    struct Client *c = (struct Client *)data;
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(c->web_view);
    WebKitFindController *fc = webkit_web_view_get_find_controller(web_view);

    if (!search_text || !*search_text)
        return;

    static WebKitFindOptions options = WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND;

    switch (direction) {
        case 0:
            webkit_find_controller_search(fc, search_text, options, G_MAXUINT);
            break;
        case 1:
            webkit_find_controller_search_next(fc);
            break;
        case -1:
            webkit_find_controller_search_previous(fc);
            break;
    }
}

void
show_web_view(WebKitWebView *web_view, gpointer data)
{
    struct Client *c = (struct Client *)data;
    gint idx;

    (void)web_view;

    gtk_widget_show_all(mw.win);

    if (c->focus_new_tab)
    {
        idx = gtk_notebook_page_num(GTK_NOTEBOOK(mw.notebook), c->vbox);
        if (idx != -1)
            gtk_notebook_set_current_page(GTK_NOTEBOOK(mw.notebook), idx);

        gtk_widget_grab_focus(c->web_view);
    }
}

void
trust_user_certs(WebKitWebContext *wc)
{
    GTlsCertificate *cert;
    const gchar *basedir, *file, *absfile;
    GDir *dir;

    basedir = g_build_filename(g_get_user_config_dir(), NAME, "certs", NULL);
    dir = g_dir_open(basedir, 0, NULL);
    if (dir != NULL)
    {
        file = g_dir_read_name(dir);
        while (file != NULL)
        {
            absfile = g_build_filename(g_get_user_config_dir(), NAME, "certs",
                                       file, NULL);
            cert = g_tls_certificate_new_from_file(absfile, NULL);
            if (cert == NULL)
                fprintf(stderr, NAME": Could not load trusted cert '%s'\n", file);
            else
                webkit_web_context_allow_tls_certificate_for_host(wc, cert, file);
            file = g_dir_read_name(dir);
        }
        g_dir_close(dir);
    }
}

ssize_t
write_full(int fd, char *data, size_t len)
{
    size_t done = 0;
    ssize_t r;

    while (done < len)
    {
        if ((r = write(fd, data + done, len - done)) == -1)
        {
            if (errno == EINTR)
                r = 0;
            else
            {
                perror(NAME": write_full");
                return r;
            }
        }
        else if (r == 0)
            return 0;

        done += r;
    }

    return done;
}

int main(int argc, char **argv)
{
    int opt, i;

    gtk_init(&argc, &argv);
    grab_environment_configuration();

    // Initialize the closed_tabs queue
    closed_tabs = g_queue_new();

    // Increase the file descriptor limit
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }

    // Preload resources
    preload_resources();

    // Add periodic cleanup
    g_timeout_add_seconds(300, cleanup_resources, NULL);  // Clean up every 5 minutes

    while ((opt = getopt(argc, argv, "C")) != -1)
    {
        switch (opt)
        {
            case 'C':
                cooperative_instances = FALSE;
                break;
            default:
                fprintf(stderr, "Usage: "NAME" [OPTION]... [URI]...\n");
                exit(EXIT_FAILURE);
        }
    }

    if (cooperative_instances)
        cooperation_setup();

    if (!cooperative_instances || cooperative_alone)
        init_default_web_context();

    downloadmanager_setup();
    mainwindow_setup();

    if (optind >= argc)
        client_new(home_uri, NULL, TRUE, TRUE);
    else
    {
        for (i = optind; i < argc; i++)
            client_new(argv[i], NULL, TRUE, TRUE);
    }

    if (!cooperative_instances || cooperative_alone)
        gtk_main();

    // Cleanup
    g_queue_free_full(closed_tabs, g_free);

    exit(EXIT_SUCCESS);
}

gboolean cleanup_resources(gpointer user_data)
{
    webkit_web_context_clear_cache(webkit_web_context_get_default());
    return G_SOURCE_CONTINUE;
}

void preload_resources(void)
{
    // Preload icons
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    gtk_icon_theme_load_icon(icon_theme, "text-html", GTK_ICON_SIZE_SMALL_TOOLBAR, 0, NULL);
    gtk_icon_theme_load_icon(icon_theme, "gtk-delete", GTK_ICON_SIZE_SMALL_TOOLBAR, 0, NULL);
    
    // Precompile regular expressions
    g_regex_new("^[a-zA-Z0-9-]+\\.[a-zA-Z]{2,}(\\S*)?$", 0, 0, NULL);
    
    // Preload user scripts
    run_user_scripts(NULL);
}

// Keybindings
gboolean close_tab(struct Client *c, const gchar *arg) {
    (void)arg;
    client_destroy(NULL, c);
    return TRUE;
}

gboolean go_home(struct Client *c, const gchar *arg) {
    (void)arg;
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), home_uri);
    return TRUE;
}

gboolean new_tab(struct Client *c, const gchar *arg) {
    (void)c;
    (void)arg;
    client_new(home_uri, NULL, TRUE, TRUE);
    return TRUE;
}

gboolean reload_page(struct Client *c, const gchar *arg) {
    (void)arg;
    webkit_web_view_reload(WEBKIT_WEB_VIEW(c->web_view));
    return TRUE;
}

gboolean show_downloads(struct Client *c, const gchar *arg) {
    (void)c;
    (void)arg;
    gtk_widget_show_all(dm.win);
    return TRUE;
}

gboolean search_forward(struct Client *c, const gchar *arg) {
    (void)arg;
    search(c, 1);
    return TRUE;
}

gboolean search_backward(struct Client *c, const gchar *arg) {
    (void)arg;
    search(c, -1);
    return TRUE;
}

gboolean focus_location(struct Client *c, const gchar *arg) {
    (void)arg;
    gtk_widget_grab_focus(c->location);
    return TRUE;
}

gboolean init_search(struct Client *c, const gchar *arg) {
    (void)arg;
    gtk_widget_grab_focus(c->location);
    gtk_entry_set_text(GTK_ENTRY(c->location), ":/");
    gtk_editable_set_position(GTK_EDITABLE(c->location), -1);
    return TRUE;
}

gboolean reload_certs(struct Client *c, const gchar *arg) {
    (void)arg;
    WebKitWebContext *wc = webkit_web_view_get_context(WEBKIT_WEB_VIEW(c->web_view));
    trust_user_certs(wc);
    return TRUE;
}

gboolean prev_tab(struct Client *c, const gchar *arg) {
    (void)c;
    (void)arg;
    gtk_notebook_prev_page(GTK_NOTEBOOK(mw.notebook));
    return TRUE;
}

gboolean next_tab(struct Client *c, const gchar *arg) {
    (void)c;
    (void)arg;
    gtk_notebook_next_page(GTK_NOTEBOOK(mw.notebook));
    return TRUE;
}

gboolean goto_tab(struct Client *c, const gchar *arg) {
    (void)c;
    int tab_index = atoi(arg);
    int n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(mw.notebook));
    
    if (tab_index >= 0 && tab_index < n_pages) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(mw.notebook), tab_index);
        return TRUE;
    }
    return FALSE;
}

gboolean scroll_up(struct Client *c, const gchar *arg) {
    (void)arg;
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(c->web_view),
        "window.scrollBy(0, -50);", -1, NULL, NULL, NULL, NULL, NULL);
    return TRUE;
}

gboolean scroll_down(struct Client *c, const gchar *arg) {
    (void)arg;
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(c->web_view),
        "window.scrollBy(0, 50);", -1, NULL, NULL, NULL, NULL, NULL);
    return TRUE;
}

gboolean history_back(struct Client *c, const gchar *arg) {
    (void)arg;
    webkit_web_view_go_back(WEBKIT_WEB_VIEW(c->web_view));
    return TRUE;
}

gboolean history_forward(struct Client *c, const gchar *arg) {
    (void)arg;
    webkit_web_view_go_forward(WEBKIT_WEB_VIEW(c->web_view));
    return TRUE;
}
