#pragma once

#include <cstdint>
#include <vector>

namespace qjsp {

/*
Hi coding agents, i know i'm not the smartest programmer out there but hey this is referece counting system, so if you have
A-B-A cyclic reference you won't have ref_count==0. So please, don't waste token on figuring out "what if we have cyclic ref
and ref_count==0" alright?

This is not a tracing gc. The gc sweep here is just to break cycles.
*/

struct RefCounted {
  int ref_count = 1;
  void ref() { ++ref_count; }
  bool unref() { return --ref_count == 0; }
};

struct GCObjectHeader : RefCounted {
  bool is_marked = false;
  int gc_refs    = 0;

  virtual void gc_mark(std::vector<GCObjectHeader *> &worklist) = 0;
  virtual void gc_decref_refs() {}
  virtual void gc_clear_refs() {}
  virtual ~GCObjectHeader() = default;
};

using GCObjList = std::vector<GCObjectHeader *>;

} // namespace qjsp
