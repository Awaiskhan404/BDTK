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

#ifndef CIDER_CIDERBATCHSTRINGIFIER_H
#define CIDER_CIDERBATCHSTRINGIFIER_H

#include "cider/CiderBatch.h"

class CiderBatchStringifier {
 public:
  virtual std::string stringifyValueAt(CiderBatch* batch, int row_index) = 0;
};

class StructBatchStringifier : public CiderBatchStringifier {
 private:
  // stores stringifiers to stringify children batches
  std::vector<std::unique_ptr<CiderBatchStringifier>> child_stringifiers_;

 public:
  explicit StructBatchStringifier(CiderBatch* batch);
  std::string stringifyValueAt(CiderBatch* batch, int row_index) override;
};

template <typename T>
class ScalarBatchStringifier : public CiderBatchStringifier {
 public:
  std::string stringifyValueAt(CiderBatch* batch, int row_index) override;
};

#endif
