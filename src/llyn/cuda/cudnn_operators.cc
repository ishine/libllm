// The MIT License (MIT)
//
// Copyright (c) 2023 Xiaoyang Chen
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <cudnn.h>
#include "llyn/internal/cuda_tensor_data.h"
#include "llyn/cuda/cuda_common.h"
#include "llyn/cuda/cudnn_operators.h"
#include "lyutil/log.h"

#define CHECK_CUDNN_STATUS(x) { \
      cudnnStatus_t status = x; \
      if (status != CUDNN_STATUS_SUCCESS) { \
        LOG(ERROR) << "Error while calling: " << #x; \
        throw ly::AbortedError(cudnnGetErrorString(status)); \
      } \
    }

namespace llyn {
namespace cuda {

CudnnOperators::CudnnOperators() : _handle{nullptr, checkDestroy<cudnnHandle_t>(cudnnDestroy)} {}

std::shared_ptr<CudnnOperators> CudnnOperators::create() {
  std::shared_ptr<CudnnOperators> ops{new CudnnOperators()};
  CHECK_CUDNN_STATUS(cudnnCreate(ops->_handle.get_pp()));
  
  return ops;
}

cudnnDataType_t CudnnOperators::getCudnnDataType(const Tensor &tensor) {
  // Only single-slot tensor is allowed.
  const internal::TensorData *tensorData = tensor.getDataObject();
  CHECK(tensorData->getNumSlot() == 1);

  switch (tensorData->getDType()) {
    case DType::kFloat16:
      return CUDNN_DATA_HALF;
    default:
      NOT_IMPL();
  }
}

template<typename T>
std::function<void(T)> CudnnOperators::checkDestroy(std::function<cudnnStatus_t(T)> destroyFunc) {
  return [destroyFunc](T handle) {
    CHECK_CUDNN_STATUS((destroyFunc(handle)));
  };
}

auto_handle<cudnnTensorDescriptor_t> CudnnOperators::createCudnnTensorDescriptor(
    const Tensor &tensor) {
  auto_handle<cudnnTensorDescriptor_t> tensorDesc{
      nullptr, checkDestroy<cudnnTensorDescriptor_t>(cudnnDestroyTensorDescriptor)};
  CHECK_CUDNN_STATUS(cudnnCreateTensorDescriptor(tensorDesc.get_pp()));

  if (tensor.isContiguous()) {
    int n = 1, h = 1, w = 1, c = 1;
    switch (tensor.getDim()) {
      case 1:
        c = tensor.getShape(0);
        break;
      case 2:
        w = tensor.getShape(0);
        c = tensor.getShape(1);
        break;
      case 3:
        h = tensor.getShape(0);
        w = tensor.getShape(1);
        c = tensor.getShape(2);
        break;
      case 4:
        n = tensor.getShape(0);
        h = tensor.getShape(1);
        w = tensor.getShape(2);
        c = tensor.getShape(3);
        break;
      default:
        NOT_IMPL();
    }
    CHECK_CUDNN_STATUS(cudnnSetTensor4dDescriptor(
        tensorDesc.get(),
        CUDNN_TENSOR_NHWC,
        getCudnnDataType(tensor),
        n,
        c,
        h,
        w));
  } else {
    int n = 1, h = 1, w = 1, c = 1;
    int ns, hs, ws, cs;
    switch (tensor.getDim()) {
      case 1:
        c = tensor.getShape(0);
        cs = tensor.getStride(0);
        ws = c * cs;
        hs = ws;
        ns = ws;
        break;
      case 2:
        w = tensor.getShape(0);
        c = tensor.getShape(1);
        ws = tensor.getStride(0);
        cs = tensor.getStride(1);
        hs = w * ws;
        ns = hs;
        break;
      case 3:
        h = tensor.getShape(0);
        w = tensor.getShape(1);
        c = tensor.getShape(2);
        hs = tensor.getStride(0);
        ws = tensor.getStride(1);
        cs = tensor.getStride(2);
        ns = hs * h;
        break;
      case 4:
        n = tensor.getShape(0);
        h = tensor.getShape(1);
        w = tensor.getShape(2);
        c = tensor.getShape(3);
        ns = tensor.getStride(0);
        hs = tensor.getStride(1);
        ws = tensor.getStride(2);
        cs = tensor.getStride(3);
        break;
      default:
        NOT_IMPL();
    }
    CHECK_CUDNN_STATUS(cudnnSetTensor4dDescriptorEx(
        tensorDesc.get(),
        getCudnnDataType(tensor),
        n,
        c,
        h,
        w,
        ns,
        cs,
        hs,
        ws));
  }

  return tensorDesc;
}

Tensor CudnnOperators::contigious(Tensor tensor) {
  if (tensor.isContiguous())
    return tensor;

  Tensor tgtTensor;
  void *alpha = nullptr;
  void *beta = nullptr;

  half alphaHalf = 1.0;
  half betaHalf = 0.0;
  int64_t alphaLong = 1;
  int64_t betaLong = 0;
  if (tensor.getDType() == DType::kFloat16) {
    alpha = &alphaHalf;
    beta = &betaHalf;
    tgtTensor = createCudaTensorHalf(tensor.getShape());
  } else if (tensor.getDType() == DType::kLong) {
    alpha = &alphaLong;
    beta = &betaLong;
    tgtTensor = createCudaTensorLong(tensor.getShape());
  } else {
    NOT_IMPL();
  }

  auto_handle<cudnnTensorDescriptor_t> srcDesc = createCudnnTensorDescriptor(tensor);
  auto_handle<cudnnTensorDescriptor_t> tgtDesc = createCudnnTensorDescriptor(tgtTensor);
  CHECK_CUDNN_STATUS(cudnnTransformTensor(
      _handle.get(),
      alpha,
      srcDesc.get(),
      tensor.getData<void>(),
      beta,
      tgtDesc.get(),
      tgtTensor.getData<void>()));

  return tgtTensor;
}

}  // cuda
}  // llyn

