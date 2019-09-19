/**
 * Copyright (c) 2019 Samsung Electronics Co., Ltd All Rights Reserved
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
 */
/**
 * @file nnstreamer-capi-single.c
 * @date 08 May 2019
 * @brief NNStreamer/Single C-API Wrapper.
 *        This allows to invoke individual input frame with NNStreamer.
 * @see	https://github.com/nnsuite/nnstreamer
 * @author MyungJoo Ham <myungjoo.ham@samsung.com>
 * @bug No known bugs except for NYI items
 */

#include <string.h>
#include <gst/app/app.h>

#include "nnstreamer.h"         /* Uses NNStreamer/Pipeline C-API */
#include "nnstreamer-single.h"
#include "nnstreamer-capi-private.h"
#include "nnstreamer_plugin_api.h"
#include "nnstreamer_conf.h"

#undef ML_SINGLE_SUPPORT_TIMEOUT
#if (GST_VERSION_MAJOR > 1 || (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR >= 10))
#define ML_SINGLE_SUPPORT_TIMEOUT
#endif

#define ML_SINGLE_MAGIC 0xfeedfeed

/**
 * @brief Global lock for single shot API
 * @detail This lock ensures that ml_single_close is thread safe. All other API
 *         functions use the mutex from the single handle. However for close,
 *         single handle mutex cannot be used as single handle is destroyed at
 *         close
 * @note This mutex is automatically initialized as it is statically declared
 */
G_LOCK_DEFINE_STATIC (magic);

/**
 * @brief Get valid handle after magic verification
 * @note Magic lock is acquired after this
 */
#define ML_SINGLE_GET_VALID_HANDLE(single_h, single) do { \
  G_LOCK (magic); \
  single_h = (ml_single *) single; \
  if (single_h->magic != ML_SINGLE_MAGIC) { \
    ml_loge ("The given param, single is invalid."); \
    G_UNLOCK (magic); \
    return ML_ERROR_INVALID_PARAMETER; \
  } \
} while (0)

/**
 * @brief Default time to wait for an output in appsink (3 seconds).
 */
#define SINGLE_DEFAULT_TIMEOUT 3000

typedef struct
{
  ml_pipeline_h pipe;

  GstElement *src;
  GstElement *sink;
  GstElement *filter;

  ml_tensors_info_s in_info;
  ml_tensors_info_s out_info;

  guint magic; /**< code to verify valid handle */
  GMutex lock; /**< Lock for internal values */
  gboolean clear_previous_buffer; /**< Previous buffer was timed out, need to clear old buffer */
  guint timeout; /**< Timeout in milliseconds */
} ml_single;

/**
 * @brief Gets the tensors info from tensor-filter.
 */
static void
ml_single_get_tensors_info_from_filter (GstElement * filter, gboolean is_input,
    ml_tensors_info_h * info)
{
  ml_tensors_info_s *input_info;
  GstTensorsInfo gst_info;
  gchar *val;
  guint rank;
  gchar *prop_prefix, *prop_name;

  if (is_input)
    prop_prefix = g_strdup ("input");
  else
    prop_prefix = g_strdup ("output");

  /* allocate handle for tensors info */
  ml_tensors_info_create (info);
  input_info = (ml_tensors_info_s *) (*info);

  gst_tensors_info_init (&gst_info);

  /* get dimensions */
  prop_name = g_strdup_printf ("%s", prop_prefix);
  g_object_get (filter, prop_name, &val, NULL);
  rank = gst_tensors_info_parse_dimensions_string (&gst_info, val);
  g_free (val);
  g_free (prop_name);

  /* set the number of tensors */
  gst_info.num_tensors = rank;

  /* get types */
  prop_name = g_strdup_printf ("%s%s", prop_prefix, "type");
  g_object_get (filter, prop_name, &val, NULL);
  rank = gst_tensors_info_parse_types_string (&gst_info, val);
  g_free (val);
  g_free (prop_name);

  if (gst_info.num_tensors != rank) {
    ml_logw ("Invalid state, tensor type is mismatched in filter.");
  }

  /* get names */
  prop_name = g_strdup_printf ("%s%s", prop_prefix, "name");
  g_object_get (filter, prop_name, &val, NULL);
  rank = gst_tensors_info_parse_names_string (&gst_info, val);
  g_free (val);
  g_free (prop_name);

  if (gst_info.num_tensors != rank) {
    ml_logw ("Invalid state, tensor name is mismatched in filter.");
  }

  ml_tensors_info_copy_from_gst (input_info, &gst_info);
  gst_tensors_info_free (&gst_info);

  g_free (prop_prefix);
}

/**
 * @brief Opens an ML model and returns the instance as a handle.
 */
int
ml_single_open (ml_single_h * single, const char *model,
    const ml_tensors_info_h input_info, const ml_tensors_info_h output_info,
    ml_nnfw_type_e nnfw, ml_nnfw_hw_e hw)
{
  ml_single *single_h;
  ml_pipeline_h pipe;
  ml_pipeline *pipe_h;
  GstElement *appsrc, *appsink, *filter;
  GstCaps *caps;
  GError *err = NULL;
  int status = ML_ERROR_NONE;
  gchar *pipeline_desc = NULL;
  gchar *path_down;
  ml_tensors_info_s *in_tensors_info, *out_tensors_info;
  bool available = false;
  bool valid = false;

  check_feature_state ();

  /* Validate the params */
  if (!single) {
    ml_loge ("The given param, single is invalid.");
    return ML_ERROR_INVALID_PARAMETER;
  }

  /* init null */
  *single = NULL;

  if (FALSE == gst_init_check (NULL, NULL, &err)) {
    if (err) {
      ml_loge ("GStreamer has the following error: %s", err->message);
      g_clear_error (&err);
    } else {
      ml_loge ("Cannot initialize GStreamer. Unknown reason.");
    }
    return ML_ERROR_STREAMS_PIPE;
  }

  in_tensors_info = (ml_tensors_info_s *) input_info;
  out_tensors_info = (ml_tensors_info_s *) output_info;

  if (input_info) {
    /* Validate input tensor info. */
    if (ml_tensors_info_validate (input_info, &valid) != ML_ERROR_NONE ||
        valid == false) {
      ml_loge ("The given param, input tensor info is invalid.");
      return ML_ERROR_INVALID_PARAMETER;
    }
  }

  if (output_info) {
    /* Validate output tensor info. */
    if (ml_tensors_info_validate (output_info, &valid) != ML_ERROR_NONE ||
        valid == false) {
      ml_loge ("The given param, output tensor info is invalid.");
      return ML_ERROR_INVALID_PARAMETER;
    }
  }

  if (!model || !g_file_test (model, G_FILE_TEST_IS_REGULAR)) {
    ml_loge ("The given param, model path [%s] is invalid.",
        GST_STR_NULL (model));
    return ML_ERROR_INVALID_PARAMETER;
  }

  /* 1. Determine nnfw */
  /* Check file extention. */
  path_down = g_ascii_strdown (model, -1);

  switch (nnfw) {
    case ML_NNFW_TYPE_ANY:
      if (g_str_has_suffix (path_down, ".tflite")) {
        ml_logi ("The given model [%s] is supposed a tensorflow-lite model.", model);
        nnfw = ML_NNFW_TYPE_TENSORFLOW_LITE;
      } else if (g_str_has_suffix (path_down, ".pb")) {
        ml_logi ("The given model [%s] is supposed a tensorflow model.", model);
        nnfw = ML_NNFW_TYPE_TENSORFLOW;
      } else if (g_str_has_suffix (path_down, NNSTREAMER_SO_FILE_EXTENSION)) {
        ml_logi ("The given model [%s] is supposed a custom filter model.", model);
        nnfw = ML_NNFW_TYPE_CUSTOM_FILTER;
      } else {
        ml_loge ("The given model [%s] has unknown extension.", model);
        status = ML_ERROR_INVALID_PARAMETER;
      }
      break;
    case ML_NNFW_TYPE_CUSTOM_FILTER:
      if (!g_str_has_suffix (path_down, NNSTREAMER_SO_FILE_EXTENSION)) {
        ml_loge ("The given model [%s] has invalid extension.", model);
        status = ML_ERROR_INVALID_PARAMETER;
      }
      break;
    case ML_NNFW_TYPE_TENSORFLOW_LITE:
      if (!g_str_has_suffix (path_down, ".tflite")) {
        ml_loge ("The given model [%s] has invalid extension.", model);
        status = ML_ERROR_INVALID_PARAMETER;
      }
      break;
    case ML_NNFW_TYPE_TENSORFLOW:
      if (!g_str_has_suffix (path_down, ".pb")) {
        ml_loge ("The given model [%s] has invalid extension.", model);
        status = ML_ERROR_INVALID_PARAMETER;
      }
      break;
    case ML_NNFW_TYPE_NNFW:
      /** @todo Need to check method for NNFW */
      ml_loge ("NNFW is not supported.");
      status = ML_ERROR_NOT_SUPPORTED;
      break;
    default:
      break;
  }

  g_free (path_down);
  if (status != ML_ERROR_NONE)
    return status;

  /* 2. Determine hw */
  /** @todo Now the param hw is ignored. (Supposed CPU only) Support others later. */
  status = ml_check_nnfw_availability (nnfw, hw, &available);
  if (status != ML_ERROR_NONE)
    return status;

  if (!available) {
    ml_loge ("The given nnfw is not available.");
    return ML_ERROR_NOT_SUPPORTED;
  }

  /* 3. Construct a pipeline */
  /* Set the pipeline desc with nnfw. */
  switch (nnfw) {
    case ML_NNFW_TYPE_CUSTOM_FILTER:
      pipeline_desc =
          g_strdup_printf
          ("appsrc name=srcx ! tensor_filter name=filterx framework=custom model=%s ! appsink name=sinkx sync=false",
          model);
      break;
    case ML_NNFW_TYPE_TENSORFLOW_LITE:
      /* We can get the tensor meta from tf-lite model. */
      pipeline_desc =
          g_strdup_printf
          ("appsrc name=srcx ! tensor_filter name=filterx framework=tensorflow-lite model=%s ! appsink name=sinkx sync=false",
          model);
      break;
    case ML_NNFW_TYPE_TENSORFLOW:
      if (in_tensors_info && out_tensors_info) {
        GstTensorsInfo in_info, out_info;
        gchar *str_dim, *str_type, *str_name;
        gchar *in_option, *out_option;

        ml_tensors_info_copy_from_ml (&in_info, in_tensors_info);
        ml_tensors_info_copy_from_ml (&out_info, out_tensors_info);

        /* Set input option */
        str_dim = gst_tensors_info_get_dimensions_string (&in_info);
        str_type = gst_tensors_info_get_types_string (&in_info);
        str_name = gst_tensors_info_get_names_string (&in_info);
        in_option = g_strdup_printf ("input=%s inputtype=%s inputname=%s",
            str_dim, str_type, str_name);
        g_free (str_dim);
        g_free (str_type);
        g_free (str_name);

        /* Set output option */
        str_dim = gst_tensors_info_get_dimensions_string (&out_info);
        str_type = gst_tensors_info_get_types_string (&out_info);
        str_name = gst_tensors_info_get_names_string (&out_info);
        out_option = g_strdup_printf ("output=%s outputtype=%s outputname=%s",
            str_dim, str_type, str_name);
        g_free (str_dim);
        g_free (str_type);
        g_free (str_name);

        pipeline_desc =
            g_strdup_printf
            ("appsrc name=srcx ! tensor_filter name=filterx framework=tensorflow model=%s %s %s ! appsink name=sinkx sync=false",
            model, in_option, out_option);

        g_free (in_option);
        g_free (out_option);
        gst_tensors_info_free (&in_info);
        gst_tensors_info_free (&out_info);
      } else {
        ml_loge ("To run the pipeline with tensorflow model, input and output information should be initialized.");
        return ML_ERROR_INVALID_PARAMETER;
      }
      break;
    default:
      /** @todo Add other fw later. */
      ml_loge ("The given nnfw is not supported.");
      return ML_ERROR_NOT_SUPPORTED;
  }

  status = ml_pipeline_construct (pipeline_desc, NULL, NULL, &pipe);
  g_free (pipeline_desc);
  if (status != ML_ERROR_NONE) {
    /* Failed to construct pipeline. */
    return status;
  }

  /* 4. Allocate */
  pipe_h = (ml_pipeline *) pipe;
  appsrc = gst_bin_get_by_name (GST_BIN (pipe_h->element), "srcx");
  appsink = gst_bin_get_by_name (GST_BIN (pipe_h->element), "sinkx");
  filter = gst_bin_get_by_name (GST_BIN (pipe_h->element), "filterx");

  single_h = g_new0 (ml_single, 1);
  g_assert (single_h);

  single_h->magic = ML_SINGLE_MAGIC;
  single_h->pipe = pipe;
  single_h->src = appsrc;
  single_h->sink = appsink;
  single_h->filter = filter;
  single_h->timeout = SINGLE_DEFAULT_TIMEOUT;
  single_h->clear_previous_buffer = FALSE;
  ml_tensors_info_initialize (&single_h->in_info);
  ml_tensors_info_initialize (&single_h->out_info);
  g_mutex_init (&single_h->lock);

  /* 5. Set in/out caps and metadata */
  if (in_tensors_info) {
    caps = ml_tensors_info_get_caps (in_tensors_info);
    ml_tensors_info_clone (&single_h->in_info, in_tensors_info);
  } else {
    ml_tensors_info_h in_info;

    ml_single_get_tensors_info_from_filter (filter, TRUE, &in_info);
    ml_tensors_info_clone (&single_h->in_info, in_info);
    ml_tensors_info_destroy (in_info);

    status = ml_tensors_info_validate (&single_h->in_info, &valid);
    if (status != ML_ERROR_NONE || valid == false) {
      ml_loge ("The input tensor info is invalid.");
      status = ML_ERROR_INVALID_PARAMETER;
      goto error;
    }

    caps = ml_tensors_info_get_caps (&single_h->in_info);
  }

  gst_app_src_set_caps (GST_APP_SRC (appsrc), caps);
  gst_caps_unref (caps);

  if (out_tensors_info) {
    caps = ml_tensors_info_get_caps (out_tensors_info);
    ml_tensors_info_clone (&single_h->out_info, out_tensors_info);
  } else {
    ml_tensors_info_h out_info;

    ml_single_get_tensors_info_from_filter (filter, FALSE, &out_info);
    ml_tensors_info_clone (&single_h->out_info, out_info);
    ml_tensors_info_destroy (out_info);

    status = ml_tensors_info_validate (&single_h->out_info, &valid);
    if (status != ML_ERROR_NONE || valid == false) {
      ml_loge ("The output tensor info is invalid.");
      status = ML_ERROR_INVALID_PARAMETER;
      goto error;
    }

    caps = ml_tensors_info_get_caps (&single_h->out_info);
  }

  gst_app_sink_set_caps (GST_APP_SINK (appsink), caps);
  gst_caps_unref (caps);

  /* set max buffer in appsink (default unlimited) and drop old buffer */
  gst_app_sink_set_max_buffers (GST_APP_SINK (appsink), 1);
  gst_app_sink_set_drop (GST_APP_SINK (appsink), TRUE);

  /* 6. Start pipeline */
  status = ml_pipeline_start (pipe);
  if (status != ML_ERROR_NONE) {
    /* Failed to construct pipeline. */
    goto error;
  }

error:
  if (status != ML_ERROR_NONE) {
    ml_single_close (single_h);
  } else {
    *single = single_h;
  }

  return status;
}

/**
 * @brief Closes the opened model handle.
 */
int
ml_single_close (ml_single_h single)
{
  ml_single *single_h;
  int status;

  check_feature_state ();

  if (!single) {
    ml_loge ("The given param, single is invalid.");
    return ML_ERROR_INVALID_PARAMETER;
  }

  ML_SINGLE_GET_VALID_HANDLE (single_h, single);
  single_h->magic = 0;

  g_mutex_lock (&single_h->lock);
  G_UNLOCK (magic);

  if (single_h->src) {
    gst_object_unref (single_h->src);
    single_h->src = NULL;
  }

  if (single_h->sink) {
    gst_object_unref (single_h->sink);
    single_h->sink = NULL;
  }

  if (single_h->filter) {
    gst_object_unref (single_h->filter);
    single_h->filter = NULL;
  }

  ml_tensors_info_free (&single_h->in_info);
  ml_tensors_info_free (&single_h->out_info);

  status = ml_pipeline_destroy (single_h->pipe);

  g_mutex_unlock (&single_h->lock);
  g_mutex_clear (&single_h->lock);

  g_free (single_h);
  return status;
}

/**
 * @brief Invokes the model with the given input data.
 */
int
ml_single_invoke (ml_single_h single,
    const ml_tensors_data_h input, ml_tensors_data_h * output)
{
  ml_single *single_h;
  ml_tensors_data_s *in_data, *result;
  GstSample *sample = NULL;
  GstBuffer *buffer;
  GstMemory *mem;
  GstMapInfo mem_info;
  GstFlowReturn ret;
  int i, status = ML_ERROR_NONE;

  check_feature_state ();

  if (!single || !input || !output) {
    ml_loge ("The given param is invalid.");
    return ML_ERROR_INVALID_PARAMETER;
  }

  ML_SINGLE_GET_VALID_HANDLE (single_h, single);
  g_mutex_lock (&single_h->lock);
  G_UNLOCK (magic);

  in_data = (ml_tensors_data_s *) input;
  *output = NULL;

  /* Validate input data */
  if (in_data->num_tensors != single_h->in_info.num_tensors) {
    ml_loge ("The given param input is invalid, different number of memory blocks.");
    status = ML_ERROR_INVALID_PARAMETER;
    goto done;
  }

  for (i = 0; i < in_data->num_tensors; i++) {
    size_t raw_size = ml_tensor_info_get_size (&single_h->in_info.info[i]);

    if (!in_data->tensors[i].tensor || in_data->tensors[i].size != raw_size) {
      ml_loge ("The given param input is invalid, different size of memory block.");
      status = ML_ERROR_INVALID_PARAMETER;
      goto done;
    }
  }

#ifdef ML_SINGLE_SUPPORT_TIMEOUT
  /* Try to clear old buffer in appsink before pushing the buffer */
  if (single_h->clear_previous_buffer) {
    ml_logw ("Previous buffer was timed out, try to clear old data.");

    sample = gst_app_sink_pull_sample (GST_APP_SINK (single_h->sink));
    if (sample) {
      gst_sample_unref (sample);
      sample = NULL;
    }

    single_h->clear_previous_buffer = FALSE;
  }
#endif

  /* Push input buffer */
  buffer = gst_buffer_new ();

  for (i = 0; i < in_data->num_tensors; i++) {
    mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
        in_data->tensors[i].tensor, in_data->tensors[i].size, 0,
        in_data->tensors[i].size, NULL, NULL);
    gst_buffer_append_memory (buffer, mem);
  }

  ret = gst_app_src_push_buffer (GST_APP_SRC (single_h->src), buffer);
  if (ret != GST_FLOW_OK) {
    ml_loge ("Cannot push a buffer into source element.");
    gst_buffer_unref (buffer);
    status = ML_ERROR_STREAMS_PIPE;
    goto done;
  }

  /* Try to get the result */
#ifdef ML_SINGLE_SUPPORT_TIMEOUT
  /* gst_app_sink_try_pull_sample() is available at gstreamer-1.10 */
  sample =
      gst_app_sink_try_pull_sample (GST_APP_SINK (single_h->sink), GST_MSECOND * single_h->timeout);
#else
  sample = gst_app_sink_pull_sample (GST_APP_SINK (single_h->sink));
#endif

  if (!sample) {
    ml_loge ("Failed to get the result from sink element.");
#ifdef ML_SINGLE_SUPPORT_TIMEOUT
    single_h->clear_previous_buffer = TRUE;
    status = ML_ERROR_TIMED_OUT;
#else
    status = ML_ERROR_UNKNOWN;
#endif
    goto done;
  }

  /* Allocate output buffer */
  status = ml_tensors_data_create (&single_h->out_info, output);
  if (status != ML_ERROR_NONE) {
    ml_loge ("Failed to allocate the memory block.");
    goto done;
  }

  result = (ml_tensors_data_s *) (*output);

  /* Copy the result */
  buffer = gst_sample_get_buffer (sample);
  for (i = 0; i < result->num_tensors; i++) {
    mem = gst_buffer_peek_memory (buffer, i);
    gst_memory_map (mem, &mem_info, GST_MAP_READ);

    memcpy (result->tensors[i].tensor, mem_info.data, mem_info.size);

    gst_memory_unmap (mem, &mem_info);
  }

done:
  if (sample)
    gst_sample_unref (sample);

  g_mutex_unlock (&single_h->lock);
  return status;
}

/**
 * @brief Gets the tensors info for the given handle.
 */
static int
ml_single_get_tensors_info (ml_single_h single, gboolean is_input,
    ml_tensors_info_h * info)
{
  ml_single *single_h;
  int status = ML_ERROR_NONE;

  check_feature_state ();

  if (!single || !info)
    return ML_ERROR_INVALID_PARAMETER;

  ML_SINGLE_GET_VALID_HANDLE (single_h, single);
  g_mutex_lock (&single_h->lock);
  G_UNLOCK (magic);

  ml_single_get_tensors_info_from_filter (single_h->filter, is_input, info);

  g_mutex_unlock (&single_h->lock);
  return status;
}

/**
 * @brief Gets the type (tensor dimension, type, name and so on) of required input data for the given handle.
 */
int
ml_single_get_input_info (ml_single_h single, ml_tensors_info_h * info)
{
  return ml_single_get_tensors_info (single, TRUE, info);
}

/**
 * @brief Gets the type (tensor dimension, type, name and so on) of output data for the given handle.
 */
int
ml_single_get_output_info (ml_single_h single, ml_tensors_info_h * info)
{
  return ml_single_get_tensors_info (single, FALSE, info);
}

/**
 * @brief Sets the maximum amount of time to wait for an output, in milliseconds.
 */
int
ml_single_set_timeout (ml_single_h single, unsigned int timeout)
{
#ifdef ML_SINGLE_SUPPORT_TIMEOUT
  ml_single *single_h;
  int status = ML_ERROR_NONE;

  check_feature_state ();

  if (!single || timeout == 0)
    return ML_ERROR_INVALID_PARAMETER;

  ML_SINGLE_GET_VALID_HANDLE (single_h, single);
  g_mutex_lock (&single_h->lock);
  G_UNLOCK (magic);

  single_h->timeout = (guint) timeout;

  g_mutex_unlock (&single_h->lock);
  return status;
#else
  return ML_ERROR_NOT_SUPPORTED;
#endif
}
