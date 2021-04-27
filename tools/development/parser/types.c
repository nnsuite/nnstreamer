/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * GStreamer Pipeline from/to PBTxt Converter Parser
 * Copyright (C) 2020 MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * @file  types.c
 * @date  12 Nov 2020
 * @brief Simplified Gstreamer's internal functions for gst2pbtxt parser (nnstreamer parser)
 * @see https://github.com/nnstreamer/nnstreamer
 * @author  MyungJoo Ham <myungjoo.ham@samsung.com>
 * @bug No known bugs except for NYI items
 */
#include <glib.h>
#include "types.h"

/**
 * @brief Get the error quark used by the parsing subsystem.
 *
 * Returns: the quark of the parse errors.
 */
GQuark
gst2pbtxt_parse_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gst_parse_error");
  return quark;
}

/** @brief Internal function to construct _Element */
static void
_nnstparser_config_element (_Element * e, const gchar * element,
    const gchar * name)
{
  e->specialType = eST_normal;
  e->element = g_strdup (element);
  e->name = g_strdup (name);
  e->refcount = 1;
  e->id = oTI_Element;
}

/**
 * @brief Replacement of gst_parse_element_make
 */
_Element *
nnstparser_element_make (const gchar * element, const gchar * name)
{
  _Element *ret = g_new0 (_Element, 1);
  _nnstparser_config_element (ret, element, name);
  return ret;
}

/**
 * @brief Replacement of gst_parse_element_make
 */
_Element *
nnstparser_gstbin_make (const gchar * element, const gchar * name)
{
  _Element *ret = g_new0 (_Element, 1);
  _nnstparser_config_element (ret, element, name);
  ret->id = oTI_GstBin;
  ret->elements = NULL;
  return ret;
}

/**
 * @brief Unref an element
 */
_Element *
nnstparser_element_unref (_Element * element)
{
  g_assert (element);

  if (element->refcount <= 0) {
    g_critical ("ERROR! Refcounter is broken: %s.", __func__);
  }
  g_assert (element->refcount > 0);

  element->refcount--;
  if (element->refcount <= 0) {
    g_free (element->element);
    g_free (element->name);
    g_free (element);
    return NULL;
  }

  return element;
}

/**
 * @brief Ref an element
 */
_Element *
nnstparser_element_ref (_Element * element)
{
  g_assert (element);

  if (element->refcount <= 0) {
    g_critical ("ERROR! Refcounter is broken: %s.", __func__);
  }
  g_assert (element->refcount > 0);

  element->refcount++;

  return element;
}

/**
 * @brief Create URL dummy element instead of gst_element_make_from_uri
 */
_Element *
nnstparser_element_from_uri (_URIType type, const gchar * uri,
    const gchar * elementname, void **error)
{
  _Element *ret = g_new0 (_Element, 1);

  g_assert (type == GST_URI_SINK || type == GST_URI_SRC);

  ret->specialType = (type == GST_URI_SINK) ? eST_URI_SINK : eST_URI_SRC;
  ret->element = g_strdup (uri);
  ret->name = g_strdup (elementname);
  ret->refcount = 1;
  return ret;
}

/**
 * @brief Substitutes GST's gst_element_link_pads_filtered ()
 */
gboolean
nnstparser_element_link_pads_filtered (_Element *src, const gchar *src_pad,
    _Element *dst, const gchar *dst_pad, gchar *filter)
{
  /* NYI */
}

/**
 * @brief Substitutes GST's gst_bin_get_by_name ()
 */
_Element *
nnstparser_bin_get_by_name (_Element * bin, const gchar * name)
{
  /* NYI */
}

/**
 * @brief Substitutes GST's gst_bin_get_by_name_recurse_up ()
 */
_Element *
nnstparser_bin_get_by_name_recurse_up (_Element * bin, const gchar * name)
{
  _Element *result;

  g_return_val_if_fail (__GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  result = nnstparser_bin_get_by_name (bin, name);

  if (!result) {
    /* NYI: May need to analyze gst object parent structure */
  }
}

/**
 * @brief Substitutes GST's gst_bin_add ()
 */
gboolean
nnstparser_bin_add (_Element * bin, _Element * element)
{
  g_return_val_if_fail (bin != NULL, FALSE);
  g_return_val_if_fail (__GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (element != NULL, FALSE);

  bin->elements = g_slist_append (bin->elements, element);

  return TRUE;
}
