/**
 * Copyright (c) 2019 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file nnstreamer-single.h
 * @date 29 March 2019
 * @brief NNStreamer single-shot invocation C-API Header.
 *        This allows to invoke a neural network model directly.
 * @see	https://github.com/nnsuite/nnstreamer
 * @author MyungJoo Ham <myungjoo.ham@samsung.com>
 * @bug No known bugs except for NYI items
 *
 * @detail This is targetting Tizen 5.5 M2.
 */

#ifndef __TIZEN_MACHINELEARNING_NNSTREAMER_SINGLE_H__
#define __TIZEN_MACHINELEARNING_NNSTREAMER_SINGLE_H__

#include <nnstreamer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/**
 * @addtogroup CAPI_ML_NNSTREAMER_SINGLE_MODULE
 * @{
 */

/**
 * @brief A handle of a single-shot instance.
 * @since_tizen 5.5
 */
typedef void *ml_single_h;

/*************
 * MAIN FUNC *
 *************/
/**
 * @brief Opens an ML model and returns the instance as a handle.
 * @since_tizen 5.5
 * @param[out] single This is the model handle opened. Users are required to close
 *                   the given instance with ml_single_close().
 * @param[in] model This is the path to the neural network model file.
 * @param[in] input_info This is required if the given model has flexible input
 *                      dimension, where the input dimension MUST be given
 *                      before executing the model.
 *                      However, once it's given, the input dimension cannot
 *                      be changed for the given model handle.
 *                      It is required by some custom filters of NNStreamer.
 *                      You may set NULL if it's not required.
 * @param[in] output_info This is required if the given model has flexible output dimension.
 * @param[in] nnfw The nerual network framework used to open the given
 *                 #model_path. Set #ML_NNFW_TYPE_ANY to let it auto-detect.
 * @param[in] hw Tell the corresponding @nnfw to use a specific hardware.
 *               Set #ML_NNFW_HW_ANY if it does not matter.
 * @return @c 0 on success. otherwise a negative error value
 * @retval #ML_ERROR_NONE Successful
 * @retval #ML_ERROR_INVALID_PARAMETER Fail. The parameter is invalid.
 * @retval #ML_ERROR_STREAMS_PIPE Failed to start the pipeline.
 *
 * @detail Even if the model has flexible input data dimensions,
 *         input data frames of an instance of a model should share the
 *         same dimension.
 */
int ml_single_open (ml_single_h *single, const char *model, const ml_tensors_info_h input_info, const ml_tensors_info_h output_info, ml_nnfw_type_e nnfw, ml_nnfw_hw_e hw);

/**
 * @brief Closes the opened model handle.
 * @since_tizen 5.5
 * @param[in] single The model handle to be closed.
 * @return @c 0 on success. otherwise a negative error value
 * @retval #ML_ERROR_NONE Successful
 * @retval #ML_ERROR_INVALID_PARAMETER Fail. The parameter is invalid (pipe is NULL?)
 */
int ml_single_close (ml_single_h single);

/**
 * @brief Invokes the model with the given input data.
 * @since_tizen 5.5
 * @param[in] single The model handle to be inferred.
 * @param[in] input The input data to be inferred.
 * @param[out] output The output buffer. Set NULL if you want to let
 *                    this function to allocate a new output buffer.
 * @return @c The output buffer. If @output is NULL, this is a newly
 *         allocated buffer; thus, the user needs to free it.
 *         If there is an error, this is set NULL. Check ml_util_get_last_error()
 *         of tizen_error.h in such cases.
 * @retval #ML_ERROR_NONE Successful
 * @retval #ML_ERROR_INVALID_PARAMETER Fail. The parameter is invalid.
 * @retval #ML_ERROR_STREAMS_PIPE Cannot push a buffer into source element.
 * @retval #ML_ERROR_TIMED_OUT Failed to get the result from sink element.
 *
 * @detail Even if the model has flexible input data dimensions,
 *         input data frames of an instance of a model should share the
 *         same dimension.
 */
ml_tensors_data_s * ml_single_inference (ml_single_h single, const ml_tensors_data_s *input, ml_tensors_data_s *output);

/*************
 * UTILITIES *
 *************/

/**
 * @brief Gets the type (tensor dimension, type, name and so on) of required input
 *        data for the given handle.
 * @detail Note that a model may not have such
 *         information if its input type is flexible.
 *         Besides, names of tensors may be not available while dimensions and
 *         types are available.
 * @since_tizen 5.5
 * @param[in] single The model handle to be investigated.
 * @param[out] info The handle of input tensors information. Caller is responsible to free the information with ml_util_destroy_tensors_info().
 * @return @c 0 on success. otherwise a negative error value.
 * @retval #ML_ERROR_NONE Successful
 * @retval #ML_ERROR_INVALID_PARAMETER Fail. The parameter is invalid.
 */
int ml_single_get_input_info (ml_single_h single, ml_tensors_info_h *info);

/**
 * @brief Gets the type (tensor dimension, type, name and so on) of output
 *        data for the given handle.
 * @detail Note that a model may not have such
 *         information if its input type is flexible and output type is
 *         not determined statically.
 *         Besides, names of tensors may be not available while dimensions and
 *         types are available.
 * @since_tizen 5.5
 * @param[in] single The model handle to be investigated.
 * @param[out] info The handle of output tensors information. Caller is responsible to free the returned with ml_util_destroy_tensors_info().
 * @return @c 0 on success. otherwise a negative error value.
 * @retval #ML_ERROR_NONE Successful
 * @retval #ML_ERROR_INVALID_PARAMETER Fail. The parameter is invalid.
 */
int ml_single_get_output_info (ml_single_h single, ml_tensors_info_h *info);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* __TIZEN_MACHINELEARNING_NNSTREAMER_SINGLE_H__ */
