#pragma once

#include "qjsp/ast.hpp"

namespace qjsp {

inline const char *node_kind_name(NodeKind k) {
    switch (k) {
    case NK_PROGRAM:        return "PROGRAM";
    case NK_BLOCK_STMT:     return "BLOCK_STMT";
    case NK_EXPR_STMT:      return "EXPR_STMT";
    case NK_IF_STMT:        return "IF_STMT";
    case NK_SWITCH_STMT:    return "SWITCH_STMT";
    case NK_SWITCH_CASE:    return "SWITCH_CASE";
    case NK_FOR_STMT:       return "FOR_STMT";
    case NK_FOR_IN_STMT:    return "FOR_IN_STMT";
    case NK_FOR_OF_STMT:    return "FOR_OF_STMT";
    case NK_WHILE_STMT:     return "WHILE_STMT";
    case NK_DO_WHILE_STMT:  return "DO_WHILE_STMT";
    case NK_BREAK_STMT:     return "BREAK_STMT";
    case NK_CONTINUE_STMT:  return "CONTINUE_STMT";
    case NK_LABELED_STMT:   return "LABELED_STMT";
    case NK_RETURN_STMT:    return "RETURN_STMT";
    case NK_THROW_STMT:     return "THROW_STMT";
    case NK_TRY_STMT:       return "TRY_STMT";
    case NK_CATCH_CLAUSE:   return "CATCH_CLAUSE";
    case NK_WITH_STMT:      return "WITH_STMT";
    case NK_DEBUGGER_STMT:  return "DEBUGGER_STMT";
    case NK_EMPTY_STMT:     return "EMPTY_STMT";
    case NK_VAR_DECL:       return "VAR_DECL";
    case NK_VAR_DECLARATOR: return "VAR_DECLARATOR";
    case NK_FUNCTION:       return "FUNCTION";
    case NK_FUNCTION_BODY:  return "FUNCTION_BODY";
    case NK_FORMAL_PARAMS:  return "FORMAL_PARAMS";
    case NK_FORMAL_PARAM:   return "FORMAL_PARAM";
    case NK_ARROW_FUNCTION: return "ARROW_FUNCTION";
    case NK_BINARY_EXPR:    return "BINARY_EXPR";
    case NK_LOGICAL_EXPR:   return "LOGICAL_EXPR";
    case NK_CONDITIONAL_EXPR: return "CONDITIONAL_EXPR";
    case NK_UNARY_EXPR:     return "UNARY_EXPR";
    case NK_UPDATE_EXPR:    return "UPDATE_EXPR";
    case NK_ASSIGNMENT_EXPR: return "ASSIGNMENT_EXPR";
    case NK_SEQUENCE_EXPR:  return "SEQUENCE_EXPR";
    case NK_MEMBER_EXPR:    return "MEMBER_EXPR";
    case NK_CALL_EXPR:      return "CALL_EXPR";
    case NK_CHAIN_EXPR:     return "CHAIN_EXPR";
    case NK_NEW_EXPR:       return "NEW_EXPR";
    case NK_TAGGED_TEMPLATE: return "TAGGED_TEMPLATE";
    case NK_AWAIT_EXPR:     return "AWAIT_EXPR";
    case NK_YIELD_EXPR:     return "YIELD_EXPR";
    case NK_META_PROPERTY:  return "META_PROPERTY";
    case NK_SUPER:          return "SUPER";
    case NK_THIS_EXPR:      return "THIS_EXPR";
    case NK_NULL_LIT:       return "NULL_LIT";
    case NK_BOOL_LIT:       return "BOOL_LIT";
    case NK_STRING_LIT:     return "STRING_LIT";
    case NK_NUMERIC_LIT:    return "NUMERIC_LIT";
    case NK_BIGINT_LIT:     return "BIGINT_LIT";
    case NK_REGEXP_LIT:     return "REGEXP_LIT";
    case NK_TEMPLATE_LIT:   return "TEMPLATE_LIT";
    case NK_TEMPLATE_ELEM:  return "TEMPLATE_ELEM";
    case NK_IDENT_REF:      return "IDENT_REF";
    case NK_BINDING_IDENT:  return "BINDING_IDENT";
    case NK_LABEL_IDENT:    return "LABEL_IDENT";
    case NK_PRIVATE_IDENT:  return "PRIVATE_IDENT";
    case NK_ASSIGNMENT_PAT: return "ASSIGNMENT_PAT";
    case NK_BINDING_REST:   return "BINDING_REST";
    case NK_ARRAY_PAT:      return "ARRAY_PAT";
    case NK_OBJECT_PAT:     return "OBJECT_PAT";
    case NK_BINDING_PROP:   return "BINDING_PROP";
    case NK_ARRAY_EXPR:     return "ARRAY_EXPR";
    case NK_OBJECT_EXPR:    return "OBJECT_EXPR";
    case NK_SPREAD:         return "SPREAD";
    case NK_OBJECT_PROP:    return "OBJECT_PROP";
    case NK_CLASS:          return "CLASS";
    case NK_CLASS_BODY:     return "CLASS_BODY";
    case NK_METHOD_DEF:     return "METHOD_DEF";
    case NK_PROPERTY_DEF:   return "PROPERTY_DEF";
    case NK_STATIC_BLOCK:   return "STATIC_BLOCK";
    case NK_IMPORT_EXPR:    return "IMPORT_EXPR";
    case NK_IMPORT_DECL:    return "IMPORT_DECL";
    case NK_IMPORT_SPEC:    return "IMPORT_SPEC";
    case NK_IMPORT_DEFAULT: return "IMPORT_DEFAULT";
    case NK_IMPORT_NAMESPACE: return "IMPORT_NAMESPACE";
    case NK_EXPORT_NAMED:   return "EXPORT_NAMED";
    case NK_EXPORT_DECL:    return "EXPORT_DECL";
    case NK_EXPORT_DEFAULT: return "EXPORT_DEFAULT";
    case NK_EXPORT_ALL:     return "EXPORT_ALL";
    case NK_EXPORT_SPEC:    return "EXPORT_SPEC";
    case NK_DIRECTIVE:      return "DIRECTIVE";
    case NK_PAREN_EXPR:     return "PAREN_EXPR";
    default:                return "UNKNOWN_NODE";
    }
}

} // namespace qjsp
