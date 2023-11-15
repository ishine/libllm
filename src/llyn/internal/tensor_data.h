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

#include <stdint.h>
#include <memory>

#include "llyn/device.h"
#include "llyn/dtype.h"
#include "lyutil/log.h"
#include "lyutil/reader.h"

namespace llyn {
namespace internal {


/// @brief holds the internal data of a Tensor.
class TensorData {
 public:
  static constexpr int MaxSlot = 3;
  static constexpr int64_t MaxNumEl = 1073741824;

  virtual ~TensorData() = default;

  // get the device of tensor data.
  virtual Device getDevice() const = 0;

  template<int SLOT, typename T>
  T *getData(int offset = 0) const;

  template<int SLOT = 0>
  DType getDType() const { return getDTypeInternal(SLOT); }

  template<int SLOT = 0>
  int64_t getNumEl() const { return getNumElInternal(SLOT); }

  template<int SLOT = 0>
  int64_t getSizeInBytes() const { return getDType<SLOT>().getTotalSize(getNumEl<SLOT>()); }

  /// @brief throw if the tensor data is invalid.
  void throwIfInvalid();

 protected:
  virtual DType getDTypeInternal(int slot) const = 0;
  virtual int64_t getNumElInternal(int slot) const = 0;
  virtual void *getDataInternal(int slot, int64_t offset) const = 0;
};

template<int SLOT, typename T>
T *TensorData::getData(int offset) const {
  CHECK(DType::getType<T>() == getDTypeInternal(SLOT));
  return reinterpret_cast<T *>(getDataInternal(SLOT, offset));
}

}  // namespace internal
}  // namespace llyn
