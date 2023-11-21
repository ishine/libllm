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

#pragma once

#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <stdint.h>
#include <type_traits>
#include "lyutil/error.h"
#include "lyutil/strings.h"
#include "llyn/tensor.h"
#include "llyn/internal/cuda_tensor_data.h"
#include "llyn/internal/tensor_shape.h"

namespace llyn {
namespace cuda {

template<typename T, int DIM>
class Subtensor {
 public:
  __device__ Subtensor(internal::TensorShape::Elem *shape, T *data) :
      _shape(shape),
      _data(data) {}

  __device__ Subtensor<T, DIM - 1> operator[](int index) {
    int64_t offset = index * this->_shape[0].stride;
    return Subtensor<T, DIM - 1>(_shape + 1, _data + offset);
  }

  __device__ const Subtensor<T, DIM - 1> operator[](int index) const {
    int64_t offset = index * this->_shape[0].stride;
    return Subtensor<T, DIM - 1>(_shape + 1, _data + offset);
  }

 private:
  internal::TensorShape::Elem *_shape;
  T *_data;
};

template<typename T>
class Subtensor<T, 1> {
 public:
  __device__ Subtensor(internal::TensorShape::Elem *shape, T *data) :
      _shape(shape),
      _data(data) {}

  __device__ typename T &operator[](int index) {
    int64_t offset = index * this->_shape[0].stride;
    return _data[offset];
  }

  __device__ typename T operator[](int index) const {
    int64_t offset = index * this->_shape[0].stride;
    return _data[offset];
  }

 private:
  internal::TensorShape::Elem *_shape;
  T *_data;
};

/// @brief A packed tensor accessor. `Packed` means the subtensor also packed with the tensor 
/// metadata.
/// @tparam T Tensor data type.
/// @tparam DIM Dimension of this tensor.
template<typename T, int DIM>
class PackedSubtensor {
 public:
  __host__ explicit PackedSubtensor(const Tensor &tensor) {
    CHECK(tensor.getDim() == DIM);
    _data = tensor.getData<void>();
    for (int i = 0; i < DIM; ++i) {
      
    }

  }

 private:
  internal::TensorShape::Elem _shape[DIM];
  T *_data;
};


}  // namespace cuda
}  // namespace cuda
