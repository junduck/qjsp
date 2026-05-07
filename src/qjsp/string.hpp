#pragma once

#include "gc.hpp"
#include "value.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace qjsp {

struct String;

constexpr uint32_t kStringLenMax = (1u << 24) - 1; // 24-bit length in meta

struct String : RefCounted {
  // meta: high 8 bits = atom_type, low 24 bits = length
  uint32_t meta = 0;
  const char *data = nullptr; // points to trailing allocation

  static String *create(std::string_view src);
  static int compare(const String *a, const String *b);

  uint32_t len() const { return meta & 0x00FFFFFFu; }
  uint8_t atom() const { return static_cast<uint8_t>(meta >> 24); }
  void set_atom(uint8_t t) { meta = (meta & 0x00FFFFFFu) | (static_cast<uint32_t>(t) << 24); }
  void set_len(uint32_t l) { meta = (l & 0x00FFFFFFu) | (meta & 0xFF000000u); }

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
  buf[0] = 0;
  return buf;
}

} // namespace qjsp
