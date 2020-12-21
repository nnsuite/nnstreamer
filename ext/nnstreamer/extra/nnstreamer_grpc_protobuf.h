/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * GStreamer / NNStreamer gRPC/protobuf support
 * Copyright (C) 2020 Dongju Chae <dongju.chae@samsung.com>
 */
/**
 * @file    nnstreamer_grpc_protobuf.h
 * @date    21 Oct 2020
 * @brief   gRPC/Protobuf wrappers for nnstreamer
 * @see     https://github.com/nnstreamer/nnstreamer
 * @author  Dongju Chae <dongju.chae@samsung.com>
 * @bug     No known bugs except for NYI items
 */

#ifndef __NNS_GRPC_PROTOBUF_H__
#define __NNS_GRPC_PROTOBUF_H__

#include "nnstreamer_grpc_common.h"
#include "nnstreamer.grpc.pb.h" /* Generated by `protoc` */

namespace grpc {

/**
 * @brief NNStreamer gRPC protobuf service impl.
 */
class ServiceImplProtobuf final
  : public NNStreamerRPC,
    public nnstreamer::protobuf::TensorService::Service
{
  public:
    ServiceImplProtobuf (const grpc_config * config);

    Status SendTensors (ServerContext *context,
        ServerReader<nnstreamer::protobuf::Tensors> *reader,
        google::protobuf::Empty *reply) override;

    Status RecvTensors (ServerContext *context,
        const google::protobuf::Empty *request,
        ServerWriter<nnstreamer::protobuf::Tensors> *writer) override;

  private:
    gboolean start_server (std::string address) override;
    gboolean start_client (std::string address) override;

    template <typename T>
    grpc::Status _write_tensors (T writer);

    template <typename T>
    grpc::Status _read_tensors (T reader);

    void _get_tensors_from_buffer (
        GstBuffer *buffer,
        nnstreamer::protobuf::Tensors &tensors);
    void _get_buffer_from_tensors (
        nnstreamer::protobuf::Tensors &tensors,
        GstBuffer **buffer);

    void _client_thread ();

    std::unique_ptr<nnstreamer::protobuf::TensorService::Stub> client_stub_;
};

}; // namespace grpc

#endif /* __NNS_GRPC_PROTOBUF_H__ */
