// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#pragma once

#include <functional>
#include <numeric>
#include <string>
#include <type_traits>
#include <vector>

#include "QnnTypes.h"
#include "core/session/onnxruntime_cxx_api.h"
#include "core/framework/node_unit.h"
#include "core/framework/tensor_shape.h"
#include "core/util/qmath.h"

namespace onnxruntime {
namespace qnn {
class QnnOpConfigWrapper;

namespace utils {
size_t GetElementSizeByType(const Qnn_DataType_t& data_type);

size_t GetElementSizeByType(ONNXTensorElementDataType elem_type);

size_t GetElementSizeByType(ONNX_NAMESPACE::TensorProto_DataType onnx_type);

// TODO: make these work with Wrappers?
std::ostream& operator<<(std::ostream& out, const Qnn_Param_t& qnn_param);
std::ostream& operator<<(std::ostream& out, const Qnn_Tensor_t& tensor);
std::ostream& operator<<(std::ostream& out, const QnnOpConfigWrapper& op_conf_wrapper);

Status GetQnnDataType(const bool is_quantized_tensor, const ONNX_NAMESPACE::TypeProto* type_proto,
                      Qnn_DataType_t& tensor_data_type);

const std::string& GetNodeName(const NodeUnit& node_unit);

bool OnnxDataTypeToQnnDataType(const int32_t data_type, Qnn_DataType_t& qnn_data_type, bool is_quantized = false);

inline Status GetOnnxTensorElemDataType(const NodeArg& node_arg, /*out*/ int32_t& onnx_data_type) {
  auto type_proto = node_arg.TypeAsProto();
  ORT_RETURN_IF_NOT(type_proto != nullptr && type_proto->has_tensor_type() && type_proto->tensor_type().has_elem_type(),
                    "NodeArg must have a tensor TypeProto");
  onnx_data_type = type_proto->tensor_type().elem_type();
  return Status::OK();
}

template <typename IntType>
static Status InvertPerm(gsl::span<const IntType> perm, /*out*/ gsl::span<IntType> perm_inv) {
  static_assert(std::is_integral<IntType>::value, "permutation arrays must contain integer elements");

  size_t rank = perm.size();
  ORT_RETURN_IF_NOT(perm_inv.size() == rank, "perm.size() != perm_inv.size()");

  for (size_t i = 0; i < rank; ++i) {
    size_t j = static_cast<size_t>(perm[i]);
    ORT_RETURN_IF_NOT(j < rank, "perm element out of range [0, rank - 1]");
    perm_inv[j] = static_cast<IntType>(i);
  }

  return Status::OK();
}

// Utility function that checks if an array of strings contains a specific string.
// Used to validate ONNX operator attributes.
template <size_t N>
static bool ArrayHasString(const std::array<std::string_view, N>& strings, std::string_view str) {
  for (auto s : strings) {
    if (s == str) {
      return true;
    }
  }

  return false;
}

std::pair<float, float> CheckMinMax(float rmin, float rmax);

template <typename T>
Status GetQminQmax(const Qnn_DataType_t qnn_data_type, T& qmin, T& qmax);

template <typename T>
inline T Saturate(const T qmax,
                  const T qmin,
                  const T quant_value) {
  if (quant_value > qmax) {
    return qmax;
  } else if (quant_value < qmin) {
    return qmin;
  } else {
    return quant_value;
  }
}

Status GetQuantParams(float rmin,
                      float rmax,
                      const Qnn_DataType_t qnn_data_type,
                      float& scale,
                      int32_t& zero_point,
                      bool symmetric = false);

double Dequantize(int32_t offset, float scale, const double quant_value);

Status Quantize(const double double_value,
                const float scale,
                const int32_t zero_point,
                const Qnn_DataType_t qnn_data_type,
                int& quant_value);

// Re-writes a buffer of packed 4-bit elements to a buffer of unpacked 8-bit elements.
// QNN requires that 4-bit weights are unpacked to 8-bit.
template <bool Signed>
Status UnpackInt4ToInt8(size_t num_int4_elems, std::vector<uint8_t>& data_bytes) {
  if constexpr (Signed) {  // INT4
    std::vector<uint8_t> packed_int4_bytes = std::move(data_bytes);
    data_bytes = std::vector<uint8_t>(num_int4_elems);

    auto dst = gsl::make_span(reinterpret_cast<int8_t*>(data_bytes.data()), data_bytes.size());
    auto src = gsl::make_span(reinterpret_cast<const Int4x2*>(packed_int4_bytes.data()), packed_int4_bytes.size());
    ORT_RETURN_IF_NOT(Int4x2::Unpack(dst, src), "Failed to unpack Tensor<Int4x2> for QNN");

    // NOTE: Masking off top 4 bits to workaround a QNN INT4 accuracy bug.
    // Docs explicitly state that masking off top 4 bits should not be required, but we have to do it.
    for (size_t i = 0; i < dst.size(); i++) {
      dst[i] &= 0x0F;  // -3 (0b1111_1101) becomes 13 (0b0000_1101)
    }
  } else {  // UINT4
    std::vector<uint8_t> packed_uint4_bytes = std::move(data_bytes);
    data_bytes = std::vector<uint8_t>(num_int4_elems);

    auto dst = gsl::make_span(reinterpret_cast<uint8_t*>(data_bytes.data()), data_bytes.size());
    auto src = gsl::make_span(reinterpret_cast<const UInt4x2*>(packed_uint4_bytes.data()), packed_uint4_bytes.size());
    ORT_RETURN_IF_NOT(UInt4x2::Unpack(dst, src), "Failed to unpack Tensor<UInt4x2> for QNN");
  }

  return Status::OK();
}

template <typename T>
std::vector<T> GetInitializerShape(const ONNX_NAMESPACE::TensorProto& tensor_proto) {
  const auto& dims = tensor_proto.dims();
  std::vector<T> tensor_shape_vec(static_cast<size_t>(dims.size()));
  for (int i = 0; i < dims.size(); ++i) {
    tensor_shape_vec[i] = static_cast<T>(dims[i]);
  }

  return tensor_shape_vec;
}

template <typename T, typename P>
Status PermuteShape(gsl::span<const T> input_shape, gsl::span<const P> perm, gsl::span<T> output_shape) {
  const size_t rank = input_shape.size();
  ORT_RETURN_IF_NOT(rank == perm.size() && rank == output_shape.size(),
                    "PermuteShape(): expect all arguments to have the same rank.");

  for (size_t i = 0; i < rank; ++i) {
    size_t p = static_cast<size_t>(perm[i]);
    output_shape[i] = input_shape[p];
  }

  return Status::OK();
}

/**
 * Wrapping onnxruntime::Node for retrieving attribute values
 */
class NodeAttrHelper {
 public:
  explicit NodeAttrHelper(const Node& node);

  // Get the attributes from the target node of the node_unit
  explicit NodeAttrHelper(const NodeUnit& node_unit);

  /*
   * Get with default
   */
  float Get(const std::string& key, float def_val) const;
  std::vector<float> Get(const std::string& key, const std::vector<float>& def_val) const;

  int64_t Get(const std::string& key, int64_t def_val) const;
  std::vector<int64_t> Get(const std::string& key, const std::vector<int64_t>& def_val) const;

  const std::string& Get(const std::string& key, const std::string& def_val) const;

  // Convert the i() or ints() of the attribute from int64_t to int32_t
  int32_t Get(const std::string& key, int32_t def_val) const;
  std::vector<int32_t> Get(const std::string& key, const std::vector<int32_t>& def_val) const;

  // Convert the i() or ints() of the attribute from int64_t to uint32_t
  uint32_t Get(const std::string& key, uint32_t def_val) const;
  std::vector<uint32_t> Get(const std::string& key, const std::vector<uint32_t>& def_val) const;

  /*
   * Get without default.
   */
  std::optional<float> GetFloat(const std::string& key) const;
  std::optional<std::vector<float>> GetFloats(const std::string& key) const;

  std::optional<int64_t> GetInt64(const std::string& key) const;
  std::optional<std::vector<int64_t>> GetInt64s(const std::string& key) const;

  std::optional<std::string> GetString(const std::string& key) const;

  bool HasAttr(const std::string& key) const;

 private:
  const NodeAttributes& node_attributes_;
};

// Get the min/max of a Clip operator. Reads values from attributes for opset < 11 and inputs after that.
// For opset 11+, if min/max are not constant initializers, will return false.
// For now we only support getting float min/max.
bool GetClipMinMax(const GraphViewer& graph_viewer, const Node& node,
                   float& min, float& max, const logging::Logger& logger);
}  // namespace utils
}  // namespace qnn
}  // namespace onnxruntime
