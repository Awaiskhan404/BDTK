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

#ifndef CIDER_FUNCTION_SUBSTRAIT_SUBSTRAITFUNCTIONMAPPING_H
#define CIDER_FUNCTION_SUBSTRAIT_SUBSTRAITFUNCTIONMAPPING_H

#include <memory>
#include <unordered_map>
#include <vector>

namespace cider::function::substrait {

using FunctionMappings = std::unordered_map<std::string, std::string>;

/// An interface describe the function names in difference between velox engine
/// own and Substrait system.
class SubstraitFunctionMappings {
 public:
  /// scalar function names in difference between engine own and Substrait.
  virtual const FunctionMappings scalarMappings() const {
    static const FunctionMappings scalarMappings{};
    return scalarMappings;
  };

  /// aggregate function names in difference between engine own and Substrait.
  virtual const FunctionMappings aggregateMappings() const {
    static const FunctionMappings aggregateMappings{};
    return aggregateMappings;
  };

  /// window function names in difference between engine own and Substrait.
  virtual const FunctionMappings windowMappings() const {
    static const FunctionMappings windowMappings{};
    return windowMappings;
  };
};

using SubstraitFunctionMappingsPtr = std::shared_ptr<const SubstraitFunctionMappings>;
}  // namespace cider::function::substrait

#endif  // CIDER_FUNCTION_SUBSTRAIT_SUBSTRAITFUNCTIONMAPPING_H
