/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef ART_RUNTIME_VERIFIER_REG_TYPE_CACHE_H_
#define ART_RUNTIME_VERIFIER_REG_TYPE_CACHE_H_

#include "base/casts.h"
#include "base/macros.h"
#include "base/stl_util.h"
#include "reg_type.h"
#include "root_visitor.h"
#include "runtime.h"

#include <stdint.h>
#include <vector>

namespace art {
namespace mirror {
class Class;
class ClassLoader;
}  // namespace mirror
namespace verifier {

class RegType;

class RegTypeCache {
 public:
  explicit RegTypeCache(bool can_load_classes) : can_load_classes_(can_load_classes) {
    entries_.reserve(64);
    FillPrimitiveAndSmallConstantTypes();
  }
  ~RegTypeCache();
  static void Init() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if (!RegTypeCache::primitive_initialized_) {
      CHECK_EQ(RegTypeCache::primitive_count_, 0);
      CreatePrimitiveAndSmallConstantTypes();
      CHECK_EQ(RegTypeCache::primitive_count_, kNumPrimitivesAndSmallConstants);
      RegTypeCache::primitive_initialized_ = true;
    }
  }
  static void ShutDown();
  const art::verifier::RegType& GetFromId(uint16_t id) const;
  const RegType& From(mirror::ClassLoader* loader, const char* descriptor, bool precise)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const RegType& FromClass(const char* descriptor, mirror::Class* klass, bool precise)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const ConstantType& FromCat1Const(int32_t value, bool precise)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const ConstantType& FromCat2ConstLo(int32_t value, bool precise)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const ConstantType& FromCat2ConstHi(int32_t value, bool precise)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const RegType& FromDescriptor(mirror::ClassLoader* loader, const char* descriptor, bool precise)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const RegType& FromUnresolvedMerge(const RegType& left, const RegType& right)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const RegType& FromUnresolvedSuperClass(const RegType& child)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const RegType& JavaLangString() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    // String is final and therefore always precise.
    return From(NULL, "Ljava/lang/String;", true);
  }
  const RegType& JavaLangThrowable(bool precise)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return From(NULL, "Ljava/lang/Throwable;", precise);
  }
  const RegType& Zero() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return FromCat1Const(0, true);
  }
  size_t GetCacheSize() {
    return entries_.size();
  }
  const RegType& Boolean() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return *BooleanType::GetInstance();
  }
  const RegType& Byte() {
    return *ByteType::GetInstance();
  }
  const RegType& Char()  {
    return *CharType::GetInstance();
  }
  const RegType& Short()  {
    return *ShortType::GetInstance();
  }
  const RegType& Integer() {
    return *IntegerType::GetInstance();
  }
  const RegType& Float() {
    return *FloatType::GetInstance();
  }
  const RegType& LongLo() {
    return *LongLoType::GetInstance();
  }
  const RegType& LongHi() {
    return *LongHiType::GetInstance();
  }
  const RegType& DoubleLo() {
    return *DoubleLoType::GetInstance();
  }
  const RegType& DoubleHi() {
    return *DoubleHiType::GetInstance();
  }
  const RegType& Undefined() {
    return *UndefinedType::GetInstance();
  }
  const RegType& Conflict() {
    return *ConflictType::GetInstance();
  }
  const RegType& JavaLangClass(bool precise) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return From(NULL, "Ljava/lang/Class;", precise);
  }
  const RegType& JavaLangObject(bool precise) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    return From(NULL, "Ljava/lang/Object;", precise);
  }
  const UninitializedType& Uninitialized(const RegType& type, uint32_t allocation_pc)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  // Create an uninitialized 'this' argument for the given type.
  const UninitializedType& UninitializedThisArgument(const RegType& type)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const RegType& FromUninitialized(const RegType& uninit_type)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const ImpreciseConstType& ByteConstant() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const ImpreciseConstType& ShortConstant() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const ImpreciseConstType& IntConstant() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const RegType& GetComponentType(const RegType& array, mirror::ClassLoader* loader)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void Dump(std::ostream& os) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const RegType& RegTypeFromPrimitiveType(Primitive::Type) const;

  void VisitRoots(RootVisitor* visitor, void* arg) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

 private:
  void FillPrimitiveAndSmallConstantTypes() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  mirror::Class* ResolveClass(const char* descriptor, mirror::ClassLoader* loader)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void ClearException();
  bool MatchDescriptor(size_t idx, const char* descriptor, bool precise)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  const ConstantType& FromCat1NonSmallConstant(int32_t value, bool precise)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template <class Type>
  static Type* CreatePrimitiveTypeInstance(const std::string& descriptor)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  static void CreatePrimitiveAndSmallConstantTypes() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // The actual storage for the RegTypes.
  std::vector<RegType*> entries_;

  // A quick look up for popular small constants.
  static constexpr int32_t kMinSmallConstant = -1;
  static constexpr int32_t kMaxSmallConstant = 4;
  static PreciseConstType* small_precise_constants_[kMaxSmallConstant - kMinSmallConstant + 1];

  static constexpr size_t kNumPrimitivesAndSmallConstants =
      12 + (kMaxSmallConstant - kMinSmallConstant + 1);

  // Have the well known global primitives been created?
  static bool primitive_initialized_;

  // Number of well known primitives that will be copied into a RegTypeCache upon construction.
  static uint16_t primitive_count_;

  // Whether or not we're allowed to load classes.
  const bool can_load_classes_;

  DISALLOW_COPY_AND_ASSIGN(RegTypeCache);
};

}  // namespace verifier
}  // namespace art

#endif  // ART_RUNTIME_VERIFIER_REG_TYPE_CACHE_H_
