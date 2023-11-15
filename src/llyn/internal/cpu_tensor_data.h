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

#include "llyn/device.h"
#include "llyn/internal/tensor_data.h"

namespace llyn {
namespace internal {

class CpuTensorData : public TensorData {
 public:
  static std::shared_ptr<TensorData> create(int64_t numel, DType dtype);
  static std::shared_ptr<TensorData> read(ly::ReadableFile *fp);

  CpuTensorData();
  ~CpuTensorData();

  Device getDevice() const override;

 private:
  struct Slot {
    Byte *data;
    int64_t numel;
    DType dtype;

    Slot();
  };

  Slot _slots[3];
  int _numSlot;

  void readSlot(ly::ReadableFile *fp, int slotIdx);

  DType getDTypeInternal(int slot) const override;
  int64_t getNumElInternal(int slot) const override;
  void *getDataInternal(int slot, int64_t offset) const override;
};

}  // namespace internal
}  // namespace llyn