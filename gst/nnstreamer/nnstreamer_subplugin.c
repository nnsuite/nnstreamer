/**
 * NNStreamer Subplugin Manager
 * Copyright (C) 2018 MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 */
/**
 * @file	nnstreamer_subplugin.c
 * @date	27 Nov 2018
 * @brief	Subplugin Manager for NNStreamer
 * @see		http://github.com/nnsuite/nnstreamer
 * @author	MyungJoo Ham <myungjoo.ham@samsung.com>
 * @bug		No known bugs except for NYI items
 *
 */

#include <dlfcn.h>
#include <glib.h>
#include <gmodule.h>
#include <gst/gstinfo.h>
#include "nnstreamer_subplugin.h"
#include "nnstreamer_conf.h"

#include <stdio.h>

typedef struct
{
  char *name; /**< The name of subplugin */
  const void *data; /**< subplugin specific data forwarded from the subplugin */
  void *handle; /**< dlopen'ed handle */
} subpluginData;

static GHashTable *subplugins[NNS_SUBPLUGIN_END] = { 0 };

G_LOCK_DEFINE_STATIC (splock);


typedef struct
{
  char *name;
  const void *data;
} holdplugins;
static GHashTable *held_subplugins[NNS_SUBPLUGIN_END] = { 0 };

static void
_heldsp_destroy (gpointer _data)
{
  holdplugins *data = _data;
  g_free (data->name);
  /* do not free data here */
  g_free (data);
}

void
hold_register_subplugin (subpluginType type, const char *name, void *data)
{
  if (held_subplugins[type] == NULL)
    held_subplugins[type] =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        _heldsp_destroy);
  g_hash_table_insert (held_subplugins[type], g_strdup (name), data);
}

static const void *
check_held_subplugin (subpluginType type, const char *name)
{
  if (held_subplugins[type] == NULL)
    return NULL;
  return g_hash_table_lookup (held_subplugins[type], name);
}

/** @brief Private function for g_hash_table data destructor, GDestroyNotify */
static void
_spdata_destroy (gpointer _data)
{
  subpluginData *data = _data;
  g_free (data->name);
  if (data->handle)
    dlclose (data->handle);
  g_free (data);
}

/** @brief Public function defined in the header */
const void *
get_subplugin (subpluginType type, const char *name)
{
  GHashTable *table;
  subpluginData *data;
  void *handle;

  nnsconf_loadconf (FALSE);

  G_LOCK (splock);

  if (subplugins[type] == NULL)
    subplugins[type] =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        _spdata_destroy);

  table = subplugins[type];
  data = g_hash_table_lookup (table, name);

  if (data == NULL) {
    /* Search and register if found with the conf */
    const gchar *fullpath = nnsconf_get_fullpath (name, type);
    char *dlsym_error;
    const void *held;
    nnstreamer_subplugin_data *nsdata;

    fprintf (stderr, "Trying to dlopen %s\n", name);

    if (fullpath == NULL) {
      fprintf (stderr, "Cannot get full path from %s / type %d\n", name, type);
      goto error;               /* No Such Thing !!! */
    }

    handle = dlopen (fullpath, RTLD_NOW);
    if (NULL == handle) {
      GST_ERROR ("Cannot dlopen %s (%s).", name, fullpath);
      goto error;
    }

    /**
     * @todo
     * If a plugin calls "probe()" at this step, stop here and return "OK"
     */
    held = check_held_subplugin (type, name);
    if (held) {
      fprintf (stderr,
          "[%s] is already registered by probe(). Skipping registering process.\n",
          name);
      G_UNLOCK (splock);
      return held;
    }

    nsdata = (nnstreamer_subplugin_data *)
        dlsym (handle, "nnstreamer_subplugin");
    dlsym_error = dlerror ();

    if (NULL == nsdata) {
      GST_ERROR ("nnstreamer_subplugin does not exists in %s (%s)", name,
          fullpath);
      goto error_handle;
    }
    if (dlsym_error) {
      GST_ERROR ("Loading nnstreamer_subplugin in %s (%s) incurs: %s", name,
          fullpath, dlsym_error);
      goto error_handle;
    }

    if (nsdata->checker != NNS_SUBPLUGIN_CHECKER) {
      GST_ERROR ("nnstreamer_subplugin of %s (%s) is broken (first bytes)",
          name, fullpath);
      goto error_handle;
    }

    if (nsdata->type != type) {
      GST_ERROR ("nnstreamer_subplugin of %s (%s) is broken (type mismatch)",
          name, fullpath);
      goto error_handle;
    }

    if (g_strcmp0 (nsdata->name, name)) {
      GST_ERROR ("nnstreamer_subplugin of %s (%s) is broken (name mismatch)",
          name, fullpath);
      goto error_handle;
    }

    g_assert (TRUE == register_subplugin (type, name, nsdata->data));
    g_assert ((data = g_hash_table_lookup (table, name)) != NULL);
    data->handle = handle;

    fprintf (stderr, "\n\nDLOPEN \"%s\", found at %s. Successful.\n\n", name,
        fullpath);
  }
  G_UNLOCK (splock);

  fprintf (stderr, "dlopening completed or skipped.\n");
  return data->data;

error_handle:
  dlclose (handle);
error:
  G_UNLOCK (splock);
  fprintf (stderr, "\n\nDLOPEN FAILED on \"%s\".\n\n", name);
  return NULL;
}

/** @brief Public function defined in the header */
gboolean
register_subplugin (subpluginType type, const char *name, const void *data)
{
  /** @todo data out of scope at add */
  subpluginData *spdata = g_new (subpluginData, 1);
  gboolean ret;

  spdata->name = g_strdup (name);
  spdata->data = data;
  spdata->handle = NULL;

  if (subplugins[type] == NULL)
    subplugins[type] =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        _spdata_destroy);

  G_LOCK (splock);
  ret = g_hash_table_insert (subplugins[type], g_strdup (name), spdata);
  G_UNLOCK (splock);

  return ret;
}

/** @brief Public function defined in the header */
gboolean
unregister_subplugin (subpluginType type, const char *name)
{
  gboolean ret;
  if (subplugins[type] == NULL)
    return FALSE;

  G_LOCK (splock);
  ret = g_hash_table_remove (subplugins[type], name);
  G_UNLOCK (splock);

  return ret;
}
