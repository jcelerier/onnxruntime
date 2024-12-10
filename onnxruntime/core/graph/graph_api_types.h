// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/framework/ort_value.h"
#include "core/graph/onnx_protobuf.h"

// ORT C interface types for OrtGraphApi can't be in a namespace.
// We need to define them here so onnxruntime::Model can be created from OrtModel.

struct OrtShape {
  ONNX_NAMESPACE::TensorShapeProto shape_proto;
};

struct OrtValueInfo {
  ONNX_NAMESPACE::ValueInfoProto value_info_proto;
};

struct OrtOpAttr {
  ONNX_NAMESPACE::AttributeProto attr_proto;
};

struct OrtNode {
  std::string operator_name;
  std::string domain_name;
  std::string node_name;

  // OrtOpAttr is 1:1 with ONNX_NAMESPACE::AttributeProto currently.
  // https://github.com/microsoft/onnxruntime/blob/bd5a759d0cdbed6e7f611c990d4eb5457a9ecf60/onnxruntime/core/session/standalone_op_invoker.cc#L318
  // Might be better if it had a wrapper struct so we have more flexibility.
  // AFAIK (TBC) that's an implementation detail so we should be able to change it.
  std::vector<ONNX_NAMESPACE::AttributeProto> attributes;
  std::vector<std::string> input_names;
  std::vector<std::string> output_names;

  // FUTURE if we need control flow nodes
  // std::unordered_map<std::string, OrtGraph> subgraphs;
};

struct OrtGraph {
  std::vector<std::unique_ptr<OrtValueInfo>> inputs;
  std::vector<std::unique_ptr<OrtValueInfo>> outputs;
  std::unordered_map<std::string, std::unique_ptr<OrtValue>> initializers;
  std::vector<std::unique_ptr<OrtNode>> nodes;
};

struct OrtModel {
  std::unique_ptr<OrtGraph> graph;
  std::unordered_map<std::string, int> domain_to_version;
};