/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdexcept>

#include "flashlight/flashlight/common/CppBackports.h"
#include "flashlight/flashlight/common/Serialization.h"
#include "flashlight/flashlight/dataset/PrefetchDataset.h"

namespace fl {

PrefetchDataset::PrefetchDataset(
    std::shared_ptr<const Dataset> dataset,
    int64_t numThreads,
    int64_t prefetchSize)
    : dataset_(dataset),
      numThreads_(numThreads),
      prefetchSize_(prefetchSize),
      curIdx_(-1) {
  if (!dataset_) {
    throw std::invalid_argument("dataset to be prefetched is null");
  }
  if (!(numThreads_ > 0 && prefetchSize_ > 0) &&
      !(numThreads_ == 0 && prefetchSize_ == 0)) {
    throw std::invalid_argument("invalid numThreads or prefetchSize");
  }
  if (numThreads_ > 0) {
    auto deviceId = af::getDevice();
    threadPool_ = cpp::make_unique<ThreadPool>(
        numThreads_,
        [deviceId](int /* threadId */) { af::setDevice(deviceId); });
  }
}

std::vector<af::array> PrefetchDataset::get(int64_t idx) const {
  checkIndexBounds(idx);

  if (numThreads_ == 0) {
    return dataset_->get(idx);
  }

  // remove from cache (if necessary)
  while (!prefetchCache_.empty() && idx != curIdx_) {
    prefetchCache_.pop();
    ++curIdx_;
  }

  // add to cache (if necessary)
  while (prefetchCache_.size() < prefetchSize_) {
    auto fetchIdx = idx + prefetchCache_.size();
    if (fetchIdx >= size()) {
      break;
    }
    prefetchCache_.emplace(threadPool_->enqueue([this, fetchIdx]() {
      auto sample = this->dataset_->get(fetchIdx);
      std::ostringstream oss;
      fl::save(oss, sample);
      return oss.str();
    }));
  }

  auto curSampleStr = prefetchCache_.front().get();
  std::istringstream iss(curSampleStr);

  std::vector<af::array> curSample;
  fl::load(iss, curSample);

  prefetchCache_.pop();
  curIdx_ = idx + 1;
  return curSample;
}

int64_t PrefetchDataset::size() const {
  return dataset_->size();
}
} // namespace fl