/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * Copyright (C) 2021 Samsung Electronics Co., Ltd.
 *
 * @file    tensor_query_client.c
 * @date    09 Jul 2021
 * @brief   GStreamer plugin to handle tensor query client
 * @author  Junhwan Kim <jejudo.kim@samsung.com>
 * @see     http://github.com/nnstreamer/nnstreamer
 * @bug     No known bugs
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "tensor_query_client.h"
#include <gio/gio.h>
#include <gio/gsocket.h>

/**
 * @brief Macro for debug mode.
 */
#ifndef DBG
#define DBG (!self->silent)
#endif

/**
 * @brief Macro for debug message.
 */
#define silent_debug(...) do { \
    if (DBG) { \
      GST_DEBUG_OBJECT (self, __VA_ARGS__); \
    } \
  } while (0)

#define silent_debug_caps(caps,msg) do { \
  if (DBG) { \
    if (caps) { \
      GstStructure *caps_s; \
      gchar *caps_s_string; \
      guint caps_size, caps_idx; \
      caps_size = gst_caps_get_size (caps);\
      for (caps_idx = 0; caps_idx < caps_size; caps_idx++) { \
        caps_s = gst_caps_get_structure (caps, caps_idx); \
        caps_s_string = gst_structure_to_string (caps_s); \
        GST_DEBUG_OBJECT (self, msg " = %s\n", caps_s_string); \
        g_free (caps_s_string); \
      } \
    } \
  } \
} while (0)

/**
 * @brief Properties.
 */
enum
{
  PROP_0,
  PROP_SINK_HOST,
  PROP_SINK_PORT,
  PROP_SRC_HOST,
  PROP_SRC_PORT,
  PROP_SILENT,
};

#define TCP_HIGHEST_PORT        65535
#define TCP_DEFAULT_HOST        "localhost"
#define TCP_DEFAULT_SINK_PORT        3000
#define TCP_DEFAULT_SRC_PORT        3001
#define DEFAULT_SILENT TRUE

GST_DEBUG_CATEGORY_STATIC (gst_tensor_query_client_debug);
#define GST_CAT_DEFAULT gst_tensor_query_client_debug

/**
 * @brief Default caps string for pads.
 */
#define CAPS_STRING GST_TENSOR_CAP_DEFAULT ";" GST_TENSORS_CAP_DEFAULT ";" GST_TENSORS_FLEX_CAP_DEFAULT

/**
 * @brief the capabilities of the inputs.
 */
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STRING));

/**
 * @brief the capabilities of the outputs.
 */
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STRING));

#define gst_tensor_query_client_parent_class parent_class
G_DEFINE_TYPE (GstTensorQueryClient, gst_tensor_query_client,
    GST_TYPE_BASE_TRANSFORM);

/* GObject vmethod implementations */
static void gst_tensor_query_client_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_tensor_query_client_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_tensor_query_client_finalize (GObject * object);

/* GstBaseTransform vmethod implementations */
static GstFlowReturn gst_tensor_query_client_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static GstCaps *gst_tensor_query_client_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_tensor_query_client_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_tensor_query_client_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_tensor_query_client_start (GstBaseTransform * trans);
static gboolean gst_tensor_query_client_stop (GstBaseTransform * trans);
static gboolean gst_tensor_query_client_transform_size (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize);
/**
 * @brief initialize the class
 */
static void
gst_tensor_query_client_class_init (GstTensorQueryClientClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *trans_class;

  trans_class = (GstBaseTransformClass *) klass;
  gstelement_class = (GstElementClass *) trans_class;
  gobject_class = (GObjectClass *) gstelement_class;

  gobject_class->set_property = gst_tensor_query_client_set_property;
  gobject_class->get_property = gst_tensor_query_client_get_property;
  gobject_class->finalize = gst_tensor_query_client_finalize;

  g_object_class_install_property (gobject_class, PROP_SINK_HOST,
      g_param_spec_string ("sink-host", "Host",
          "A tenor query sink host to send the packets to/from",
          TCP_DEFAULT_HOST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SINK_PORT,
      g_param_spec_int ("sink-port", "Port",
          "The port of tensor query sink to send the packets to/from", 0,
          TCP_HIGHEST_PORT, TCP_DEFAULT_SINK_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SRC_HOST,
      g_param_spec_string ("src-host", "Host",
          "A tenor query src host to send the packets to/from",
          TCP_DEFAULT_HOST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SRC_PORT,
      g_param_spec_int ("src-port", "Port",
          "The port of tensor query src to send the packets to/from", 0,
          TCP_HIGHEST_PORT, TCP_DEFAULT_SRC_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output",
          DEFAULT_SILENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  trans_class->passthrough_on_same_caps = FALSE;
  trans_class->transform_ip_on_passthrough = FALSE;

  /* Processing units */
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_tensor_query_client_transform);

  /* Negotiation units */
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_tensor_query_client_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_tensor_query_client_fixate_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_tensor_query_client_set_caps);
  /* Allocation units */
  trans_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_tensor_query_client_transform_size);

  /* start/stop to call open/close */
  trans_class->start = GST_DEBUG_FUNCPTR (gst_tensor_query_client_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_tensor_query_client_stop);

  gst_element_class_set_static_metadata (gstelement_class,
      "TensorQueryClient", "Filter/Tensor/Query",
      "Handle querying tensor data through the network",
      "Samsung Electronics Co., Ltd.");

  GST_DEBUG_CATEGORY_INIT (gst_tensor_query_client_debug, "tensor_query_client",
      0, "Tensor Query Client");
}

/**
 * @brief initialize the new element
 */
static void
gst_tensor_query_client_init (GstTensorQueryClient * self)
{
  self->silent = DEFAULT_SILENT;

  self->sink_host = g_strdup (TCP_DEFAULT_HOST);
  self->sink_port = TCP_DEFAULT_SINK_PORT;
  self->src_host = g_strdup (TCP_DEFAULT_HOST);
  self->src_port = TCP_DEFAULT_SRC_PORT;

  self->sink_socket = NULL;
  self->src_socket = NULL;
  self->sink_cancellable = g_cancellable_new ();
  self->src_cancellable = g_cancellable_new ();

  gst_tensors_config_init (&self->in_config);
  gst_tensors_config_init (&self->out_config);

  GST_OBJECT_FLAG_UNSET (self, GST_TENSOR_QUERY_CLIENT_SINK_SOCKET_OPEN);
  GST_OBJECT_FLAG_UNSET (self, GST_TENSOR_QUERY_CLIENT_SRC_SOCKET_OPEN);
}

/**
 * @brief finalize the object
 */
static void
gst_tensor_query_client_finalize (GObject * object)
{
  GstTensorQueryClient *self = GST_TENSOR_QUERY_CLIENT (object);

  if (self->sink_cancellable)
    g_object_unref (self->sink_cancellable);
  self->sink_cancellable = NULL;

  if (self->src_cancellable)
    g_object_unref (self->src_cancellable);
  self->src_cancellable = NULL;

  if (self->sink_socket)
    g_object_unref (self->sink_socket);
  self->sink_socket = NULL;

  if (self->src_socket)
    g_object_unref (self->src_socket);
  self->src_socket = NULL;

  g_free (self->sink_host);
  self->sink_host = NULL;
  g_free (self->src_host);
  self->src_host = NULL;

  gst_tensors_config_free (&self->in_config);
  gst_tensors_config_free (&self->out_config);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * @brief set property
 */
static void
gst_tensor_query_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTensorQueryClient *self = GST_TENSOR_QUERY_CLIENT (object);

  switch (prop_id) {
    case PROP_SINK_HOST:
      if (!g_value_get_string (value)) {
        g_warning ("host property cannot be NULL");
        break;
      }
      g_free (self->sink_host);
      self->sink_host = g_value_dup_string (value);
      break;
    case PROP_SINK_PORT:
      self->sink_port = g_value_get_int (value);
      break;
    case PROP_SRC_HOST:
      if (!g_value_get_string (value)) {
        g_warning ("host property cannot be NULL");
        break;
      }
      g_free (self->src_host);
      self->src_host = g_value_dup_string (value);
      break;
    case PROP_SRC_PORT:
      self->src_port = g_value_get_int (value);
      break;
    case PROP_SILENT:
      self->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * @brief get property
 */
static void
gst_tensor_query_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTensorQueryClient *self = GST_TENSOR_QUERY_CLIENT (object);

  switch (prop_id) {
    case PROP_SINK_HOST:
      g_value_set_string (value, self->sink_host);
      break;
    case PROP_SINK_PORT:
      g_value_set_int (value, self->sink_port);
      break;
    case PROP_SRC_HOST:
      g_value_set_string (value, self->src_host);
      break;
    case PROP_SRC_PORT:
      g_value_set_int (value, self->src_port);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, self->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * @brief Convert static tensor to flexible tensor.
 */
static GstBuffer *
gst_tensor_query_client_transform_flex_tensor (GstTensorQueryClient * self,
    GstBuffer * buf)
{
  GstBuffer *buffer;
  GstMemory *mem;
  GstTensorsInfo *info;
  GstTensorMetaInfo meta;
  guint i;

  info = &self->in_config.info;

  buffer = gst_buffer_new ();

  for (i = 0; i < info->num_tensors; i++) {
    mem = gst_buffer_peek_memory (buf, i);

    if (gst_tensor_info_is_flexible (&info->info[i])) {
      mem = gst_memory_ref (mem);
    } else {
      /* append header */
      gst_tensor_info_convert_to_meta (&info->info[i], &meta);
      mem = gst_tensor_meta_info_append_header (&meta, mem);
    }

    gst_buffer_append_memory (buffer, mem);
  }

  gst_buffer_copy_into (buffer, buf, GST_BUFFER_COPY_METADATA, 0, -1);

  return buffer;
}

/**
 * @brief Update source pad caps if source pad caps is changed.
 */
static void
gst_tensor_query_client_update_caps (GstTensorQueryClient * self,
    GstTensorsConfig * config)
{
  GstCaps *curr_caps, *new_caps;
  GstPad *src_pad = GST_BASE_TRANSFORM_SRC_PAD (&self->element);

  curr_caps = gst_pad_get_current_caps (src_pad);
  silent_debug_caps (curr_caps, "current caps");
  new_caps = gst_tensor_pad_caps_from_config (src_pad, config);
  silent_debug_caps (new_caps, "new caps");

  if (curr_caps == NULL || !gst_caps_is_equal (curr_caps, new_caps)) {
    ml_logd ("Update source pad caps of tensor query client");
    gst_pad_set_caps (src_pad, new_caps);
  }

  if (curr_caps)
    gst_caps_unref (curr_caps);
  gst_caps_unref (new_caps);
}

/**
 * @brief Parse flexible tensor.
 * @todo Remove the duplicated code with the tensor_converter.
 */
static void
gst_tensor_query_client_parse_flex_tensor (GstTensorQueryClient * self,
    GstBuffer * flex_tensor_buf, GstBuffer * statc_tensor_buf)
{
  GstTensorMetaInfo meta;
  GstMemory *mem;
  gsize s1, hsize;
  guint n;
  GstTensorsConfig config;

  gst_tensors_config_init (&config);
  config.info.num_tensors = gst_buffer_n_memory (flex_tensor_buf);
  config.rate_n = self->out_config.rate_n;
  config.rate_d = self->out_config.rate_d;

  for (n = 0; n < config.info.num_tensors; n++) {
    mem = gst_buffer_peek_memory (flex_tensor_buf, n);
    s1 = gst_memory_get_sizes (mem, NULL, NULL);

    /* flex-tensor has header in each mem block */
    gst_tensor_meta_info_parse_memory (&meta, mem);
    gst_tensor_meta_info_convert (&meta, &config.info.info[n]);

    hsize = gst_tensor_meta_info_get_header_size (&meta);
    s1 -= hsize;

    gst_buffer_append_memory (statc_tensor_buf, gst_memory_share (mem, hsize,
            s1));
  }
  if (!gst_tensors_config_is_equal (&self->out_config, &config)) {
    nns_logd ("output tensors config and received config are different.");
    gst_tensor_query_client_update_caps (self, &config);
  }
  gst_tensors_config_free (&config);
}

/**
 * @brief non-ip transform. required vmethod of GstBaseTransform.
 */
static GstFlowReturn
gst_tensor_query_client_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstTensorQueryClient *self = GST_TENSOR_QUERY_CLIENT (trans);
  GstMapInfo map;
  gsize written = 0;
  GError *err = NULL;
  gsize byte_received = 0;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *sending_buf = NULL;
  GstBuffer *receive_buf = NULL;

  g_return_val_if_fail (GST_OBJECT_FLAG_IS_SET (self,
          GST_TENSOR_QUERY_CLIENT_SINK_SOCKET_OPEN), GST_FLOW_FLUSHING);

  /** Convert to flexible tensor */
  /** tensor_query_* elements commuicate using flexible tensor */
  if (!gst_tensors_info_is_flexible (&self->in_config.info)) {
    sending_buf = gst_tensor_query_client_transform_flex_tensor (self, inbuf);
  }

  /** Send data to query server */
  gst_buffer_map (sending_buf, &map, GST_MAP_READ);

  /* write buffer data */
  while (written < map.size) {
    gssize rret = g_socket_send (self->src_socket, (gchar *) map.data + written,
        map.size - written, self->src_cancellable, &err);
    if (rret < 0)
      goto write_error;
    written += rret;
  }
  gst_buffer_unmap (sending_buf, &map);
  gst_buffer_unref (sending_buf);

  /** Read data from query server */
  receive_buf = gst_buffer_new ();
  ret =
      gst_tensor_query_socket_receive (self->sink_socket,
      self->sink_cancellable, &byte_received, receive_buf);

  /** Convert flexible tensor to static tensor */
  gst_tensor_query_client_parse_flex_tensor (self, receive_buf, outbuf);
  gst_buffer_unref (receive_buf);

  return GST_FLOW_OK;

  /* ERRORS */
write_error:
  {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      ret = GST_FLOW_FLUSHING;
      nns_logd ("Cancelled reading from socket");
    } else {
      nns_loge ("Error while sending data to \"%s:%d\".", self->sink_host,
          self->sink_port);
      ret = GST_FLOW_ERROR;
    }
    gst_buffer_unmap (inbuf, &map);
    g_clear_error (&err);

    return ret;
  }
}

/**
 * @brief configure tensor-srcpad cap from "proposed" cap. (GST Standard)
 * @note Be careful not to fix/set caps at this stage. Negotiation not completed yet.
 */
static GstCaps *
gst_tensor_query_client_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filtercap)
{
  GstTensorQueryClient *self = GST_TENSOR_QUERY_CLIENT (trans);
  GstCaps *result = gst_caps_new_empty ();
  guint i;

  silent_debug ("Calling TransformCaps, direction = %d\n", direction);
  silent_debug_caps (caps, "from");
  silent_debug_caps (filtercap, "filter");

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s, *const_s = gst_caps_get_structure (caps, i);
    s = gst_structure_copy (const_s);

    result = gst_caps_merge_structure_full (result, s,
        gst_caps_features_copy (gst_caps_get_features (caps, i)));
  }

  if (filtercap && gst_caps_get_size (filtercap) > 0) {
    GstCaps *intersection =
        gst_caps_intersect_full (filtercap, result, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (result);
    result = intersection;
  }

  silent_debug_caps (result, "to");

  return result;
}

/**
 * @brief fixate caps. required vmethod of GstBaseTransform.
 */
static GstCaps *
gst_tensor_query_client_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstTensorQueryClient *self = GST_TENSOR_QUERY_CLIENT (trans);

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  silent_debug_caps (othercaps, "fixate caps");

  return gst_caps_fixate (othercaps);
}

/**
 * @brief set caps. required vmethod of GstBaseTransform.
 */
static gboolean
gst_tensor_query_client_set_caps (GstBaseTransform * trans,
    GstCaps * in_caps, GstCaps * out_caps)
{
  GstTensorQueryClient *self = GST_TENSOR_QUERY_CLIENT (trans);
  GstTensorsConfig in_config;
  GstStructure *structure;
  gboolean ret = TRUE;

  gst_tensors_config_init (&in_config);

  structure = gst_caps_get_structure (in_caps, 0);
  gst_tensors_config_from_structure (&in_config, structure);

  if (!gst_tensors_config_validate (&in_config)) {
    GST_ERROR_OBJECT (self, "Invalid caps, failed to configure input info.");
    ret = FALSE;
    goto done;
  }

  gst_tensors_info_copy (&self->in_config.info, &in_config.info);
  self->in_config.rate_n = in_config.rate_n;
  self->in_config.rate_d = in_config.rate_d;

  /** Update ouput tensors config later */
  gst_tensors_info_copy (&self->out_config.info, &in_config.info);
  self->out_config.rate_n = in_config.rate_n;
  self->out_config.rate_d = in_config.rate_d;

  silent_debug ("setcaps called in: %" GST_PTR_FORMAT " out: %" GST_PTR_FORMAT,
      in_caps, out_caps);

done:
  gst_tensors_config_free (&in_config);
  return ret;
}

/**
 * @brief Tell the framework the required size of buffer based on the info of the other side pad. optional vmethod of BaseTransform
 */
static gboolean
gst_tensor_query_client_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize)
{
  *othersize = 0;
  return TRUE;
}

/**
 * @brief Connect to server
 */
static gboolean
gst_tensor_query_client_connect (GstTensorQueryClient * self, gboolean is_sink)
{
  gchar *host;
  guint16 port;
  GCancellable *cancellable;
  GSocketAddress *saddr = NULL;
  GSocket *socket = NULL;
  GError *err = NULL;

  if (is_sink) {
    host = self->sink_host;
    port = self->sink_port;
    cancellable = self->sink_cancellable;
  } else {
    host = self->src_host;
    port = self->src_port;
    cancellable = self->src_cancellable;
  }
  socket = gst_tensor_query_socket_new (host, port, cancellable, &saddr);
  if (!socket) {
    nns_loge ("Failed to create new socket or connect to host");
    g_object_unref (saddr);
    return FALSE;
  }

  if (!g_socket_connect (socket, saddr, cancellable, &err)) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      nns_logd ("Cancelled connecting");
    } else {
      nns_loge ("Failed to connect to host");
    }
    g_clear_error (&err);
    g_object_unref (saddr);
    return FALSE;
  }
  g_object_unref (saddr);

  if (is_sink) {
    self->sink_socket = socket;
    GST_OBJECT_FLAG_SET (self, GST_TENSOR_QUERY_CLIENT_SINK_SOCKET_OPEN);
  } else {
    self->src_socket = socket;
    GST_OBJECT_FLAG_SET (self, GST_TENSOR_QUERY_CLIENT_SRC_SOCKET_OPEN);
  }
  return TRUE;
}

/**
 * @brief Called when the element starts processing. optional vmethod of BaseTransform
 * @param[in] trans "this" pointer
 * @return TRUE if there is no error.
 */
static gboolean
gst_tensor_query_client_start (GstBaseTransform * trans)
{
  GstTensorQueryClient *self = GST_TENSOR_QUERY_CLIENT (trans);

  /** Connect to tensor_query_src */
  if (!GST_OBJECT_FLAG_IS_SET (self, GST_TENSOR_QUERY_CLIENT_SINK_SOCKET_OPEN)) {
    if (!gst_tensor_query_client_connect (self, TRUE)) {
      nns_loge ("Failed to connect tensor_query_src");
      return FALSE;
    }
  }
  /** Connect to tensor_query_sink */
  if (!GST_OBJECT_FLAG_IS_SET (self, GST_TENSOR_QUERY_CLIENT_SRC_SOCKET_OPEN)) {
    if (!gst_tensor_query_client_connect (self, FALSE)) {
      nns_loge ("Failed to connect tensor_query_sink");
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * @brief Close the socket
 */
static void
gst_tensor_query_client_close_socket (GstTensorQueryClient * self,
    GSocket ** socket)
{
  GError *err = NULL;

  GST_DEBUG_OBJECT (self, "closing socket");
  if (!g_socket_close (*socket, &err)) {
    GST_ERROR_OBJECT (self, "Failed to close socket: %s", err->message);
    g_clear_error (&err);
  }
  g_object_unref (*socket);
  *socket = NULL;
}

/**
 * @brief Called when the element stops processing. optional vmethod of BaseTransform
 * @param[in] trans "this" pointer
 * @return TRUE if there is no error.
 */
static gboolean
gst_tensor_query_client_stop (GstBaseTransform * trans)
{
  GstTensorQueryClient *self = GST_TENSOR_QUERY_CLIENT (trans);

  if (self->sink_socket) {
    gst_tensor_query_client_close_socket (self, &self->sink_socket);
    GST_OBJECT_FLAG_UNSET (self, GST_TENSOR_QUERY_CLIENT_SINK_SOCKET_OPEN);
  }
  if (self->src_socket) {
    gst_tensor_query_client_close_socket (self, &self->src_socket);
    GST_OBJECT_FLAG_UNSET (self, GST_TENSOR_QUERY_CLIENT_SRC_SOCKET_OPEN);
  }

  return TRUE;
}
