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

#include <gtk/gtk.h>

/// Invoked when the user confirms the rename dialog. Ownership of both arrays
/// (free functions: g_free) is transferred to the callback.
typedef void (*FsearchRenameDialogResponse)(GPtrArray *old_paths, GPtrArray *new_paths, gpointer user_data);

/// Shows the rename dialog for `paths` (GPtrArray of malloc'd full paths, sorted).
/// Takes ownership of `paths`.
void
fsearch_rename_dialog_run(GtkWindow *parent_window, GPtrArray *paths, FsearchRenameDialogResponse callback, gpointer data);
