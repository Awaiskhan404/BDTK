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

#include "CiderJoinBuild.h"

#include "velox/exec/Task.h"

namespace facebook::velox::plugin {

void CiderJoinBridge::setData(std::vector<VectorPtr> data) {
  std::vector<ContinuePromise> promises;
  {
    std::lock_guard<std::mutex> l(mutex_);
    VELOX_CHECK(!data_.has_value(), "setData may be called only once");
    data_ = std::move(data);
    promises = std::move(promises_);
  }
  notify(std::move(promises));
}

std::optional<std::vector<VectorPtr>> CiderJoinBridge::dataOrFuture(
    ContinueFuture* future) {
  std::lock_guard<std::mutex> l(mutex_);
  VELOX_CHECK(!cancelled_, "Getting data after the build side is aborted");
  if (data_.has_value()) {
    return data_;
  }
  promises_.emplace_back("CiderJoinBridge::dataOrFuture");
  *future = promises_.back().getSemiFuture();
  return std::nullopt;
}

CiderJoinBuild::CiderJoinBuild(int32_t operatorId,
                               exec::DriverCtx* driverCtx,
                               std::shared_ptr<const CiderPlanNode> joinNode)
    : Operator(driverCtx, nullptr, operatorId, joinNode->id(), "CiderJoinBuild") {}

void CiderJoinBuild::addInput(RowVectorPtr input) {
  if (input->size() > 0) {
    // Load lazy vectors before storing.
    for (auto& child : input->children()) {
      child->loadedVector();
    }
    data_.emplace_back(std::move(input));
  }
}

void CiderJoinBuild::noMoreInput() {
  Operator::noMoreInput();
  std::vector<ContinuePromise> promises;
  std::vector<std::shared_ptr<exec::Driver>> peers;
  // The last Driver to hit CiderJoinBuild::finish gathers the data from
  // all build Drivers and hands it over to the probe side. At this
  // point all build Drivers are continued and will free their
  // state. allPeersFinished is true only for the last Driver of the
  // build pipeline.
  if (!operatorCtx_->task()->allPeersFinished(
          planNodeId(), operatorCtx_->driver(), &future_, promises, peers)) {
    return;
  }

  for (auto& peer : peers) {
    auto op = peer->findOperator(planNodeId());
    auto* build = dynamic_cast<CiderJoinBuild*>(op);
    VELOX_CHECK(build);
    data_.insert(data_.begin(), build->data_.begin(), build->data_.end());
  }

  // Realize the promises so that the other Drivers (which were not
  // the last to finish) can continue from the barrier and finish.
  peers.clear();
  for (auto& promise : promises) {
    promise.setValue();
  }

  auto joinBridge = operatorCtx_->task()->getCustomJoinBridge(
      operatorCtx_->driverCtx()->splitGroupId, planNodeId());
  if (auto ciderJoinBridge = std::dynamic_pointer_cast<CiderJoinBridge>(joinBridge)) {
    ciderJoinBridge->setData(std::move(data_));
  }
}

exec::BlockingReason CiderJoinBuild::isBlocked(ContinueFuture* future) {
  if (!future_.valid()) {
    return exec::BlockingReason::kNotBlocked;
  }
  *future = std::move(future_);
  return exec::BlockingReason::kWaitForJoinBuild;
}

bool CiderJoinBuild::isFinished() {
  return !future_.valid() && noMoreInput_;
}

}  // namespace facebook::velox::plugin
