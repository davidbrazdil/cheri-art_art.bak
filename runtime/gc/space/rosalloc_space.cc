
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

#include "rosalloc_space.h"

#include "rosalloc_space-inl.h"
#include "gc/accounting/card_table.h"
#include "gc/heap.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "runtime.h"
#include "thread.h"
#include "thread_list.h"
#include "utils.h"

#include <valgrind.h>
#include <memcheck/memcheck.h>

namespace art {
namespace gc {
namespace space {

static const bool kPrefetchDuringRosAllocFreeList = true;

RosAllocSpace::RosAllocSpace(const std::string& name, MemMap* mem_map,
                             art::gc::allocator::RosAlloc* rosalloc, byte* begin, byte* end,
                             byte* limit, size_t growth_limit)
    : MallocSpace(name, mem_map, begin, end, limit, growth_limit), rosalloc_(rosalloc),
      rosalloc_for_alloc_(rosalloc) {
  CHECK(rosalloc != NULL);
}

RosAllocSpace* RosAllocSpace::Create(const std::string& name, size_t initial_size, size_t growth_limit,
                                     size_t capacity, byte* requested_begin, bool low_memory_mode) {
  uint64_t start_time = 0;
  if (VLOG_IS_ON(heap) || VLOG_IS_ON(startup)) {
    start_time = NanoTime();
    VLOG(startup) << "RosAllocSpace::Create entering " << name
                  << " initial_size=" << PrettySize(initial_size)
                  << " growth_limit=" << PrettySize(growth_limit)
                  << " capacity=" << PrettySize(capacity)
                  << " requested_begin=" << reinterpret_cast<void*>(requested_begin);
  }

  // Memory we promise to rosalloc before it asks for morecore.
  // Note: making this value large means that large allocations are unlikely to succeed as rosalloc
  // will ask for this memory from sys_alloc which will fail as the footprint (this value plus the
  // size of the large allocation) will be greater than the footprint limit.
  size_t starting_size = kPageSize;
  MemMap* mem_map = CreateMemMap(name, starting_size, &initial_size, &growth_limit, &capacity,
                                 requested_begin);
  if (mem_map == NULL) {
    LOG(ERROR) << "Failed to create mem map for alloc space (" << name << ") of size "
               << PrettySize(capacity);
    return NULL;
  }
  allocator::RosAlloc* rosalloc = CreateRosAlloc(mem_map->Begin(), starting_size, initial_size,
                                                 low_memory_mode);
  if (rosalloc == NULL) {
    LOG(ERROR) << "Failed to initialize rosalloc for alloc space (" << name << ")";
    return NULL;
  }

  // Protect memory beyond the initial size.
  byte* end = mem_map->Begin() + starting_size;
  if (capacity - initial_size > 0) {
    CHECK_MEMORY_CALL(mprotect, (end, capacity - initial_size, PROT_NONE), name);
  }

  // Everything is set so record in immutable structure and leave
  RosAllocSpace* space;
  byte* begin = mem_map->Begin();
  if (RUNNING_ON_VALGRIND > 0) {
    space = new ValgrindMallocSpace<RosAllocSpace, art::gc::allocator::RosAlloc*>(
        name, mem_map, rosalloc, begin, end, begin + capacity, growth_limit, initial_size);
  } else {
    space = new RosAllocSpace(name, mem_map, rosalloc, begin, end, begin + capacity, growth_limit);
  }
  // We start out with only the initial size possibly containing objects.
  if (VLOG_IS_ON(heap) || VLOG_IS_ON(startup)) {
    LOG(INFO) << "RosAllocSpace::Create exiting (" << PrettyDuration(NanoTime() - start_time)
        << " ) " << *space;
  }
  return space;
}

allocator::RosAlloc* RosAllocSpace::CreateRosAlloc(void* begin, size_t morecore_start, size_t initial_size,
                                                   bool low_memory_mode) {
  // clear errno to allow PLOG on error
  errno = 0;
  // create rosalloc using our backing storage starting at begin and
  // with a footprint of morecore_start. When morecore_start bytes of
  // memory is exhaused morecore will be called.
  allocator::RosAlloc* rosalloc = new art::gc::allocator::RosAlloc(
      begin, morecore_start,
      low_memory_mode ?
          art::gc::allocator::RosAlloc::kPageReleaseModeAll :
          art::gc::allocator::RosAlloc::kPageReleaseModeSizeAndEnd);
  if (rosalloc != NULL) {
    rosalloc->SetFootprintLimit(initial_size);
  } else {
    PLOG(ERROR) << "RosAlloc::Create failed";
    }
  return rosalloc;
}

mirror::Object* RosAllocSpace::Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) {
  return AllocNonvirtual(self, num_bytes, bytes_allocated);
}

mirror::Object* RosAllocSpace::AllocWithGrowth(Thread* self, size_t num_bytes, size_t* bytes_allocated) {
  mirror::Object* result;
  {
    MutexLock mu(self, lock_);
    // Grow as much as possible within the space.
    size_t max_allowed = Capacity();
    rosalloc_->SetFootprintLimit(max_allowed);
    // Try the allocation.
    result = AllocWithoutGrowthLocked(self, num_bytes, bytes_allocated);
    // Shrink back down as small as possible.
    size_t footprint = rosalloc_->Footprint();
    rosalloc_->SetFootprintLimit(footprint);
  }
  // Note RosAlloc zeroes memory internally.
  // Return the new allocation or NULL.
  CHECK(!kDebugSpaces || result == NULL || Contains(result));
  return result;
}

MallocSpace* RosAllocSpace::CreateInstance(const std::string& name, MemMap* mem_map, void* allocator,
                                           byte* begin, byte* end, byte* limit, size_t growth_limit) {
  return new RosAllocSpace(name, mem_map, reinterpret_cast<allocator::RosAlloc*>(allocator),
                           begin, end, limit, growth_limit);
}

size_t RosAllocSpace::Free(Thread* self, mirror::Object* ptr) {
  if (kDebugSpaces) {
    CHECK(ptr != NULL);
    CHECK(Contains(ptr)) << "Free (" << ptr << ") not in bounds of heap " << *this;
  }
  const size_t bytes_freed = AllocationSizeNonvirtual(ptr);
  if (kRecentFreeCount > 0) {
    MutexLock mu(self, lock_);
    RegisterRecentFree(ptr);
  }
  rosalloc_->Free(self, ptr);
  return bytes_freed;
}

size_t RosAllocSpace::FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) {
  DCHECK(ptrs != NULL);

  // Don't need the lock to calculate the size of the freed pointers.
  size_t bytes_freed = 0;
  for (size_t i = 0; i < num_ptrs; i++) {
    mirror::Object* ptr = ptrs[i];
    const size_t look_ahead = 8;
    if (kPrefetchDuringRosAllocFreeList && i + look_ahead < num_ptrs) {
      __builtin_prefetch(reinterpret_cast<char*>(ptrs[i + look_ahead]));
    }
    bytes_freed += AllocationSizeNonvirtual(ptr);
  }

  if (kRecentFreeCount > 0) {
    MutexLock mu(self, lock_);
    for (size_t i = 0; i < num_ptrs; i++) {
      RegisterRecentFree(ptrs[i]);
    }
  }

  if (kDebugSpaces) {
    size_t num_broken_ptrs = 0;
    for (size_t i = 0; i < num_ptrs; i++) {
      if (!Contains(ptrs[i])) {
        num_broken_ptrs++;
        LOG(ERROR) << "FreeList[" << i << "] (" << ptrs[i] << ") not in bounds of heap " << *this;
      } else {
        size_t size = rosalloc_->UsableSize(ptrs[i]);
        memset(ptrs[i], 0xEF, size);
      }
    }
    CHECK_EQ(num_broken_ptrs, 0u);
  }

  rosalloc_->BulkFree(self, reinterpret_cast<void**>(ptrs), num_ptrs);
  return bytes_freed;
}

// Callback from rosalloc when it needs to increase the footprint
extern "C" void* art_heap_rosalloc_morecore(allocator::RosAlloc* rosalloc, intptr_t increment) {
  Heap* heap = Runtime::Current()->GetHeap();
  DCHECK(heap->GetNonMovingSpace()->IsRosAllocSpace());
  DCHECK_EQ(heap->GetNonMovingSpace()->AsRosAllocSpace()->GetRosAlloc(), rosalloc);
  return heap->GetNonMovingSpace()->MoreCore(increment);
}

size_t RosAllocSpace::AllocationSize(const mirror::Object* obj) {
  return AllocationSizeNonvirtual(obj);
}

size_t RosAllocSpace::Trim() {
  {
    MutexLock mu(Thread::Current(), lock_);
    // Trim to release memory at the end of the space.
    rosalloc_->Trim();
  }
  // Attempt to release pages if it does not release all empty pages.
  if (!rosalloc_->DoesReleaseAllPages()) {
    VLOG(heap) << "RosAllocSpace::Trim() ";
    size_t reclaimed = 0;
    InspectAllRosAlloc(DlmallocMadviseCallback, &reclaimed);
    return reclaimed;
  }
  return 0;
}

void RosAllocSpace::Walk(void(*callback)(void *start, void *end, size_t num_bytes, void* callback_arg),
                         void* arg) {
  InspectAllRosAlloc(callback, arg);
  callback(NULL, NULL, 0, arg);  // Indicate end of a space.
}

size_t RosAllocSpace::GetFootprint() {
  MutexLock mu(Thread::Current(), lock_);
  return rosalloc_->Footprint();
}

size_t RosAllocSpace::GetFootprintLimit() {
  MutexLock mu(Thread::Current(), lock_);
  return rosalloc_->FootprintLimit();
}

void RosAllocSpace::SetFootprintLimit(size_t new_size) {
  MutexLock mu(Thread::Current(), lock_);
  VLOG(heap) << "RosAllocSpace::SetFootprintLimit " << PrettySize(new_size);
  // Compare against the actual footprint, rather than the Size(), because the heap may not have
  // grown all the way to the allowed size yet.
  size_t current_space_size = rosalloc_->Footprint();
  if (new_size < current_space_size) {
    // Don't let the space grow any more.
    new_size = current_space_size;
  }
  rosalloc_->SetFootprintLimit(new_size);
}

uint64_t RosAllocSpace::GetBytesAllocated() {
  size_t bytes_allocated = 0;
  InspectAllRosAlloc(art::gc::allocator::RosAlloc::BytesAllocatedCallback, &bytes_allocated);
  return bytes_allocated;
}

uint64_t RosAllocSpace::GetObjectsAllocated() {
  size_t objects_allocated = 0;
  InspectAllRosAlloc(art::gc::allocator::RosAlloc::ObjectsAllocatedCallback, &objects_allocated);
  return objects_allocated;
}

void RosAllocSpace::InspectAllRosAlloc(void (*callback)(void *start, void *end, size_t num_bytes, void* callback_arg),
                                       void* arg) NO_THREAD_SAFETY_ANALYSIS {
  // TODO: NO_THREAD_SAFETY_ANALYSIS.
  Thread* self = Thread::Current();
  if (Locks::mutator_lock_->IsExclusiveHeld(self)) {
    // The mutators are already suspended. For example, a call path
    // from SignalCatcher::HandleSigQuit().
    rosalloc_->InspectAll(callback, arg);
  } else {
    // The mutators are not suspended yet.
    DCHECK(!Locks::mutator_lock_->IsSharedHeld(self));
    ThreadList* tl = Runtime::Current()->GetThreadList();
    tl->SuspendAll();
    {
      MutexLock mu(self, *Locks::runtime_shutdown_lock_);
      MutexLock mu2(self, *Locks::thread_list_lock_);
      rosalloc_->InspectAll(callback, arg);
    }
    tl->ResumeAll();
  }
}

void RosAllocSpace::RevokeThreadLocalBuffers(Thread* thread) {
  rosalloc_->RevokeThreadLocalRuns(thread);
}

void RosAllocSpace::RevokeAllThreadLocalBuffers() {
  rosalloc_->RevokeAllThreadLocalRuns();
}

}  // namespace space
}  // namespace gc
}  // namespace art
