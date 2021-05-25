/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * GStreamer / NNStreamer subplugin "protobuf" to support tensor converter and
 * decoder
 * Copyright (C) 2020 Gichan Jang <gichan2.jang@samsung.com>
 */
/**
 * @file        nnstreamer_protobuf.cc
 * @date        16 June 2020
 * @brief       Protobuf util function for nnstreamer
 * @see         https://github.com/nnstreamer/nnstreamer
 * @author      Gichan Jang <gichan2.jang@samsung.com>
 * @bug         No known bugs except for NYI items
 *
 */
/**
 * Install protobuf
 * We assume that you use Ubuntu linux distribution.
 * You may simply download binary packages from PPA
 *
 * $ sudo apt-add-repository ppa:nnstreamer
 * $ sudo apt update
 * $ sudo apt install libprotobuf-dev libprotobuf-lite17 libprotobuf17
 * protobuf-compiler17
 */

#include <nnstreamer_log.h>
#include <nnstreamer_plugin_api.h>
#include "nnstreamer.pb.h" /* Generated by `protoc` */
#include "nnstreamer_protobuf.h"

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
GstFlowReturn
gst_tensor_decoder_protobuf (const GstTensorsConfig *config,
    const GstTensorMemory *input, GstBuffer *outbuf)
{
  GstMapInfo out_info;
  GstMemory *out_mem;
  size_t size, outbuf_size;
  nnstreamer::protobuf::Tensors tensors;
  nnstreamer::protobuf::Tensors::frame_rate *fr = NULL;
  guint num_tensors;

  if (!config || !input || !outbuf) {
    ml_loge ("NULL parameter is passed to tensor_decoder::protobuf");
    return GST_FLOW_ERROR;
  }

  num_tensors = config->info.num_tensors;
  if (num_tensors <= 0 || num_tensors > NNS_TENSOR_SIZE_LIMIT) {
    ml_loge ("The number of input tenosrs "
             "exceeds more than NNS_TENSOR_SIZE_LIMIT, %s",
        NNS_TENSOR_SIZE_LIMIT_STR);
    return GST_FLOW_ERROR;
  }
  tensors.set_num_tensor (num_tensors);

  fr = tensors.mutable_fr ();
  if (!fr) {
    nns_loge ("Failed to get pointer of tensors / tensordec-protobuf");
    return GST_FLOW_ERROR;
  }

  fr->set_rate_n (config->rate_n);
  fr->set_rate_d (config->rate_d);

  for (unsigned int i = 0; i < num_tensors; ++i) {
    nnstreamer::protobuf::Tensor *tensor = tensors.add_tensor ();
    gchar *name = NULL;

    name = config->info.info[i].name;
    if (name == NULL) {
      tensor->set_name ("");
    } else {
      tensor->set_name (name);
    }

    tensor->set_type (
        (nnstreamer::protobuf::Tensor::Tensor_type)config->info.info[i].type);

    for (int j = 0; j < NNS_TENSOR_RANK_LIMIT; ++j) {
      tensor->add_dimension (config->info.info[i].dimension[j]);
    }

    tensor->set_data (input[i].data, (int)input[i].size);
  }

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

  if (!gst_memory_map (out_mem, &out_info, GST_MAP_WRITE)) {
    nns_loge ("Cannot map output memory / tensordec-protobuf");
    gst_memory_unref (out_mem);
    return GST_FLOW_ERROR;
  }

  tensors.SerializeToArray (out_info.data, size);

  gst_memory_unmap (out_mem, &out_info);

  if (outbuf_size == 0)
    gst_buffer_append_memory (outbuf, out_mem);
  else
    gst_memory_unref (out_mem);

  return GST_FLOW_OK;
}

/** @brief tensor converter plugin's NNStreamerExternalConverter callback */
GstBuffer *
gst_tensor_converter_protobuf (GstBuffer *in_buf, GstTensorsConfig *config, void *priv_data)
{
  nnstreamer::protobuf::Tensors tensors;
  nnstreamer::protobuf::Tensors::frame_rate *fr = NULL;
  GstMemory *in_mem, *out_mem;
  GstMapInfo in_info;
  GstBuffer *out_buf;
  guint mem_size;
  gpointer mem_data;

  if (!in_buf || !config) {
    ml_loge ("NULL parameter is passed to tensor_converter::protobuf");
    return NULL;
  }

  in_mem = gst_buffer_peek_memory (in_buf, 0);

  if (!gst_memory_map (in_mem, &in_info, GST_MAP_READ)) {
    nns_loge ("Cannot map input memory / tensor_converter_protobuf");
    return NULL;
  }

  tensors.ParseFromArray (in_info.data, in_info.size);

  config->info.num_tensors = tensors.num_tensor ();
  fr = tensors.mutable_fr ();
  config->rate_n = fr->rate_n ();
  config->rate_d = fr->rate_d ();
  out_buf = gst_buffer_new ();

  for (guint i = 0; i < config->info.num_tensors; i++) {
    const nnstreamer::protobuf::Tensor *tensor = &tensors.tensor (i);
    std::string _name = tensor->name ();
    const gchar *name = _name.c_str ();

    config->info.info[i].name = (name && strlen (name) > 0) ? g_strdup (name) : NULL;
    config->info.info[i].type = (tensor_type)tensor->type ();
    for (guint j = 0; j < NNS_TENSOR_RANK_LIMIT; j++) {
      config->info.info[i].dimension[j] = tensor->dimension (j);
    }
    mem_size = tensor->data ().length ();
    mem_data = g_memdup (tensor->data ().c_str (), mem_size);

    out_mem = gst_memory_new_wrapped (
        GST_MEMORY_FLAG_READONLY, mem_data, mem_size, 0, mem_size, NULL, NULL);

    gst_buffer_append_memory (out_buf, out_mem);
  }

  /** copy timestamps */
  gst_buffer_copy_into (
      out_buf, in_buf, (GstBufferCopyFlags)GST_BUFFER_COPY_METADATA, 0, -1);
  gst_memory_unmap (in_mem, &in_info);

  return out_buf;
}
