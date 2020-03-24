/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * GStreamer / NNStreamer tensor_decoder subplugin, "protobuf"
 * Copyright (C) 2020 Yongjoo Ahn <yongjoo1.ahn@samsung.com>
 */
/**
 * @file        tensordec-protobuf.cc
 * @date        25 Mar 2020
 * @brief       NNStreamer tensor-decoder subplugin, "protobuf",
 *              which converts tensor or tensors to Protocol Buffers.
 * @see         https://github.com/nnstreamer/nnstreamer
 * @author      Yongjoo Ahn <yongjoo1.ahn@samsung.com>
 * @bug         No known bugs except for NYI items
 *
 */

#include <glib.h>
#include <gst/gstinfo.h>
#include <tensor_typedef.h>
#include <nnstreamer_plugin_api_decoder.h>
#include <nnstreamer_plugin_api.h>
#include <nnstreamer_log.h>

#include "nnstreamer.pb.h"    /* Generated by `protoc` */

void init_pb (void) __attribute__ ((constructor));
void fini_pb (void) __attribute__ ((destructor));

/**
 * @brief tensordec-plugin's GstTensorDecoderDef callback
 */
static int
pb_init (void **pdata)
{
  /**
   * Verify that the version of the library we linked is
   * compatibile with the headers.
   */
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  *pdata = NULL; /* no private data are needed for this sub-plugin */
  return TRUE;
}

/** 
 * @brief tensordec-plugin's GstTensorDecoderDef callback 
 */
static void
pb_exit (void **pdata)
{
  google::protobuf::ShutdownProtobufLibrary ();
  return;
}

/**
 * @brief tensordec-plugin's GstTensorDecoderDef callback
 */
static int
pb_setOption (void **pdata, int opNum, const char *param)
{
  return TRUE;
}

/**
 * @brief tensordec-plugin's GstTensorDecoderDef callback
 */
static GstCaps *
pb_getOutCaps (void **pdata, const GstTensorsConfig * config)
{
  return gst_caps_from_string (GST_PROTOBUF_TENSOR_CAP_DEFAULT);
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static GstFlowReturn
pb_decode (void **pdata, const GstTensorsConfig * config,
    const GstTensorMemory * input, GstBuffer * outbuf)
{
  GstMapInfo out_info;
  GstMemory *out_mem;
  size_t size, outbuf_size;

  NNStreamer::Tensors tensors;
  NNStreamer::Tensors::frame_rate *fr = NULL;

  const unsigned int num_tensors = config->info.num_tensors;
  g_assert (num_tensors > 0);
  tensors.set_num_tensor (num_tensors);

  fr = tensors.mutable_fr ();
  if (!fr) {
    nns_loge ("Failed to get pointer of tensors / tensordec-protobuf");
    return GST_FLOW_ERROR;
  }

  fr->set_rate_n (config->rate_n);
  fr->set_rate_d (config->rate_d);
  
  for (unsigned int i = 0; i < num_tensors; ++i) {
    NNStreamer::Tensor *tensor = tensors.add_tensor ();
    gchar *name = NULL;

    name = config->info.info[i].name;
    if (name == NULL) {
      tensor->set_name ("Anonymous");
    } else {
      tensor->set_name (name);
    }

    tensor->set_type ((NNStreamer::Tensor::Tensor_type)
                          config->info.info[i].type);

    for (int j = 0; j < NNS_TENSOR_RANK_LIMIT; ++j) {
      tensor->add_dimension (config->info.info[i].dimension[j]);
    }

    tensor->set_data (input[i].data, (int) input[i].size);
  }

  g_assert (outbuf);

  size = tensors.ByteSizeLong ();
  outbuf_size = gst_buffer_get_size (outbuf);
  
  if (outbuf_size == 0) {
    out_mem = gst_allocator_alloc (NULL, size, NULL);
  } else {
    if (outbuf_size < size) {
      gst_buffer_set_size (outbuf, size);
    }
    out_mem = gst_buffer_get_all_memory (outbuf);
  }

  if (FALSE == gst_memory_map (out_mem, &out_info, GST_MAP_WRITE)) {
    nns_loge ("Cannot map aoutput memory / tensordec-protobuf");
    return GST_FLOW_ERROR;
  }

  tensors.SerializeToArray (out_info.data, size);

  gst_memory_unmap (out_mem, &out_info);

  if (gst_buffer_get_size (outbuf) == 0) {
    gst_buffer_append_memory (outbuf, out_mem);
  }
  
  return GST_FLOW_OK;
}

static gchar decoder_subplugin_protobuf[] = "protobuf";

/**
 * @brief protocol buffers tensordec-plugin GstTensorDecoderDef instance
 */
static GstTensorDecoderDef protobuf = {
  .modename = decoder_subplugin_protobuf,
  .init = pb_init,
  .exit = pb_exit,
  .setOption = pb_setOption,
  .getOutCaps = pb_getOutCaps,
  .decode = pb_decode
};

/**
 * @brief Initialize this object for tensordec-plugin
 */
void
init_pb (void)
{
  nnstreamer_decoder_probe (&protobuf);
}

/** @brief Destruct this object for tensordec-plugin */
void
fini_pb (void)
{
  nnstreamer_decoder_exit (protobuf.modename);
}
