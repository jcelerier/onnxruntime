// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#include "onnxruntime_pybind_mlvalue.h"
#include "python/onnxruntime_pybind_state_common.h"
#include "pybind11/numpy.h"

#define NO_IMPORT_ARRAY
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#define PY_ARRAY_UNIQUE_SYMBOL onnxruntime_python_ARRAY_API
#include "python/numpy_helper.h"

#include "core/graph/graph.h"
#include "core/framework/tensor_shape.h"
#include "core/framework/tensor.h"
#include "core/framework/allocator.h"
#include "core/framework/tensorprotoutils.h"
#include "core/framework/TensorSeq.h"
#include "core/framework/data_types.h"
#include "core/framework/onnxruntime_typeinfo.h"

#include "core/framework/data_transfer_utils.h"
#include "core/framework/data_types_internal.h"
#include "core/providers/get_execution_providers.h"
#include "core/framework/kernel_registry.h"
#include "core/framework/provider_options_utils.h"

#ifdef USE_DML
using Microsoft::WRL::ComPtr;

#include <wil/wrl.h>
#include "core/providers/dml/DmlExecutionProvider/src/External/D3DX12/d3dx12.h"
#include "core/providers/dml/DmlExecutionProvider/src/ErrorHandling.h"
#include "core/providers/dml/DmlExecutionProvider/src/DescriptorPool.h"
#include "core/providers/dml/DmlExecutionProvider/src/DmlCommittedResourceAllocator.h"
#include "core/providers/dml/DmlExecutionProvider/inc/DmlExecutionProvider.h"
#include "core/providers/dml/DmlExecutionProvider/src/BucketizedBufferAllocator.h"
#include "core/providers/dml/DmlExecutionProvider/src/PooledUploadHeap.h"
#include "core/providers/dml/DmlExecutionProvider/src/ReadbackHeap.h"
#include "core/providers/dml/DmlExecutionProvider/src/AllocationInfo.h"
#endif
namespace onnxruntime {
namespace python {

namespace py = pybind11;
using namespace onnxruntime::logging;

const char* PYTHON_ORTVALUE_OBJECT_NAME = "OrtValue";
const char* PYTHON_ORTVALUE_NATIVE_OBJECT_ATTR = "_ortvalue";

static bool PyObjectCheck_NumpyArray(PyObject* o) {
  return (PyObject_HasAttrString(o, "__array_finalize__") != 0);
}

bool IsNumpyArray(py::object& obj) {
  return PyObjectCheck_NumpyArray(obj.ptr());
}

int GetNumpyArrayType(const py::object& obj) {
  return PyArray_TYPE(reinterpret_cast<PyArrayObject*>(obj.ptr()));
}

bool IsNumericNumpyArray(const py::object& py_object) {
  if (PyObjectCheck_NumpyArray(py_object.ptr())) {
    int npy_type = PyArray_TYPE(reinterpret_cast<PyArrayObject*>(py_object.ptr()));
    return IsNumericNumpyType(npy_type);
  }

  return false;
}

namespace {
template <typename... T>
std::vector<py::dtype> MakeTypes() {
  std::vector<py::dtype> result = {py::dtype::of<T>()...};
  return result;
}
}  // namespace

bool IsNumericDType(const py::dtype& dtype) {
  static const std::vector<py::dtype> numeric =
      MakeTypes<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double>();
  return std::any_of(numeric.cbegin(), numeric.cend(), [&dtype](const py::dtype& dt) {
    return dtype.is(dt);
  });
}

TensorShape GetShape(const py::array& arr) {
  auto span = gsl::make_span(arr.shape(), arr.ndim());
  TensorShapeVector shape_vec(span.begin(), span.end());
  TensorShape shape(shape_vec);
  return shape;
}

void CpuToCpuMemCpy(void* dst, const void* src, size_t num_bytes) {
  memcpy(dst, src, num_bytes);
}

OrtMemoryInfo GetMemoryInfoPerDeviceType(const OrtDevice& ort_device) {
  OrtMemoryInfo mem_info;
  if (ort_device.Type() == OrtDevice::CPU) {
    mem_info = GetAllocator()->Info();
  }
#if USE_CUDA
  else if (ort_device.Type() == OrtDevice::GPU) {
    if (!IsCudaDeviceIdValid(logging::LoggingManager::DefaultLogger(), ort_device.Id())) {
      ORT_THROW("The provided device id doesn't match any available GPUs on the machine: ", ort_device.Id());
    }
    mem_info = GetCudaAllocator(ort_device.Id())->Info();
  }
#endif
  else {
    ORT_THROW("Unsupported OrtDevice type: ", ort_device.Type());
  }
  return mem_info;
}

int32_t GetTensorProtoType(const OrtValue& ort_value) {
  if (ort_value.IsTensor()) {
    return ort_value.Get<Tensor>().GetElementType();
#if !defined(DISABLE_SPARSE_TENSORS)
  } else if (ort_value.IsSparseTensor()) {
    return ort_value.Get<SparseTensor>().GetElementType();
#endif
  } else if (ort_value.IsTensorSequence()) {
    return ort_value.Get<TensorSeq>().DataType()->AsPrimitiveDataType()->GetDataType();
  } else {
    throw std::runtime_error("Tensor proto_type is unavailable for this value.");
  }
}

#ifdef USE_CUDA
void CpuToCudaMemCpy(void* dst, const void* src, size_t num_bytes) {
  GetProviderInfo_CUDA().cudaMemcpy_HostToDevice(dst, src, num_bytes);
}

void CudaToCpuMemCpy(void* dst, const void* src, size_t num_bytes) {
  GetProviderInfo_CUDA().cudaMemcpy_DeviceToHost(dst, src, num_bytes);
}

const std::unordered_map<OrtDevice::DeviceType, MemCpyFunc>* GetCudaToHostMemCpyFunction() {
  static std::unordered_map<OrtDevice::DeviceType, MemCpyFunc> map{
      {OrtDevice::GPU, CudaToCpuMemCpy}};

  return &map;
}

bool IsCudaDeviceIdValid(const onnxruntime::logging::Logger& logger, int id) {
  int num_devices = GetProviderInfo_CUDA().cudaGetDeviceCount();

  if (0 == num_devices) {
    LOGS(logger, WARNING) << "your system does not have a CUDA capable device.";
    return false;
  }

  if (id < 0 || id >= num_devices) {
    LOGS(logger, WARNING) << "cuda_device=" << id << " is invalid, must choose device ID between 0 and " << num_devices - 1;
    return false;
  }

  return true;
}

AllocatorPtr GetCudaAllocator(OrtDevice::DeviceId id) {
  // Current approach is not thread-safe, but there are some bigger infra pieces to put together in order to make
  // multi-threaded CUDA allocation work we need to maintain a per-thread CUDA allocator

  // We are leaking this map so we do not accidentally destroy CUDA Allocator instance
  // after we unloaded CUDA provider library. Appeasing static analysis warning and using make_unique.
  static auto* id_to_allocator_map = std::make_unique<std::unordered_map<OrtDevice::DeviceId, AllocatorPtr>>().release();

  auto hit = id_to_allocator_map->find(id);
  if (hit == id_to_allocator_map->end()) {
    // TODO: Expose knobs so that users can set fields associated with OrtArenaCfg so that we can pass it to the following method
    auto cuda_allocator = GetProviderInfo_CUDA().CreateCudaAllocator(id, gpu_mem_limit, arena_extend_strategy, external_allocator_info, nullptr);
    hit = id_to_allocator_map->emplace(id, std::move(cuda_allocator)).first;
  }

  return hit->second;
}

std::unique_ptr<IDataTransfer> GetGPUDataTransfer() {
  // Using default stream
  return GetProviderInfo_CUDA().CreateGPUDataTransfer();
}

#endif

#ifdef USE_DML

constexpr GUID dml_readback_heap_guid = {0x00d32df8, 0xea2d, 0x40bf, {0xa4, 0x47, 0x9c, 0xb4, 0xbc, 0xf1, 0x1d, 0x5e}};
constexpr GUID dml_upload_heap_guid = {0x125235f9, 0xef41, 0x4043, {0xa4, 0x9d, 0xdd, 0xc9, 0x61, 0xe7, 0xdb, 0xee}};

AllocatorPtr GetDmlAllocator(OrtDevice::DeviceId id) {
  // Current approach is not thread-safe, but there are some bigger infra pieces to put together in order to make
  // multi-threaded DML allocation work, including maintaining a per-thread DML allocator.

  // We are leaking this map so we do not accidentally destroy the DML Allocator instance
  // after we unloaded DML provider library. Appeasing static analysis warning and using make_unique.
  static auto* id_to_allocator_map = std::make_unique<std::unordered_map<OrtDevice::DeviceId, AllocatorPtr>>().release();

  auto hit = id_to_allocator_map->find(id);
  if (hit == id_to_allocator_map->end()) {
    constexpr uint32_t device_id = 0;
    auto d3d12_device = onnxruntime::DMLProviderFactoryCreator::CreateD3D12Device(device_id, false);

    ComPtr<Dml::ExecutionContext> context;
    uint32_t execution_context_ptr_size = gsl::narrow_cast<uint32_t>(sizeof(context.GetAddressOf()));

    // First, check if an I/O binding API that was used before this session or another session has already created a queue
    if (FAILED(d3d12_device->GetPrivateData(dml_execution_context_guid, &execution_context_ptr_size, context.GetAddressOf()))) {
      D3D12_COMMAND_QUEUE_DESC cmd_queue_desc = {};
      cmd_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
      cmd_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;

      ComPtr<ID3D12CommandQueue> cmd_queue;
      ORT_THROW_IF_FAILED(d3d12_device->CreateCommandQueue(&cmd_queue_desc, IID_PPV_ARGS(cmd_queue.ReleaseAndGetAddressOf())));

      auto dml_device = onnxruntime::DMLProviderFactoryCreator::CreateDMLDevice(d3d12_device.Get());
      ORT_THROW_IF_FAILED(d3d12_device->SetPrivateDataInterface(dml_device_guid, dml_device.Get()));

      context = wil::MakeOrThrow<Dml::ExecutionContext>(d3d12_device.Get(), dml_device.Get(), cmd_queue.Get(), true, true);
      ORT_THROW_IF_FAILED(d3d12_device->SetPrivateDataInterface(dml_execution_context_guid, context.Get()));
    }

    // We leak the readback and upload heap to keep them alive, just like the map
    auto readback_heap = std::make_unique<Dml::ReadbackHeap>(d3d12_device.Get(), context.Get()).release();
    auto upload_heap = std::make_unique<Dml::PooledUploadHeap>(d3d12_device.Get(), context.Get()).release();

    auto dml_allocator = std::make_shared<Dml::BucketizedBufferAllocator>(
        d3d12_device.Get(),
        context.Get(),
        CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        std::make_unique<Dml::DmlCommittedResourceAllocator>(d3d12_device.Get()));
    dml_allocator->SetDefaultRoundingMode(AllocatorRoundingMode::Enabled);
    context->SetAllocator(dml_allocator);

    ORT_THROW_IF_FAILED(d3d12_device->SetPrivateData(dml_readback_heap_guid, sizeof(readback_heap), &readback_heap));
    ORT_THROW_IF_FAILED(d3d12_device->SetPrivateData(dml_upload_heap_guid, sizeof(upload_heap), &upload_heap));

    hit = id_to_allocator_map->emplace(id, std::move(dml_allocator)).first;
  }

  return hit->second;
}

void CpuToDmlMemCpy(void* dst, const void* src, size_t num_bytes) {
  const auto* allocInfo = static_cast<const Dml::AllocationInfo*>(dst);
  ID3D12Resource* dst_data = allocInfo->GetResource();

  ComPtr<ID3D12Device> d3d12_device;
  ORT_THROW_IF_FAILED(dst_data->GetDevice(IID_PPV_ARGS(d3d12_device.ReleaseAndGetAddressOf())));

  Dml::PooledUploadHeap* upload_heap = nullptr;
  uint32_t upload_heap_size = gsl::narrow_cast<uint32_t>(sizeof(upload_heap));
  ORT_THROW_IF_FAILED(d3d12_device->GetPrivateData(dml_upload_heap_guid, &upload_heap_size, &upload_heap));

  upload_heap->BeginUploadToGpu(
      dst_data, 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, gsl::make_span(static_cast<const std::byte*>(src), num_bytes));
}

void DmlToCpuMemCpy(void* dst, const void* src, size_t num_bytes) {
  const auto* allocInfo = static_cast<const Dml::AllocationInfo*>(src);
  ID3D12Resource* src_data = allocInfo->GetResource();

  ComPtr<ID3D12Device> d3d12_device;
  ORT_THROW_IF_FAILED(src_data->GetDevice(IID_PPV_ARGS(d3d12_device.ReleaseAndGetAddressOf())));

  Dml::ReadbackHeap* readback_heap = nullptr;
  uint32_t readback_heap_size = gsl::narrow_cast<uint32_t>(sizeof(readback_heap));
  ORT_THROW_IF_FAILED(d3d12_device->GetPrivateData(dml_readback_heap_guid, &readback_heap_size, &readback_heap));

  // ReadbackFromGpu already syncs with the CPU and waits for the copy to be completed, so we dont need to sync after
  // this call
  readback_heap->ReadbackFromGpu(
      gsl::make_span(static_cast<std::byte*>(dst), num_bytes),
      src_data,
      0,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

const std::unordered_map<OrtDevice::DeviceType, MemCpyFunc>* GetDmlToHostMemCpyFunction() {
  static std::unordered_map<OrtDevice::DeviceType, MemCpyFunc> map{
      {OrtDevice::DML, DmlToCpuMemCpy}};

  return &map;
}

#endif

#ifdef USE_CANN
void CpuToCannMemCpy(void* dst, const void* src, size_t num_bytes) {
  GetProviderInfo_CANN().cannMemcpy_HostToDevice(dst, src, num_bytes);
}

void CannToCpuMemCpy(void* dst, const void* src, size_t num_bytes) {
  GetProviderInfo_CANN().cannMemcpy_DeviceToHost(dst, src, num_bytes);
}

const std::unordered_map<OrtDevice::DeviceType, MemCpyFunc>* GetCannToHostMemCpyFunction() {
  static std::unordered_map<OrtDevice::DeviceType, MemCpyFunc> map{
      {OrtDevice::NPU, CannToCpuMemCpy}};

  return &map;
}

bool IsCannDeviceIdValid(const onnxruntime::logging::Logger& logger, int id) {
  int num_devices = GetProviderInfo_CANN().cannGetDeviceCount();

  if (0 == num_devices) {
    LOGS(logger, WARNING) << "your system does not have a CANN capable device.";
    return false;
  }

  if (id < 0 || id >= num_devices) {
    LOGS(logger, WARNING) << "cann_device=" << id << " is invalid, must choose device ID between 0 and "
                          << num_devices - 1;
    return false;
  }

  return true;
}

AllocatorPtr GetCannAllocator(OrtDevice::DeviceId id) {
  size_t npu_mem_limit = std::numeric_limits<size_t>::max();
  onnxruntime::ArenaExtendStrategy arena_extend_strategy = onnxruntime::ArenaExtendStrategy::kNextPowerOfTwo;

  static auto* id_to_allocator_map =
      std::make_unique<std::unordered_map<OrtDevice::DeviceId, AllocatorPtr>>().release();
  auto hit = id_to_allocator_map->find(id);
  if (hit == id_to_allocator_map->end()) {
    auto cann_allocator = GetProviderInfo_CANN().CreateCannAllocator(id, npu_mem_limit, arena_extend_strategy, nullptr);
    hit = id_to_allocator_map->emplace(id, std::move(cann_allocator)).first;
  }

  return hit->second;
}

#endif

#ifdef USE_ROCM
void CpuToRocmMemCpy(void* dst, const void* src, size_t num_bytes) {
  GetProviderInfo_ROCM().rocmMemcpy_HostToDevice(dst, src, num_bytes);
}

void RocmToCpuMemCpy(void* dst, const void* src, size_t num_bytes) {
  GetProviderInfo_ROCM().rocmMemcpy_DeviceToHost(dst, src, num_bytes);
}

const std::unordered_map<OrtDevice::DeviceType, MemCpyFunc>* GetRocmToHostMemCpyFunction() {
  static std::unordered_map<OrtDevice::DeviceType, MemCpyFunc> map{
      {OrtDevice::GPU, RocmToCpuMemCpy}};

  return &map;
}

bool IsRocmDeviceIdValid(const onnxruntime::logging::Logger& logger, int id) {
  int num_devices = GetProviderInfo_ROCM().hipGetDeviceCount();

  if (0 == num_devices) {
    LOGS(logger, WARNING) << "your system does not have a ROCM capable device.";
    return false;
  }

  if (id < 0 || id >= num_devices) {
    LOGS(logger, WARNING) << "rocm_device=" << id << " is invalid, must choose device ID between 0 and " << num_devices - 1;
    return false;
  }

  return true;
}

AllocatorPtr GetRocmAllocator(OrtDevice::DeviceId id) {
  // Current approach is not thread-safe, but there are some bigger infra pieces to put together in order to make
  // multi-threaded ROCM allocation work we need to maintain a per-thread ROCM allocator

  static auto* id_to_allocator_map = new std::unordered_map<OrtDevice::DeviceId, AllocatorPtr>();

  if (id_to_allocator_map->find(id) == id_to_allocator_map->end()) {
    // TODO: Expose knobs so that users can set fields associated with OrtArenaCfg so that we can pass it to the following method
    id_to_allocator_map->insert({id, GetProviderInfo_ROCM().CreateRocmAllocator(id, gpu_mem_limit, arena_extend_strategy, external_allocator_info, nullptr)});
  }

  return (*id_to_allocator_map)[id];
}

#endif

int OnnxRuntimeTensorToNumpyType(const DataTypeImpl* tensor_type) {
  static std::map<MLDataType, int> type_map{
      {DataTypeImpl::GetType<bool>(), NPY_BOOL},
      {DataTypeImpl::GetType<float>(), NPY_FLOAT},
      {DataTypeImpl::GetType<MLFloat16>(), NPY_FLOAT16},
      {DataTypeImpl::GetType<double>(), NPY_DOUBLE},
      {DataTypeImpl::GetType<int8_t>(), NPY_INT8},
      {DataTypeImpl::GetType<uint8_t>(), NPY_UINT8},
      {DataTypeImpl::GetType<int16_t>(), NPY_INT16},
      {DataTypeImpl::GetType<uint16_t>(), NPY_UINT16},
      {DataTypeImpl::GetType<int32_t>(), NPY_INT},
      {DataTypeImpl::GetType<uint32_t>(), NPY_UINT},
      {DataTypeImpl::GetType<int64_t>(), NPY_LONGLONG},
      {DataTypeImpl::GetType<uint64_t>(), NPY_ULONGLONG},
      {DataTypeImpl::GetType<std::string>(), NPY_OBJECT},
  };

  const auto it = type_map.find(tensor_type);
  if (it == type_map.end()) {
    throw std::runtime_error("No corresponding Numpy type for Tensor Type.");
  } else {
    return it->second;
  }
}

MLDataType NumpyTypeToOnnxRuntimeTensorType(int numpy_type) {
  static std::map<int, MLDataType> type_map{
      {NPY_BOOL, DataTypeImpl::GetType<bool>()},
      {NPY_FLOAT, DataTypeImpl::GetType<float>()},
      // Special, not a C type expands to enum value of 16
      {NPY_FLOAT16, DataTypeImpl::GetType<MLFloat16>()},
      {NPY_DOUBLE, DataTypeImpl::GetType<double>()},
      // We dont want to use size specific types such
      // as NPY_INT32 bc they are not enums but hash defines
      // which may map into other enums and may conflict with other entries here
      // also NPY docs define these sizes as platform specific, thus we
      // choose to do some rudimentary checks for proper mapping on C++ size
      {NPY_BYTE, DataTypeImpl::GetType<int8_t>()},
      {NPY_UBYTE, DataTypeImpl::GetType<uint8_t>()},
      {NPY_SHORT, sizeof(short) == sizeof(int16_t) ? DataTypeImpl::GetType<int16_t>()
                                                   : DataTypeImpl::GetType<int32_t>()},
      {NPY_USHORT, sizeof(unsigned short) == sizeof(uint16_t) ? DataTypeImpl::GetType<uint16_t>()
                                                              : DataTypeImpl::GetType<uint32_t>()},
      {NPY_INT,
       sizeof(int) == sizeof(int32_t) ? DataTypeImpl::GetType<int32_t>()
                                      : DataTypeImpl::GetType<int64_t>()},
      {NPY_UINT, sizeof(int) == sizeof(int32_t) ? DataTypeImpl::GetType<uint32_t>()
                                                : DataTypeImpl::GetType<uint64_t>()},

      {NPY_LONG,
       sizeof(long) == sizeof(int32_t) ? DataTypeImpl::GetType<int32_t>()
                                       : DataTypeImpl::GetType<int64_t>()},
      {NPY_ULONG,
       sizeof(unsigned long) == sizeof(uint32_t) ? DataTypeImpl::GetType<uint32_t>()
                                                 : DataTypeImpl::GetType<uint64_t>()},
      {NPY_LONGLONG, DataTypeImpl::GetType<int64_t>()},
      {NPY_ULONGLONG, DataTypeImpl::GetType<uint64_t>()},
      {NPY_UNICODE, DataTypeImpl::GetType<std::string>()},
      {NPY_STRING, DataTypeImpl::GetType<std::string>()},
      {NPY_OBJECT, DataTypeImpl::GetType<std::string>()},
      {NPY_VOID, DataTypeImpl::GetType<std::string>()}};

  const auto it = type_map.find(numpy_type);
  if (it == type_map.end()) {
    throw std::runtime_error("Numpy_type " + std::to_string(numpy_type) +
                             " can't be converted to MLDataType.");
  } else {
    return it->second;
  }
}

MLDataType OnnxTypeToOnnxRuntimeTensorType(int onnx_element_type) {
  return DataTypeImpl::TensorTypeFromONNXEnum(onnx_element_type)->GetElementType();
}

// Expects tensor allocated
// Requires the input array to be c_contigious for primitive types so they can be mem-copied
// Seq can be either py::array or some py::sequence, however, for memcpy we attempt to cast to array.
void CopyDataToTensor(const py::array& py_array, int npy_type, Tensor& tensor, MemCpyFunc mem_cpy_to_device) {
  // The iteration is wrong for py_array for some reason, use indexing
  const PyArrayObject* darray = reinterpret_cast<const PyArrayObject*>(py_array.ptr());
  const auto total_items = tensor.Shape().Size();
  assert(total_items == py_array.size());
  const auto item_size = py_array.itemsize();
  if (npy_type == NPY_UNICODE) {
    // Copy string data which needs to be done after Tensor is allocated.
    // and convert all of the above to strings
    auto dst = tensor.MutableDataAsSpan<std::string>();
    const auto num_chars = item_size / PyUnicode_4BYTE_KIND;
    const char* src = reinterpret_cast<const char*>(py_array.data());
    for (int i = 0; i < total_items; ++i, src += item_size) {
      PyObject* pStr = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, src, num_chars);
      UniqueDecRefPtr<PyObject> strGuard(pStr, DecRefFn<PyObject>());
      const char* str = PyUnicode_AsUTF8(pStr);
      UniqueDecRefPtr<PyObject> strUtf8Guard(pStr, DecRefFn<PyObject>());
      if (str == NULL) {
        dst[i].clear();
      } else {
        // Size is equal to the longest string size, numpy stores
        // strings in a single array.
        dst[i] = str;
      }
    }
  } else if (npy_type == NPY_VOID || npy_type == NPY_STRING) {
    // Copy string data which needs to be done after Tensor is allocated.
    // Strings are given as bytes (encoded strings).
    // NPY_VOID does not trim final 0.
    // NPY_STRING assumes bytes string ends with a final 0.
    std::string* dst = tensor.MutableData<std::string>();
    const char* src = reinterpret_cast<const char*>(py_array.data());
    for (int i = 0; i < total_items; i++, src += item_size) {
      if (npy_type == NPY_STRING) {
        dst[i] = src;
      } else {
        dst[i].assign(src, item_size);
      }
    }
  } else if (npy_type == NPY_OBJECT) {
    // Converts object into string.
    std::string* dst = tensor.MutableData<std::string>();
    const char* src = reinterpret_cast<const char*>(py_array.data());
    for (int i = 0; i < total_items; ++i, src += item_size) {
      // Python unicode strings are assumed to be USC-4. Strings are stored as UTF-8.
      PyObject* item = PyArray_GETITEM(darray, src);
      PyObject* pStr = PyObject_Str(item);
      UniqueDecRefPtr<PyObject> strGuard(pStr, DecRefFn<PyObject>());
      dst[i] = py::reinterpret_borrow<py::str>(pStr);
    }
  } else {
    size_t len = 0;
    Status status = Tensor::CalculateTensorStorageSize(tensor.DataType(), tensor.Shape(), /*alignment*/ 0, len);
    if (!status.IsOK()) {
      throw std::runtime_error(status.ErrorMessage());
    }
    void* buffer = tensor.MutableDataRaw();
    if ((py_array.flags() & py::array::c_style) == 0) {
      throw std::runtime_error("CopyData from sequence: Expecting a contiguous array");
    }
    mem_cpy_to_device(buffer, py_array.data(), len);
  }
}

// Setting `use_numpy_data_memory` to `true` will ensure that the underlying numpy array buffer is directly used
// as the backing data buffer for the ORT Tensor where applicable (for numeric tensors)
// The numpy object owns the memory and needs to be alive until the corresponding OrtValue is in scope
static Tensor CreateTensorOverPrimitiveDataOrCopy(const AllocatorPtr& alloc,
                                                  const py::array& py_array,
                                                  int npy_type,
                                                  bool use_numpy_data_memory = true,
                                                  MemCpyFunc mem_cpy_to_device = CpuToCpuMemCpy) {
  TensorShape shape = GetShape(py_array);
  auto element_type = NumpyTypeToOnnxRuntimeTensorType(npy_type);

  Tensor result;

  if (IsNumericNumpyType(npy_type) && use_numpy_data_memory && (py_array.flags() & py::array::c_style)) {
    // const_cast is necessary due to Tensor API
    result = Tensor(element_type, shape, const_cast<void*>(py_array.data()), alloc->Info());
  } else {
    result = Tensor(element_type, shape, alloc);
    CopyDataToTensor(py_array, npy_type, result, mem_cpy_to_device);
  }

  return result;
}

static bool CheckIfInputIsSequenceType(const std::string& name_input,
                                       const InputDefList* input_def_list,
                                       /*out*/ onnx::TypeProto& type_proto) {
  // get sequence type from the model
  const auto& def_list = *input_def_list;
  auto ret_it = std::find_if(std::begin(def_list), std::end(def_list),
                             [&name_input](const NodeArg* node_arg) { return name_input == node_arg->Name(); });
  if (ret_it == std::end(def_list)) {
    throw std::runtime_error("Failed to find input with name: " + name_input + " in the model input def list");
  }
  const auto* temp = (*ret_it)->TypeAsProto();
  if (!temp) {
    throw std::runtime_error("Corresponding type_proto is null");
  } else {
    if (temp->has_optional_type()) {
      const ::onnx::TypeProto_Optional& optional_type_proto = temp->optional_type();
      type_proto = optional_type_proto.elem_type();
    } else {
      type_proto = *temp;
    }
  }

  return type_proto.has_sequence_type();
}

static void CreateSequenceOfTensors(AllocatorPtr alloc, const std::string& name_input,
                                    const InputDefList* input_def_list, py::list& py_list,
                                    OrtValue& mlvalue) {
  onnx::TypeProto type_proto;
  if (!CheckIfInputIsSequenceType(name_input, input_def_list, type_proto)) {
    throw std::runtime_error("Input is not of sequence type");
  }

  // set the seq type
  MLDataType seq_dtype = OrtTypeInfo::ElementTypeFromProto(
      static_cast<ONNX_NAMESPACE::TensorProto_DataType>(type_proto.sequence_type().elem_type().tensor_type().elem_type()));
  int npy_type = OnnxRuntimeTensorToNumpyType(seq_dtype);

  auto p_tensor_sequence = std::make_unique<TensorSeq>(seq_dtype);

  // populate the seq
  // expecting a list of numpy arrays
  for (const auto& item : py_list) {
    auto py_array = py::array::ensure(item);
    if (!py_array) {
      throw std::runtime_error("CreateSequenceOfTensors: list item is not a numpy array");
    }
    TensorShape shape = GetShape(py_array);
    Tensor tensor(seq_dtype, shape, alloc);
    CopyDataToTensor(py_array, npy_type, tensor, CpuToCpuMemCpy);
    p_tensor_sequence->Add(std::move(tensor));
  }

  auto ml_tensor_sequence = DataTypeImpl::GetType<TensorSeq>();
  mlvalue.Init(p_tensor_sequence.release(),
               ml_tensor_sequence,
               ml_tensor_sequence->GetDeleteFunc());
}

// This function will always copy data since we are getting a sequence as input
// as if when the user directly supplied it. This is the edge case.
static void CreateTensorMLValueFromSequence(const std::string& name_input,
                                            const py::object& py_object,
                                            MLDataType ml_type,
                                            const AllocatorPtr& alloc,
                                            OrtValue& mlvalue) {
  auto py_array = py::array::ensure(py_object, py::array::c_style);
  if (!py_array) {
    throw std::runtime_error(
        "CreateTensorMLValueFromSequence: unable to convert incoming object to py::array for input:" + name_input);
  }
  TensorShape shape = GetShape(py_array);
  Tensor tensor{ml_type, shape, alloc};
  int npy_type = OnnxRuntimeTensorToNumpyType(ml_type);
  CopyDataToTensor(py_array, npy_type, tensor, CpuToCpuMemCpy);
  Tensor::InitOrtValue(std::move(tensor), mlvalue);
}

std::string _get_type_name(int64_t&) {
  return std::string("int64_t");
}

std::string _get_type_name(float&) {
  return std::string("float");
}

std::string _get_type_name(std::string&) {
  return std::string("string");
}

#if !defined(DISABLE_ML_OPS)
template <typename KeyType, typename ValueType, typename KeyGetterType, typename ValueGetterType>
static void CreateMapMLValue_LoopIntoMap(Py_ssize_t& pos, PyObject*& key, const std::string& name_input, PyObject*& value,
                                         PyObject* item, std::map<KeyType, ValueType>& current,
                                         KeyGetterType keyGetter, ValueGetterType valueGetter) {
  KeyType ckey;
  ValueType cvalue;
  do {
    if (!keyGetter(key, ckey)) {
      PyObject* pType = PyObject_Type(key);
      auto pStr = PyObject_Str(pType);
      py::str spyType = py::reinterpret_borrow<py::str>(pStr);
      std::string sType = spyType;
      Py_XDECREF(pStr);
      Py_XDECREF(pType);
      Py_XDECREF(item);
      throw std::runtime_error(std::string("Unexpected key type  ") + sType +
                               std::string(", it cannot be linked to C type ") +
                               _get_type_name(ckey) + std::string(" for input '") +
                               name_input + std::string("'."));
    }

    if (!valueGetter(value, cvalue)) {
      PyObject* pType = PyObject_Type(value);
      auto pStr = PyObject_Str(pType);
      py::str spyType = py::reinterpret_borrow<py::str>(pStr);
      std::string sType = spyType;
      Py_XDECREF(pStr);
      Py_XDECREF(pType);
      Py_XDECREF(item);
      throw std::runtime_error(std::string("Unexpected value type  ") + sType +
                               std::string(", it cannot be linked to C type ") +
                               _get_type_name(ckey) + std::string(" for input '") +
                               name_input + std::string("'."));
    }
    current[ckey] = cvalue;
  } while (PyDict_Next(item, &pos, &key, &value));
}

template <typename KeyType, typename ValueType, typename KeyGetterType, typename ValueGetterType>
static void CreateMapMLValue_Map(Py_ssize_t& pos, PyObject*& key, const std::string& name_input, PyObject*& value,
                                 PyObject* item, AllocatorPtr /*alloc*/, OrtValue* p_mlvalue, KeyGetterType keyGetter,
                                 ValueGetterType valueGetter) {
  std::unique_ptr<std::map<KeyType, ValueType>> dst;
  dst = std::make_unique<std::map<KeyType, ValueType>>();
  CreateMapMLValue_LoopIntoMap(pos, key, name_input, value, item, *dst, keyGetter, valueGetter);
  p_mlvalue->Init(dst.release(), DataTypeImpl::GetType<std::map<KeyType, ValueType>>(),
                  DataTypeImpl::GetType<std::map<KeyType, ValueType>>()->GetDeleteFunc());
}

template <typename KeyType, typename ValueType, typename KeyGetterType, typename ValueGetterType>
void CreateMapMLValue_VectorMap(Py_ssize_t& pos, PyObject*& key, const std::string& name_input, PyObject*& value,
                                PyObject* iterator, PyObject* item, AllocatorPtr /*alloc*/, OrtValue* p_mlvalue,
                                KeyGetterType keyGetter, ValueGetterType valueGetter) {
  std::unique_ptr<std::vector<std::map<KeyType, ValueType>>> dstVector;
  dstVector = std::make_unique<std::vector<std::map<KeyType, ValueType>>>();
  int index = 0;
  do {
    dstVector->push_back(std::map<KeyType, ValueType>());
    CreateMapMLValue_LoopIntoMap(pos, key, name_input, value, item, (*dstVector)[index], keyGetter, valueGetter);
    Py_DECREF(item);
    ++index;
    item = iterator == NULL ? NULL : PyIter_Next(iterator);
  } while (item != NULL);
  p_mlvalue->Init(dstVector.release(), DataTypeImpl::GetType<std::vector<std::map<KeyType, ValueType>>>(),
                  DataTypeImpl::GetType<std::vector<std::map<KeyType, ValueType>>>()->GetDeleteFunc());
}

static void CreateMapMLValue_AgnosticMap(Py_ssize_t& pos, PyObject*& key, const std::string& name_input, PyObject*& value,
                                         PyObject* iterator, PyObject* item, AllocatorPtr alloc, OrtValue& mlvalue) {
  // If iterator is NULL, it returns a single Map,
  // if is not NULL, it returns a VectorMap.
  auto int64Getter = [](PyObject* obj, int64_t& value) -> bool {
    value = PyLong_AsLong(obj);
    return !PyErr_Occurred();
  };

  auto floatGetter = [](PyObject* obj, float& value) -> bool {
    if (PyFloat_Check(obj)) {
      value = (float)PyFloat_AS_DOUBLE(obj);
      return true;
    } else if (PyNumber_Check(obj)) {
      value = (float)PyFloat_AsDouble(obj);
      return true;
    } else {
      return false;
    }
  };

  auto stringGetter = [](PyObject* obj, std::string& value) -> bool {
    PyObject* pStr = PyObject_Str(obj);
    if (pStr == NULL) {
      return false;
    }
    value = py::reinterpret_borrow<py::str>(pStr);
    Py_DECREF(pStr);
    return true;
  };

  if (iterator == NULL) {
    if (PyLong_Check(key)) {
      // Regular Python.
      CreateMapMLValue_Map<int64_t, float>(pos, key, name_input, value, item, alloc, &mlvalue, int64Getter, floatGetter);
    } else if (PyNumber_Check(key)) {
      // For numpy type.
      CreateMapMLValue_Map<int64_t, float>(pos, key, name_input, value, item, alloc, &mlvalue, int64Getter, floatGetter);
    } else if (PyUnicode_Check(key)) {
      CreateMapMLValue_Map<std::string, float>(pos, key, name_input, value, item, alloc, &mlvalue, stringGetter, floatGetter);
    } else {
      PyObject* pType = PyObject_Type(key);
      PyObject* pStr = PyObject_Str(pType);
      py::str spyType = py::reinterpret_borrow<py::str>(pStr);
      std::string sType = spyType;
      Py_XDECREF(pType);
      Py_XDECREF(pStr);
      throw std::runtime_error(std::string("Key type must be int or string (not ") + sType +
                               std::string(") for input '") + name_input + std::string("'."));
    }
  } else {
    if (PyLong_Check(key)) {
      CreateMapMLValue_VectorMap<int64_t, float>(pos, key, name_input, value, iterator, item, alloc, &mlvalue, int64Getter, floatGetter);
    } else if (PyNumber_Check(key)) {
      // For numpy type.
      CreateMapMLValue_VectorMap<int64_t, float>(pos, key, name_input, value, iterator, item, alloc, &mlvalue, int64Getter, floatGetter);
    } else if (PyUnicode_Check(key)) {
      CreateMapMLValue_VectorMap<std::string, float>(pos, key, name_input, value, iterator, item, alloc, &mlvalue, stringGetter, floatGetter);
    } else {
      PyObject* pType = PyObject_Type(value);
      PyObject* pStr = PyObject_Str(pType);
      py::str spyType = py::reinterpret_borrow<py::str>(pStr);
      std::string sType = spyType;
      Py_XDECREF(pType);
      Py_XDECREF(pStr);
      throw std::runtime_error(std::string("Key type must be int or string (not ") + sType +
                               std::string(") for input '") + name_input + std::string("'."));
    }
  }
}

static void CreateMapMLValue_AgnosticVectorMap(PyObject* iterator, PyObject* item, AllocatorPtr alloc,
                                               const std::string& name_input, OrtValue& mlvalue) {
  // CreateMapMLValue is called by CreateGenericTerableMLValue
  // or CreateGenericMLValue which ensures
  // item is a dictionary, no need to check type again.
  // This functions starts to iterate on the first
  // element of the dictionary and calls CreateMapMLValue_AgnosticMap
  // which determines the container type. This type
  // is based on the first pair of the dictionary
  // and all the function assumes the key and value type remain the same
  // for all pairs in the dictionary.

  // If iterator is NULL, it returns a single Map,
  // if is not NULL, it returns a VectorMap.

  PyObject *key, *value;
  Py_ssize_t pos = 0;

  if (PyDict_Next(item, &pos, &key, &value)) {
    CreateMapMLValue_AgnosticMap(pos, key, name_input, value, iterator, item, alloc, mlvalue);
  } else {
    throw std::runtime_error("Size of dictionary is empty, unable to run the prediction.");
  }
}
#endif

static void CreateGenericIterableMLValue(PyObject* iterator, AllocatorPtr alloc, const std::string& name_input,
                                         OrtValue& mlvalue) {
  PyObject* item;
  item = PyIter_Next(iterator);
  if (item == NULL) {
    throw std::runtime_error("Input '" + name_input + "' must not be empty.");
  }
  if (PyObjectCheck_NumpyArray(item)) {
    PyObject* pType = PyObject_Type(item);
    PyObject* pStr = PyObject_Str(pType);
    py::str spyType = py::reinterpret_borrow<py::str>(pStr);
    std::string sType = spyType;
    Py_XDECREF(pType);
    Py_XDECREF(pStr);
    throw std::runtime_error("Iterable of " + sType + " should be given as array for input '" +
                             name_input + std::string("'."));
  } else {
    // We expect a dictionary.
    if (!PyDict_Check(item)) {
      throw std::runtime_error("Input must be a list of dictionaries or a single numpy array for input '" +
                               name_input + std::string("'."));
    }
#if !defined(DISABLE_ML_OPS)
    CreateMapMLValue_AgnosticVectorMap(iterator, item, alloc, name_input, mlvalue);
#else
    ORT_UNUSED_PARAMETER(alloc);
    ORT_UNUSED_PARAMETER(p_mlvalue);
    throw std::runtime_error("Map type is not supported in this build.");
#endif
  }
}

// Setting `use_numpy_data_memory` to `true` will ensure that the underlying numpy array buffer is directly used
// as the backing data buffer for the ORT Tensor where applicable (for numeric tensors)
// The numpy object owns the memory and needs to be alive until the corresponding OrtValue is in scope
void CreateGenericMLValue(const onnxruntime::InputDefList* input_def_list, const AllocatorPtr& alloc,
                          const std::string& name_input, const py::object& value,
                          OrtValue& mlvalue, bool accept_only_numpy_array,
                          bool use_numpy_data_memory, MemCpyFunc mem_cpy_to_device) {
  ONNX_NAMESPACE::TypeProto type_proto;
  if (PyObjectCheck_NumpyArray(value.ptr())) {
    // The most frequent case: input comes as an array.
    int npy_type = GetNumpyArrayType(value);
    auto py_array = py::cast<py::array>(value);
    if ((py_array.flags() & py::array::c_style) == 0) {
      throw std::runtime_error("Input must be a contiguous array for input '" + name_input + "'.");
    }
    auto tensor = CreateTensorOverPrimitiveDataOrCopy(alloc, py_array, npy_type, use_numpy_data_memory,
                                                      mem_cpy_to_device);
    Tensor::InitOrtValue(std::move(tensor), mlvalue);
  } else if (!accept_only_numpy_array &&
             PyList_Check(value.ptr()) &&
             !CheckIfInputIsSequenceType(name_input, input_def_list, type_proto)) {
    // This is not a sequence tensor. This is just a regular tensor fed through as a list.
    if (!(utils::HasTensorType(type_proto) && utils::HasElementType(type_proto))) {
      throw std::runtime_error(
          "The graph is missing type information needed to construct the ORT tensor for input " + name_input);
    }

    MLDataType ml_type = OrtTypeInfo::ElementTypeFromProto(
        static_cast<ONNX_NAMESPACE::TensorProto_DataType>(type_proto.tensor_type().elem_type()));

    CreateTensorMLValueFromSequence(name_input, value, ml_type, alloc, mlvalue);
  } else if (!accept_only_numpy_array && PyList_Check(value.ptr())) {
    auto py_list = py::cast<py::list>(value);
    CreateSequenceOfTensors(alloc, name_input, input_def_list, py_list, mlvalue);
  } else if (!accept_only_numpy_array && PyDict_Check(value.ptr())) {
#if !defined(DISABLE_ML_OPS)
    CreateMapMLValue_AgnosticVectorMap((PyObject*)NULL, value.ptr(), alloc, name_input, mlvalue);
#else
    ORT_UNUSED_PARAMETER(p_mlvalue);
    throw std::runtime_error("Map type is not supported in this build.");
#endif

  } else if (!accept_only_numpy_array && strcmp(Py_TYPE(value.ptr())->tp_name, PYTHON_ORTVALUE_OBJECT_NAME) == 0) {
    // This is an OrtValue coming in directly from Python, so assign the underlying native OrtValue handle
    // to the OrtValue object that we are going to use for Run().
    // This should just increase the ref counts of the underlying shared_ptrs in the native OrtValue
    // and the ref count will be decreased when the OrtValue used for Run() is destroyed upon exit.
    mlvalue = *value.attr(PYTHON_ORTVALUE_NATIVE_OBJECT_ATTR).cast<OrtValue*>();
  } else if (!accept_only_numpy_array) {
    auto iterator = PyObject_GetIter(value.ptr());
    if (iterator == NULL) {
      // The pype cannot be handled.
      PyObject* pType = PyObject_Type(value.ptr());
      PyObject* pStr = PyObject_Str(pType);
      py::str spyType = py::reinterpret_borrow<py::str>(pStr);
      std::string sType = spyType;
      Py_XDECREF(pType);
      Py_XDECREF(pStr);
      throw std::runtime_error(std::string("Unable to handle object of type ") + sType);
    }
    // We assume the object is iterable.
    // iterator should not be NULL due to previous test.
    try {
      CreateGenericIterableMLValue(iterator, alloc, name_input, mlvalue);
    } catch (const std::runtime_error&) {
      Py_DECREF(iterator);
      throw;
    }
    Py_DECREF(iterator);
  } else {
    throw std::runtime_error("Unable to create OrtValue from the given python object");
  }
}

}  // namespace python
}  // namespace onnxruntime
