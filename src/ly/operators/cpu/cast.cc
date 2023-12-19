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

#include "ly/operators/cpu/cast.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include "lyutil/half.h"
#include "ly/tensor.h"
#include "ly/operators/cpu/cpu_tensor_data.h"
#include "ly/operators/cpu/lookup.h"
#include "ly/operators/cpu/tensor.h"


namespace ly {
namespace op {
namespace cpu {

Tensor cast(Tensor A, DType dtype) {
  if (A.getDType() == dtype) {
    return A;
  } else if (A.getDType() == DType::kFloat && dtype == DType::kQInt4Group32) {
    return castFp32ToQ4(A);
  } else if (A.getDType() == DType::kQInt4Group32 && dtype == DType::kFloat) {
    return castQ4ToFp32(A);
  } else {
    NOT_IMPL();
  }
}

Tensor castQ4ToFp32(Tensor A) {
  Tensor x = op::cpu::tensor(A.getShape(), DType::kFloat);
  op::cpu::applyDequant<QInt4Group32>(0, A.getNumEl(), A.getDataObject(), x.getData<float>());

  return x;
}

Tensor castFp32ToQ4(Tensor A) {
  CHECK(A.isContiguous()) << "unable to cast a non-contiguous tensor to Q4";
  constexpr int groupSize = QInt4Group32::GroupSize;

  int64_t numel = A.getNumEl();
  const float *dataA = A.getData<float>();
  std::vector<uint8_t> data(numel / 2);
  std::vector<uint16_t> qscale(numel / groupSize);
  std::vector<uint8_t> qzero(numel / groupSize / 2);

  int nb = numel / groupSize;
  for (int i = 0; i < nb; ++i) {
    int begin = i * groupSize;
    int end = (i + 1) * groupSize;

    float minVal = *std::min_element(dataA + begin, dataA + end);
    float maxVal = *std::max_element(dataA + begin, dataA + end);

    float scale = (maxVal - minVal) / 15.0;
    float zeroFp32 = roundf(-minVal / scale);
    CHECK(zeroFp32 >= 0.0f && zeroFp32 <= 15.0f);
    uint8_t zero = static_cast<uint8_t>(zeroFp32);

    for (int j = 0; j < 32; j += 2) {
      float dlFp32 = roundf((dataA[begin + j] - minVal) / scale);
      float dhFp32 = roundf((dataA[begin + j + 1] - minVal) / scale);
      CHECK(dlFp32 >= 0.0f && dlFp32 <= 15.0f && dhFp32 >= 0.0f && dhFp32 <= 15.0f);

      uint8_t dl = static_cast<uint8_t>(dlFp32);
      uint8_t dh = static_cast<uint8_t>(dhFp32);
      data[(begin + j) / 2] = (dh << 4) | dl;
    }

    if (i % 2 == 0) {
      qzero[i / 2] = 0;
      qzero[i / 2] |= zero;
    } else {
      qzero[i / 2] |= (zero << 4);
    }

    qscale[i] = lut::cvtss_sh(scale);
  }

  auto tensorData = CpuTensorData::create({
      {numel, DType::kQInt4Group32},
      {numel / groupSize, DType::kFloat16},
      {numel / groupSize / 2, DType::kUInt8}});
  memcpy(tensorData->getSlot(0)->getRawData(), data.data(), data.size());
  memcpy(tensorData->getSlot(1)->getRawData(), qscale.data(), qscale.size() * sizeof(uint16_t));
  memcpy(tensorData->getSlot(2)->getRawData(), qzero.data(), qzero.size());

  auto tensorShape = std::make_shared<internal::TensorShape>(A.getShape());
  return Tensor::create(tensorShape, tensorData);
}

}  // cpu
}  // op
}  // ly