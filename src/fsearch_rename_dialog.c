#define G_LOG_DOMAIN "fsearch-rename-dialog"

#include "fsearch_rename_dialog.h"

#include "fsearch.h"
#include "fsearch_config.h"
#include "fsearch_file_utils.h"
#include "fsearch_rename_engine.h"

#include <glib/gi18n.h>
#include <stdlib.h>
#include <string.h>

typedef struct FsearchRenameDialog FsearchRenameDialog;

struct FsearchRenameDialog {
    GtkBuilder *builder;
    GtkWidget *dialog;
    GtkWidget *ok_button;

    GtkEntry *search_entry;
    GtkEntry *replace_entry;
    GtkToggleButton *match_case;
    GtkToggleButton *diacritics;
    GtkToggleButton *use_regex;
    GtkToggleButton *ignore_extension;

    GtkTreeView *old_view;
    GtkTreeView *new_view;
    GtkListStore *old_store;
    GtkListStore *new_store;

    GtkComboBoxText *preset_combo;
    GtkWidget *preset_save_button;
    GtkWidget *preset_delete_button;

    GPtrArray *paths;      // full paths of the items to rename
    GPtrArray *new_paths;  // full paths of the preview results (parallel to paths)
    GArray *statuses;      // FsearchRenameStatus per item (parallel to paths)

    guint preview_timeout_id;

    FsearchRenameDialogResponse callback;
    gpointer callback_data;
};

static void
fsearch_rename_dialog_free(FsearchRenameDialog *dialog) {
    if (dialog->preview_timeout_id) {
        g_source_remove(dialog->preview_timeout_id);
    }
    g_clear_object(&dialog->builder);
    g_clear_pointer(&dialog->paths, g_ptr_array_unref);
    g_clear_pointer(&dialog->new_paths, g_ptr_array_unref);
    if (dialog->statuses) {
        g_array_free(dialog->statuses, TRUE);
    }
    g_clear_pointer(&dialog->dialog, gtk_widget_destroy);
    g_clear_pointer(&dialog, free);
}

static void
collect_options(FsearchRenameDialog *dialog, FsearchRenameOptions *options) {
    memset(options, 0, sizeof(*options));
    options->search = g_strdup(gtk_entry_get_text(dialog->search_entry));
    options->replace = g_strdup(gtk_entry_get_text(dialog->replace_entry));
    options->match_case = gtk_toggle_button_get_active(dialog->match_case);
    options->diacritics = gtk_toggle_button_get_active(dialog->diacritics);
    options->use_regex = gtk_toggle_button_get_active(dialog->use_regex);
    options->ignore_extension = gtk_toggle_button_get_active(dialog->ignore_extension);
    options->counter_start = 1;
}

static void
update_ok_button(FsearchRenameDialog *dialog) {
    if (!dialog->ok_button || !dialog->statuses) {
        return;
    }

    uint32_t num_changed = 0;
    uint32_t num_blocked = 0;
    for (uint32_t i = 0; i < dialog->statuses->len; ++i) {
        const FsearchRenameStatus status = g_array_index(dialog->statuses, FsearchRenameStatus, i);
        if (status & (FSEARCH_RENAME_STATUS_EMPTY | FSEARCH_RENAME_STATUS_INVALID_CHARS)) {
            num_blocked++;
        }
        else if (!(status & FSEARCH_RENAME_STATUS_UNCHANGED)) {
            num_changed++;
        }
    }

    char *tooltip = NULL;
    if (num_blocked > 0) {
        tooltip = g_strdup_printf(ngettext("%d item will be skipped (conflict or invalid name)",
                                           "%d items will be skipped (conflict or invalid name)",
                                           num_blocked),
                                  num_blocked);
    }
    gtk_widget_set_tooltip_text(dialog->ok_button, tooltip);
    g_clear_pointer(&tooltip, g_free);

    gtk_widget_set_sensitive(dialog->ok_button, num_changed > 0);
}

static void
rename_dialog_update_preview(FsearchRenameDialog *dialog) {
    FsearchRenameOptions options = {0};
    collect_options(dialog, &options);

    GError *error = NULL;
    FsearchRenameEngine *engine = fsearch_rename_engine_new(&options, &error);

    // Invalid regular expression: flag the search entry and disable OK
    g_object_set(dialog->search_entry, "secondary-icon-name", error ? "dialog-error-symbolic" : NULL, NULL);
    g_object_set(dialog->search_entry,
                 "secondary-icon-tooltip-text",
                 error ? error->message : NULL,
                 NULL);

    g_clear_error(&error);

    gtk_list_store_clear(dialog->new_store);
    g_clear_pointer(&dialog->new_paths, g_ptr_array_unref);
    dialog->new_paths = g_ptr_array_new_with_free_func(g_free);
    if (dialog->statuses) {
        g_array_free(dialog->statuses, TRUE);
    }
    dialog->statuses = g_array_new(FALSE, FALSE, sizeof(FsearchRenameStatus));

    for (uint32_t i = 0; i < dialog->paths->len; ++i) {
        const char *old_path = g_ptr_array_index(dialog->paths, i);
        g_autofree char *old_name = g_path_get_basename(old_path);

        g_autofree char *new_name = NULL;
        FsearchRenameStatus status = FSEARCH_RENAME_STATUS_UNCHANGED;

        if (engine) {
            new_name = fsearch_rename_engine_apply(engine, old_name, options.counter_start + i);
            status = fsearch_rename_engine_check(new_name);
            if (g_strcmp0(old_name, new_name) == 0) {
                status |= FSEARCH_RENAME_STATUS_UNCHANGED;
            }
        }
        else {
            new_name = g_strdup(old_name);
            status |= FSEARCH_RENAME_STATUS_UNCHANGED;
        }

        g_autofree char *dir_path = g_path_get_dirname(old_path);
        g_autofree char *new_path = g_build_filename(dir_path, new_name, NULL);

        const char *color = NULL;
        if (status & (FSEARCH_RENAME_STATUS_EMPTY | FSEARCH_RENAME_STATUS_INVALID_CHARS)) {
            color = "red";
        }
        else if (!(status & FSEARCH_RENAME_STATUS_UNCHANGED) && fsearch_file_utils_path_exists(new_path)
                 && g_strcmp0(new_path, old_path) != 0) {
            // The target of the rename already exists
            status |= FSEARCH_RENAME_STATUS_INVALID_CHARS;
            color = "red";
        }
        else if (status & FSEARCH_RENAME_STATUS_UNCHANGED) {
            color = "grey";
        }

        g_array_append_val(dialog->statuses, status);
        g_ptr_array_add(dialog->new_paths, g_strdup(new_path));

        GtkTreeIter iter = {0};
        gtk_list_store_append(dialog->new_store, &iter);
        gtk_list_store_set(dialog->new_store, &iter, 0, new_name, 1, color, -1);
    }

    fsearch_rename_engine_free(engine);
    g_clear_pointer(&options.search, g_free);
    g_clear_pointer(&options.replace, g_free);

    update_ok_button(dialog);
}

static gboolean
rename_dialog_preview_timeout_cb(gpointer user_data) {
    FsearchRenameDialog *dialog = user_data;
    dialog->preview_timeout_id = 0;
    rename_dialog_update_preview(dialog);
    return G_SOURCE_REMOVE;
}

static void
rename_dialog_schedule_preview(FsearchRenameDialog *dialog) {
    if (dialog->preview_timeout_id) {
        return;
    }
    dialog->preview_timeout_id = g_timeout_add(120, rename_dialog_preview_timeout_cb, dialog);
}

static void
on_rename_preset_combo_changed(GtkComboBoxText *combo, gpointer user_data);

static void
refresh_preset_combo(FsearchRenameDialog *dialog, const char *name_to_select) {
    GtkComboBoxText *combo = dialog->preset_combo;
    g_signal_handlers_block_by_func(combo, on_rename_preset_combo_changed, dialog);
    gtk_combo_box_text_remove_all(combo);

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    int select_idx = -1;
    if (config && config->rename_presets) {
        for (uint32_t i = 0; i < config->rename_presets->len; ++i) {
            FsearchRenamePreset *preset = g_ptr_array_index(config->rename_presets, i);
            gtk_combo_box_text_append_text(combo, preset->name);
            if (name_to_select && g_strcmp0(preset->name, name_to_select) == 0) {
                select_idx = (int)i;
            }
        }
    }

    g_signal_handlers_unblock_by_func(combo, on_rename_preset_combo_changed, dialog);

    if (select_idx >= 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), select_idx);
    }
}

static void
on_rename_preset_save_clicked(GtkButton *button, gpointer user_data) {
    FsearchRenameDialog *dialog = user_data;

    GtkDialog *prompt = GTK_DIALOG(
        gtk_dialog_new_with_buttons(_("Save Preset"),
                                    GTK_WINDOW(dialog->dialog),
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    _("_Cancel"),
                                    GTK_RESPONSE_CANCEL,
                                    _("_OK"),
                                    GTK_RESPONSE_OK,
                                    NULL));
    gtk_dialog_set_default_response(prompt, GTK_RESPONSE_OK);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    GtkWidget *content = gtk_dialog_get_content_area(prompt);
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new(_("Preset name:")), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 2);
    gtk_widget_show_all(GTK_WIDGET(content));

    char *name = NULL;
    if (gtk_dialog_run(prompt) == GTK_RESPONSE_OK) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (text && text[0] != '\0') {
            name = g_strdup(text);
        }
    }
    gtk_widget_destroy(GTK_WIDGET(prompt));

    if (!name) {
        return;
    }

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    if (!config) {
        g_free(name);
        return;
    }

    if (!config->rename_presets) {
        config->rename_presets = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_rename_preset_free);
    }

    // Replace an existing preset of the same name, otherwise append
    int existing_idx = -1;
    for (uint32_t i = 0; i < config->rename_presets->len; ++i) {
        FsearchRenamePreset *preset = g_ptr_array_index(config->rename_presets, i);
        if (g_strcmp0(preset->name, name) == 0) {
            existing_idx = (int)i;
            break;
        }
    }

    FsearchRenameOptions options = {0};
    collect_options(dialog, &options);
    FsearchRenamePreset *preset = fsearch_rename_preset_new(name,
                                                            options.search,
                                                            options.replace,
                                                            options.match_case,
                                                            options.diacritics,
                                                            options.use_regex,
                                                            options.ignore_extension,
                                                            options.counter_start);
    g_clear_pointer(&options.search, g_free);
    g_clear_pointer(&options.replace, g_free);

    if (existing_idx >= 0) {
        g_ptr_array_remove_index(config->rename_presets, (uint32_t)existing_idx);
    }
    g_ptr_array_add(config->rename_presets, preset);
    config_save(config);

    refresh_preset_combo(dialog, name);

    g_free(name);
}

static void
on_rename_preset_delete_clicked(GtkButton *button, gpointer user_data) {
    FsearchRenameDialog *dialog = user_data;

    const int active = gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->preset_combo));
    if (active < 0) {
        return;
    }

    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    if (!config || !config->rename_presets || (uint32_t)active >= config->rename_presets->len) {
        return;
    }

    g_ptr_array_remove_index(config->rename_presets, (uint32_t)active);
    config_save(config);

    refresh_preset_combo(dialog, NULL);
}

static void
on_rename_preset_combo_changed(GtkComboBoxText *combo, gpointer user_data) {
    FsearchRenameDialog *dialog = user_data;

    const int active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    FsearchConfig *config = fsearch_application_get_config(FSEARCH_APPLICATION_DEFAULT);
    if (active < 0 || !config || !config->rename_presets || (uint32_t)active >= config->rename_presets->len) {
        return;
    }

    const FsearchRenamePreset *preset = g_ptr_array_index(config->rename_presets, (uint32_t)active);

    gtk_entry_set_text(dialog->search_entry, preset->options.search ? preset->options.search : "");
    gtk_entry_set_text(dialog->replace_entry, preset->options.replace ? preset->options.replace : "");
    gtk_toggle_button_set_active(dialog->match_case, preset->options.match_case ? TRUE : FALSE);
    gtk_toggle_button_set_active(dialog->diacritics, preset->options.diacritics ? TRUE : FALSE);
    gtk_toggle_button_set_active(dialog->use_regex, preset->options.use_regex ? TRUE : FALSE);
    gtk_toggle_button_set_active(dialog->ignore_extension, preset->options.ignore_extension ? TRUE : FALSE);
}

static void
on_rename_option_changed(GtkEditable *editable, gpointer user_data) {
    FsearchRenameDialog *dialog = user_data;
    rename_dialog_schedule_preview(dialog);
}

static void
on_rename_option_toggled(GtkToggleButton *toggle, gpointer user_data) {
    FsearchRenameDialog *dialog = user_data;
    gtk_widget_set_sensitive(GTK_WIDGET(dialog->diacritics),
                             !gtk_toggle_button_get_active(dialog->use_regex));
    rename_dialog_schedule_preview(dialog);
}

static void
on_rename_dialog_response(GtkDialog *gtk_dialog, GtkResponseType response, gpointer user_data) {
    FsearchRenameDialog *dialog = user_data;

    GPtrArray *old_paths = NULL;
    GPtrArray *new_paths = NULL;

    if (response == GTK_RESPONSE_OK && dialog->statuses && dialog->new_paths) {
        old_paths = g_ptr_array_new_with_free_func(g_free);
        new_paths = g_ptr_array_new_with_free_func(g_free);

        for (uint32_t i = 0; i < dialog->paths->len; ++i) {
            const FsearchRenameStatus status = g_array_index(dialog->statuses, FsearchRenameStatus, i);
            const char *old_path = g_ptr_array_index(dialog->paths, i);
            const char *new_path = g_ptr_array_index(dialog->new_paths, i);
            if (!(status & (FSEARCH_RENAME_STATUS_EMPTY | FSEARCH_RENAME_STATUS_INVALID_CHARS))
                && !(status & FSEARCH_RENAME_STATUS_UNCHANGED)) {
                g_ptr_array_add(old_paths, g_strdup(old_path));
                g_ptr_array_add(new_paths, g_strdup(new_path));
            }
        }
    }

    FsearchRenameDialogResponse callback = dialog->callback;
    gpointer callback_data = dialog->callback_data;

    fsearch_rename_dialog_free(dialog);

    if (callback) {
        callback(old_paths, new_paths, callback_data);
    }
}

void
fsearch_rename_dialog_run(GtkWindow *parent_window, GPtrArray *paths, FsearchRenameDialogResponse callback, gpointer data) {
    g_return_if_fail(paths);

    FsearchRenameDialog *dialog = calloc(1, sizeof(FsearchRenameDialog));
    g_assert(dialog);

    dialog->callback = callback;
    dialog->callback_data = data;
    dialog->paths = paths; // takes ownership

    dialog->builder = gtk_builder_new_from_resource("/io/github/cboxdoerfer/fsearch/ui/fsearch_rename_dialog.ui");
    dialog->dialog = GTK_WIDGET(gtk_builder_get_object(dialog->builder, "FsearchRenameDialogWindow"));
    gtk_window_set_transient_for(GTK_WINDOW(dialog->dialog), parent_window);

    gtk_dialog_add_button(GTK_DIALOG(dialog->dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
    dialog->ok_button = gtk_dialog_add_button(GTK_DIALOG(dialog->dialog), _("_OK"), GTK_RESPONSE_OK);
    g_signal_connect(dialog->dialog, "response", G_CALLBACK(on_rename_dialog_response), dialog);

    dialog->search_entry = GTK_ENTRY(gtk_builder_get_object(dialog->builder, "rename_search_entry"));
    dialog->replace_entry = GTK_ENTRY(gtk_builder_get_object(dialog->builder, "rename_replace_entry"));
    dialog->match_case = GTK_TOGGLE_BUTTON(gtk_builder_get_object(dialog->builder, "rename_match_case"));
    dialog->diacritics = GTK_TOGGLE_BUTTON(gtk_builder_get_object(dialog->builder, "rename_diacritics"));
    dialog->use_regex = GTK_TOGGLE_BUTTON(gtk_builder_get_object(dialog->builder, "rename_regex"));
    dialog->ignore_extension = GTK_TOGGLE_BUTTON(gtk_builder_get_object(dialog->builder, "rename_ignore_extension"));
    dialog->old_view = GTK_TREE_VIEW(gtk_builder_get_object(dialog->builder, "rename_old_view"));
    dialog->new_view = GTK_TREE_VIEW(gtk_builder_get_object(dialog->builder, "rename_new_view"));
    dialog->old_store = GTK_LIST_STORE(gtk_builder_get_object(dialog->builder, "rename_old_store"));
    dialog->new_store = GTK_LIST_STORE(gtk_builder_get_object(dialog->builder, "rename_new_store"));
    dialog->preset_combo = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(dialog->builder, "rename_preset_combo"));
    dialog->preset_save_button = GTK_WIDGET(gtk_builder_get_object(dialog->builder, "rename_preset_save_button"));
    dialog->preset_delete_button = GTK_WIDGET(gtk_builder_get_object(dialog->builder, "rename_preset_delete_button"));

    g_signal_connect(dialog->search_entry, "changed", G_CALLBACK(on_rename_option_changed), dialog);
    g_signal_connect(dialog->replace_entry, "changed", G_CALLBACK(on_rename_option_changed), dialog);
    g_signal_connect(dialog->match_case, "toggled", G_CALLBACK(on_rename_option_toggled), dialog);
    g_signal_connect(dialog->diacritics, "toggled", G_CALLBACK(on_rename_option_toggled), dialog);
    g_signal_connect(dialog->use_regex, "toggled", G_CALLBACK(on_rename_option_toggled), dialog);
    g_signal_connect(dialog->ignore_extension, "toggled", G_CALLBACK(on_rename_option_toggled), dialog);
    g_signal_connect(dialog->preset_combo, "changed", G_CALLBACK(on_rename_preset_combo_changed), dialog);
    g_signal_connect(dialog->preset_save_button, "clicked", G_CALLBACK(on_rename_preset_save_clicked), dialog);
    g_signal_connect(dialog->preset_delete_button, "clicked", G_CALLBACK(on_rename_preset_delete_clicked), dialog);

    // Fill the list of original names
    for (uint32_t i = 0; i < dialog->paths->len; ++i) {
        g_autofree char *name = g_path_get_basename(g_ptr_array_index(dialog->paths, i));
        GtkTreeIter iter = {0};
        gtk_list_store_append(dialog->old_store, &iter);
        gtk_list_store_set(dialog->old_store, &iter, 0, name, -1);
    }

    refresh_preset_combo(dialog, NULL);
    rename_dialog_update_preview(dialog);

    gtk_widget_show(dialog->dialog);
}
