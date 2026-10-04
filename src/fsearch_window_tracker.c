/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#define G_LOG_DOMAIN "fsearch-window-tracker"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fsearch_window_tracker.h"

#include "fsearch.h"
#include "fsearch_config.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <glib.h>
#include <gtk/gtk.h>

#ifdef HAVE_WNCK
#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>
#endif

#ifdef HAVE_WNCK
// Most recently used file manager windows, owned by libwnck.
// Head of the queue = most recently used.
static GQueue *fm_window_mru = NULL;
static WnckScreen *fm_screen = NULL;
#endif

static bool
window_tracker_is_supported(void) {
#if !defined(HAVE_WNCK) || !defined(GDK_WINDOWING_X11)
    return false;
#else
    GdkDisplay *display = gdk_display_get_default();
    return display != NULL && GDK_IS_X11_DISPLAY(display);
#endif
}

#ifdef HAVE_WNCK
static bool
is_file_manager_window(WnckWindow *win) {
    g_return_val_if_fail(win != NULL, false);

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    if (!config || !config->file_manager_window_classes) {
        return false;
    }

    const char *class_name = wnck_window_get_class_group_name(win);
    if (!class_name || class_name[0] == '\0') {
        return false;
    }

    g_auto(GStrv) classes = g_strsplit(config->file_manager_window_classes, ",", -1);
    for (char **c = classes; *c != NULL; c++) {
        g_strstrip(*c);
        if ((*c)[0] != '\0' && g_ascii_strcasecmp(*c, class_name) == 0) {
            return true;
        }
    }
    return false;
}

static void
mru_push_front(WnckWindow *win) {
    g_queue_remove(fm_window_mru, win);
    g_queue_push_head(fm_window_mru, win);
}

static void
on_tracker_window_opened(WnckScreen *screen, WnckWindow *win, gpointer user_data) {
    if (is_file_manager_window(win)) {
        g_debug("[window-tracker] file manager window opened");
        mru_push_front(win);
    }
}

static void
on_tracker_window_closed(WnckScreen *screen, WnckWindow *win, gpointer user_data) {
    g_queue_remove(fm_window_mru, win);
}

static void
on_tracker_active_window_changed(WnckScreen *screen, WnckWindow *previously_active, WnckWindow *active, gpointer user_data) {
    if (active && is_file_manager_window(active)) {
        mru_push_front(active);
    }
}
#endif

void
fsearch_window_tracker_start(void) {
#ifdef HAVE_WNCK
    if (!window_tracker_is_supported()) {
        g_debug("[window-tracker] not running on X11, file manager window tracking is disabled");
        return;
    }
    if (fm_window_mru) {
        return;
    }
    fm_window_mru = g_queue_new();

    wnck_set_client_type(WNCK_CLIENT_TYPE_PAGER);

    fm_screen = wnck_screen_get_default();
    if (!fm_screen) {
        return;
    }
    wnck_screen_force_update(fm_screen);

    // Populate the queue with already open windows. The list is stacked bottom to
    // top, so prepending in iteration order makes the topmost window the most
    // recently used one.
    GList *windows = wnck_screen_get_windows_stacked(fm_screen);
    for (GList *l = windows; l != NULL; l = l->next) {
        WnckWindow *win = l->data;
        if (is_file_manager_window(win)) {
            mru_push_front(win);
        }
    }

    g_signal_connect(fm_screen, "window-opened", G_CALLBACK(on_tracker_window_opened), NULL);
    g_signal_connect(fm_screen, "window-closed", G_CALLBACK(on_tracker_window_closed), NULL);
    g_signal_connect(fm_screen, "active-window-changed", G_CALLBACK(on_tracker_active_window_changed), NULL);

    g_debug("[window-tracker] started, %u file manager window(s) found", g_queue_get_length(fm_window_mru));
#else
    g_debug("[window-tracker] built without libwnck, file manager window tracking is disabled");
#endif
}

void
fsearch_window_tracker_switch_to_last_file_manager_window(void) {
#ifdef HAVE_WNCK
    if (!window_tracker_is_supported() || !fm_window_mru || g_queue_is_empty(fm_window_mru)) {
        g_debug("[window-tracker] no file manager window to switch to");
        return;
    }

    WnckWindow *win = g_queue_pop_head(fm_window_mru);
    g_return_if_fail(win != NULL);

    // Rotate, so repeated calls cycle through all file manager windows.
    g_queue_push_tail(fm_window_mru, win);

    g_debug("[window-tracker] switching to file manager window");

    // Fetch a fresh X server timestamp. WMs are free to ignore activation
    // requests that carry timestamp 0.
    guint32 timestamp = GDK_CURRENT_TIME;
#ifdef GDK_WINDOWING_X11
    GdkDisplay *display = gdk_display_get_default();
    if (display && GDK_IS_X11_DISPLAY(display)) {
        timestamp = gdk_x11_get_server_time(gdk_screen_get_root_window(gdk_screen_get_default()));
    }
#endif
    wnck_window_activate(win, timestamp);
#else
    g_debug("[window-tracker] built without libwnck, nothing to switch to");
#endif
}
