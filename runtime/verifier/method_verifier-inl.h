/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ART_RUNTIME_VERIFIER_METHOD_VERIFIER_INL_H_
#define ART_RUNTIME_VERIFIER_METHOD_VERIFIER_INL_H_

#include "base/logging.h"
#include "method_verifier.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache.h"

namespace art {
namespace verifier {

const DexFile::CodeItem* MethodVerifier::CodeItem() const {
  return code_item_;
}

RegisterLine* MethodVerifier::GetRegLine(uint32_t dex_pc) {
  return reg_table_.GetLine(dex_pc);
}

const InstructionFlags& MethodVerifier::GetInstructionFlags(size_t index) const {
  return insn_flags_[index];
}

mirror::ClassLoader* MethodVerifier::GetClassLoader() {
  return class_loader_->get();
}

mirror::DexCache* MethodVerifier::GetDexCache() {
  return dex_cache_->get();
}

MethodReference MethodVerifier::GetMethodReference() const {
  return MethodReference(dex_file_, dex_method_idx_);
}

uint32_t MethodVerifier::GetAccessFlags() const {
  return method_access_flags_;
}

bool MethodVerifier::HasCheckCasts() const {
  return has_check_casts_;
}

bool MethodVerifier::HasVirtualOrInterfaceInvokes() const {
  return has_virtual_or_interface_invokes_;
}

bool MethodVerifier::HasFailures() const {
  return !failure_messages_.empty();
}

const RegType& MethodVerifier::ResolveCheckedClass(uint32_t class_idx) {
  DCHECK(!HasFailures());
  const RegType& result = ResolveClassAndCheckAccess(class_idx);
  DCHECK(!HasFailures());
  return result;
}

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_METHOD_VERIFIER_INL_H_
