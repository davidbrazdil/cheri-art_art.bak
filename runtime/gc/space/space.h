/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_GC_SPACE_SPACE_H_
#define ART_RUNTIME_GC_SPACE_SPACE_H_

#include <string>

#include "UniquePtr.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "gc/accounting/space_bitmap.h"
#include "globals.h"
#include "image.h"
#include "mem_map.h"

namespace art {
namespace mirror {
  class Object;
}  // namespace mirror

namespace gc {

namespace accounting {
  class SpaceBitmap;
}  // namespace accounting

class Heap;

namespace space {

class AllocSpace;
class BumpPointerSpace;
class ContinuousSpace;
class DiscontinuousSpace;
class MallocSpace;
class DlMallocSpace;
class RosAllocSpace;
class ImageSpace;
class LargeObjectSpace;

static constexpr bool kDebugSpaces = kIsDebugBuild;

// See Space::GetGcRetentionPolicy.
enum GcRetentionPolicy {
  // Objects are retained forever with this policy for a space.
  kGcRetentionPolicyNeverCollect,
  // Every GC cycle will attempt to collect objects in this space.
  kGcRetentionPolicyAlwaysCollect,
  // Objects will be considered for collection only in "full" GC cycles, ie faster partial
  // collections won't scan these areas such as the Zygote.
  kGcRetentionPolicyFullCollect,
};
std::ostream& operator<<(std::ostream& os, const GcRetentionPolicy& policy);

enum SpaceType {
  kSpaceTypeImageSpace,
  kSpaceTypeAllocSpace,
  kSpaceTypeZygoteSpace,
  kSpaceTypeBumpPointerSpace,
  kSpaceTypeLargeObjectSpace,
};
std::ostream& operator<<(std::ostream& os, const SpaceType& space_type);

// A space contains memory allocated for managed objects.
class Space {
 public:
  // Dump space. Also key method for C++ vtables.
  virtual void Dump(std::ostream& os) const;

  // Name of the space. May vary, for example before/after the Zygote fork.
  const char* GetName() const {
    return name_.c_str();
  }

  // The policy of when objects are collected associated with this space.
  GcRetentionPolicy GetGcRetentionPolicy() const {
    return gc_retention_policy_;
  }

  // Does the space support allocation?
  virtual bool CanAllocateInto() const {
    return true;
  }

  // Is the given object contained within this space?
  virtual bool Contains(const mirror::Object* obj) const = 0;

  // The kind of space this: image, alloc, zygote, large object.
  virtual SpaceType GetType() const = 0;

  // Is this an image space, ie one backed by a memory mapped image file.
  bool IsImageSpace() const {
    return GetType() == kSpaceTypeImageSpace;
  }
  ImageSpace* AsImageSpace();

  // Is this a dlmalloc backed allocation space?
  bool IsMallocSpace() const {
    SpaceType type = GetType();
    return type == kSpaceTypeAllocSpace || type == kSpaceTypeZygoteSpace;
  }
  MallocSpace* AsMallocSpace();

  virtual bool IsDlMallocSpace() const {
    return false;
  }
  virtual DlMallocSpace* AsDlMallocSpace() {
    LOG(FATAL) << "Unreachable";
    return NULL;
  }
  virtual bool IsRosAllocSpace() const {
    return false;
  }
  virtual RosAllocSpace* AsRosAllocSpace() {
    LOG(FATAL) << "Unreachable";
    return NULL;
  }

  // Is this the space allocated into by the Zygote and no-longer in use?
  bool IsZygoteSpace() const {
    return GetType() == kSpaceTypeZygoteSpace;
  }

  // Is this space a bump pointer space?
  bool IsBumpPointerSpace() const {
    return GetType() == kSpaceTypeBumpPointerSpace;
  }
  virtual BumpPointerSpace* AsBumpPointerSpace() {
    LOG(FATAL) << "Unreachable";
    return NULL;
  }

  // Does this space hold large objects and implement the large object space abstraction?
  bool IsLargeObjectSpace() const {
    return GetType() == kSpaceTypeLargeObjectSpace;
  }
  LargeObjectSpace* AsLargeObjectSpace();

  virtual bool IsContinuousSpace() const {
    return false;
  }
  ContinuousSpace* AsContinuousSpace();

  virtual bool IsDiscontinuousSpace() const {
    return false;
  }
  DiscontinuousSpace* AsDiscontinuousSpace();

  virtual bool IsAllocSpace() const {
    return false;
  }
  virtual AllocSpace* AsAllocSpace() {
    LOG(FATAL) << "Unimplemented";
    return nullptr;
  }

  virtual ~Space() {}

 protected:
  Space(const std::string& name, GcRetentionPolicy gc_retention_policy);

  void SetGcRetentionPolicy(GcRetentionPolicy gc_retention_policy) {
    gc_retention_policy_ = gc_retention_policy;
  }

  // Name of the space that may vary due to the Zygote fork.
  std::string name_;

 protected:
  // When should objects within this space be reclaimed? Not constant as we vary it in the case
  // of Zygote forking.
  GcRetentionPolicy gc_retention_policy_;

 private:
  friend class art::gc::Heap;
  DISALLOW_COPY_AND_ASSIGN(Space);
};
std::ostream& operator<<(std::ostream& os, const Space& space);

// AllocSpace interface.
class AllocSpace {
 public:
  // Number of bytes currently allocated.
  virtual uint64_t GetBytesAllocated() = 0;
  // Number of objects currently allocated.
  virtual uint64_t GetObjectsAllocated() = 0;

  // Allocate num_bytes without allowing growth. If the allocation
  // succeeds, the output parameter bytes_allocated will be set to the
  // actually allocated bytes which is >= num_bytes.
  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) = 0;

  // Return the storage space required by obj.
  virtual size_t AllocationSize(const mirror::Object* obj) = 0;

  // Returns how many bytes were freed.
  virtual size_t Free(Thread* self, mirror::Object* ptr) = 0;

  // Returns how many bytes were freed.
  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) = 0;

  // Revoke any sort of thread-local buffers that are used to speed up
  // allocations for the given thread, if the alloc space
  // implementation uses any. No-op by default.
  virtual void RevokeThreadLocalBuffers(Thread* /*thread*/) {}

  // Revoke any sort of thread-local buffers that are used to speed up
  // allocations for all the threads, if the alloc space
  // implementation uses any. No-op by default.
  virtual void RevokeAllThreadLocalBuffers() {}

 protected:
  AllocSpace() {}
  virtual ~AllocSpace() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(AllocSpace);
};

// Continuous spaces have bitmaps, and an address range. Although not required, objects within
// continuous spaces can be marked in the card table.
class ContinuousSpace : public Space {
 public:
  // Address at which the space begins.
  byte* Begin() const {
    return begin_;
  }

  // Current address at which the space ends, which may vary as the space is filled.
  byte* End() const {
    return end_;
  }

  // The end of the address range covered by the space.
  byte* Limit() const {
    return limit_;
  }

  // Change the end of the space. Be careful with use since changing the end of a space to an
  // invalid value may break the GC.
  void SetEnd(byte* end) {
    end_ = end;
  }

  void SetLimit(byte* limit) {
    limit_ = limit;
  }

  // Current size of space
  size_t Size() const {
    return End() - Begin();
  }

  virtual accounting::SpaceBitmap* GetLiveBitmap() const = 0;
  virtual accounting::SpaceBitmap* GetMarkBitmap() const = 0;

  // Maximum which the mapped space can grow to.
  virtual size_t Capacity() const {
    return Limit() - Begin();
  }

  // Is object within this space? We check to see if the pointer is beyond the end first as
  // continuous spaces are iterated over from low to high.
  bool HasAddress(const mirror::Object* obj) const {
    const byte* byte_ptr = reinterpret_cast<const byte*>(obj);
    return byte_ptr >= Begin() && byte_ptr < Limit();
  }

  bool Contains(const mirror::Object* obj) const {
    return HasAddress(obj);
  }

  virtual bool IsContinuousSpace() const {
    return true;
  }

  virtual ~ContinuousSpace() {}

 protected:
  ContinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy,
                  byte* begin, byte* end, byte* limit) :
      Space(name, gc_retention_policy), begin_(begin), end_(end), limit_(limit) {
  }

  // The beginning of the storage for fast access.
  byte* begin_;

  // Current end of the space.
  byte* volatile end_;

  // Limit of the space.
  byte* limit_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ContinuousSpace);
};

// A space where objects may be allocated higgledy-piggledy throughout virtual memory. Currently
// the card table can't cover these objects and so the write barrier shouldn't be triggered. This
// is suitable for use for large primitive arrays.
class DiscontinuousSpace : public Space {
 public:
  accounting::SpaceSetMap* GetLiveObjects() const {
    return live_objects_.get();
  }

  accounting::SpaceSetMap* GetMarkObjects() const {
    return mark_objects_.get();
  }

  virtual bool IsDiscontinuousSpace() const {
    return true;
  }

  virtual ~DiscontinuousSpace() {}

 protected:
  DiscontinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy);

  UniquePtr<accounting::SpaceSetMap> live_objects_;
  UniquePtr<accounting::SpaceSetMap> mark_objects_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DiscontinuousSpace);
};

class MemMapSpace : public ContinuousSpace {
 public:
  // Size of the space without a limit on its growth. By default this is just the Capacity, but
  // for the allocation space we support starting with a small heap and then extending it.
  virtual size_t NonGrowthLimitCapacity() const {
    return Capacity();
  }

  MemMap* GetMemMap() {
    return mem_map_.get();
  }

  const MemMap* GetMemMap() const {
    return mem_map_.get();
  }

 protected:
  MemMapSpace(const std::string& name, MemMap* mem_map, byte* begin, byte* end, byte* limit,
              GcRetentionPolicy gc_retention_policy)
      : ContinuousSpace(name, gc_retention_policy, begin, end, limit),
        mem_map_(mem_map) {
  }

  // Underlying storage of the space
  UniquePtr<MemMap> mem_map_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MemMapSpace);
};

// Used by the heap compaction interface to enable copying from one type of alloc space to another.
class ContinuousMemMapAllocSpace : public MemMapSpace, public AllocSpace {
 public:
  virtual bool IsAllocSpace() const {
    return true;
  }

  virtual AllocSpace* AsAllocSpace() {
    return this;
  }

  virtual void Clear() {
    LOG(FATAL) << "Unimplemented";
  }

 protected:
  ContinuousMemMapAllocSpace(const std::string& name, MemMap* mem_map, byte* begin,
                             byte* end, byte* limit, GcRetentionPolicy gc_retention_policy)
      : MemMapSpace(name, mem_map, begin, end, limit, gc_retention_policy) {
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ContinuousMemMapAllocSpace);
};

}  // namespace space
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_SPACE_SPACE_H_
