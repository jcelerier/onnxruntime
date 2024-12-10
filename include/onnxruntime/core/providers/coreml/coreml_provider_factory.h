// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#pragma once

#include "onnxruntime_c_api.h"

// COREMLFlags are bool options we want to set for CoreML EP
// This enum is defined as bit flags, and cannot have negative value
// To generate an uint32_t coreml_flags for using with OrtSessionOptionsAppendExecutionProvider_CoreML below,
//   uint32_t coreml_flags = 0;
//   coreml_flags |= COREML_FLAG_USE_CPU_ONLY;
enum COREMLFlags {
  COREML_FLAG_USE_NONE = 0x000,

  // Using CPU only in CoreML EP, this may decrease the perf but will provide
  // reference output value without precision loss, which is useful for validation
  COREML_FLAG_USE_CPU_ONLY = 0x001,

  // Enable CoreML EP on subgraph
  COREML_FLAG_ENABLE_ON_SUBGRAPH = 0x002,

  // By default CoreML Execution provider will be enabled for all compatible Apple devices
  // Enable this option will only enable CoreML EP for Apple devices with ANE (Apple Neural Engine)
  // Please note, enable this option does not guarantee the entire model to be executed using ANE only
  COREML_FLAG_ONLY_ENABLE_DEVICE_WITH_ANE = 0x004,

  // Only allow CoreML EP to take nodes with inputs with static shapes. By default it will also allow inputs with
  // dynamic shapes. However, the performance may be negatively impacted if inputs have dynamic shapes.
  COREML_FLAG_ONLY_ALLOW_STATIC_INPUT_SHAPES = 0x008,

  // Create an MLProgram. By default it will create a NeuralNetwork model. Requires Core ML 5 or later.
  COREML_FLAG_CREATE_MLPROGRAM = 0x010,

  // https://developer.apple.com/documentation/coreml/mlcomputeunits?language=objc
  // there are four compute units:
  // MLComputeUnitsCPUAndNeuralEngine|MLComputeUnitsCPUAndGPU|MLComputeUnitsCPUOnly|MLComputeUnitsAll
  // different CU will have different performance and power consumption
  COREML_FLAG_USE_CPU_AND_GPU = 0x020,
  // Keep COREML_FLAG_LAST at the end of the enum definition
  // And assign the last COREMLFlag to it
  COREML_FLAG_LAST = COREML_FLAG_USE_CPU_AND_GPU,
};

// MLComputeUnits can be one of the following values:
// 'MLComputeUnitsCPUAndNeuralEngine|MLComputeUnitsCPUAndGPU|MLComputeUnitsCPUOnly|MLComputeUnitsAll'
// these values are intended to be used with Ort::SessionOptions::AppendExecutionProvider (C++ API)
// and SessionOptionsAppendExecutionProvider (C API). For the old API, use COREMLFlags instead.
static const char* const kCoremlProviderOption_MLComputeUnits = "MLComputeUnits";
static const char* const kCoremlProviderOption_ModelFormat = "ModelFormat";
// same as COREML_FLAG_ONLY_ALLOW_STATIC_INPUT_SHAPES
static const char* const kCoremlProviderOption_RequireStaticInputShapes = "RequireStaticInputShapes";
static const char* const kCoremlProviderOption_EnableOnSubgraphs = "EnableOnSubgraphs";
// provided by https://developer.apple.com/documentation/coreml/mloptimizationhints-swift.struct/specializationstrategy-swift.property
// Core ML segments the model’s compute graph and specializes each segment for the target compute device.
// This process can affect the model loading time and the prediction latency.
// Use this option to tailor the specialization strategy for your model.
static const char* const kCoremlProviderOption_SpecializationStrategy = "SpecializationStrategy";
// Profile the Core ML MLComputePlan.
// This logs the hardware each operator is dispatched to and the estimated execution time.
// Intended for developer usage but provide useful diagnostic information if performance is not as expected.
static const char* const kCoremlProviderOption_ProfileComputePlan = "ProfileComputePlan";
// please refer to https://developer.apple.com/documentation/coreml/mlmodelconfiguration/allowlowprecisionaccumulationongpu
static const char* const kCoremlProviderOption_AllowLowPrecisionAccumulationOnGPU = "AllowLowPrecisionAccumulationOnGPU";
// Specify the path to cache the model.
// CoreML EP will convert onnx subgraph to CoreML model and save to disk.
// If this path is not specified, the model will be saved to a temp directory and deleted after the session is closed.
// otherwise, the model will be saved to the specified path and User should manage to delete the model.
// The basic logic is:
//   if (ModelCachePath != nullptr && ModelCachePath/cache_coreml.exists()) {
//     // load from cache_coreml
//   } else {
//     // save to ModelCachePath
//   }
// we wound not detect if the cached model match the onnx subgraph, so User should carefully manage the cache for a new model.
static const char* const kCoremlProviderOption_ModelCachePath = "ModelCachePath";

#ifdef __cplusplus
extern "C" {
#endif

ORT_EXPORT ORT_API_STATUS(OrtSessionOptionsAppendExecutionProvider_CoreML,
                          _In_ OrtSessionOptions* options, uint32_t coreml_flags);

#ifdef __cplusplus
}
#endif
