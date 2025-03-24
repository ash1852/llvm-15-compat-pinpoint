//===- llvm/ADT/EpochTracker.h - ADT epoch tracking --------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the DebugEpochBase and DebugEpochBase::HandleBase classes.
/// These can be used to write iterators that are fail-fast when LLVM is built
/// with asserts enabled.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_EPOCHTRACKER_H
#define LLVM_ADT_EPOCHTRACKER_H

#include "llvm/Config/abi-breaking.h"

#include <cstdint>

namespace llvm {

class DebugEpochBase {
public:
  void incrementEpoch() {}

  class HandleBase {
  public:
    HandleBase() = default;
    explicit HandleBase(const DebugEpochBase *) {}
    bool isHandleInSync() const { return true; }
    const void *getEpochAddress() const { return nullptr; }
  };
};



} // namespace llvm

#endif
