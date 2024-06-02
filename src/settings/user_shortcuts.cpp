/*
 * SPDX-FileCopyrightText: © 2021 Alexandros Theodotou <alex@zrythm.org>
 *
 * SPDX-License-Identifier: LicenseRef-ZrythmLicense
 */

#include "settings/user_shortcuts.h"
#include "utils/file.h"
#include "utils/objects.h"
#include "utils/string.h"
#include "zrythm.h"

void
user_shortcut_free (UserShortcut * shortcut)
{
  g_free_and_null (shortcut->action);
  g_free_and_null (shortcut->primary);
  g_free_and_null (shortcut->secondary);

  object_zero_and_free (shortcut);
}

#if 0
static char *
get_user_shortcuts_file_path (void)
{
  char * zrythm_dir = ZRYTHM->get_dir(USER_TOP);
  g_return_val_if_fail (zrythm_dir, NULL);

  return g_build_filename (zrythm_dir, "shortcuts.yaml", NULL);
}

static bool
is_yaml_our_version (const char * yaml)
{
  bool same_version = false;
  char version_str[120];
  sprintf (version_str, "schema_version: %d\n", USER_SHORTCUTS_SCHEMA_VERSION);
  same_version = g_str_has_prefix (yaml, version_str);
  if (!same_version)
    {
      sprintf (
        version_str, "---\nschema_version: %d\n", USER_SHORTCUTS_SCHEMA_VERSION);
      same_version = g_str_has_prefix (yaml, version_str);
    }

  return same_version;
}
#endif

/**
 * Reads the file and fills up the object.
 */
UserShortcuts *
user_shortcuts_new (void)
{
  UserShortcuts * self = object_new (UserShortcuts);
  return self;

#if 0
  GError * err = NULL;
  char *   path = get_user_shortcuts_file_path ();
  if (!file_exists (path))
    {
      g_message ("User shortcuts file at %s does not exist", path);
return_new_instance:
      g_free (path);
      UserShortcuts * self = object_new (UserShortcuts);
      return self;
    }
  char * yaml = NULL;
  g_file_get_contents (path, &yaml, NULL, &err);
  if (err != NULL)
    {
      g_warning (
        "Failed to create UserShortcuts "
        "from %s",
        path);
      g_free (err);
      g_free (yaml);
      goto return_new_instance;
    }

  /* if not same version, purge file and return
   * a new instance */
  if (!is_yaml_our_version (yaml))
    {
      g_message (
        "Found old user shortcuts file version. "
        "Purging file and creating a new one.");
      g_free (yaml);
      goto return_new_instance;
    }

  UserShortcuts * self =
    (UserShortcuts *) yaml_deserialize (yaml, &user_shortcuts_schema, &err);
  if (!self)
    {
      g_warning (
        "Failed to deserialize "
        "UserShortcuts from %s: \n%s",
        path, err->message);
      g_error_free (err);
      g_free (yaml);
      goto return_new_instance;
    }
  g_free (yaml);
  g_free (path);

  return self;
#endif
}

/**
 * Returns a shortcut for the given action, or @p
 * default_shortcut if not found.
 *
 * @param primary Whether to get the primary
 *   shortcut, otherwise the secondary.
 */
const char *
user_shortcuts_get (
  UserShortcuts * self,
  bool            primary,
  const char *    action,
  const char *    default_shortcut)
{
  for (int i = 0; i < self->num_shortcuts; i++)
    {
      UserShortcut * cur_shortcut = self->shortcuts[i];
      if (string_is_equal (cur_shortcut->action, action))
        {
          if (primary)
            return cur_shortcut->primary;
          else
            return cur_shortcut->secondary;
        }
    }

  return default_shortcut;
}

void
user_shortcuts_free (UserShortcuts * self)
{
  for (int i = 0; i < self->num_shortcuts; i++)
    {
      object_free_w_func_and_null (user_shortcut_free, self->shortcuts[i]);
    }

  object_zero_and_free (self);
}
