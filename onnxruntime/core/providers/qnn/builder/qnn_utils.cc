// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <functional>
#include <numeric>
#include <string>
#include <vector>
#include <map>

#include "core/common/common.h"
#include "core/framework/data_types.h"
#include "core/framework/tensorprotoutils.h"
#include "qnn_utils.h"
#include "core/providers/qnn/builder/qnn_def.h"
#include "core/graph/graph_viewer.h"

namespace onnxruntime {
namespace qnn {
namespace utils {

size_t GetElementSizeByType(const Qnn_DataType_t& data_type) {
  const static std::unordered_map<Qnn_DataType_t, size_t> data_type_to_size = {
      {QNN_DATATYPE_INT_8, 1},
      {QNN_DATATYPE_INT_16, 2},
      {QNN_DATATYPE_INT_32, 4},
      {QNN_DATATYPE_INT_64, 8},
      {QNN_DATATYPE_UINT_8, 1},
      {QNN_DATATYPE_UINT_16, 2},
      {QNN_DATATYPE_UINT_32, 4},
      {QNN_DATATYPE_UINT_64, 8},
      {QNN_DATATYPE_FLOAT_16, 2},
      {QNN_DATATYPE_FLOAT_32, 4},
      {QNN_DATATYPE_BOOL_8, 1},
      {QNN_DATATYPE_SFIXED_POINT_8, 1},
      {QNN_DATATYPE_SFIXED_POINT_16, 2},
      {QNN_DATATYPE_SFIXED_POINT_32, 4},
      {QNN_DATATYPE_UFIXED_POINT_8, 1},
      {QNN_DATATYPE_UFIXED_POINT_16, 2},
      {QNN_DATATYPE_UFIXED_POINT_32, 4},
  };

  auto pos = data_type_to_size.find(data_type);
  ORT_ENFORCE(pos != data_type_to_size.end(), "Unknown QNN data type", data_type);
  return pos->second;
}
size_t GetElementSizeByType(ONNXTensorElementDataType elem_type) {
  const static std::unordered_map<ONNXTensorElementDataType, size_t> elem_type_to_size = {
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT4, sizeof(Int4x2)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT4, sizeof(UInt4x2)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8, sizeof(int8_t)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16, sizeof(int16_t)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32, sizeof(int32_t)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, sizeof(int64_t)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8, sizeof(uint8_t)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16, sizeof(uint16_t)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32, sizeof(uint32_t)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64, sizeof(uint64_t)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16, 2},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, sizeof(float)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE, sizeof(double)},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL, sizeof(bool)}};

  auto pos = elem_type_to_size.find(elem_type);
  ORT_ENFORCE(pos != elem_type_to_size.end(), "Unknown element type", elem_type);
  return pos->second;
}

size_t GetElementSizeByType(ONNX_NAMESPACE::TensorProto_DataType onnx_type) {
  switch (onnx_type) {
    case ONNX_NAMESPACE::TensorProto_DataType_INT4:
      return sizeof(Int4x2);
    case ONNX_NAMESPACE::TensorProto_DataType_UINT4:
      return sizeof(UInt4x2);
    case ONNX_NAMESPACE::TensorProto_DataType_INT8:
      return sizeof(int8_t);
    case ONNX_NAMESPACE::TensorProto_DataType_UINT8:
      return sizeof(uint8_t);
    case ONNX_NAMESPACE::TensorProto_DataType_INT16:
      return sizeof(int16_t);
    case ONNX_NAMESPACE::TensorProto_DataType_UINT16:
      return sizeof(uint16_t);
    case ONNX_NAMESPACE::TensorProto_DataType_INT32:
      return sizeof(int32_t);
    case ONNX_NAMESPACE::TensorProto_DataType_UINT32:
      return sizeof(uint32_t);
    case ONNX_NAMESPACE::TensorProto_DataType_INT64:
      return sizeof(int64_t);
    case ONNX_NAMESPACE::TensorProto_DataType_UINT64:
      return sizeof(uint64_t);
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT16:
      return 2;
    case ONNX_NAMESPACE::TensorProto_DataType_FLOAT:
      return sizeof(float);
    case ONNX_NAMESPACE::TensorProto_DataType_DOUBLE:
      return sizeof(double);
    case ONNX_NAMESPACE::TensorProto_DataType_BOOL:
      return sizeof(bool);
    default:
      return 0;
  }
  // Unreachable
}

std::ostream& operator<<(std::ostream& out, const Qnn_Scalar_t& scalar) {
  switch (scalar.dataType) {
    case QNN_DATATYPE_INT_8:
      out << static_cast<int32_t>(scalar.int8Value);
      break;
    case QNN_DATATYPE_INT_16:
      out << scalar.int16Value;
      break;
    case QNN_DATATYPE_INT_32:
      out << scalar.int32Value;
      break;
    case QNN_DATATYPE_INT_64:
      out << "int64_t is not supported";
      break;
    case QNN_DATATYPE_UINT_8:
      out << static_cast<int32_t>(scalar.uint8Value);
      break;
    case QNN_DATATYPE_UINT_16:
      out << scalar.uint16Value;
      break;
    case QNN_DATATYPE_UINT_32:
      out << scalar.uint32Value;
      break;
    case QNN_DATATYPE_UINT_64:
      out << "uint64_t is not supported";
      break;
    case QNN_DATATYPE_FLOAT_16:
      break;
    case QNN_DATATYPE_FLOAT_32:
      out << scalar.floatValue;
      break;
    case QNN_DATATYPE_SFIXED_POINT_8:
    case QNN_DATATYPE_SFIXED_POINT_16:
    case QNN_DATATYPE_SFIXED_POINT_32:
    case QNN_DATATYPE_UFIXED_POINT_8:
    case QNN_DATATYPE_UFIXED_POINT_16:
    case QNN_DATATYPE_UFIXED_POINT_32:
      out << "usigned fixedpoint data is not supported";
      break;
    case QNN_DATATYPE_BOOL_8:
      out << static_cast<int32_t>(scalar.bool8Value);
      break;
    default:
      ORT_THROW("Unknown Qnn Data type");
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_DataType_t& data_type) {
  switch (data_type) {
    case QNN_DATATYPE_INT_8:
      out << "QNN_DATATYPE_INT_8";
      break;
    case QNN_DATATYPE_INT_16:
      out << "QNN_DATATYPE_INT_16";
      break;
    case QNN_DATATYPE_INT_32:
      out << "QNN_DATATYPE_INT_32";
      break;
    case QNN_DATATYPE_INT_64:
      out << "QNN_DATATYPE_INT_64";
      break;
    case QNN_DATATYPE_UINT_8:
      out << "QNN_DATATYPE_UINT_8";
      break;
    case QNN_DATATYPE_UINT_16:
      out << "QNN_DATATYPE_UINT_16";
      break;
    case QNN_DATATYPE_UINT_32:
      out << "QNN_DATATYPE_UINT_32";
      break;
    case QNN_DATATYPE_UINT_64:
      out << "QNN_DATATYPE_UINT_64";
      break;
    case QNN_DATATYPE_FLOAT_16:
      out << "QNN_DATATYPE_FLOAT_16";
      break;
    case QNN_DATATYPE_FLOAT_32:
      out << "QNN_DATATYPE_FLOAT_32";
      break;
    case QNN_DATATYPE_SFIXED_POINT_8:
      out << "QNN_DATATYPE_SFIXED_POINT_8";
      break;
    case QNN_DATATYPE_SFIXED_POINT_16:
      out << "QNN_DATATYPE_SFIXED_POINT_16";
      break;
    case QNN_DATATYPE_SFIXED_POINT_32:
      out << "QNN_DATATYPE_SFIXED_POINT_32";
      break;
    case QNN_DATATYPE_UFIXED_POINT_8:
      out << "QNN_DATATYPE_UFIXED_POINT_8";
      break;
    case QNN_DATATYPE_UFIXED_POINT_16:
      out << "QNN_DATATYPE_UFIXED_POINT_16";
      break;
    case QNN_DATATYPE_UFIXED_POINT_32:
      out << "QNN_DATATYPE_UFIXED_POINT_32";
      break;
    case QNN_DATATYPE_BOOL_8:
      out << "QNN_DATATYPE_BOOL_8";
      break;
    case QNN_DATATYPE_SFIXED_POINT_4:
      out << "QNN_DATATYPE_SFIXED_POINT_4";
      break;
    case QNN_DATATYPE_UFIXED_POINT_4:
      out << "QNN_DATATYPE_UFIXED_POINT_4";
      break;
    default:
      ORT_THROW("Unknown Qnn Data type");
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_Definition_t& definition) {
  switch (definition) {
    case QNN_DEFINITION_IMPL_GENERATED:
      out << "QNN_DEFINITION_IMPL_GENERATED";
      break;
    case QNN_DEFINITION_DEFINED:
      out << "QNN_DEFINITION_DEFINED";
      break;
    case QNN_DEFINITION_UNDEFINED:
      out << "QNN_DEFINITION_UNDEFINED";
      break;
    default:
      out << "Undefined";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_QuantizationEncoding_t& encoding) {
  switch (encoding) {
    case QNN_QUANTIZATION_ENCODING_SCALE_OFFSET:
      out << "QNN_QUANTIZATION_ENCODING_SCALE_OFFSET";
      break;
    case QNN_QUANTIZATION_ENCODING_AXIS_SCALE_OFFSET:
      out << "QNN_QUANTIZATION_ENCODING_AXIS_SCALE_OFFSET";
      break;
    case QNN_QUANTIZATION_ENCODING_BW_SCALE_OFFSET:
      out << "QNN_QUANTIZATION_ENCODING_BW_SCALE_OFFSET";
      break;
    case QNN_QUANTIZATION_ENCODING_BW_AXIS_SCALE_OFFSET:
      out << "QNN_QUANTIZATION_ENCODING_BW_AXIS_SCALE_OFFSET";
      break;
    case QNN_QUANTIZATION_ENCODING_UNDEFINED:
      out << "QNN_QUANTIZATION_ENCODING_UNDEFINED";
      break;
    default:
      out << "Uknown quantization encoding";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_QuantizeParams_t& quantize_params) {
  out << " encodingDefinition=" << quantize_params.encodingDefinition;
  out << " quantizationEncoding=" << quantize_params.quantizationEncoding;
  if (quantize_params.encodingDefinition == QNN_DEFINITION_IMPL_GENERATED ||
      quantize_params.encodingDefinition == QNN_DEFINITION_DEFINED) {
    if (quantize_params.quantizationEncoding == QNN_QUANTIZATION_ENCODING_SCALE_OFFSET) {
      out << " scale=" << quantize_params.scaleOffsetEncoding.scale;
      out << " offset=" << quantize_params.scaleOffsetEncoding.offset;
    } else if (quantize_params.quantizationEncoding == QNN_QUANTIZATION_ENCODING_BW_SCALE_OFFSET) {
      out << " bitwidth=" << quantize_params.bwScaleOffsetEncoding.bitwidth;
      out << " scale=" << quantize_params.bwScaleOffsetEncoding.scale;
      out << " offset=" << quantize_params.bwScaleOffsetEncoding.offset;
    } else if (quantize_params.quantizationEncoding == QNN_QUANTIZATION_ENCODING_AXIS_SCALE_OFFSET) {
      out << " axis=" << quantize_params.axisScaleOffsetEncoding.axis;
      size_t num_elems = quantize_params.axisScaleOffsetEncoding.numScaleOffsets;
      bool truncate = num_elems > 20;
      num_elems = truncate ? 20 : num_elems;
      out << " scales=(";
      for (size_t i = 0; i < num_elems; i++) {
        out << quantize_params.axisScaleOffsetEncoding.scaleOffset[i].scale << (i == num_elems - 1 ? "" : " ");
      }
      out << ") offsets=(";
      for (size_t i = 0; i < num_elems; i++) {
        out << quantize_params.axisScaleOffsetEncoding.scaleOffset[i].offset << (i == num_elems - 1 ? "" : " ");
      }
      out << (truncate ? "...)" : ")");
    } else if (quantize_params.quantizationEncoding == QNN_QUANTIZATION_ENCODING_BW_AXIS_SCALE_OFFSET) {
      out << " axis=" << quantize_params.bwAxisScaleOffsetEncoding.axis;
      out << " bw=" << quantize_params.bwAxisScaleOffsetEncoding.bitwidth;
      size_t num_elems = quantize_params.bwAxisScaleOffsetEncoding.numElements;
      bool truncate = num_elems > 20;
      num_elems = truncate ? 20 : num_elems;
      out << " scales=(";
      for (size_t i = 0; i < num_elems; i++) {
        out << quantize_params.bwAxisScaleOffsetEncoding.scales[i] << (i == num_elems - 1 ? "" : " ");
      }
      out << ") offsets=(";
      for (size_t i = 0; i < num_elems; i++) {
        out << quantize_params.bwAxisScaleOffsetEncoding.offsets[i] << (i == num_elems - 1 ? "" : " ");
      }
      out << (truncate ? "...)" : ")");
    } else {
      out << " encoding not supported.";
    }
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_TensorType_t& tensor_type) {
  switch (tensor_type) {
    case QNN_TENSOR_TYPE_APP_WRITE:
      out << "QNN_TENSOR_TYPE_APP_WRITE";
      break;
    case QNN_TENSOR_TYPE_APP_READ:
      out << "QNN_TENSOR_TYPE_APP_READ";
      break;
    case QNN_TENSOR_TYPE_APP_READWRITE:
      out << "QNN_TENSOR_TYPE_APP_READWRITE";
      break;
    case QNN_TENSOR_TYPE_NATIVE:
      out << "QNN_TENSOR_TYPE_NATIVE";
      break;
    case QNN_TENSOR_TYPE_STATIC:
      out << "QNN_TENSOR_TYPE_STATIC";
      break;
    case QNN_TENSOR_TYPE_NULL:
      out << "QNN_TENSOR_TYPE_NULL";
      break;
    default:
      out << "Unsupported type";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_TensorMemType_t& mem_type) {
  switch (mem_type) {
    case QNN_TENSORMEMTYPE_RAW:
      out << "QNN_TENSORMEMTYPE_RAW";
      break;
    case QNN_TENSORMEMTYPE_MEMHANDLE:
      out << "QNN_TENSORMEMTYPE_MEMHANDLE";
      break;
    default:
      out << "Unsupported mem type";
  }
  return out;
}
template <typename T>
std::ostream& operator<<(std::ostream& out, const Qnn_ClientBuffer_t& client_bufer) {
  T* data = reinterpret_cast<T*>(client_bufer.data);
  out << " dataSize=" << client_bufer.dataSize;
  uint32_t count = client_bufer.dataSize / sizeof(T);
  const bool truncate = count > 100;

  count = truncate ? 100 : count;  // limit to 100 data
  out << " clientBuf=(";
  for (uint32_t i = 0; i < count; i++) {
    if constexpr (sizeof(T) == 1) {
      out << static_cast<int32_t>(data[i]) << " ";
    } else {
      out << data[i] << " ";
    }
  }
  out << (truncate ? "..." : "") << ")";
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_Tensor_t& tensor) {
  out << " name=" << GetQnnTensorName(tensor);
  out << " id=" << GetQnnTensorID(tensor);
  out << " version=" << tensor.version;
  out << " type=" << GetQnnTensorType(tensor);
  out << " dataFormat=" << GetQnnTensorDataFormat(tensor);
  out << " dataType=" << GetQnnTensorDataType(tensor);
  out << " rank=" << GetQnnTensorRank(tensor);
  out << " dimensions=(";
  for (uint32_t i = 0; i < GetQnnTensorRank(tensor); i++) {
    out << GetQnnTensorDims(tensor)[i] << " ";
  }
  out << ")";
  out << " memType=" << GetQnnTensorMemType(tensor);
// TODO: the code below has compilation errors with the latest ABSL
#if 0
  if (GetQnnTensorMemType(tensor) == QNN_TENSORMEMTYPE_RAW) {
    if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_FLOAT_32) {
      operator<< <float>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_UINT_32 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_UFIXED_POINT_32) {
      operator<< <uint32_t>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_INT_32 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_SFIXED_POINT_32) {
      operator<< <int32_t>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_UINT_16 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_UFIXED_POINT_16) {
      operator<< <uint16_t>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_INT_16 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_SFIXED_POINT_16) {
      operator<< <int16_t>(out, GetQnnTensorClientBuf(tensor));
    } else if (GetQnnTensorDataType(tensor) == QNN_DATATYPE_UINT_8 ||
               GetQnnTensorDataType(tensor) == QNN_DATATYPE_UFIXED_POINT_8) {
      operator<< <uint8_t>(out, GetQnnTensorClientBuf(tensor));
    } else {
      operator<< <int8_t>(out, GetQnnTensorClientBuf(tensor));
    }
  }
#endif
  out << " quantizeParams:" << GetQnnTensorQParams(tensor);
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_ParamType_t& param_type) {
  switch (param_type) {
    case QNN_PARAMTYPE_SCALAR:
      out << "QNN_PARAMTYPE_SCALAR";
      break;
    case QNN_PARAMTYPE_TENSOR:
      out << "QNN_PARAMTYPE_TENSOR";
      break;
    default:
      out << "Unknown type";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Qnn_Param_t& qnn_param) {
  out << " type=" << qnn_param.paramType;
  out << " name=" << qnn_param.name;
  if (qnn_param.paramType == QNN_PARAMTYPE_TENSOR) {
    out << qnn_param.tensorParam;
  } else {
    out << " value=" << qnn_param.scalarParam;
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const QnnOpConfigWrapper& op_conf_wrapper) {
  out << "Qnn_OpConfig node name: " << op_conf_wrapper.GetOpName()
      << " package_name: " << op_conf_wrapper.GetPackageName()
      << " QNN_op_type: " << op_conf_wrapper.GetTypeName()
      << " num_of_inputs: " << op_conf_wrapper.GetInputsNum()
      << " num_of_outputs: " << op_conf_wrapper.GetOutputsNum()
      << " num_of_params: " << op_conf_wrapper.GetParamsNum();

  out << std::endl
      << " node_inputs:" << std::endl;
  for (uint32_t i = 0; i < op_conf_wrapper.GetInputsNum(); i++) {
    out << op_conf_wrapper.GetInputTensors()[i] << std::endl;
  }
  out << " node_outputs:" << std::endl;
  for (uint32_t i = 0; i < op_conf_wrapper.GetOutputsNum(); i++) {
    out << op_conf_wrapper.GetOutputTensors()[i] << std::endl;
  }
  out << " node_params:" << std::endl;
  for (uint32_t i = 0; i < op_conf_wrapper.GetParamsNum(); i++) {
    out << op_conf_wrapper.GetParams()[i] << std::endl;
  }
  return out;
}

Status GetQnnDataType(const bool is_quantized_tensor, const ONNX_NAMESPACE::TypeProto* type_proto,
                      Qnn_DataType_t& tensor_data_type) {
  if (!type_proto || !type_proto->tensor_type().has_elem_type()) {
    return ORT_MAKE_STATUS(ONNXRUNTIME, INVALID_ARGUMENT, "The tensor doesn't have elem_type.");
  }

  int32_t onnx_data_type = type_proto->tensor_type().elem_type();
  ORT_RETURN_IF_NOT(OnnxDataTypeToQnnDataType(onnx_data_type, tensor_data_type, is_quantized_tensor),
                    "Failed to map Onnx data type to Qnn data type!");

  return Status::OK();
}

const std::string& GetNodeName(const NodeUnit& node_unit) {
  const std::string& node_name = node_unit.Name();
  if (node_name.empty()) {
    return node_unit.Outputs()[0].node_arg.Name();
  }

  return node_name;
}

bool OnnxDataTypeToQnnDataType(const int32_t onnx_data_type, Qnn_DataType_t& qnn_data_type, bool is_quantized) {
  const std::unordered_map<int32_t, Qnn_DataType_t> onnx_to_qnn_data_type = {
      {ONNX_NAMESPACE::TensorProto_DataType_INT8, QNN_DATATYPE_INT_8},
      {ONNX_NAMESPACE::TensorProto_DataType_INT16, QNN_DATATYPE_INT_16},
      {ONNX_NAMESPACE::TensorProto_DataType_INT32, QNN_DATATYPE_INT_32},
      {ONNX_NAMESPACE::TensorProto_DataType_INT64, QNN_DATATYPE_INT_64},
      {ONNX_NAMESPACE::TensorProto_DataType_UINT8, QNN_DATATYPE_UINT_8},
      {ONNX_NAMESPACE::TensorProto_DataType_UINT16, QNN_DATATYPE_UINT_16},
      {ONNX_NAMESPACE::TensorProto_DataType_UINT32, QNN_DATATYPE_UINT_32},
      {ONNX_NAMESPACE::TensorProto_DataType_UINT64, QNN_DATATYPE_UINT_64},
      {ONNX_NAMESPACE::TensorProto_DataType_FLOAT16, QNN_DATATYPE_FLOAT_16},
      {ONNX_NAMESPACE::TensorProto_DataType_FLOAT, QNN_DATATYPE_FLOAT_32},
      {ONNX_NAMESPACE::TensorProto_DataType_BOOL, QNN_DATATYPE_BOOL_8},
  };

  const std::unordered_map<int32_t, Qnn_DataType_t> onnx_to_qnn_data_type_quantized = {
      {ONNX_NAMESPACE::TensorProto_DataType_INT4, QNN_DATATYPE_SFIXED_POINT_8},
      {ONNX_NAMESPACE::TensorProto_DataType_INT8, QNN_DATATYPE_SFIXED_POINT_8},
      {ONNX_NAMESPACE::TensorProto_DataType_INT16, QNN_DATATYPE_SFIXED_POINT_16},
      {ONNX_NAMESPACE::TensorProto_DataType_INT32, QNN_DATATYPE_SFIXED_POINT_32},
      {ONNX_NAMESPACE::TensorProto_DataType_INT64, QNN_DATATYPE_INT_64},
      {ONNX_NAMESPACE::TensorProto_DataType_UINT4, QNN_DATATYPE_UFIXED_POINT_8},
      {ONNX_NAMESPACE::TensorProto_DataType_UINT8, QNN_DATATYPE_UFIXED_POINT_8},
      {ONNX_NAMESPACE::TensorProto_DataType_UINT16, QNN_DATATYPE_UFIXED_POINT_16},
      {ONNX_NAMESPACE::TensorProto_DataType_UINT32, QNN_DATATYPE_UFIXED_POINT_32},
      {ONNX_NAMESPACE::TensorProto_DataType_UINT64, QNN_DATATYPE_UINT_64},
      {ONNX_NAMESPACE::TensorProto_DataType_FLOAT16, QNN_DATATYPE_FLOAT_16},
      {ONNX_NAMESPACE::TensorProto_DataType_FLOAT, QNN_DATATYPE_FLOAT_32},
      {ONNX_NAMESPACE::TensorProto_DataType_BOOL, QNN_DATATYPE_BOOL_8},
  };

  const auto do_type_mapping = [](const std::unordered_map<int32_t, Qnn_DataType_t>& mapping_table,
                                  const int32_t onnx_data_type,
                                  Qnn_DataType_t& qnn_data_type) -> bool {
    auto pos = mapping_table.find(onnx_data_type);
    if (pos == mapping_table.end()) {
      return false;
    }
    qnn_data_type = pos->second;
    return true;
  };

  if (is_quantized) {
    return do_type_mapping(onnx_to_qnn_data_type_quantized, onnx_data_type, qnn_data_type);
  } else {
    return do_type_mapping(onnx_to_qnn_data_type, onnx_data_type, qnn_data_type);
  }
}

std::pair<float, float> CheckMinMax(float rmin, float rmax) {
  // Ensure a minimum range of 0.0001 (required by QNN)
  rmax = std::max(rmax, rmin + 0.0001f);

  // Both QNN and ORT require the range to include 0.0f
  rmin = std::min(rmin, 0.0f);
  rmax = std::max(rmax, 0.0f);

  return std::make_pair(rmin, rmax);
}

template <typename T>
Status GetQminQmax(const Qnn_DataType_t qnn_data_type,
                   T& qmin,
                   T& qmax) {
  if (qnn_data_type == QNN_DATATYPE_SFIXED_POINT_8) {
    qmin = static_cast<T>(std::numeric_limits<int8_t>::min());
    qmax = static_cast<T>(std::numeric_limits<int8_t>::max());
  } else if (qnn_data_type == QNN_DATATYPE_UFIXED_POINT_8) {
    qmin = static_cast<T>(std::numeric_limits<uint8_t>::min());
    qmax = static_cast<T>(std::numeric_limits<uint8_t>::max());
  } else if (qnn_data_type == QNN_DATATYPE_SFIXED_POINT_16) {
    qmin = static_cast<T>(std::numeric_limits<int16_t>::min());
    qmax = static_cast<T>(std::numeric_limits<int16_t>::max());
  } else if (qnn_data_type == QNN_DATATYPE_UFIXED_POINT_16) {
    qmin = static_cast<T>(std::numeric_limits<uint16_t>::min());
    qmax = static_cast<T>(std::numeric_limits<uint16_t>::max());
  } else if (qnn_data_type == QNN_DATATYPE_SFIXED_POINT_32) {
    qmin = static_cast<T>(std::numeric_limits<int32_t>::min());
    qmax = static_cast<T>(std::numeric_limits<int32_t>::max());
  } else {
    ORT_RETURN_IF(true, "Qnn Data Type: %d not supported yet.", qnn_data_type);
  }
  return Status::OK();
}

Status GetQuantParams(float rmin,
                      float rmax,
                      const Qnn_DataType_t qnn_data_type,
                      float& scale,
                      int32_t& zero_point,
                      bool symmetric) {
  std::tie(rmin, rmax) = CheckMinMax(rmin, rmax);
  if (symmetric) {
    float abs_max = std::max(abs(rmax), abs(rmin));
    rmax = abs_max;
    rmin = -abs_max;
  }

  float qmin = 0.0f;
  float qmax = 255.0f;
  ORT_RETURN_IF_ERROR(GetQminQmax(qnn_data_type, qmin, qmax));

  scale = (rmax - rmin) / (qmax - qmin);
  float initial_zero_point = 0.0f;
  if (symmetric) {
    initial_zero_point = std::round(rmin + rmax) / 2;
  } else {
    initial_zero_point = qmin - (rmin / scale);
  }
  zero_point = static_cast<int32_t>(RoundHalfToEven(Saturate(qmax, qmin, initial_zero_point)));
  // To match QNN quantization definition
  zero_point = 0 - zero_point;
  return Status::OK();
}

double Dequantize(int32_t offset, float scale, const double quant_value) {
  double offset_d = static_cast<double>(offset);
  double scale_d = static_cast<double>(scale);
  return (quant_value + offset_d) * scale_d;
}

Status Quantize(const double double_value,
                const float scale,
                const int32_t zero_point,
                const Qnn_DataType_t qnn_data_type,
                int& quant_value) {
  int qmin = 0;
  int qmax = 255;
  ORT_RETURN_IF_ERROR(GetQminQmax(qnn_data_type, qmin, qmax));
  quant_value = Saturate(qmax, qmin, static_cast<int>(std::round((double_value / scale) - zero_point)));
  return Status::OK();
}

static bool GetType(const NodeArg& node_arg, int32_t& type, const logging::Logger& logger) {
  type = ONNX_NAMESPACE::TensorProto_DataType_UNDEFINED;
  const auto* type_proto = node_arg.TypeAsProto();
  if (!type_proto || !type_proto->has_tensor_type() || !type_proto->tensor_type().has_elem_type()) {
    LOGS(logger, WARNING) << "NodeArg [" << node_arg.Name() << "] has no input type";
    return false;
  }

  type = type_proto->tensor_type().elem_type();
  return true;
}

NodeAttrHelper::NodeAttrHelper(const onnxruntime::Node& node)
    : node_attributes_(node.GetAttributes()) {}

NodeAttrHelper::NodeAttrHelper(const NodeUnit& node_unit)
    : node_attributes_(node_unit.GetNode().GetAttributes()) {}

float NodeAttrHelper::Get(const std::string& key, float def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    return entry->second.f();
  }

  return def_val;
}

int32_t NodeAttrHelper::Get(const std::string& key, int32_t def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    return narrow<int32_t>(entry->second.i());
  }

  return def_val;
}

uint32_t NodeAttrHelper::Get(const std::string& key, uint32_t def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    return narrow<uint32_t>(entry->second.i());
  }

  return def_val;
}

int64_t NodeAttrHelper::Get(const std::string& key, int64_t def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    return entry->second.i();
  }

  return def_val;
}

const std::string& NodeAttrHelper::Get(const std::string& key, const std::string& def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    return entry->second.s();
  }

  return def_val;
}

std::vector<int32_t> NodeAttrHelper::Get(const std::string& key, const std::vector<int32_t>& def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    const auto& attr = entry->second;
    std::vector<int32_t> v;
    v.reserve(static_cast<size_t>(attr.ints_size()));
    std::transform(attr.ints().cbegin(), attr.ints().cend(), std::back_inserter(v),
                   [](int64_t val) -> int32_t { return narrow<int32_t>(val); });
    return v;
  }

  return def_val;
}

std::vector<uint32_t> NodeAttrHelper::Get(const std::string& key, const std::vector<uint32_t>& def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    const auto& attr = entry->second;
    std::vector<uint32_t> v;
    v.reserve(static_cast<size_t>(attr.ints_size()));
    std::transform(attr.ints().cbegin(), attr.ints().cend(), std::back_inserter(v),
                   [](int64_t val) -> uint32_t { return narrow<uint32_t>(val); });
    return v;
  }

  return def_val;
}

std::vector<int64_t> NodeAttrHelper::Get(const std::string& key, const std::vector<int64_t>& def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    const auto& values = entry->second.ints();
    return std::vector<int64_t>{values.cbegin(), values.cend()};
  }

  return def_val;
}

std::vector<std::string> NodeAttrHelper::Get(const std::string& key, const std::vector<std::string>& def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    const auto& values = entry->second.strings();
    return std::vector<std::string>{values.cbegin(), values.cend()};
  }

  return def_val;
}

std::vector<float> NodeAttrHelper::Get(const std::string& key, const std::vector<float>& def_val) const {
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    const auto& values = entry->second.floats();
    return std::vector<float>{values.cbegin(), values.cend()};
  }

  return def_val;
}

std::optional<float> NodeAttrHelper::GetFloat(const std::string& key) const {
  std::optional<float> result;
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    result = entry->second.f();
  }

  return result;
}

std::optional<int64_t> NodeAttrHelper::GetInt64(const std::string& key) const {
  std::optional<int64_t> result;
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    result = entry->second.i();
  }

  return result;
}

std::optional<std::vector<float>> NodeAttrHelper::GetFloats(const std::string& key) const {
  std::optional<std::vector<float>> result;
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    const auto& values = entry->second.floats();
    result = std::vector<float>(values.begin(), values.end());
  }

  return result;
}

std::optional<std::vector<int64_t>> NodeAttrHelper::GetInt64s(const std::string& key) const {
  std::optional<std::vector<int64_t>> result;
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    const auto& values = entry->second.ints();
    result = std::vector<int64_t>(values.begin(), values.end());
  }

  return result;
}

std::optional<std::string> NodeAttrHelper::GetString(const std::string& key) const {
  std::optional<std::string> result;
  if (auto entry = node_attributes_.find(key); entry != node_attributes_.end()) {
    result = entry->second.s();
  }

  return result;
}

bool NodeAttrHelper::HasAttr(const std::string& key) const {
  return Contains(node_attributes_, key);
}
static bool GetClipMinMaxImpl(const GraphViewer& graph_viewer, const Node& node, float& min, float& max,
                              const logging::Logger& logger) {
  const auto& node_name = node.Name();
  int32_t input_type;
  if (!GetType(*node.InputDefs()[0], input_type, logger)) {
    return false;
  }

  min = std::numeric_limits<float>::lowest();
  max = std::numeric_limits<float>::max();

  if (node.SinceVersion() < 11) {  // Clip opset 1, 6 is using attributes for min/max
    NodeAttrHelper helper(node);
    // attributes will be always float
    min = helper.Get("min", std::numeric_limits<float>::lowest());
    max = helper.Get("max", std::numeric_limits<float>::max());
  } else {
    auto get_value =
        [&](const ONNX_NAMESPACE::TensorProto* initializer, std::string_view type, float& value) -> bool {
      if (!initializer) {
        LOGS(logger, VERBOSE) << type << " input of Clip must be a constant initializer";
        return false;
      }

      switch (input_type) {
        case ONNX_NAMESPACE::TensorProto_DataType_FLOAT: {
          auto status = onnxruntime::utils::UnpackTensor(*initializer, graph_viewer.ModelPath(), &value, 1);
          if (!status.IsOK()) {
            LOGS(logger, ERROR) << "GetClipMinMax() failed to unpack float initializer: " << status.ErrorMessage();
            return false;
          }
          break;
        }
        case ONNX_NAMESPACE::TensorProto_DataType_FLOAT16: {
          MLFloat16 f16_val{};
          auto status = onnxruntime::utils::UnpackTensor(*initializer, graph_viewer.ModelPath(), &f16_val, 1);
          if (!status.IsOK()) {
            LOGS(logger, ERROR) << "GetClipMinMax() failed to unpack float16 initializer: " << status.ErrorMessage();
            return false;
          }
          value = f16_val.ToFloat();
          break;
        }
        default:
          LOGS(logger, VERBOSE) << "GetClipMinMax() only supports float and float16 as min and max inputs for now."
                                << " The node [" << node_name << "] has input type: " << input_type;
          return false;
      }

      return true;
    };

    // min and max are both optional. could have neither, one or both.
    if (node.InputDefs().size() > 1 && node.InputDefs()[1]->Exists()) {
      // we have input min
      const auto& min_name = node.InputDefs()[1]->Name();
      const auto* min_value = graph_viewer.GetConstantInitializer(min_name);
      if (!get_value(min_value, "Min", min)) {
        return false;
      }
    }

    if (node.InputDefs().size() > 2 && node.InputDefs()[2]->Exists()) {
      // we have input max
      const auto& max_name = node.InputDefs()[2]->Name();
      const auto* max_value = graph_viewer.GetConstantInitializer(max_name);
      if (!get_value(max_value, "Max", max)) {
        return false;
      }
    }
  }

  return true;
}

bool GetClipMinMax(const GraphViewer& graph_viewer, const Node& node, float& min, float& max,
                   const logging::Logger& logger) {
  return GetClipMinMaxImpl(graph_viewer, node, min, max, logger);
}

}  // namespace utils
}  // namespace qnn
}  // namespace onnxruntime
