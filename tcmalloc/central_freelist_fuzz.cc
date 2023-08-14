// Copyright 2020 The TCMalloc Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "absl/log/check.h"
#include "tcmalloc/central_freelist.h"
#include "tcmalloc/mock_central_freelist.h"
#include "tcmalloc/mock_static_forwarder.h"

GOOGLE_MALLOC_SECTION_BEGIN
namespace tcmalloc {
namespace {

using CentralFreeList =
    tcmalloc_internal::central_freelist_internal::CentralFreeList<
        tcmalloc_internal::MockStaticForwarder>;
using CentralFreelistEnv =
    tcmalloc_internal::FakeCentralFreeListEnvironment<CentralFreeList>;
using tcmalloc_internal::kMaxObjectsToMove;

template <typename Env>
int RunFuzzer(const uint8_t* data, size_t size) {
  if (size < 5 || size > 100000) {
    // size < 5 for bare minimum buzz test for a single operation.
    // Avoid overly large inputs as we perform some shuffling and checking.
    return 0;
  }

  Env env;
  std::vector<void*> objects;

  for (int i = 0; i + 5 < size; i += 5) {
    // data[N] : choose the operation.
    const uint8_t op = data[i];
    // We only use data[N+1] right now. data[N+4:N+2] are currently reserved.
    // TODO(271282540): Add support for multiple size classes for fuzzing.
    uint32_t value;
    memcpy(&value, &data[i + 1], sizeof(value));

    switch (op & 0x7) {
      case 0: {
        // Allocate objects.
        // value[7:0] : number of objects to allocate.
        const uint8_t num_objects = value & 0x00FF;
        void* batch[kMaxObjectsToMove];
        const size_t n = num_objects % kMaxObjectsToMove + 1;
        int allocated = env.central_freelist().RemoveRange(batch, n);
        objects.insert(objects.end(), batch, batch + allocated);
        break;
      }
      case 1: {
        // Deallocate objects if number of previously allocated objects is
        // non-empty. value[7:0] : number of objects to deallocate.
        if (objects.empty()) break;

        const uint8_t num_objects = value & 0x00FF;
        const size_t n = std::min<size_t>(num_objects % kMaxObjectsToMove + 1,
                                          objects.size());
        env.central_freelist().InsertRange({&objects[objects.size() - n], n});
        objects.resize(objects.size() - n);
        break;
      }
      case 2: {
        // Shuffle allocated objects such that we don't return them in the same
        // order we allocated them.
        const int seed = value & 0x00FF;
        std::mt19937 rng(seed);
        // Limit number of elements to shuffle so that we don't spend a lot of
        // time in shuffling a large number of objects.
        constexpr int kMaxToShuffle = 10 * kMaxObjectsToMove;
        if (objects.size() <= kMaxToShuffle) {
          std::shuffle(objects.begin(), objects.end(), rng);
        } else {
          std::shuffle(objects.end() - kMaxToShuffle, objects.end(), rng);
        }
        break;
      }
      case 3: {
        // Check stats.
        tcmalloc_internal::SpanStats stats =
            env.central_freelist().GetSpanStats();
        CHECK_EQ(env.central_freelist().length() + objects.size(),
                 stats.obj_capacity);
        if (objects.empty()) {
          CHECK_EQ(stats.num_live_spans(), 0);
        } else {
          CHECK_GT(stats.num_live_spans(), 0);
        }
        break;
      }
    }
  }

  // Clean up.
  const size_t allocated = objects.size();
  size_t returned = 0;
  while (returned < allocated) {
    const size_t to_return = std::min(allocated - returned, kMaxObjectsToMove);
    env.central_freelist().InsertRange({&objects[returned], to_return});
    returned += to_return;
  }
  return 0;
}

}  // namespace
}  // namespace tcmalloc
GOOGLE_MALLOC_SECTION_END

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  tcmalloc::RunFuzzer<tcmalloc::CentralFreelistEnv>(data, size);
  return 0;
}
