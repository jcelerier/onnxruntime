// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/common/status.h"
#include "QnnInterface.h"
#include "qnn_def.h"
#include "core/common/logging/logging.h"
#include "core/framework/node_unit.h"
#include "core/graph/graph_viewer.h"
#include "core/providers/shared/utils/utils.h"
#include "core/providers/qnn/builder/qnn_quant_params_wrapper.h"

namespace onnxruntime {
namespace qnn {

// Stores information about an ONNX input or output tensor.
// Filled out by QnnModelWrapper::GetTensorInfo()
struct TensorInfo {
  std::vector<uint32_t> shape;
  Qnn_DataType_t qnn_data_type;
  QnnQuantParamsWrapper quant_param;
  bool is_initializer;
  const ONNX_NAMESPACE::TensorProto* initializer_tensor;
};

struct ModelSettings {
  bool offload_graph_io_quantization = false;
};

class QnnModelWrapper {
 public:
  QnnModelWrapper(const GraphViewer& graph_viewer,
                  const logging::Logger& logger,
                  const QNN_INTERFACE_VER_TYPE& qnn_interface,
                  const Qnn_BackendHandle_t& backend_handle,
                  const std::unordered_map<std::string, size_t>& input_index_map,
                  const std::unordered_map<std::string, size_t>& output_index_map,
                  const std::unordered_set<std::string>& initializer_lookup,
                  QnnBackendType qnn_backend_type,
                  const ModelSettings& model_settings)
      : graph_viewer_(graph_viewer),
        logger_(logger),
        qnn_interface_(qnn_interface),
        backend_handle_(backend_handle),
        input_index_map_(input_index_map),
        output_index_map_(output_index_map),
        initializer_lookup_(initializer_lookup),
        qnn_backend_type_(qnn_backend_type),
        model_settings_(model_settings) {
  }
  ORT_DISALLOW_COPY_ASSIGNMENT_AND_MOVE(QnnModelWrapper);

  ~QnnModelWrapper() = default;

  const ModelSettings& GetModelSettings() const { return model_settings_; }

  bool CreateQnnGraph(const Qnn_ContextHandle_t& context,
                      const std::string& graph_name,
                      const QnnGraph_Config_t** graph_configs = nullptr);

  // Make a QnnTensorWrapper from an onnx input or output.
  Status MakeTensorWrapper(const NodeUnitIODef& tensor, QnnTensorWrapper& tensor_wrapper) const;

  // Add to internal tensor wrapper table
  bool AddTensorWrapper(QnnTensorWrapper&& tensor_wrapper);

  // Add to internal param wrapper table
  bool AddParamWrapper(QnnParamWrapper&& param_wrapper);

  const QnnTensorWrapper& GetQnnTensorWrapper(const std::string& tensor_name);

  // Utility function to validate a QNN node. Does not modify this object's state.
  Status ValidateQnnNode(const std::string& node_name,
                         const std::string& package_name,
                         const std::string& qnn_op_type,
                         std::vector<Qnn_Tensor_t>&& input_tensors,
                         std::vector<Qnn_Tensor_t>&& output_tensors,
                         std::vector<Qnn_Param_t>&& params) const;

  bool CreateQnnNode(const std::string& name,
                     const std::string& package_name,
                     const std::string& type,
                     std::vector<std::string>&& input_names,
                     std::vector<std::string>&& output_names,
                     std::vector<std::string>&& param_tensor_names,
                     bool do_op_validation = false);

  bool ComposeQnnGraph();

  Qnn_GraphHandle_t GetQnnGraph() { return graph_; }

  std::string GetQnnGraphName() const { return graph_name_; }

  // Move input tensor wrappers to GraphInfo, QnnModelWrapper end of live
  std::vector<QnnTensorWrapper>&& GetGraphInputTensorWrappers() {
    GetGraphInputOutputTensorWrapper(model_input_names_, model_input_tensor_wrappers_);
    return std::move(model_input_tensor_wrappers_);
  }

  // Move output tensor wrappers to GraphInfo, QnnModelWrapper end of live
  std::vector<QnnTensorWrapper>&& GetGraphOutputTensorWrappers() {
    GetGraphInputOutputTensorWrapper(model_output_names_, model_output_tensor_wrappers_);
    return std::move(model_output_tensor_wrappers_);
  }

  const InitializedTensorSet& GetInitializerTensors() const { return graph_viewer_.GetAllInitializedTensors(); }

  const ONNX_NAMESPACE::TensorProto* GetInitializerTensor(const std::string& tensor_name) const {
    return graph_viewer_.GetConstantInitializer(tensor_name, true);
  }

  bool IsInitializerInput(std::string input_name) const {
    return initializer_lookup_.find(input_name) != initializer_lookup_.end();
  }

  static bool GetOnnxShape(const NodeArg& node_arg, std::vector<uint32_t>& shape);

  bool IsQnnTensorWrapperExist(const std::string& tensor_name) const;

  bool IsGraphOutput(const std::string& tensor_name) const {
    return output_index_map_.find(tensor_name) != output_index_map_.end();
  }

  bool IsGraphInput(const std::string& tensor_name) const {
    return input_index_map_.find(tensor_name) != input_index_map_.end();
  }

  Qnn_TensorType_t GetTensorType(const std::string& tensor_name) const {
    if (IsInitializerInput(tensor_name)) {
      return QNN_TENSOR_TYPE_STATIC;
    } else if (IsGraphInput(tensor_name)) {
      return QNN_TENSOR_TYPE_APP_WRITE;
    } else if (IsGraphOutput(tensor_name)) {
      return QNN_TENSOR_TYPE_APP_READ;
    } else {
      return QNN_TENSOR_TYPE_NATIVE;
    }
  }

  Status GetTensorInfo(const NodeUnitIODef& input, TensorInfo& input_info) const;

  Status AddReshapeNode(const std::string& input_name,
                        const std::string& output_name,
                        const std::vector<uint32_t>& input_shape,
                        const std::vector<uint32_t>& output_shape,
                        const Qnn_DataType_t& tensor_data_type,
                        const QnnQuantParamsWrapper& quantize_param,
                        bool do_op_validation,
                        bool is_for_input = true,
                        bool is_for_output = false);

  Status AddTransposeNode(NodeIndex node_index,
                          const std::string& input_name,
                          const std::string& output_name,
                          const std::vector<uint32_t>& input_shape,
                          const std::vector<uint32_t>& transpose_perm,
                          const std::vector<uint32_t>& output_shape,
                          const Qnn_DataType_t& tensor_data_type,
                          const QnnQuantParamsWrapper& quantize_param,
                          bool do_op_validation,
                          bool is_for_input = true,
                          bool is_for_output = false);

  // Tranpose NCHW->HWCN for QNN weight
  Status AddNchwToHwcnTranspose(NodeIndex node_index,
                                const std::string& input_name,
                                const std::string& output_name,
                                const std::vector<uint32_t>& input_shape,
                                const std::vector<uint32_t>& output_shape,
                                const Qnn_DataType_t& tensor_data_type,
                                const QnnQuantParamsWrapper& quantize_param,
                                bool do_op_validation,
                                bool is_for_input = true,
                                bool is_for_output = false,
                                bool is_3d = false) {
    LOGS(logger_, VERBOSE) << "Add NCHW->HWCN Transpose node after Conv weight input: " << input_name
                           << " -> " << output_name;
    auto perm = is_3d ? nchw2hwcn_perm_3d : nchw2hwcn_perm;
    std::vector<uint32_t> transpose_perm;
    transpose_perm.resize(perm.size());
    std::transform(perm.begin(), perm.end(),
                   transpose_perm.begin(), [](size_t item) -> uint32_t {
                     return narrow<uint32_t>(item);
                   });
    return AddTransposeNode(node_index, input_name, output_name, input_shape, transpose_perm, output_shape,
                            tensor_data_type, quantize_param, do_op_validation, is_for_input, is_for_output);
  }

  // Tranpose CNHW->HWCN for QNN weight
  Status AddCnhwToHwcnTranspose(NodeIndex node_index,
                                const std::string& input_name,
                                const std::string& output_name,
                                const std::vector<uint32_t>& input_shape,
                                const std::vector<uint32_t>& output_shape,
                                const Qnn_DataType_t& tensor_data_type,
                                const QnnQuantParamsWrapper& quantize_param,
                                bool do_op_validation,
                                bool is_for_input = true,
                                bool is_for_output = false,
                                bool is_3d = false) {
    LOGS(logger_, VERBOSE) << "Add CNHW->HWCN Transpose node after ConvTranspose weight input: " << input_name
                           << " -> " << output_name;
    auto perm = is_3d ? cnhw2hwcn_perm_3d : cnhw2hwcn_perm;
    std::vector<uint32_t> transpose_perm;
    transpose_perm.resize(perm.size());
    std::transform(perm.begin(), perm.end(),
                   transpose_perm.begin(), [](size_t item) -> uint32_t {
                     return narrow<uint32_t>(item);
                   });
    return AddTransposeNode(node_index, input_name, output_name, input_shape, transpose_perm, output_shape,
                            tensor_data_type, quantize_param, do_op_validation, is_for_input, is_for_output);
  }

  Status UnpackInitializerData(const ONNX_NAMESPACE::TensorProto& initializer,
                               std::vector<uint8_t>& unpacked_tensor) const;

  QnnBackendType GetQnnBackendType() const { return qnn_backend_type_; }

  const GraphViewer& GetGraphViewer() const { return graph_viewer_; }

  // Unpack float scales from initializer (1 scale for per-tensor, > 1 for per-axis).
  Status UnpackScales(const std::string& initializer_name, std::vector<float>& scales) const;

  // Unpack zero-points from initializer and convert to int32_t (1 zero-point for per-tensor, > 1 for per-channel).
  Status UnpackZeroPoints(const std::string& initializer_name,
                          /*out*/ std::vector<int32_t>& zero_points,
                          /*out*/ int32_t& onnx_data_type) const;

  // Checks if a tensor in the ONNX graph is per-channel quantized.
  Status IsPerChannelQuantized(const onnxruntime::NodeUnitIODef& io_def,
                               /*out*/ bool& is_per_channel,
                               /*out*/ int64_t& axis) const;

 private:
  bool CreateQnnInputOutputTensors(const std::string& qnn_node_name,
                                   const std::vector<std::string>& names,
                                   std::vector<Qnn_Tensor_t>& tensor_wrappers,
                                   bool do_op_validation = false);

  bool IsQnnParamExit(const std::string& param_tensor_name) const;

  bool CreateQnnParamTensors(const std::string& qnn_node_name,
                             const std::vector<std::string>& param_tensor_names,
                             std::vector<Qnn_Param_t>& qnn_params,
                             bool do_op_validation = false);

  bool IsQDQNode(const Node& node) const {
    if (node.OpType() == "QuantizeLinear" || node.OpType() == "DequantizeLinear") {
      return true;
    }
    return false;
  }

  bool IsQnnTensorCreated(const std::string& tensor_name) {
    auto pos = tensor_created_map_.find(tensor_name);
    if (pos == tensor_created_map_.end()) {
      return false;
    }
    return pos->second;
  }

  void GetGraphInputOutputTensorWrapper(const std::vector<std::string>& names,
                                        std::vector<QnnTensorWrapper>& wrappers_list);

  const GraphViewer& graph_viewer_;
  const logging::Logger& logger_;
  const QNN_INTERFACE_VER_TYPE& qnn_interface_;
  const Qnn_BackendHandle_t& backend_handle_;
  Qnn_GraphHandle_t graph_ = nullptr;
  std::string graph_name_ = "";

  std::vector<std::string> model_input_names_;
  std::vector<std::string> model_output_names_;
  std::vector<QnnTensorWrapper> model_input_tensor_wrappers_;
  std::vector<QnnTensorWrapper> model_output_tensor_wrappers_;
  // All QnnTensorWrapper for the graph
  std::unordered_map<std::string, QnnTensorWrapper> model_tensors_map_;
  // All QnnParamWrapper for the graph
  std::unordered_map<std::string, QnnParamWrapper> model_params_map_;
  std::vector<QnnOpProperty> qnn_op_property_list_;
  // <tensor_name, qnn_tensor_created> -- true means qnn tensor created in qnn graph
  // it includs normal qnn_tensors and qnn_tensors inside param_tensors
  std::unordered_map<std::string, bool> tensor_created_map_;
  const std::unordered_map<std::string, size_t>& input_index_map_;
  const std::unordered_map<std::string, size_t>& output_index_map_;
  const std::unordered_set<std::string>& initializer_lookup_;
  QnnBackendType qnn_backend_type_ = QnnBackendType::CPU;
  ModelSettings model_settings_ = {};
};  // QnnModelWrapper

}  // namespace qnn
}  // namespace onnxruntime
