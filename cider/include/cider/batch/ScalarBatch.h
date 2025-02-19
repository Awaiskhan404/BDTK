/*
 * Copyright (c) 2022 Intel Corporation.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef CIDER_SCALAR_BATCH_H
#define CIDER_SCALAR_BATCH_H

#include <type_traits>

#include "CiderBatch.h"

template <typename T>
class ScalarBatch final : public CiderBatch {
 public:
  static std::unique_ptr<ScalarBatch<T>> Create(ArrowSchema* schema,
                                                std::shared_ptr<CiderAllocator> allocator,
                                                ArrowArray* array = nullptr) {
    return array ? std::make_unique<ScalarBatch<T>>(schema, array, allocator)
                 : std::make_unique<ScalarBatch<T>>(schema, allocator);
  }

  using NativeType = std::conditional_t<std::is_same_v<bool, T>, uint8_t, T>;

  explicit ScalarBatch(ArrowSchema* schema, std::shared_ptr<CiderAllocator> allocator)
      : CiderBatch(schema, allocator) {
    checkArrowEntries();
  }
  explicit ScalarBatch(ArrowSchema* schema,
                       ArrowArray* array,
                       std::shared_ptr<CiderAllocator> allocator)
      : CiderBatch(schema, array, allocator) {
    checkArrowEntries();
  }

  NativeType* getMutableRawData() {
    CHECK(!isMoved());
    return reinterpret_cast<NativeType*>(const_cast<void*>(getBuffersPtr()[1]));
  }

  const NativeType* getRawData() const {
    CHECK(!isMoved());
    return reinterpret_cast<const NativeType*>(getBuffersPtr()[1]);
  }

 protected:
  bool resizeData(int64_t size) override {
    CHECK(!isMoved());
    if (!permitBufferAllocate()) {
      return false;
    }

    auto array_holder = reinterpret_cast<CiderArrowArrayBufferHolder*>(getArrayPrivate());

    if constexpr (std::is_same_v<T, bool>) {
      array_holder->allocBuffer(1, (size + 7) >> 3);
    } else {
      array_holder->allocBuffer(1, sizeof(T) * size);
    }
    setLength(size);

    return true;
  }

 private:
  void checkArrowEntries() const {
    CHECK_EQ(getChildrenNum(), 0);
    CHECK_EQ(getBufferNum(), 2);
  }
};

class VarcharBatch final : public CiderBatch {
 public:
  static std::unique_ptr<VarcharBatch> Create(ArrowSchema* schema,
                                              std::shared_ptr<CiderAllocator> allocator,
                                              ArrowArray* array = nullptr) {
    return array ? std::make_unique<VarcharBatch>(schema, array, allocator)
                 : std::make_unique<VarcharBatch>(schema, allocator);
  }

  explicit VarcharBatch(ArrowSchema* schema, std::shared_ptr<CiderAllocator> allocator)
      : CiderBatch(schema, allocator) {
    checkArrowEntries();
  }
  explicit VarcharBatch(ArrowSchema* schema,
                        ArrowArray* array,
                        std::shared_ptr<CiderAllocator> allocator)
      : CiderBatch(schema, array, allocator) {
    checkArrowEntries();
  }

  uint8_t* getMutableRawData() {
    CHECK(!isMoved());
    return reinterpret_cast<uint8_t*>(
        const_cast<void*>(getBuffersPtr()[getDataBufferIndex()]));
  }

  const uint8_t* getRawData() const {
    CHECK(!isMoved());
    return reinterpret_cast<const uint8_t*>(getBuffersPtr()[getDataBufferIndex()]);
  }

  int32_t* getMutableRawOffset() {
    CHECK(!isMoved());
    return reinterpret_cast<int32_t*>(
        const_cast<void*>(getBuffersPtr()[getOffsetBufferIndex()]));
  }

  const int32_t* getRawOffset() const {
    CHECK(!isMoved());
    return reinterpret_cast<const int32_t*>(getBuffersPtr()[getOffsetBufferIndex()]);
  }

  bool resizeDataBuffer(int64_t size) {
    CHECK(!isMoved());
    if (!permitBufferAllocate()) {
      return false;
    }

    auto array_holder = reinterpret_cast<CiderArrowArrayBufferHolder*>(getArrayPrivate());
    array_holder->allocBuffer(2, size);
    return true;
  }

 protected:
  inline const size_t getOffsetBufferIndex() const { return 1; }
  inline const size_t getDataBufferIndex() const { return 2; }

  bool resizeData(int64_t size) override {
    CHECK(!isMoved());
    if (!permitBufferAllocate()) {
      return false;
    }

    auto array_holder = reinterpret_cast<CiderArrowArrayBufferHolder*>(getArrayPrivate());
    size_t origin_offset_len = array_holder->getBufferSizeAt(1);
    array_holder->allocBuffer(1, sizeof(int32_t) * (size + 1));  // offset buffer
    std::memset((void*)(getMutableRawOffset() + origin_offset_len / sizeof(int32_t)),
                0,
                sizeof(int32_t) * (size + 1) - origin_offset_len);
    size_t bytes = array_holder->getBufferSizeAt(2);
    array_holder->allocBuffer(2, bytes);  // data buffer, it should never shrink.

    setLength(size);

    return true;
  }

 private:
  void checkArrowEntries() const {
    CHECK_EQ(getChildrenNum(), 0);
    CHECK_EQ(getBufferNum(), 3);
  }
};

#endif
