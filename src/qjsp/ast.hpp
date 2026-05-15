#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace qjsp {

using NodeIndex = uint32_t;
static constexpr NodeIndex NodeNull = UINT32_MAX;

struct IndexRange {
    uint32_t start = 0;
    uint32_t len = 0;
};

struct Span {
    uint32_t start = 0;
    uint32_t end = 0;
};

enum BinOp : uint32_t {
    BinEq, BinNeq, BinSeq, BinSneq,
    BinLt, BinGt, BinLte, BinGte,
    BinInstanceof, BinIn,
    BinShl, BinSar, BinShr,
    BinAdd, BinSub,
    BinMul, BinDiv, BinMod, BinPow,
    BinBand, BinBor, BinBxor,
};

enum LogOp : uint32_t {
    LogAnd, LogOr, LogNullish,
};

enum AsgnOp : uint32_t {
    AsgnAssign,
    AsgnAdd, AsgnSub, AsgnMul, AsgnDiv, AsgnMod, AsgnPow,
    AsgnBand, AsgnBor, AsgnBxor, AsgnShl, AsgnSar, AsgnShr,
    AsgnNullish, AsgnLand, AsgnLor,
};

enum UnOp : uint32_t {
    UnMinus, UnPlus, UnBang, UnTilde,
    UnTypeof, UnVoid, UnDelete,
};

enum UpdOp : uint32_t {
    UpdInc, UpdDec,
};

enum NodeKind : uint32_t {
    NK_PROGRAM,
    NK_BLOCK_STMT,
    NK_EXPR_STMT,
    NK_IF_STMT,
    NK_SWITCH_STMT,
    NK_SWITCH_CASE,
    NK_FOR_STMT,
    NK_FOR_IN_STMT,
    NK_FOR_OF_STMT,
    NK_WHILE_STMT,
    NK_DO_WHILE_STMT,
    NK_BREAK_STMT,
    NK_CONTINUE_STMT,
    NK_LABELED_STMT,
    NK_RETURN_STMT,
    NK_THROW_STMT,
    NK_TRY_STMT,
    NK_CATCH_CLAUSE,
    NK_WITH_STMT,
    NK_DEBUGGER_STMT,
    NK_EMPTY_STMT,
    NK_VAR_DECL,
    NK_VAR_DECLARATOR,
    NK_FUNCTION,
    NK_FUNCTION_BODY,
    NK_FORMAL_PARAMS,
    NK_FORMAL_PARAM,
    NK_ARROW_FUNCTION,
    NK_BINARY_EXPR,
    NK_LOGICAL_EXPR,
    NK_CONDITIONAL_EXPR,
    NK_UNARY_EXPR,
    NK_UPDATE_EXPR,
    NK_ASSIGNMENT_EXPR,
    NK_SEQUENCE_EXPR,
    NK_MEMBER_EXPR,
    NK_CALL_EXPR,
    NK_CHAIN_EXPR,
    NK_NEW_EXPR,
    NK_TAGGED_TEMPLATE,
    NK_AWAIT_EXPR,
    NK_YIELD_EXPR,
    NK_META_PROPERTY,
    NK_SUPER,
    NK_THIS_EXPR,
    NK_NULL_LIT,
    NK_BOOL_LIT,
    NK_STRING_LIT,
    NK_NUMERIC_LIT,
    NK_BIGINT_LIT,
    NK_REGEXP_LIT,
    NK_TEMPLATE_LIT,
    NK_TEMPLATE_ELEM,
    NK_IDENT_REF,
    NK_BINDING_IDENT,
    NK_LABEL_IDENT,
    NK_PRIVATE_IDENT,
    NK_ASSIGNMENT_PAT,
    NK_BINDING_REST,
    NK_ARRAY_PAT,
    NK_OBJECT_PAT,
    NK_BINDING_PROP,
    NK_ARRAY_EXPR,
    NK_OBJECT_EXPR,
    NK_SPREAD,
    NK_OBJECT_PROP,
    NK_CLASS,
    NK_CLASS_BODY,
    NK_METHOD_DEF,
    NK_PROPERTY_DEF,
    NK_STATIC_BLOCK,
    NK_IMPORT_EXPR,
    NK_IMPORT_DECL,
    NK_IMPORT_SPEC,
    NK_IMPORT_DEFAULT,
    NK_IMPORT_NAMESPACE,
    NK_EXPORT_NAMED,
    NK_EXPORT_DEFAULT,
    NK_EXPORT_ALL,
    NK_EXPORT_SPEC,
    NK_DIRECTIVE,
    NK_PAREN_EXPR,
};

// ─── Node flags (stored in data fields) ──────────────────────────────────────

namespace NF {
    constexpr uint32_t Computed = 1u << 0;
    constexpr uint32_t Optional = 1u << 1;
    constexpr uint32_t Shorthand = 1u << 2;
    constexpr uint32_t Async = 1u << 0;
    constexpr uint32_t Generator = 1u << 1;
    constexpr uint32_t IsExpr = 1u << 2;
    constexpr uint32_t Prefix = 1u << 0;
    constexpr uint32_t Delegate = 1u << 0;
    constexpr uint32_t Tail = 1u << 0;
    constexpr uint32_t Await = 1u << 3;
    constexpr uint32_t Static = 1u << 4;
    constexpr uint32_t ExprBody = 1u << 3;
}

enum VarKind : uint32_t { VarVar, VarLet, VarConst };

enum MethodKind : uint32_t { MethodInit, MethodGet, MethodSet, MethodCtor };

struct Node {
    NodeKind kind;
    Span span;
    uint32_t data[8];
};

struct ParseError {
    Span span;
    std::string message;
};

class AstTree {
public:
    std::vector<Node> nodes;
    std::vector<uint32_t> extras;
    const uint8_t *source = nullptr;
    uint32_t source_len = 0;
    NodeIndex root = NodeNull;
    std::vector<ParseError> errors;

    void init(const uint8_t *src, uint32_t len) {
        source = src;
        source_len = len;
        nodes.reserve(len / 8);
        extras.reserve(len / 16);
    }

    NodeIndex alloc(NodeKind kind, Span sp,
                    uint32_t d0 = NodeNull, uint32_t d1 = NodeNull,
                    uint32_t d2 = 0, uint32_t d3 = 0,
                    uint32_t d4 = 0, uint32_t d5 = 0,
                    uint32_t d6 = 0, uint32_t d7 = 0) {
        auto idx = static_cast<NodeIndex>(nodes.size());
        nodes.push_back({kind, sp, {d0, d1, d2, d3, d4, d5, d6, d7}});
        return idx;
    }

    IndexRange add_extras(const uint32_t *items, uint32_t count) {
        if (count == 0) return {0, 0};
        IndexRange r{static_cast<uint32_t>(extras.size()), count};
        extras.insert(extras.end(), items, items + count);
        return r;
    }

    const Node &node(NodeIndex i) const { return nodes[i]; }
    NodeKind kind(NodeIndex i) const { return nodes[i].kind; }
    Span span(NodeIndex i) const { return nodes[i].span; }
    uint32_t d(NodeIndex i, int f) const { return nodes[i].data[f]; }
    uint32_t &d(NodeIndex i, int f) { return nodes[i].data[f]; }

    IndexRange range(NodeIndex i, int f) const {
        return {nodes[i].data[f], nodes[i].data[f + 1]};
    }

    const uint32_t *extra(IndexRange r) const { return extras.data() + r.start; }
    const uint32_t *extra_data() const { return extras.data(); }

    void error(Span sp, const char *msg) {
        errors.push_back({sp, std::string(msg)});
    }
};

} // namespace qjsp
