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

#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct FsearchRenameOptions {
    char *search;
    char *replace;
    bool match_case;
    bool diacritics;
    bool use_regex;
    bool ignore_extension;
    int64_t counter_start;
} FsearchRenameOptions;

typedef enum FsearchRenameStatus {
    FSEARCH_RENAME_STATUS_OK = 0,
    FSEARCH_RENAME_STATUS_UNCHANGED = 1 << 0,
    FSEARCH_RENAME_STATUS_EMPTY = 1 << 1,
    FSEARCH_RENAME_STATUS_INVALID_CHARS = 1 << 2,
} FsearchRenameStatus;

typedef struct FsearchRenameEngine FsearchRenameEngine;

/// A named set of rename options, persisted in fsearch.conf
typedef struct FsearchRenamePreset {
    char *name;
    FsearchRenameOptions options;
} FsearchRenamePreset;

FsearchRenamePreset *
fsearch_rename_preset_new(const char *name,
                          const char *search,
                          const char *replace,
                          bool match_case,
                          bool diacritics,
                          bool use_regex,
                          bool ignore_extension,
                          int64_t counter_start);

FsearchRenamePreset *
fsearch_rename_preset_copy(FsearchRenamePreset *preset);

void
fsearch_rename_preset_free(FsearchRenamePreset *preset);

/// Compiles the expressions in `options`. Returns NULL with `error` set when the
/// regular expression is invalid.
FsearchRenameEngine *
fsearch_rename_engine_new(const FsearchRenameOptions *options, GError **error);

void
fsearch_rename_engine_free(FsearchRenameEngine *engine);

/// Applies the search and replace expressions to `old_name`.
/// `counter` is the value every `#` inside the replace expression expands to.
/// Returns a newly allocated string, which is `old_name` unchanged when nothing
/// matches or the search expression is empty.
char *
fsearch_rename_engine_apply(FsearchRenameEngine *engine, const char *old_name, int64_t counter);

/// Checks a computed new name for validity. Never returns an error for names
/// which are merely unchanged.
FsearchRenameStatus
fsearch_rename_engine_check(const char *new_name);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchRenameEngine, fsearch_rename_engine_free)
