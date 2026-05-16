#include <cstdio>
#include <fstream>
#include <string>
#include "qjsp/parser2.hpp"

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: parse_file <file>\n"); return 2; }

  std::ifstream f(argv[1]);
  if (!f) { fprintf(stderr, "IO_ERROR\n"); return 2; }
  std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

  qjsp::Parser p;
  p.init(reinterpret_cast<const uint8_t*>(src.data()), static_cast<uint32_t>(src.size()));
  p.parse();

  if (p.tree().errors.empty()) return 0;

  for (auto &e : p.tree().errors) {
    fprintf(stdout, "%s\n", e.message.c_str());
  }
  return 1;
}
