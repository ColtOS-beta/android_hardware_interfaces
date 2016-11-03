/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "graphics_allocator_hidl_hal_test"

#include <unordered_set>

#include <android-base/logging.h>
#include <android/hardware/graphics/allocator/2.0/IAllocator.h>
#include <gtest/gtest.h>

namespace android {
namespace hardware {
namespace graphics {
namespace allocator {
namespace V2_0 {
namespace tests {
namespace {

#define CHECK_FEATURE_OR_SKIP(FEATURE_NAME)                 \
  do {                                                      \
    if (!hasCapability(FEATURE_NAME)) {                     \
      std::cout << "[  SKIPPED ] Feature " << #FEATURE_NAME \
                << " not supported" << std::endl;           \
      return;                                               \
    }                                                       \
  } while (0)

class TempDescriptor {
 public:
  TempDescriptor(const sp<IAllocator>& allocator,
                 const IAllocator::BufferDescriptorInfo& info)
      : mAllocator(allocator), mError(Error::NO_RESOURCES) {
    mAllocator->createDescriptor(
        info, [&](const auto& tmpError, const auto& tmpDescriptor) {
          mError = tmpError;
          mDescriptor = tmpDescriptor;
        });
  }

  ~TempDescriptor() {
    if (mError == Error::NONE) {
      mAllocator->destroyDescriptor(mDescriptor);
    }
  }

  bool isValid() const { return (mError == Error::NONE); }

  operator BufferDescriptor() const { return mDescriptor; }

 private:
  sp<IAllocator> mAllocator;
  Error mError;
  BufferDescriptor mDescriptor;
};

class GraphicsAllocatorHidlTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mAllocator = IAllocator::getService("gralloc");
    ASSERT_NE(mAllocator, nullptr);

    initCapabilities();

    mDummyDescriptorInfo.width = 64;
    mDummyDescriptorInfo.height = 64;
    mDummyDescriptorInfo.format = PixelFormat::RGBA_8888;
    mDummyDescriptorInfo.producerUsageMask =
        static_cast<uint64_t>(ProducerUsage::CPU_WRITE);
    mDummyDescriptorInfo.consumerUsageMask =
        static_cast<uint64_t>(ConsumerUsage::CPU_READ);
  }

  void TearDown() override {}

  /**
   * Initialize the set of supported capabilities.
   */
  void initCapabilities() {
    mAllocator->getCapabilities([this](const auto& capabilities) {
      std::vector<IAllocator::Capability> caps = capabilities;
      mCapabilities.insert(caps.cbegin(), caps.cend());
    });
  }

  /**
   * Test whether a capability is supported.
   */
  bool hasCapability(IAllocator::Capability capability) const {
    return (mCapabilities.count(capability) > 0);
  }

  sp<IAllocator> mAllocator;
  IAllocator::BufferDescriptorInfo mDummyDescriptorInfo{};

 private:
  std::unordered_set<IAllocator::Capability> mCapabilities;
};

TEST_F(GraphicsAllocatorHidlTest, GetCapabilities) {
  auto ret = mAllocator->getCapabilities([](const auto& capabilities) {
    std::vector<IAllocator::Capability> caps = capabilities;
    for (auto cap : caps) {
      EXPECT_NE(IAllocator::Capability::INVALID, cap);
    }
  });

  ASSERT_TRUE(ret.getStatus().isOk());
}

TEST_F(GraphicsAllocatorHidlTest, DumpDebugInfo) {
  auto ret = mAllocator->dumpDebugInfo([](const auto&) {
    // nothing to do
  });

  ASSERT_TRUE(ret.getStatus().isOk());
}

TEST_F(GraphicsAllocatorHidlTest, CreateDestroyDescriptor) {
  Error error;
  BufferDescriptor descriptor;
  auto ret = mAllocator->createDescriptor(
      mDummyDescriptorInfo,
      [&](const auto& tmpError, const auto& tmpDescriptor) {
        error = tmpError;
        descriptor = tmpDescriptor;
      });

  ASSERT_TRUE(ret.getStatus().isOk());
  ASSERT_EQ(Error::NONE, error);

  auto err_ret = mAllocator->destroyDescriptor(descriptor);
  ASSERT_TRUE(err_ret.getStatus().isOk());
  ASSERT_EQ(Error::NONE, static_cast<Error>(err_ret));
}

/**
 * Test testAllocate with a single buffer descriptor.
 */
TEST_F(GraphicsAllocatorHidlTest, TestAllocateBasic) {
  CHECK_FEATURE_OR_SKIP(IAllocator::Capability::TEST_ALLOCATE);

  TempDescriptor descriptor(mAllocator, mDummyDescriptorInfo);
  ASSERT_TRUE(descriptor.isValid());

  hidl_vec<BufferDescriptor> descriptors;
  descriptors.resize(1);
  descriptors[0] = descriptor;

  auto ret = mAllocator->testAllocate(descriptors);
  ASSERT_TRUE(ret.getStatus().isOk());

  auto error = static_cast<Error>(ret);
  ASSERT_TRUE(error == Error::NONE || error == Error::NOT_SHARED);
}

/**
 * Test testAllocate with two buffer descriptors.
 */
TEST_F(GraphicsAllocatorHidlTest, TestAllocateArray) {
  CHECK_FEATURE_OR_SKIP(IAllocator::Capability::TEST_ALLOCATE);

  TempDescriptor descriptor(mAllocator, mDummyDescriptorInfo);
  ASSERT_TRUE(descriptor.isValid());

  hidl_vec<BufferDescriptor> descriptors;
  descriptors.resize(2);
  descriptors[0] = descriptor;
  descriptors[1] = descriptor;

  auto ret = mAllocator->testAllocate(descriptors);
  ASSERT_TRUE(ret.getStatus().isOk());

  auto error = static_cast<Error>(ret);
  ASSERT_TRUE(error == Error::NONE || error == Error::NOT_SHARED);
}

/**
 * Test allocate/free with a single buffer descriptor.
 */
TEST_F(GraphicsAllocatorHidlTest, AllocateFreeBasic) {
  TempDescriptor descriptor(mAllocator, mDummyDescriptorInfo);
  ASSERT_TRUE(descriptor.isValid());

  hidl_vec<BufferDescriptor> descriptors;
  descriptors.resize(1);
  descriptors[0] = descriptor;

  Error error;
  std::vector<Buffer> buffers;
  auto ret = mAllocator->allocate(
      descriptors, [&](const auto& tmpError, const auto& tmpBuffers) {
        error = tmpError;
        buffers = tmpBuffers;
      });

  ASSERT_TRUE(ret.getStatus().isOk());
  ASSERT_TRUE(error == Error::NONE || error == Error::NOT_SHARED);
  EXPECT_EQ(1u, buffers.size());

  if (!buffers.empty()) {
    auto err_ret = mAllocator->free(buffers[0]);
    EXPECT_TRUE(err_ret.getStatus().isOk());
    EXPECT_EQ(Error::NONE, static_cast<Error>(err_ret));
  }
}

/**
 * Test allocate/free with an array of buffer descriptors.
 */
TEST_F(GraphicsAllocatorHidlTest, AllocateFreeArray) {
  TempDescriptor descriptor1(mAllocator, mDummyDescriptorInfo);
  ASSERT_TRUE(descriptor1.isValid());

  TempDescriptor descriptor2(mAllocator, mDummyDescriptorInfo);
  ASSERT_TRUE(descriptor2.isValid());

  hidl_vec<BufferDescriptor> descriptors;
  descriptors.resize(3);
  descriptors[0] = descriptor1;
  descriptors[1] = descriptor1;
  descriptors[2] = descriptor2;

  Error error;
  std::vector<Buffer> buffers;
  auto ret = mAllocator->allocate(
      descriptors, [&](const auto& tmpError, const auto& tmpBuffers) {
        error = tmpError;
        buffers = tmpBuffers;
      });

  ASSERT_TRUE(ret.getStatus().isOk());
  ASSERT_TRUE(error == Error::NONE || error == Error::NOT_SHARED);
  EXPECT_EQ(descriptors.size(), buffers.size());

  for (auto buf : buffers) {
    auto err_ret = mAllocator->free(buf);
    EXPECT_TRUE(err_ret.getStatus().isOk());
    EXPECT_EQ(Error::NONE, static_cast<Error>(err_ret));
  }
}

TEST_F(GraphicsAllocatorHidlTest, ExportHandle) {
  TempDescriptor descriptor(mAllocator, mDummyDescriptorInfo);
  ASSERT_TRUE(descriptor.isValid());

  hidl_vec<BufferDescriptor> descriptors;
  descriptors.resize(1);
  descriptors[0] = descriptor;

  Error error;
  std::vector<Buffer> buffers;
  auto ret = mAllocator->allocate(
      descriptors, [&](const auto& tmpError, const auto& tmpBuffers) {
        error = tmpError;
        buffers = tmpBuffers;
      });

  ASSERT_TRUE(ret.getStatus().isOk());
  ASSERT_TRUE(error == Error::NONE || error == Error::NOT_SHARED);
  ASSERT_EQ(1u, buffers.size());

  ret = mAllocator->exportHandle(
      descriptors[0], buffers[0],
      [&](const auto& tmpError, const auto&) { error = tmpError; });
  EXPECT_TRUE(ret.getStatus().isOk());
  EXPECT_EQ(Error::NONE, error);

  auto err_ret = mAllocator->free(buffers[0]);
  EXPECT_TRUE(err_ret.getStatus().isOk());
  EXPECT_EQ(Error::NONE, static_cast<Error>(err_ret));
}

}  // namespace anonymous
}  // namespace tests
}  // namespace V2_0
}  // namespace allocator
}  // namespace graphics
}  // namespace hardware
}  // namespace android

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  int status = RUN_ALL_TESTS();
  ALOGI("Test result = %d", status);

  return status;
}
