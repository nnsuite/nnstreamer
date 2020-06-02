/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * GStreamer / NNStreamer tensor_converter subplugin, "Flatbuffer"
 * Copyright (C) 2020 Gichan Jang <gichan2.jang@samsung.com>
 */
 /**

 * @file        tensor_converter_flatbuf.cc
 * @date        14 May 2020
 * @brief       NNStreamer tensor-converter subplugin, "flatbuffer",
 *              which converts flatbuufers byte stream to tensors.
 * @see         https://github.com/nnsuite/nnstreamer
 * @author      Gichan Jang <gichan2.jang@samsung.com>
 * @bug         No known bugs except for NYI items
 *
 */

 /**
  * Install flatbuffers
  * We assume that you use Ubuntu linux distribution.
  * You may simply download binary packages from PPA
  *
  * $ sudo apt-add-repository ppa:nnstreamer
  * $ sudo apt update
  * $ sudo apt install libflatbuffers libflatbuffers-dev flatbuffers-compiler
  */

#include <iostream>
#include <fstream>
#include <typeinfo>
#include <glib.h>
#include <gst/gstinfo.h>
#include <nnstreamer_plugin_api.h>
#include "nnstreamer_plugin_api_converter.h"
#include <nnstreamer_log.h>
#include <nnstreamer_generated.h>    // Generated by `flatc`.

using namespace NNStreamer;
using namespace flatbuffers;
void init_fbc (void) __attribute__ ((constructor));
void fini_fbc (void) __attribute__ ((destructor));

/** @brief tensor converter plugin's NNStreamerExternalConverter callback */
static GstCaps *
fbc_query_caps (const GstTensorsConfig * config)
{
  return gst_caps_from_string (GST_FLATBUF_TENSOR_CAP_DEFAULT);
}

/** @brief tensor converter plugin's NNStreamerExternalConverter callback */
static gboolean
fbc_get_out_config (const GstCaps * in_cap, GstTensorsConfig * config)
{
  GstStructure *structure;
  g_return_val_if_fail (config != NULL, FALSE);
  gst_tensors_config_init (config);
  g_return_val_if_fail (in_cap != NULL, FALSE);

  structure = gst_caps_get_structure (in_cap, 0);
  g_return_val_if_fail (structure != NULL, FALSE);

  /* All tensor info should be updated later in chain function. */
  config->info.info[0].type = _NNS_UINT8;
  config->info.num_tensors = 1;
  if (gst_tensor_parse_dimension ("1:1:1:1", 
        config->info.info[0].dimension) == 0) {
    ml_loge ("Failed to set initial dimension for subplugin");
    return FALSE;
  }

  if (gst_structure_has_field (structure, "framerate")) {
    gst_structure_get_fraction (structure, "framerate", &config->rate_n,
        &config->rate_d);
  } else {
    /* cannot get the framerate */
    config->rate_n = 0;
    config->rate_d = 1;
  }
  return TRUE;
}

/** @brief tensor converter plugin's NNStreamerExternalConverter callback
 *  @todo : Consider multi frames, return Bufferlist and 
 *          remove frame size and the number of frames
 */
static GstBuffer *
fbc_convert (GstBuffer * in_buf, gsize * frame_size, guint * frames_in,
    GstTensorsConfig * config)
{
  const Tensors *tensors;
  const Vector < Offset < Tensor >> *tensor;
  const Vector < unsigned char >*tensor_data;
  frame_rate fr;
  GstBuffer *out_buf;
  GstMemory *in_mem, *out_mem;
  GstMapInfo in_info;
  guint mem_size;
  gpointer mem_data;

  in_mem = gst_buffer_peek_memory (in_buf, 0);
  g_assert (gst_memory_map (in_mem, &in_info, GST_MAP_READ));
  g_critical ("buffer size : %lu", gst_buffer_get_size (in_buf));
  tensors = GetTensors (in_info.data);
  g_assert (tensors);

  config->info.num_tensors = tensors->num_tensor ();
  config->rate_n = tensors->fr ()->rate_n ();
  config->rate_d = tensors->fr ()->rate_d ();

  tensor = tensors->tensor ();

  out_buf = gst_buffer_new ();

  for (guint i = 0; i < config->info.num_tensors; i++) {
    config->info.info[i].name =
        g_strdup (tensor->Get (i)->name ()->str ().c_str ());
    config->info.info[i].type = (tensor_type) tensor->Get (i)->type ();
    tensor_data = tensor->Get (i)->data ();

    for (guint j = 0; j < NNS_TENSOR_RANK_LIMIT; j++) {
      config->info.info[i].dimension[j] =
          tensor->Get (i)->dimension ()->Get (j);
    }
    *frame_size = mem_size = VectorLength (tensor_data);
    *frames_in = 1;

    mem_data = g_memdup (tensor_data->data (), mem_size);

    out_mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
        mem_data, mem_size, 0, mem_size, NULL, NULL);

    gst_buffer_append_memory (out_buf, out_mem);      
  }

  /** copy timestamps */
  gst_buffer_copy_into (out_buf, in_buf, (GstBufferCopyFlags) GST_BUFFER_COPY_METADATA, 0, -1);
  gst_memory_unmap (in_mem, &in_info);

  return out_buf;
}

static gchar converter_subplugin_flatbuf[] = "flatbuf";

/** @brief flatbuffer tensor converter sub-plugin NNStreamerExternalConverter instance */
static NNStreamerExternalConverter flatBuf = {
  .media_type_name = converter_subplugin_flatbuf,
  .convert = fbc_convert,
  .get_out_config = fbc_get_out_config,
  .query_caps = fbc_query_caps
};

/** @brief Initialize this object for tensor converter sub-plugin */
void
init_fbc (void)
{
  registerExternalConverter (&flatBuf);
}

/** @brief Destruct this object for tensor converter sub-plugin */
void
fini_fbc (void)
{
  unregisterExternalConverter (flatBuf.media_type_name);
}
