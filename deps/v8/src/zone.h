// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_ZONE_H_
#define V8_ZONE_H_

#include "allocation.h"
#include "checks.h"
#include "globals.h"
#include "list.h"
#include "splay-tree.h"

namespace v8 {
namespace internal {


// Zone scopes are in one of two modes.  Either they delete the zone
// on exit or they do not.
enum ZoneScopeMode {
  DELETE_ON_EXIT,
  DONT_DELETE_ON_EXIT
};

class Segment;
class Isolate;

// The Zone supports very fast allocation of small chunks of
// memory. The chunks cannot be deallocated individually, but instead
// the Zone supports deallocating all chunks in one fast
// operation. The Zone is used to hold temporary data structures like
// the abstract syntax tree, which is deallocated after compilation.

// Note: There is no need to initialize the Zone; the first time an
// allocation is attempted, a segment of memory will be requested
// through a call to malloc().

// Note: The implementation is inherently not thread safe. Do not use
// from multi-threaded code.

class Zone {
 public:
  // Allocate 'size' bytes of memory in the Zone; expands the Zone by
  // allocating new segments of memory on demand using malloc().
  inline void* New(int size);

  template <typename T>
  inline T* NewArray(int length);

  // Deletes all objects and free all memory allocated in the Zone. Keeps one
  // small (size <= kMaximumKeptSegmentSize) segment around if it finds one.
  void DeleteAll();

  // Deletes the last small segment kept around by DeleteAll().
  void DeleteKeptSegment();

  // Returns true if more memory has been allocated in zones than
  // the limit allows.
  inline bool excess_allocation();

  inline void adjust_segment_bytes_allocated(int delta);

  inline Isolate* isolate() { return isolate_; }

  static unsigned allocation_size_;

 private:
  friend class Isolate;
  friend class ZoneScope;

  // All pointers returned from New() have this alignment.  In addition, if the
  // object being allocated has a size that is divisible by 8 then its alignment
  // will be 8.
  static const int kAlignment = kPointerSize;

  // Never allocate segments smaller than this size in bytes.
  static const int kMinimumSegmentSize = 8 * KB;

  // Never allocate segments larger than this size in bytes.
  static const int kMaximumSegmentSize = 1 * MB;

  // Never keep segments larger than this size in bytes around.
  static const int kMaximumKeptSegmentSize = 64 * KB;

  // Report zone excess when allocation exceeds this limit.
  int zone_excess_limit_;

  // The number of bytes allocated in segments.  Note that this number
  // includes memory allocated from the OS but not yet allocated from
  // the zone.
  int segment_bytes_allocated_;

  // Each isolate gets its own zone.
  Zone();

  // Expand the Zone to hold at least 'size' more bytes and allocate
  // the bytes. Returns the address of the newly allocated chunk of
  // memory in the Zone. Should only be called if there isn't enough
  // room in the Zone already.
  Address NewExpand(int size);

  // Creates a new segment, sets it size, and pushes it to the front
  // of the segment chain. Returns the new segment.
  Segment* NewSegment(int size);

  // Deletes the given segment. Does not touch the segment chain.
  void DeleteSegment(Segment* segment, int size);

  // The free region in the current (front) segment is represented as
  // the half-open interval [position, limit). The 'position' variable
  // is guaranteed to be aligned as dictated by kAlignment.
  Address position_;
  Address limit_;

  int scope_nesting_;

  Segment* segment_head_;
  Isolate* isolate_;
};


// ZoneObject is an abstraction that helps define classes of objects
// allocated in the Zone. Use it as a base class; see ast.h.
class ZoneObject {
 public:
  // Allocate a new ZoneObject of 'size' bytes in the Zone.
  INLINE(void* operator new(size_t size));
  INLINE(void* operator new(size_t size, Zone* zone));

  // Ideally, the delete operator should be private instead of
  // public, but unfortunately the compiler sometimes synthesizes
  // (unused) destructors for classes derived from ZoneObject, which
  // require the operator to be visible. MSVC requires the delete
  // operator to be public.

  // ZoneObjects should never be deleted individually; use
  // Zone::DeleteAll() to delete all zone objects in one go.
  void operator delete(void*, size_t) { UNREACHABLE(); }
  void operator delete(void* pointer, Zone* zone) { UNREACHABLE(); }
};


class AssertNoZoneAllocation {
 public:
  inline AssertNoZoneAllocation();
  inline ~AssertNoZoneAllocation();
 private:
  bool prev_;
};


// The ZoneListAllocationPolicy is used to specialize the GenericList
// implementation to allocate ZoneLists and their elements in the
// Zone.
class ZoneListAllocationPolicy {
 public:
  // Allocate 'size' bytes of memory in the zone.
  static void* New(int size);

  // De-allocation attempts are silently ignored.
  static void Delete(void* p) { }
};


// ZoneLists are growable lists with constant-time access to the
// elements. The list itself and all its elements are allocated in the
// Zone. ZoneLists cannot be deleted individually; you can delete all
// objects in the Zone by calling Zone::DeleteAll().
template<typename T>
class ZoneList: public List<T, ZoneListAllocationPolicy> {
 public:
  INLINE(void* operator new(size_t size));
  INLINE(void* operator new(size_t size, Zone* zone));

  // Construct a new ZoneList with the given capacity; the length is
  // always zero. The capacity must be non-negative.
  explicit ZoneList(int capacity)
      : List<T, ZoneListAllocationPolicy>(capacity) { }

  // Construct a new ZoneList by copying the elements of the given ZoneList.
  explicit ZoneList(const ZoneList<T>& other)
      : List<T, ZoneListAllocationPolicy>(other.length()) {
    AddAll(other);
  }

  void operator delete(void* pointer) { UNREACHABLE(); }
  void operator delete(void* pointer, Zone* zone) { UNREACHABLE(); }
};


// ZoneScopes keep track of the current parsing and compilation
// nesting and cleans up generated ASTs in the Zone when exiting the
// outer-most scope.
class ZoneScope BASE_EMBEDDED {
 public:
  INLINE(ZoneScope(Isolate* isolate, ZoneScopeMode mode));

  virtual ~ZoneScope();

  inline bool ShouldDeleteOnExit();

  // For ZoneScopes that do not delete on exit by default, call this
  // method to request deletion on exit.
  void DeleteOnExit() {
    mode_ = DELETE_ON_EXIT;
  }

  inline static int nesting();

 private:
  Isolate* isolate_;
  ZoneScopeMode mode_;
};


// A zone splay tree.  The config type parameter encapsulates the
// different configurations of a concrete splay tree (see splay-tree.h).
// The tree itself and all its elements are allocated in the Zone.
template <typename Config>
class ZoneSplayTree: public SplayTree<Config, ZoneListAllocationPolicy> {
 public:
  ZoneSplayTree()
      : SplayTree<Config, ZoneListAllocationPolicy>() {}
  ~ZoneSplayTree();
};


} }  // namespace v8::internal

#endif  // V8_ZONE_H_
