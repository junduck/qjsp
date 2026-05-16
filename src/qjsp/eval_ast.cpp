#include "qjsp/ast_emit.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/parser2.hpp"
#include "qjsp/reg_interpreter.hpp"
#include <cstring>
#include <memory>

namespace qjsp {

Value RegInterpreter::eval_source_ast(const char *source, const char *filename) {
  auto len = static_cast<uint32_t>(std::strlen(source));
  auto buf = std::make_unique<uint8_t[]>(len + 4);
  std::memcpy(buf.get(), source, len + 1);
  buf[len + 1] = 0;
  buf[len + 2] = 0;
  buf[len + 3] = 0;

  Parser parser;
  parser.init(buf.get(), len);
  NodeIndex root = parser.parse();

  if (root == NodeNull)
    return Value::exception();

  auto &tree = parser.tree();
  AstEmitter emitter(e_, tree, buf.get(), len);
  Bytecode *b = emitter.emit_program(root);

  if (!b)
    return Value::exception();

  Value result = eval(b);
  delete b;
  return result;
}

} // namespace qjsp
