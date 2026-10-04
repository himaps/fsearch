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

#define G_LOG_DOMAIN "fsearch-rename-engine"

#include "fsearch_rename_engine.h"

#include <stdlib.h>
#include <string.h>

struct FsearchRenameEngine {
    FsearchRenameOptions options;
    GRegex *regex;      // regex mode: the compiled search expression
    char *search;       // literal mode: the search expression
};

// Expands the replace expression: `##` becomes a literal `#`, a single `#`
// becomes the per item counter and in regex mode `\0` to `\9` become match
// back references.
static void
expand_replacement(const char *replace, GString *result, GMatchInfo *match_info, int64_t counter) {
    if (!replace) {
        return;
    }

    for (const char *p = replace; *p != '\0'; p++) {
        if (*p == '#') {
            if (p[1] == '#') {
                g_string_append_c(result, '#');
                p++;
            }
            else {
                g_string_append_printf(result, "%" G_GINT64_FORMAT, counter);
            }
        }
        else if (match_info && *p == '\\' && p[1] >= '0' && p[1] <= '9') {
            gint group = p[1] - '0';
            g_autofree char *group_match = g_match_info_fetch(match_info, group);
            if (group_match) {
                g_string_append(result, group_match);
            }
            p++;
        }
        else {
            g_string_append_c(result, *p);
        }
    }
}

// Folds a single character: decomposes it and drops combining marks, so that
// `é` and `e` compare equal when diacritic matching is enabled.
static gunichar
fold_diacritics(gunichar c) {
    gchar utf8_char[8] = {0};
    if (g_unichar_to_utf8(c, utf8_char) == 0) {
        return c;
    }

    g_autofree char *decomposed = g_utf8_normalize(utf8_char, -1, G_NORMALIZE_ALL);
    if (!decomposed) {
        return c;
    }

    for (const char *p = decomposed; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar decomposed_char = g_utf8_get_char(p);
        GUnicodeType type = g_unichar_type(decomposed_char);
        if (type != G_UNICODE_NON_SPACING_MARK && type != G_UNICODE_COMBINING_MARK
            && type != G_UNICODE_ENCLOSING_MARK) {
            return g_unichar_tolower(decomposed_char);
        }
    }
    return g_unichar_tolower(c);
}

// Returns the byte offset of the next (non-overlapping) match of `needle` in
// `haystack` at or after `start_offset`, or -1 when there is no match. On match
// `match_end` receives the byte offset just after the match inside `haystack`.
static int64_t
find_literal_match(const char *haystack,
                   const char *needle,
                   bool match_case,
                   bool diacritics,
                   size_t start_offset,
                   size_t *match_end) {
    const char *h = haystack + start_offset;
    while (*h != '\0') {
        const char *h_probe = h;
        const char *n = needle;
        bool matched = true;

        while (*n != '\0') {
            if (*h_probe == '\0') {
                matched = false;
                break;
            }
            gunichar h_char = g_utf8_get_char(h_probe);
            gunichar n_char = g_utf8_get_char(n);

            if (diacritics) {
                h_char = fold_diacritics(h_char);
                n_char = fold_diacritics(n_char);
            }
            if (!match_case) {
                h_char = g_unichar_tolower(h_char);
                n_char = g_unichar_tolower(n_char);
            }
            if (h_char != n_char) {
                matched = false;
                break;
            }
            h_probe = g_utf8_next_char(h_probe);
            n = g_utf8_next_char(n);
        }

        if (matched) {
            *match_end = (size_t)(h_probe - haystack);
            return h - haystack;
        }
        h = g_utf8_next_char(h);
    }
    return -1;
}

static char *
literal_replace(const char *base, FsearchRenameEngine *engine, int64_t counter) {
    const char *needle = engine->search;
    const char *replace = engine->options.replace ? engine->options.replace : "";

    if (!needle || needle[0] == '\0') {
        return g_strdup(base);
    }

    GString *result = g_string_new(NULL);
    size_t copy_offset = 0;

    int64_t match_offset = 0;
    size_t match_end = 0;
    while ((match_offset = find_literal_match(base,
                                              needle,
                                              engine->options.match_case,
                                              engine->options.diacritics,
                                              copy_offset,
                                              &match_end)) >= 0) {
        g_string_append_len(result, base + copy_offset, match_offset - copy_offset);
        expand_replacement(replace, result, NULL, counter);
        copy_offset = match_end;
    }
    g_string_append(result, base + copy_offset);

    return g_string_free(result, FALSE);
}

typedef struct {
    FsearchRenameEngine *engine;
    int64_t counter;
    bool failed;
} RegexReplaceContext;

static gboolean
regex_replace_eval_cb(const GMatchInfo *match_info, GString *result, gpointer user_data) {
    RegexReplaceContext *ctx = user_data;
    expand_replacement(ctx->engine->options.replace ? ctx->engine->options.replace : "", result, (GMatchInfo *)match_info, ctx->counter);
    return FALSE; // continue replacing
}

// Splits a filename into base name and extension. Mirrors the behaviour of
// fsearch_string_get_extension(): hidden files (`.foo`) and files ending in a
// dot have no extension.
static void
split_extension(const char *name, char **base, char **extension) {
    const char *ext = strrchr(name, '.');
    if (!ext || ext == name || ext[1] == '\0') {
        *base = g_strdup(name);
        *extension = g_strdup("");
    }
    else {
        *base = g_strndup(name, ext - name);
        *extension = g_strdup(ext + 1);
    }
}

char *
fsearch_rename_engine_apply(FsearchRenameEngine *engine, const char *old_name, int64_t counter) {
    g_return_val_if_fail(engine, NULL);
    g_return_val_if_fail(old_name && old_name[0] != '\0', NULL);

    char *base = NULL;
    char *extension = NULL;
    if (engine->options.ignore_extension) {
        split_extension(old_name, &base, &extension);
    }
    else {
        base = g_strdup(old_name);
        extension = g_strdup("");
    }

    char *new_base = NULL;
    if (engine->options.use_regex) {
        if (!engine->regex) {
            new_base = g_strdup(base);
        }
        else {
            RegexReplaceContext ctx = {.engine = engine, .counter = counter, .failed = false};
            GError *error = NULL;
            new_base = g_regex_replace_eval(engine->regex,
                                            base,
                                            -1,
                                            0,
                                            0,
                                            regex_replace_eval_cb,
                                            &ctx,
                                            &error);
            if (error) {
                g_debug("[rename-engine] regex replace failed: %s", error->message);
                g_clear_pointer(&new_base, g_free);
                new_base = g_strdup(base);
                g_clear_error(&error);
            }
        }
    }
    else {
        new_base = literal_replace(base, engine, counter);
    }

    GString *result = g_string_new(new_base);
    if (extension && extension[0] != '\0') {
        g_string_append_c(result, '.');
        g_string_append(result, extension);
    }

    g_free(base);
    g_free(extension);
    g_free(new_base);

    return g_string_free(result, FALSE);
}

FsearchRenameStatus
fsearch_rename_engine_check(const char *new_name) {
    if (!new_name || new_name[0] == '\0') {
        return FSEARCH_RENAME_STATUS_EMPTY;
    }
    if (strchr(new_name, '/')) {
        return FSEARCH_RENAME_STATUS_INVALID_CHARS;
    }
    for (const char *p = new_name; *p != '\0'; p++) {
        if ((guchar)*p < 0x20) {
            return FSEARCH_RENAME_STATUS_INVALID_CHARS;
        }
    }
    return FSEARCH_RENAME_STATUS_OK;
}

FsearchRenameEngine *
fsearch_rename_engine_new(const FsearchRenameOptions *options, GError **error) {
    g_return_val_if_fail(options, NULL);
    g_return_val_if_fail(!error || !*error, NULL);

    FsearchRenameEngine *engine = calloc(1, sizeof(FsearchRenameEngine));
    g_assert(engine);

    engine->options = *options;
    engine->options.search = g_strdup(options->search);
    engine->options.replace = g_strdup(options->replace);
    engine->search = g_strdup(options->search);

    if (options->use_regex && options->search && options->search[0] != '\0') {
        GRegexCompileFlags flags = options->match_case ? 0 : G_REGEX_CASELESS;
        engine->regex = g_regex_new(options->search, flags, 0, error);
        if (!engine->regex) {
            fsearch_rename_engine_free(engine);
            return NULL;
        }
    }

    return engine;
}

FsearchRenamePreset *
fsearch_rename_preset_new(const char *name,
                          const char *search,
                          const char *replace,
                          bool match_case,
                          bool diacritics,
                          bool use_regex,
                          bool ignore_extension,
                          int64_t counter_start) {
    FsearchRenamePreset *preset = calloc(1, sizeof(FsearchRenamePreset));
    g_assert(preset);

    preset->name = g_strdup(name ? name : "");
    preset->options.search = g_strdup(search ? search : "");
    preset->options.replace = g_strdup(replace ? replace : "");
    preset->options.match_case = match_case;
    preset->options.diacritics = diacritics;
    preset->options.use_regex = use_regex;
    preset->options.ignore_extension = ignore_extension;
    preset->options.counter_start = counter_start;

    return preset;
}

FsearchRenamePreset *
fsearch_rename_preset_copy(FsearchRenamePreset *preset) {
    g_return_val_if_fail(preset, NULL);

    return fsearch_rename_preset_new(preset->name,
                                     preset->options.search,
                                     preset->options.replace,
                                     preset->options.match_case,
                                     preset->options.diacritics,
                                     preset->options.use_regex,
                                     preset->options.ignore_extension,
                                     preset->options.counter_start);
}

void
fsearch_rename_preset_free(FsearchRenamePreset *preset) {
    if (!preset) {
        return;
    }
    g_clear_pointer(&preset->name, g_free);
    g_clear_pointer(&preset->options.search, g_free);
    g_clear_pointer(&preset->options.replace, g_free);
    g_clear_pointer(&preset, free);
}

void
fsearch_rename_engine_free(FsearchRenameEngine *engine) {
    if (!engine) {
        return;
    }
    g_clear_pointer(&engine->options.search, g_free);
    g_clear_pointer(&engine->options.replace, g_free);
    g_clear_pointer(&engine->search, g_free);
    g_clear_pointer(&engine->regex, g_regex_unref);
    g_clear_pointer(&engine, free);
}
