/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/IOBufQueue.h>
#include <proxygen/lib/http/codec/HQFramer.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <quic/codec/QuicInteger.h>

// Writes out the common frame header without checks
size_t writeFrameHeaderManual(folly::IOBufQueue& queue,
                              uint64_t decodedType,
                              uint64_t decodedLength) {
  folly::io::QueueAppender appender(&queue, proxygen::hq::kMaxFrameHeaderSize);
  auto typeRes = quic::encodeQuicInteger(decodedType, appender);
  CHECK(typeRes.hasValue());
  auto lengthRes = quic::encodeQuicInteger(decodedLength, appender);
  CHECK(lengthRes.hasValue());
  return *typeRes + *lengthRes;
}

// Write a valid frame for each frame type
void writeValidFrame(folly::IOBufQueue& queue, proxygen::hq::FrameType type) {
  switch (type) {
    case proxygen::hq::FrameType::PRIORITY:
      proxygen::hq::writePriority(
          queue,
          {
              proxygen::hq::PriorityElementType::REQUEST_STREAM,
              proxygen::hq::PriorityElementType::REQUEST_STREAM,
              true,
              123,
              234,
              30,
          });
      break;
    case proxygen::hq::FrameType::SETTINGS:
      proxygen::hq::writeSettings(
          queue,
          {{proxygen::hq::SettingId::MAX_HEADER_LIST_SIZE,
            proxygen::hq::SettingValue(4)},
           {(proxygen::hq::SettingId)*proxygen::hq::getGreaseId(
                folly::Random::rand32(16)),
            proxygen::hq::SettingValue(0xFACEB00C)}});
      break;
    case proxygen::hq::FrameType::CANCEL_PUSH:
    case proxygen::hq::FrameType::GOAWAY:
    case proxygen::hq::FrameType::MAX_PUSH_ID:
    case proxygen::hq::FrameType::PUSH_PROMISE: {
      // Just a varlength integer ID
      uint64_t id = 123456;
      size_t maybeDataSize = 100;
      auto idSize = quic::getQuicIntegerSize(id);
      writeFrameHeaderManual(
          queue,
          static_cast<uint64_t>(type),
          *idSize + (type == proxygen::hq::FrameType::PUSH_PROMISE
                         ? maybeDataSize
                         : 0));
      folly::io::QueueAppender appender(&queue, *idSize);
      quic::encodeQuicInteger(id, appender);
      if (type == proxygen::hq::FrameType::PUSH_PROMISE) {
        // header data for push-promise
        uint8_t simplePushPromise[] = {0x00, 0x00, 0xC0, 0xC1, 0xD1, 0xD7};
        queue.append(folly::IOBuf::copyBuffer(simplePushPromise, 6));
      }
      break;
    }
    case proxygen::hq::FrameType::HEADERS: {
      uint8_t simpleHeaders[] = {0x00, 0x00, 0xC0, 0xC1, 0xD1, 0xD7};
      auto data = folly::IOBuf::copyBuffer(simpleHeaders, 6);
      writeFrameHeaderManual(
          queue, static_cast<uint64_t>(type), data->computeChainDataLength());
      queue.append(std::move(data));
      break;
    }
    default: {
      // all the other frames (DATA, GREASE_*, ...) just
      // have a binary blob as payload
      auto data = proxygen::makeBuf(500);
      writeFrameHeaderManual(
          queue, static_cast<uint64_t>(type), data->length());
      queue.append(data->clone());
      break;
    }
  }
}