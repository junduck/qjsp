#include "qjsp/ast_emit.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/reg_opcode_info.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cstring>
#include <cstdlib>

namespace qjsp {

// ─── BinOp → RegOp lookup ──────────────────────────────────────────────────

static RegOp binop_to_reg(BinOp bop) {
    static constexpr RegOp kMap[] = {
        RegOp::EQ, RegOp::NEQ, RegOp::SEQ, RegOp::SNEQ,
        RegOp::LT, RegOp::GT, RegOp::LTE, RegOp::GTE,
        RegOp::INSTANCEOF, RegOp::EQ, // BinIn → reuse EQ (not directly a binop, handled specially)
        RegOp::SHL, RegOp::SAR, RegOp::SHR,
        RegOp::ADD, RegOp::SUB,
        RegOp::MUL, RegOp::DIV, RegOp::MOD, RegOp::POW,
        RegOp::AND, RegOp::OR, RegOp::XOR,
    };
    return kMap[static_cast<int>(bop)];
}

static RegOp compound_binop(AsgnOp aop) {
    switch (aop) {
    case AsgnAdd: return RegOp::ADD;
    case AsgnSub: return RegOp::SUB;
    case AsgnMul: return RegOp::MUL;
    case AsgnDiv: return RegOp::DIV;
    case AsgnMod: return RegOp::MOD;
    case AsgnPow: return RegOp::POW;
    case AsgnBand: return RegOp::AND;
    case AsgnBor: return RegOp::OR;
    case AsgnBxor: return RegOp::XOR;
    case AsgnShl: return RegOp::SHL;
    case AsgnSar: return RegOp::SAR;
    case AsgnShr: return RegOp::SHR;
    default: return RegOp::NOP;
    }
}

static uint8_t u8(int r) { return static_cast<uint8_t>(r); }
static uint16_t u16(int r) { return static_cast<uint16_t>(r); }
static int16_t s16(int r) { return static_cast<int16_t>(r); }

// ─── Constructor / destructor ───────────────────────────────────────────────

AstEmitter::AstEmitter(Engine *e, AstTree &tree, const uint8_t *source, uint32_t source_len)
    : e_(e), tree_(tree), source_(source), source_len_(source_len) {}

AstEmitter::~AstEmitter() {
}

// ─── Atom helpers ───────────────────────────────────────────────────────────

Atom AstEmitter::atom_for_span(Span sp) const {
    auto sv = src_slice(sp);
    return e_->intern(sv);
}

Atom AstEmitter::atom_for_str(std::string_view sv) const {
    return e_->intern(sv);
}

// ─── Variable resolution ────────────────────────────────────────────────────
//
// All variable lookups go through bindings_.lookup() or bindings_.lookup_captured().
// The BindingTable is built during scope analysis and sealed before emission.

// ─── Scope management ───────────────────────────────────────────────────────

void AstEmitter::push_scope() {
    scope_level_++;
}

void AstEmitter::pop_scope() {
    scope_level_--;
}

// ─── Break/continue ─────────────────────────────────────────────────────────

int AstEmitter::find_break_label(Atom label, bool is_continue) const {
    for (int i = static_cast<int>(break_stack_.size()); i-- > 0;) {
        auto &bt = break_stack_[i];
        if (label == kAtomNull || bt.label == label) {
            return is_continue ? bt.continue_label : bt.break_label;
        }
    }
    return -1;
}

// ─── Scope analysis ─────────────────────────────────────────────────────────

void AstEmitter::collect_declarator(NodeIndex decl_node, uint32_t kind) {
    Node &dn = tree_.nodes[decl_node];
    NodeIndex id = dn.data[0];

    if (id == NodeNull) return;
    NodeKind k = tree_.kind(id);

    if (k == NK_IDENT_REF || k == NK_BINDING_IDENT) {
        Atom name = atom_for_span(tree_.span(id));
        switch (kind) {
        case VarVar:   bindings_.add_var(name);   break;
        case VarLet:   bindings_.add_let(name);   break;
        case VarConst: bindings_.add_const(name); break;
        }
    }
}

void AstEmitter::collect_vars(NodeIndex node, bool is_var) {
    if (node == NodeNull) return;
    Node &n = tree_.nodes[node];
    NodeKind k = n.kind;

    switch (k) {
    case NK_VAR_DECL: {
        auto range = tree_.range(node, 0);
        auto vk = static_cast<uint32_t>(n.data[2]);
        for (uint32_t i = 0; i < range.len; i++) {
            collect_declarator(tree_.extras[range.start + i], vk);
        }
        break;
    }
    case NK_FUNCTION: {
        NodeIndex id = n.data[0];
        if (id != NodeNull) {
            Atom name = atom_for_span(tree_.span(id));
            bindings_.add_function(name);
        }
        break;
    }
    case NK_PROGRAM:
    case NK_BLOCK_STMT:
    case NK_FUNCTION_BODY: {
        bindings_.begin_scope();
        auto range = tree_.range(node, 0);
        for (uint32_t i = 0; i < range.len; i++)
            collect_vars(tree_.extras[range.start + i], is_var);
        bindings_.end_scope();
        break;
    }
    case NK_IF_STMT: {
        collect_vars(n.data[1], is_var);
        if (n.data[2] != NodeNull) collect_vars(n.data[2], is_var);
        if (n.data[3] != NodeNull) collect_vars(n.data[3], is_var);
        break;
    }
    case NK_FOR_STMT: {
        if (n.data[0] != NodeNull) collect_vars(n.data[0], is_var);
        break;
    }
    case NK_FOR_IN_STMT:
    case NK_FOR_OF_STMT: {
        collect_vars(n.data[0], is_var);
        break;
    }
    case NK_WHILE_STMT:
    case NK_DO_WHILE_STMT: {
        collect_vars(n.data[1], is_var);
        break;
    }
    case NK_LABELED_STMT: {
        if (n.data[1] != NodeNull) collect_vars(n.data[1], is_var);
        break;
    }
    case NK_TRY_STMT: {
        if (n.data[0] != NodeNull) collect_vars(n.data[0], is_var);
        if (n.data[1] != NodeNull) {
            Node &catch_node = tree_.nodes[n.data[1]];
            bindings_.begin_scope();
            if (catch_node.data[0] != NodeNull) {
                collect_vars(catch_node.data[0], is_var);
            }
            if (catch_node.data[1] != NodeNull)
                collect_vars(catch_node.data[1], is_var);
            bindings_.end_scope();
        }
        if (n.data[2] != NodeNull) collect_vars(n.data[2], is_var);
        break;
    }
    case NK_SWITCH_STMT: {
        auto range = tree_.range(node, 0);
        for (uint32_t i = 0; i < range.len; i++)
            collect_vars(tree_.extras[range.start + i], is_var);
        break;
    }
    case NK_SWITCH_CASE: {
        bindings_.begin_scope();
        auto range = tree_.range(node, 0);
        for (uint32_t i = 0; i < range.len; i++)
            collect_vars(tree_.extras[range.start + i], is_var);
        bindings_.end_scope();
        break;
    }
    case NK_EXPR_STMT: {
        break;
    }
    default:
        break;
    }
}

void AstEmitter::analyze_scope(NodeIndex body) {
    collect_vars(body, false);
    bindings_.seal();
}

// ─── Freeze into Bytecode ───────────────────────────────────────────────────

Bytecode *AstEmitter::freeze() {
    patch_labels();

    auto *b = new Bytecode();
    b->ref_count = 1;

    b->instr_count = static_cast<uint32_t>(code_.size());
    b->byte_code_len = b->instr_count * 4;
    b->byte_code_buf = std::make_unique<uint8_t[]>(b->byte_code_len);
    std::memcpy(b->byte_code_buf.get(), code_.data(), b->byte_code_len);

    b->cpool_count = static_cast<uint32_t>(cpool_.size());
    b->cpool = std::make_unique<Value[]>(b->cpool_count);
    for (uint32_t i = 0; i < b->cpool_count; i++)
        b->cpool[i] = std::move(cpool_[i]);

    b->arg_count = static_cast<uint16_t>(bindings_.arg_count());
    b->var_count = 0;
    int total_defs = bindings_.entry_count();
    if (total_defs > 0) {
        b->vardefs = std::make_unique<BytecodeVarDef[]>(total_defs);
        b->var_count = static_cast<uint16_t>(total_defs);
        for (int i = 0; i < total_defs; i++) {
            auto &bd = bindings_.entries()[i];
            auto &vd = b->vardefs[i];
            vd.var_name = bd.name;
            vd.scope_next = -1;
            vd.flags = 0;
            vd.set_is_const(is_const_kind(bd.kind));
            vd.set_is_lexical(is_lexical_kind(bd.kind));
            vd.set_is_captured(false);  // upvalues handled via closure_vars
            vd.var_ref_idx = 0;
        }
    }

    b->closure_var_count = static_cast<uint32_t>(bindings_.closure_vars().size());
    if (b->closure_var_count > 0) {
        b->closure_var = std::make_unique<ClosureVar[]>(b->closure_var_count);
        for (uint32_t i = 0; i < b->closure_var_count; i++)
            b->closure_var[i] = bindings_.closure_vars()[i];
    }

    b->func_name = func_name_;
    b->reg_count = static_cast<uint16_t>(max_temp_);
    b->stack_size = b->reg_count;
    b->var_ref_count = static_cast<uint16_t>(bindings_.upvalue_count());
    b->defined_arg_count = static_cast<uint16_t>(bindings_.arg_count());
    b->flags1 |= 0x01; // has_prototype

    return b;
}

// ─── emit_program ───────────────────────────────────────────────────────────

Bytecode *AstEmitter::emit_program(NodeIndex root) {
    Node &prog = tree_.nodes[root];
    auto body_range = tree_.range(root, 0);

    analyze_scope(root);

    next_temp_ = first_temp();
    max_temp_ = next_temp_;

    eval_ret_reg_ = 0;

    for (uint32_t i = 0; i < body_range.len; i++)
        emit_stmt(tree_.extras[body_range.start + i]);

    emit_iABC(RegOp::RETURN, 0, 0, 0);

    auto *bc = freeze();

    return bc;
}

// ─── emit_function ──────────────────────────────────────────────────────────

Bytecode *AstEmitter::emit_function(NodeIndex func_node, bool is_expr) {
    Node &fn = tree_.nodes[func_node];
    NodeIndex id_node = fn.data[0];
    NodeIndex params_node = fn.data[1];
    NodeIndex body_node = fn.data[2];
    uint32_t flags = fn.data[3];

    is_expr_ = is_expr;
    if (id_node != NodeNull) {
        func_name_ = atom_for_span(tree_.span(id_node));
    }

    // Collect params
    if (params_node != NodeNull && tree_.kind(params_node) == NK_FORMAL_PARAMS) {
        auto prange = tree_.range(params_node, 0);
        for (uint32_t i = 0; i < prange.len; i++) {
            NodeIndex param = tree_.extras[prange.start + i];
            if (tree_.kind(param) == NK_FORMAL_PARAM) {
                NodeIndex pname = tree_.nodes[param].data[0];
                if (pname != NodeNull) {
                    Atom name = atom_for_span(tree_.span(pname));
                    bindings_.add_argument(name);
                }
            }
        }
    }

    analyze_scope(body_node);

    next_temp_ = first_temp();
    max_temp_ = next_temp_;

    emit_body(tree_.range(body_node, 0));
    emit_iABx(RegOp::RETURN0, 0, 0);

    return freeze();
}

// ─── emit_body ──────────────────────────────────────────────────────────────

void AstEmitter::emit_body(IndexRange stmts) {
    for (uint32_t i = 0; i < stmts.len; i++)
        emit_stmt(tree_.extras[stmts.start + i]);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Statement emitters
// ═══════════════════════════════════════════════════════════════════════════

void AstEmitter::emit_stmt(NodeIndex node) {
    if (node == NodeNull) return;
    Node &n = tree_.nodes[node];
    switch (n.kind) {
    case NK_EXPR_STMT:     emit_expr_stmt(node); break;
    case NK_DIRECTIVE: {
        auto sv = src_slice(tree_.span(node));
        if (sv.size() >= 2 && (sv.front() == '"' || sv.front() == '\''))
            sv = sv.substr(1, sv.size() - 2);
        int ci = cpool_add(StrPrim::create(sv));
        int r = alloc_temp();
        emit_iABx(RegOp::LOADK, u8(r), u16(ci));
        if (eval_ret_reg_ >= 0)
            emit_iABC(RegOp::MOVE, u8(eval_ret_reg_), u8(r), 0);
        free_temp();
        break;
    }
    case NK_BLOCK_STMT:    emit_block_stmt(node); break;
    case NK_IF_STMT:       emit_if_stmt(node); break;
    case NK_FOR_STMT:      emit_for_stmt(node); break;
    case NK_FOR_IN_STMT:   emit_for_in_stmt(node); break;
    case NK_FOR_OF_STMT:   emit_for_of_stmt(node); break;
    case NK_WHILE_STMT:    emit_while_stmt(node); break;
    case NK_DO_WHILE_STMT: emit_do_while_stmt(node); break;
    case NK_RETURN_STMT:   emit_return_stmt(node); break;
    case NK_THROW_STMT:    emit_throw_stmt(node); break;
    case NK_TRY_STMT:      emit_try_stmt(node); break;
    case NK_VAR_DECL:      emit_var_decl(node); break;
    case NK_BREAK_STMT:    emit_break_stmt(node); break;
    case NK_CONTINUE_STMT: emit_continue_stmt(node); break;
    case NK_LABELED_STMT:  emit_labeled_stmt(node); break;
    case NK_SWITCH_STMT:   emit_switch_stmt(node); break;
    case NK_EMPTY_STMT:    break;
    case NK_DEBUGGER_STMT: break;
    case NK_FUNCTION: {
        NodeIndex id = n.data[0];
        if (id != NodeNull) {
            Atom name = atom_for_span(tree_.span(id));
            if (bindings_.has(name)) {
                auto child = std::make_unique<AstEmitter>(e_, tree_, source_, source_len_);
                child->parent_ = this;
                child->bindings_.parent_table = &this->bindings_;
                Bytecode *bc = child->emit_function(node, false);
                bc->func_name = name;

                int placeholder = cpool_add(Value::bytecode(nullptr));
                int r = alloc_temp();
                emit_iABx(RegOp::FCLOSURE, u8(r), u16(placeholder));
                emit_store(id, r);
                free_temp();

                // Replace placeholder
                for (int i = 0; i < static_cast<int>(cpool_.size()); i++) {
                    if (cpool_[i].is_bytecode() && cpool_[i].as<Bytecode>() == nullptr) {
                        cpool_[i] = Value::bytecode(bc);
                        break;
                    }
                }
                child.release();
            }
        }
        break;
    }
    default:
        break;
    }
}

void AstEmitter::emit_expr_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int r = emit_expr(n.data[0]);
    if (eval_ret_reg_ >= 0 && r >= 0)
        emit_iABC(RegOp::MOVE, u8(eval_ret_reg_), u8(r), 0);
    if (r >= 0) free_temp();
}

void AstEmitter::emit_block_stmt(NodeIndex node) {
    auto range = tree_.range(node, 0);
    push_scope();
    emit_body(range);
    pop_scope();
}

void AstEmitter::emit_if_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int cond = emit_expr(n.data[0]);
    int else_lbl = new_label();
    emit_jump(RegOp::IS_FALSE, else_lbl, u8(cond));
    free_temp();

    emit_stmt(n.data[1]);

    if (n.data[2] != NodeNull) {
        int end_lbl = new_label();
        emit_jump(RegOp::JMP, end_lbl, 0);
        bind_label(else_lbl);
        emit_stmt(n.data[2]);
        bind_label(end_lbl);
    } else {
        bind_label(else_lbl);
    }
}

void AstEmitter::emit_for_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];

    if (n.data[0] != NodeNull) emit_stmt(n.data[0]);

    int test_lbl = new_label();
    int cont_lbl = new_label();
    int break_lbl = new_label();

    bind_label(test_lbl);

    if (n.data[1] != NodeNull) {
        int cond = emit_expr(n.data[1]);
        emit_jump(RegOp::IS_FALSE, break_lbl, u8(cond));
        free_temp();
    }

    push_break(break_lbl, cont_lbl);

    emit_stmt(n.data[3]);

    bind_label(cont_lbl);

    if (n.data[2] != NodeNull) {
        int upd = emit_expr(n.data[2]);
        if (upd >= 0) free_temp();
    }

    emit_jump(RegOp::JMP, test_lbl, 0);
    bind_label(break_lbl);
    pop_break();
}

void AstEmitter::emit_for_in_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    NodeIndex left = n.data[0];

    int obj_reg = emit_expr(n.data[1]);
    int iter_reg = alloc_temp();
    emit_iABC(RegOp::FOR_IN_START, u8(iter_reg), u8(obj_reg), 0);
    free_temp(); // obj

    Atom var_name = atom_for_span(tree_.span(left));
    const Binding *b = bindings_.lookup(var_name);
    int key_reg = b ? b->slot : alloc_temp();
    int more_reg = alloc_temp();

    int loop_lbl = new_label();
    int body_lbl = new_label();
    int break_lbl = new_label();

    emit_jump(RegOp::JMP, loop_lbl, 0);
    bind_label(body_lbl);

    push_break(break_lbl, loop_lbl);
    emit_stmt(n.data[3]);

    bind_label(loop_lbl);
    emit_iABC(RegOp::FOR_IN_NEXT, u8(key_reg), u8(iter_reg), u8(more_reg));
    emit_jump(RegOp::IS_TRUE, body_lbl, u8(more_reg));
    bind_label(break_lbl);
    pop_break();

    free_temp(); // more_reg
    free_temp(); // iter_reg
    if (!b) free_temp();
}

void AstEmitter::emit_for_of_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];

    int iterable_reg = emit_expr(n.data[1]);
    int iter_reg = alloc_temp();
    int more_reg = alloc_temp();
    int val_reg = alloc_temp();

    emit_iABC(RegOp::FOR_OF_START, u8(iter_reg), u8(iterable_reg), 0);
    free_temp(); // iterable

    Atom var_name = atom_for_span(tree_.span(n.data[0]));
    const Binding *b = bindings_.lookup(var_name);
    int target_reg = b ? b->slot : alloc_temp();

    int loop_lbl = new_label();
    int body_lbl = new_label();
    int break_lbl = new_label();

    emit_jump(RegOp::JMP, loop_lbl, 0);
    bind_label(body_lbl);

    push_break(break_lbl, loop_lbl);
    emit_stmt(n.data[3]);

    bind_label(loop_lbl);
    emit_iABC(RegOp::FOR_OF_NEXT, u8(val_reg), u8(iter_reg), u8(more_reg));
    if (target_reg != val_reg)
        emit_iABC(RegOp::MOVE, u8(target_reg), u8(val_reg), 0);
    emit_jump(RegOp::IS_TRUE, body_lbl, u8(more_reg));
    bind_label(break_lbl);
    pop_break();

    free_temp(); // val_reg
    free_temp(); // more_reg
    free_temp(); // iter_reg
    if (!b) free_temp();
}

void AstEmitter::emit_while_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int cont_lbl = new_label();
    int break_lbl = new_label();
    push_break(break_lbl, cont_lbl);

    bind_label(cont_lbl);
    int cond = emit_expr(n.data[0]);
    emit_jump(RegOp::IS_FALSE, break_lbl, u8(cond));
    free_temp();

    emit_stmt(n.data[1]);
    emit_jump(RegOp::JMP, cont_lbl, 0);
    bind_label(break_lbl);
    pop_break();
}

void AstEmitter::emit_do_while_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int body_lbl = new_label();
    int cont_lbl = new_label();
    int break_lbl = new_label();
    push_break(break_lbl, cont_lbl);

    bind_label(body_lbl);
    emit_stmt(n.data[1]);

    bind_label(cont_lbl);
    int cond = emit_expr(n.data[0]);
    emit_jump(RegOp::IS_TRUE, body_lbl, u8(cond));
    free_temp();
    bind_label(break_lbl);
    pop_break();
}

void AstEmitter::emit_return_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    if (n.data[0] != NodeNull) {
        int r = emit_expr(n.data[0]);
        emit_iABC(RegOp::RETURN, u8(r), 0, 0);
        free_temp();
    } else {
        emit_iABx(RegOp::RETURN0, 0, 0);
    }
}

void AstEmitter::emit_throw_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int r = emit_expr(n.data[0]);
    emit_iABC(RegOp::THROW, u8(r), 0, 0);
    free_temp();
}

void AstEmitter::emit_try_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int exc_reg = alloc_temp();

    int catch_lbl = new_label();
    int finalize_lbl = new_label();
    int after_lbl = new_label();
    int skip_catch_lbl = new_label();

    bool has_finally = (n.data[2] != NodeNull);

    emit_jump(RegOp::CATCH, catch_lbl, u8(exc_reg));

    if (n.data[0] != NodeNull) emit_stmt(n.data[0]);

    if (has_finally) {
        emit_jump(RegOp::GOSUB, finalize_lbl, 0);
    }
    emit_iABx(RegOp::UNCATCH, 0, 0);
    emit_jump(RegOp::JMP, skip_catch_lbl, 0);

    bind_label(catch_lbl);

    if (n.data[1] != NodeNull) {
        Node &catch_clause = tree_.nodes[n.data[1]];
        NodeIndex param = catch_clause.data[0];
        if (param != NodeNull) {
            emit_store(param, exc_reg);
        }
        push_scope();
        if (catch_clause.data[1] != NodeNull)
            emit_stmt(catch_clause.data[1]);
        pop_scope();
    }

    if (has_finally) {
        emit_jump(RegOp::GOSUB, finalize_lbl, 0);
    }
    emit_iABx(RegOp::UNCATCH, 0, 0);

    bind_label(skip_catch_lbl);
    emit_jump(RegOp::JMP, after_lbl, 0);

    if (has_finally) {
        bind_label(finalize_lbl);
        emit_stmt(n.data[2]);
        emit_iABx(RegOp::RET, 0, 0);
    }

    bind_label(after_lbl);
}

void AstEmitter::emit_var_decl(NodeIndex node) {
    Node &n = tree_.nodes[node];
    auto range = tree_.range(node, 0);
    for (uint32_t i = 0; i < range.len; i++) {
        NodeIndex decl = tree_.extras[range.start + i];
        Node &d = tree_.nodes[decl];
        NodeIndex id = d.data[0];
        NodeIndex init = d.data[1];

        if (init != NodeNull && id != NodeNull) {
            int val = emit_expr(init);
            emit_store(id, val);
            free_temp();
        }
    }
}

void AstEmitter::emit_break_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    Atom label = kAtomNull;
    if (n.data[0] != NodeNull)
        label = atom_for_span(tree_.span(n.data[0]));
    int lbl = find_break_label(label, false);
    if (lbl >= 0)
        emit_jump(RegOp::JMP, lbl, 0);
}

void AstEmitter::emit_continue_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    Atom label = kAtomNull;
    if (n.data[0] != NodeNull)
        label = atom_for_span(tree_.span(n.data[0]));
    int lbl = find_break_label(label, true);
    if (lbl >= 0)
        emit_jump(RegOp::JMP, lbl, 0);
}

void AstEmitter::emit_labeled_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    Atom label = atom_for_span(tree_.span(node));
    int break_lbl = new_label();
    push_break(break_lbl, -1, label);
    emit_stmt(n.data[1]);
    bind_label(break_lbl);
    pop_break();
}

void AstEmitter::emit_switch_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int disc = emit_expr(n.data[2]);

    auto cases_range = tree_.range(node, 0);
    int end_lbl = new_label();
    int chain_lbl = new_label();

    push_break(end_lbl, -1);

    struct CaseInfo {
        int body_lbl;
        NodeIndex test_node;
        bool has_test;
    };
    std::vector<CaseInfo> cases;

    for (uint32_t i = 0; i < cases_range.len; i++) {
        NodeIndex ci = tree_.extras[cases_range.start + i];
        Node &cn = tree_.nodes[ci];
        int body_lbl = new_label();
        CaseInfo info;
        info.body_lbl = body_lbl;
        info.has_test = (cn.data[2] != NodeNull);
        info.test_node = info.has_test ? cn.data[2] : NodeNull;
        cases.push_back(info);
    }

    emit_jump(RegOp::JMP, chain_lbl, 0);

    for (uint32_t i = 0; i < cases_range.len; i++) {
        NodeIndex ci = tree_.extras[cases_range.start + i];
        Node &cn = tree_.nodes[ci];
        bind_label(cases[i].body_lbl);
        auto body_range = tree_.range(ci, 0);
        for (uint32_t j = 0; j < body_range.len; j++)
            emit_stmt(tree_.extras[body_range.start + j]);
    }

    emit_jump(RegOp::JMP, end_lbl, 0);

    bind_label(chain_lbl);
    for (auto &c : cases) {
        if (c.has_test) {
            int test_val = emit_expr(c.test_node);
            emit_iABC(RegOp::SEQ, u8(test_val), u8(disc), u8(test_val));
            emit_jump(RegOp::IS_TRUE, c.body_lbl, u8(test_val));
            free_temp();
        } else {
            emit_jump(RegOp::JMP, c.body_lbl, 0);
        }
    }
    emit_jump(RegOp::JMP, end_lbl, 0);

    free_temp(); // disc

    bind_label(end_lbl);
    pop_break();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Expression emitters
// ═══════════════════════════════════════════════════════════════════════════

int AstEmitter::emit_expr(NodeIndex node) {
    if (node == NodeNull) {
        int r = alloc_temp();
        emit_iABx(RegOp::LOADUNDEF, u8(r), 0);
        return r;
    }
    Node &n = tree_.nodes[node];
    switch (n.kind) {
    case NK_NUMERIC_LIT:   return emit_numeric_lit(node);
    case NK_STRING_LIT:    return emit_string_lit(node);
    case NK_BOOL_LIT:      return emit_bool_lit(node);
    case NK_NULL_LIT:      return emit_null_lit();
    case NK_IDENT_REF:     return emit_ident_ref(node);
    case NK_THIS_EXPR:     return emit_this_expr();
    case NK_BINARY_EXPR:   return emit_binary_expr(node);
    case NK_LOGICAL_EXPR:  return emit_logical_expr(node);
    case NK_UNARY_EXPR:    return emit_unary_expr(node);
    case NK_UPDATE_EXPR:   return emit_update_expr(node);
    case NK_ASSIGNMENT_EXPR: return emit_assignment_expr(node);
    case NK_CONDITIONAL_EXPR: return emit_conditional_expr(node);
    case NK_SEQUENCE_EXPR: return emit_sequence_expr(node);
    case NK_MEMBER_EXPR:   return emit_member_expr(node);
    case NK_CALL_EXPR:     return emit_call_expr(node);
    case NK_NEW_EXPR:      return emit_new_expr(node);
    case NK_ARRAY_EXPR:    return emit_array_expr(node);
    case NK_OBJECT_EXPR:   return emit_object_expr(node);
    case NK_FUNCTION:      return emit_func_expr(node);
    case NK_ARROW_FUNCTION: return emit_arrow_func(node);
    case NK_PAREN_EXPR:    return emit_paren_expr(node);
    case NK_TEMPLATE_LIT:  return emit_template_lit(node);
    case NK_AWAIT_EXPR:    return emit_expr(n.data[0]);
    case NK_YIELD_EXPR:    return emit_expr(n.data[0]);
    default: {
        int r = alloc_temp();
        emit_iABx(RegOp::LOADUNDEF, u8(r), 0);
        return r;
    }
    }
}

// ─── Literals ───────────────────────────────────────────────────────────────

int AstEmitter::emit_numeric_lit(NodeIndex node) {
    auto sv = src_slice(tree_.span(node));
    double val = std::strtod(sv.data(), nullptr);
    int32_t iv = static_cast<int32_t>(val);
    int r = alloc_temp();
    if (val == static_cast<double>(iv) && iv >= -32768 && iv <= 32767) {
        emit_iAsBx(RegOp::LOADINT, u8(r), s16(iv));
    } else {
        int ci = cpool_add(Value::float64(val));
        emit_iABx(RegOp::LOADK, u8(r), u16(ci));
    }
    return r;
}

int AstEmitter::emit_string_lit(NodeIndex node) {
    auto sv = src_slice(tree_.span(node));
    if (sv.size() >= 2 && (sv.front() == '"' || sv.front() == '\'')) {
        sv = sv.substr(1, sv.size() - 2);
    }
    int ci = cpool_add(StrPrim::create(sv));
    int r = alloc_temp();
    emit_iABx(RegOp::LOADK, u8(r), u16(ci));
    return r;
}

int AstEmitter::emit_bool_lit(NodeIndex node) {
    int r = alloc_temp();
    Node &n = tree_.nodes[node];
    emit_iABx(n.data[0] ? RegOp::LOADTRUE : RegOp::LOADFALSE, u8(r), 0);
    return r;
}

int AstEmitter::emit_null_lit() {
    int r = alloc_temp();
    emit_iABx(RegOp::LOADNULL, u8(r), 0);
    return r;
}

int AstEmitter::emit_this_expr() {
    int r = alloc_temp();
    emit_iABC(RegOp::MOVE, u8(r), 0, 0);
    return r;
}

int AstEmitter::emit_ident_ref(NodeIndex node) {
    Atom name = atom_for_span(tree_.span(node));

    // Resolve via binding table — walks local + parent chains for upvalues
    const Binding *b = bindings_.lookup_captured(name);
    if (b) {
        int r = alloc_temp();
        if (b->kind == BindKind::Upvalue) {
            emit_iABC(RegOp::GETUPVAL, u8(r), u8(b->slot), 0);
        } else {
            emit_iABC(RegOp::MOVE, u8(r), u8(b->slot), 0);
        }
        return r;
    }

    // Global: GETFIELD on this (R[0])
    int r = alloc_temp();
    int ci = cpool_add(e_->atom_to_value(name));
    emit_iABC(RegOp::GETFIELD, u8(r), 0, u8(ci));
    return r;
}

// ─── Binary expression ─────────────────────────────────────────────────────

int AstEmitter::emit_binary_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    auto bop = static_cast<BinOp>(n.data[2]);

    if (bop == BinInstanceof) {
        int obj = emit_expr(n.data[0]);
        int ctor = emit_expr(n.data[1]);
        emit_iABC(RegOp::INSTANCEOF, u8(obj), u8(obj), u8(ctor));
        free_temp(); // ctor
        return obj;
    }

    int left = emit_expr(n.data[0]);
    int right = emit_expr(n.data[1]);
    RegOp rop = binop_to_reg(bop);
    emit_iABC(rop, u8(left), u8(left), u8(right));
    free_temp(); // right
    return left;
}

// ─── Logical expression ────────────────────────────────────────────────────

int AstEmitter::emit_logical_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    auto lop = static_cast<LogOp>(n.data[2]);

    int left = emit_expr(n.data[0]);
    int end_lbl = new_label();

    RegOp jump_op;
    switch (lop) {
    case LogAnd:     jump_op = RegOp::IS_FALSE; break;
    case LogOr:      jump_op = RegOp::IS_TRUE; break;
    case LogNullish: jump_op = RegOp::IS_NULLISH; break;
    default:         jump_op = RegOp::IS_FALSE; break;
    }

    emit_jump(jump_op, end_lbl, u8(left));
    free_temp();

    int right = emit_expr(n.data[1]);
    emit_iABC(RegOp::MOVE, u8(left), u8(right), 0);
    free_temp();

    bind_label(end_lbl);
    return left;
}

// ─── Unary expression ──────────────────────────────────────────────────────

int AstEmitter::emit_unary_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    auto uop = static_cast<UnOp>(n.data[1]);

    if (uop == UnVoid) {
        int arg = emit_expr(n.data[0]);
        free_temp();
        int r = alloc_temp();
        emit_iABx(RegOp::LOADUNDEF, u8(r), 0);
        return r;
    }
    if (uop == UnDelete) {
        int arg = emit_expr(n.data[0]);
        free_temp();
        int r = alloc_temp();
        emit_iABx(RegOp::LOADTRUE, u8(r), 0);
        return r;
    }
    if (uop == UnPlus) {
        return emit_expr(n.data[0]);
    }

    int arg = emit_expr(n.data[0]);
    RegOp opc;
    switch (uop) {
    case UnMinus:  opc = RegOp::NEG; break;
    case UnBang:   opc = RegOp::LNOT; break;
    case UnTilde:  opc = RegOp::BNOT; break;
    case UnTypeof: opc = RegOp::TYPEOF; break;
    default:       return arg;
    }
    emit_iABC(opc, u8(arg), u8(arg), 0);
    return arg;
}

// ─── Update expression (++, --) ────────────────────────────────────────────

int AstEmitter::emit_update_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    auto uop = static_cast<UpdOp>(n.data[1]);
    bool prefix = (n.data[2] & NF::Prefix) != 0;
    RegOp opc = (uop == UpdInc) ? RegOp::INC : RegOp::DEC;

    NodeIndex arg = n.data[0];

    if (prefix) {
        int old_val = emit_load(arg);
        emit_iABC(opc, u8(old_val), u8(old_val), 0);
        emit_store(arg, old_val);
        return old_val;
    }

    int old_val = emit_load(arg);
    int new_val = alloc_temp();
    emit_iABC(opc, u8(new_val), u8(old_val), 0);
    emit_store(arg, new_val);
    free_temp(); // new_val
    return old_val;
}

// ─── Assignment expression ─────────────────────────────────────────────────

int AstEmitter::emit_assignment_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    auto aop = static_cast<AsgnOp>(n.data[2]);
    NodeIndex left = n.data[0];
    NodeIndex right = n.data[1];

    if (aop == AsgnAssign) {
        int val = emit_expr(right);
        emit_store(left, val);
        return val;
    }

    RegOp cbop = compound_binop(aop);
    if (cbop != RegOp::NOP) {
        int rhs = emit_expr(right);
        int lhs = emit_load(left);
        emit_iABC(cbop, u8(lhs), u8(lhs), u8(rhs));
        emit_store(left, lhs);
        free_temp(); // rhs
        return lhs;
    }

    // Logical assignment: ??=, &&=, ||=
    int lhs = emit_load(left);
    int end_lbl = new_label();
    RegOp jump_op;
    switch (aop) {
    case AsgnNullish: jump_op = RegOp::IS_NULLISH; break;
    case AsgnLand:    jump_op = RegOp::IS_FALSE; break;
    case AsgnLor:     jump_op = RegOp::IS_TRUE; break;
    default:          jump_op = RegOp::IS_FALSE; break;
    }

    emit_jump(jump_op, end_lbl, u8(lhs));
    free_temp(); // lhs

    int rhs = emit_expr(right);
    emit_store(left, rhs);
    bind_label(end_lbl);

    // We need to return a value. After the jump path, lhs was freed and
    // rhs is the current top. After the fall-through, rhs is the top.
    // Re-allocate to get a clean top.
    int result = alloc_temp();
    // At this point either lhs or rhs holds the value, but we lost lhs's reg.
    // The result of logical assignment is the value that was stored.
    // We need to re-read it. Simplest: MOVE from rhs (which holds the stored value).
    // But on the short-circuit path, rhs was never emitted...
    // Just re-load the lhs value.
    emit_iABC(RegOp::MOVE, u8(result), u8(rhs), 0);
    return result;
}

// ─── Conditional expression (ternary) ──────────────────────────────────────

int AstEmitter::emit_conditional_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int cond = emit_expr(n.data[0]);
    int else_lbl = new_label();
    int end_lbl = new_label();

    emit_jump(RegOp::IS_FALSE, else_lbl, u8(cond));

    int then_val = emit_expr(n.data[1]);
    emit_iABC(RegOp::MOVE, u8(cond), u8(then_val), 0);
    free_temp(); // then_val
    emit_jump(RegOp::JMP, end_lbl, 0);

    bind_label(else_lbl);
    int else_val = emit_expr(n.data[2]);
    emit_iABC(RegOp::MOVE, u8(cond), u8(else_val), 0);
    free_temp(); // else_val

    bind_label(end_lbl);
    return cond;
}

// ─── Sequence expression ───────────────────────────────────────────────────

int AstEmitter::emit_sequence_expr(NodeIndex node) {
    auto range = tree_.range(node, 0);
    int last = -1;
    for (uint32_t i = 0; i < range.len; i++) {
        if (last >= 0) free_temp();
        last = emit_expr(tree_.extras[range.start + i]);
    }
    return last;
}

// ─── Member expression ─────────────────────────────────────────────────────

int AstEmitter::emit_member_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int obj = emit_expr(n.data[0]);
    uint32_t flags = n.data[2];
    bool computed = (flags & NF::Computed) != 0;
    bool optional = (flags & NF::Optional) != 0;

    if (optional) {
        int end_lbl = new_label();
        emit_jump(RegOp::IS_NULLISH, end_lbl, u8(obj));
        if (computed) {
            int prop = emit_expr(n.data[1]);
            emit_iABC(RegOp::GETELEM, u8(obj), u8(obj), u8(prop));
            free_temp(); // prop
        } else {
            Atom prop_name = atom_for_span(tree_.span(n.data[1]));
            int ci = cpool_add(e_->atom_to_value(prop_name));
            emit_iABC(RegOp::GETFIELD, u8(obj), u8(obj), u8(ci));
        }
        bind_label(end_lbl);
        return obj;
    }

    if (computed) {
        int prop = emit_expr(n.data[1]);
        emit_iABC(RegOp::GETELEM, u8(obj), u8(obj), u8(prop));
        free_temp(); // prop
    } else {
        Atom prop_name = atom_for_span(tree_.span(n.data[1]));
        int ci = cpool_add(e_->atom_to_value(prop_name));
        emit_iABC(RegOp::GETFIELD, u8(obj), u8(obj), u8(ci));
    }
    return obj;
}

// ─── Call expression ────────────────────────────────────────────────────────

int AstEmitter::emit_call_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    NodeIndex callee = n.data[0];
    auto args_range = tree_.range(node, 1);
    bool is_method = (callee != NodeNull && tree_.kind(callee) == NK_MEMBER_EXPR);

    int func_reg = emit_expr(callee);

    if (is_method) {
        int this_reg = alloc_temp();
        // Re-emit the object part of the member expr for 'this'
        // The member expr has left=object at data[0]
        Node &mem = tree_.nodes[callee];
        int obj = emit_expr(mem.data[0]);
        emit_iABC(RegOp::MOVE, u8(this_reg), u8(obj), 0);
        free_temp(); // obj
    }

    int argc = 0;
    for (uint32_t i = 0; i < args_range.len; i++) {
        emit_expr(tree_.extras[args_range.start + i]);
        argc++;
    }

    // Free all temps: argc args + 1 func + (is_method ? 1 this : 0)
    int total_free = argc + 1 + (is_method ? 1 : 0);
    for (int i = 0; i < total_free; i++)
        free_temp();

    int ret = alloc_temp();
    if (is_method)
        emit_iABC(RegOp::CALL_M, u8(ret), u8(func_reg), u8(argc));
    else
        emit_iABC(RegOp::CALL, u8(ret), u8(func_reg), u8(argc));

    return ret;
}

// ─── New expression ────────────────────────────────────────────────────────

int AstEmitter::emit_new_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    NodeIndex callee = n.data[0];
    auto args_range = tree_.range(node, 1);

    int callee_reg = emit_expr(callee);

    for (uint32_t i = 0; i < args_range.len; i++)
        emit_expr(tree_.extras[args_range.start + i]);

    int total_free = static_cast<int>(args_range.len) + 1;
    for (int i = 0; i < total_free; i++)
        free_temp();

    int ret = alloc_temp();
    emit_iABC(RegOp::CTOR, u8(ret), u8(callee_reg), u8(args_range.len));
    return ret;
}

// ─── Array expression ──────────────────────────────────────────────────────

int AstEmitter::emit_array_expr(NodeIndex node) {
    auto range = tree_.range(node, 0);
    int arr = alloc_temp();
    emit_iABC(RegOp::NEWARR, u8(arr), 0, 0);

    for (uint32_t i = 0; i < range.len; i++) {
        NodeIndex elem = tree_.extras[range.start + i];
        if (elem == NodeNull) continue;
        if (tree_.kind(elem) == NK_SPREAD) {
            int val = emit_expr(tree_.nodes[elem].data[0]);
            emit_iABC(RegOp::APPEND, u8(arr), u8(val), 0);
            free_temp();
        } else {
            int val = emit_expr(elem);
            emit_iABC(RegOp::APPEND, u8(arr), u8(val), 0);
            free_temp();
        }
    }
    return arr;
}

// ─── Object expression ─────────────────────────────────────────────────────

int AstEmitter::emit_object_expr(NodeIndex node) {
    auto range = tree_.range(node, 0);
    int obj = alloc_temp();
    emit_iABx(RegOp::NEWOBJ, u8(obj), 0);

    for (uint32_t i = 0; i < range.len; i++) {
        NodeIndex prop = tree_.extras[range.start + i];
        Node &pn = tree_.nodes[prop];
        uint32_t flags = pn.data[2];
        bool computed = (flags & NF::Computed) != 0;
        bool shorthand = (flags & NF::Shorthand) != 0;

        if (computed) {
            int key = emit_expr(pn.data[0]);
            int val = emit_expr(pn.data[1]);
            emit_iABC(RegOp::DEFINE_ELEM, u8(obj), u8(key), u8(val));
            free_temp(); // val
            free_temp(); // key
        } else {
            Atom key_name;
            if (shorthand) {
                key_name = atom_for_span(tree_.span(pn.data[1]));
            } else {
                key_name = atom_for_span(tree_.span(pn.data[0]));
            }
            int ci = cpool_add(e_->atom_to_value(key_name));
            int val = emit_expr(pn.data[1]);
            emit_iABC(RegOp::DEFINE_FIELD, u8(obj), u8(ci), u8(val));
            free_temp(); // val
        }
    }
    return obj;
}

// ─── Function expression ───────────────────────────────────────────────────

int AstEmitter::emit_func_expr(NodeIndex node) {
    auto child = std::make_unique<AstEmitter>(e_, tree_, source_, source_len_);
    child->parent_ = this;
    child->bindings_.parent_table = &this->bindings_;
    Bytecode *bc = child->emit_function(node, true);

    int placeholder = cpool_add(Value::bytecode(nullptr));
    int r = alloc_temp();
    emit_iABx(RegOp::FCLOSURE, u8(r), u16(placeholder));

    // Replace placeholder with actual bytecode
    for (int i = 0; i < static_cast<int>(cpool_.size()); i++) {
        if (cpool_[i].is_bytecode() && cpool_[i].as<Bytecode>() == nullptr) {
            cpool_[i] = Value::bytecode(bc);
            break;
        }
    }
    child.release();
    return r;
}

// ─── Arrow function ────────────────────────────────────────────────────────

int AstEmitter::emit_arrow_func(NodeIndex node) {
    Node &n = tree_.nodes[node];
    NodeIndex params = n.data[0];
    NodeIndex body = n.data[1];
    uint32_t flags = n.data[2];
    bool is_expr_body = (flags & NF::ExprBody) != 0;

    // Create a synthetic NK_FUNCTION node to reuse emit_function
    Span sp = tree_.span(node);
    NodeIndex func_node = tree_.alloc(NK_FUNCTION, sp,
        NodeNull, params, body, flags);

    return emit_func_expr(func_node);
}

// ─── Paren expression ──────────────────────────────────────────────────────

int AstEmitter::emit_paren_expr(NodeIndex node) {
    Node &n = tree_.nodes[node];
    return emit_expr(n.data[0]);
}

// ─── Template literal ──────────────────────────────────────────────────────

int AstEmitter::emit_template_lit(NodeIndex node) {
    Node &n = tree_.nodes[node];
    auto quasis_range = tree_.range(node, 0);
    auto exprs_range = tree_.range(node, 2);

    if (exprs_range.len == 0) {
        // Single quasi — just a string
        // The first quasi has the full template content
        if (quasis_range.len > 0) {
            NodeIndex q = tree_.extras[quasis_range.start];
            Span qsp = tree_.span(q);
            auto sv = src_slice(qsp);
            int ci = cpool_add(StrPrim::create(sv));
            int r = alloc_temp();
            emit_iABx(RegOp::LOADK, u8(r), u16(ci));
            return r;
        }
        int r = alloc_temp();
        emit_iABx(RegOp::LOADNULL, u8(r), 0);
        return r;
    }

    // Build string concatenation: start with first quasi, then + expr + quasi ...
    int result = -1;
    for (uint32_t i = 0; i < exprs_range.len; i++) {
        if (i == 0) {
            // First quasi
            NodeIndex q = tree_.extras[quasis_range.start];
            Span qsp = tree_.span(q);
            auto sv = src_slice(qsp);
            int ci = cpool_add(StrPrim::create(sv));
            result = alloc_temp();
            emit_iABx(RegOp::LOADK, u8(result), u16(ci));
        }

        // Expression
        int expr_val = emit_expr(tree_.extras[exprs_range.start + i]);
        emit_iABC(RegOp::ADD, u8(result), u8(result), u8(expr_val));
        free_temp(); // expr_val

        // Next quasi
        if (i + 1 < quasis_range.len) {
            NodeIndex q = tree_.extras[quasis_range.start + i + 1];
            Span qsp = tree_.span(q);
            auto sv = src_slice(qsp);
            if (!sv.empty()) {
                int ci = cpool_add(StrPrim::create(sv));
                int quasi_reg = alloc_temp();
                emit_iABx(RegOp::LOADK, u8(quasi_reg), u16(ci));
                emit_iABC(RegOp::ADD, u8(result), u8(result), u8(quasi_reg));
                free_temp();
            }
        }
    }

    if (result < 0) {
        result = alloc_temp();
        emit_iABx(RegOp::LOADNULL, u8(result), 0);
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  LValue handling (store to / load from)
// ═══════════════════════════════════════════════════════════════════════════

void AstEmitter::emit_store(NodeIndex target, int value_reg) {
    if (target == NodeNull) return;
    NodeKind k = tree_.kind(target);

    if (k == NK_IDENT_REF || k == NK_BINDING_IDENT) {
        Atom name = atom_for_span(tree_.span(target));
        const Binding *b = bindings_.lookup_captured(name);
        if (b) {
            if (b->kind == BindKind::Upvalue) {
                emit_iABC(RegOp::SETUPVAL, u8(value_reg), u8(b->slot), 0);
            } else {
                emit_iABC(RegOp::MOVE, u8(b->slot), u8(value_reg), 0);
            }
            return;
        }
        // Global: SETFIELD on this (R[0])
        int ci = cpool_add(e_->atom_to_value(name));
        emit_iABC(RegOp::SETFIELD, 0, u8(ci), u8(value_reg));
        return;
    }

    if (k == NK_MEMBER_EXPR) {
        Node &n = tree_.nodes[target];
        uint32_t flags = n.data[2];
        bool computed = (flags & NF::Computed) != 0;
        int obj = emit_expr(n.data[0]);
        if (computed) {
            int prop = emit_expr(n.data[1]);
            emit_iABC(RegOp::SETELEM, u8(obj), u8(prop), u8(value_reg));
            free_temp(); // prop
        } else {
            Atom prop_name = atom_for_span(tree_.span(n.data[1]));
            int ci = cpool_add(e_->atom_to_value(prop_name));
            emit_iABC(RegOp::SETFIELD, u8(obj), u8(ci), u8(value_reg));
        }
        free_temp(); // obj
        return;
    }
}

int AstEmitter::emit_load(NodeIndex target) {
    if (target == NodeNull) {
        int r = alloc_temp();
        emit_iABx(RegOp::LOADUNDEF, u8(r), 0);
        return r;
    }
    NodeKind k = tree_.kind(target);

    if (k == NK_IDENT_REF || k == NK_BINDING_IDENT) {
        return emit_ident_ref(target);
    }
    if (k == NK_MEMBER_EXPR) {
        return emit_member_expr(target);
    }
    // Fallback
    return emit_expr(target);
}

bool AstEmitter::is_member_expr(NodeIndex node) const {
    return node != NodeNull && tree_.kind(node) == NK_MEMBER_EXPR;
}

RegOp AstEmitter::binop_to_reg(BinOp bop) const {
    return qjsp::binop_to_reg(bop);
}

} // namespace qjsp
