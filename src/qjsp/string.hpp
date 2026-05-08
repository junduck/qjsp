#pragma once

#include "gc.hpp"
#include "value.hpp"
#include <cstdint>
#include <cstring>
#include <string_view>

namespace qjsp {

struct String;

constexpr uint32_t kStringLenMax = (1u << 31) - 1;

struct String : RefCounted {
  uint32_t meta    = 0;
  char const *data = nullptr; // points to trailing allocation

  static String *create(std::string_view src);
  static int compare(const String *a, const String *b);

  uint32_t len() const { return meta & 0x7FFFFFFFu; }
  bool is_interned() const { return meta >> 31; }
  void set_interned() { meta |= (1u << 31); }

  std::string_view view() const { return {data, len()}; }
  char operator[](size_t i) const { return data[i]; }
};

inline char *string_to_cstr(const String *s) {
  auto *buf = new char[s->len() + 1];
  std::memcpy(buf, s->data, s->len());
  buf[s->len()] = 0;
  return buf;
}

inline char *value_to_cstr(Value v) {
  if (v.is_string())
    return string_to_cstr(v.as<String>());
  auto *buf = new char[1];
  buf[0]    = 0;
  return buf;
}

} // namespace qjsp
