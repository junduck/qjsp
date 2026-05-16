#include "qjsp/ast_emit.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/reg_opcode_info.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

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
        free_temp(r);
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
                free_temp(r);

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
    int r = alloc_temp();
    emit_expr(n.data[0], r);
    if (eval_ret_reg_ >= 0)
        emit_iABC(RegOp::MOVE, u8(eval_ret_reg_), u8(r), 0);
    free_temp(r);
}

void AstEmitter::emit_block_stmt(NodeIndex node) {
    auto range = tree_.range(node, 0);
    push_scope();
    emit_body(range);
    pop_scope();
}

void AstEmitter::emit_if_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int cond = alloc_temp();
    emit_expr(n.data[0], cond);
    int else_lbl = new_label();
    emit_jump(RegOp::IS_FALSE, else_lbl, u8(cond));
    free_temp(cond);

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
        int cond = alloc_temp();
        emit_expr(n.data[1], cond);
        emit_jump(RegOp::IS_FALSE, break_lbl, u8(cond));
        free_temp(cond);
    }

    push_break(break_lbl, cont_lbl);

    emit_stmt(n.data[3]);

    bind_label(cont_lbl);

    if (n.data[2] != NodeNull) {
        int upd = alloc_temp();
        emit_expr(n.data[2], upd);
        free_temp(upd);
    }

    emit_jump(RegOp::JMP, test_lbl, 0);
    bind_label(break_lbl);
    pop_break();
}

void AstEmitter::emit_for_in_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    NodeIndex left = n.data[0];

    int iter_reg = alloc_temp();
    int more_reg = alloc_temp();

    int obj_reg = alloc_temp();
    emit_expr(n.data[1], obj_reg);
    emit_iABC(RegOp::FOR_IN_START, u8(iter_reg), u8(obj_reg), 0);
    free_temp(obj_reg);  // LIFO ✓

    Atom var_name = atom_for_span(tree_.span(left));
    const Binding *b = bindings_.lookup(var_name);
    int key_reg = b ? b->slot : alloc_temp();

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

    if (!b) free_temp(key_reg);
    free_temp(more_reg);
    free_temp(iter_reg);
}

void AstEmitter::emit_for_of_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];

    int iter_reg = alloc_temp();
    int more_reg = alloc_temp();
    int val_reg = alloc_temp();

    int iterable_reg = alloc_temp();
    emit_expr(n.data[1], iterable_reg);
    emit_iABC(RegOp::FOR_OF_START, u8(iter_reg), u8(iterable_reg), 0);
    free_temp(iterable_reg);  // LIFO ✓

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

    if (!b) free_temp(target_reg);
    free_temp(val_reg);
    free_temp(more_reg);
    free_temp(iter_reg);
}

void AstEmitter::emit_while_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int cont_lbl = new_label();
    int break_lbl = new_label();
    push_break(break_lbl, cont_lbl);

    bind_label(cont_lbl);
    int cond = alloc_temp();
    emit_expr(n.data[0], cond);
    emit_jump(RegOp::IS_FALSE, break_lbl, u8(cond));
    free_temp(cond);

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
    int cond = alloc_temp();
    emit_expr(n.data[0], cond);
    emit_jump(RegOp::IS_TRUE, body_lbl, u8(cond));
    free_temp(cond);
    bind_label(break_lbl);
    pop_break();
}

void AstEmitter::emit_return_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    if (n.data[0] != NodeNull) {
        int r = alloc_temp();
        emit_expr(n.data[0], r);
        emit_iABC(RegOp::RETURN, u8(r), 0, 0);
        free_temp(r);
    } else {
        emit_iABx(RegOp::RETURN0, 0, 0);
    }
}

void AstEmitter::emit_throw_stmt(NodeIndex node) {
    Node &n = tree_.nodes[node];
    int r = alloc_temp();
    emit_expr(n.data[0], r);
    emit_iABC(RegOp::THROW, u8(r), 0, 0);
    free_temp(r);
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
            int val = alloc_temp();
            emit_expr(init, val);
            emit_store(id, val);
            free_temp(val);
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
    int disc = alloc_temp();
    emit_expr(n.data[2], disc);

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
            int test_val = alloc_temp();
            emit_expr(c.test_node, test_val);
            emit_iABC(RegOp::SEQ, u8(test_val), u8(disc), u8(test_val));
            emit_jump(RegOp::IS_TRUE, c.body_lbl, u8(test_val));
            free_temp(test_val);
        } else {
            emit_jump(RegOp::JMP, c.body_lbl, 0);
        }
    }
    emit_jump(RegOp::JMP, end_lbl, 0);

    free_temp(disc);

    bind_label(end_lbl);
    pop_break();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Expression emitters
// ═══════════════════════════════════════════════════════════════════════════

void AstEmitter::emit_expr(NodeIndex node, int dst) {
    if (node == NodeNull) {
        emit_iABx(RegOp::LOADUNDEF, u8(dst), 0);
        return;
    }
    Node &n = tree_.nodes[node];
    switch (n.kind) {
    case NK_NUMERIC_LIT:   emit_numeric_lit(node, dst); break;
    case NK_STRING_LIT:    emit_string_lit(node, dst); break;
    case NK_BOOL_LIT:      emit_bool_lit(node, dst); break;
    case NK_NULL_LIT:      emit_null_lit(dst); break;
    case NK_IDENT_REF:     emit_ident_ref(node, dst); break;
    case NK_THIS_EXPR:     emit_this_expr(dst); break;
    case NK_BINARY_EXPR:   emit_binary_expr(node, dst); break;
    case NK_LOGICAL_EXPR:  emit_logical_expr(node, dst); break;
    case NK_UNARY_EXPR:    emit_unary_expr(node, dst); break;
    case NK_UPDATE_EXPR:   emit_update_expr(node, dst); break;
    case NK_ASSIGNMENT_EXPR: emit_assignment_expr(node, dst); break;
    case NK_CONDITIONAL_EXPR: emit_conditional_expr(node, dst); break;
    case NK_SEQUENCE_EXPR: emit_sequence_expr(node, dst); break;
    case NK_MEMBER_EXPR:   emit_member_expr(node, dst); break;
    case NK_CALL_EXPR:     emit_call_expr(node, dst); break;
    case NK_NEW_EXPR:      emit_new_expr(node, dst); break;
    case NK_ARRAY_EXPR:    emit_array_expr(node, dst); break;
    case NK_OBJECT_EXPR:   emit_object_expr(node, dst); break;
    case NK_FUNCTION:      emit_func_expr(node, dst); break;
    case NK_ARROW_FUNCTION: emit_arrow_func(node, dst); break;
    case NK_PAREN_EXPR:    emit_paren_expr(node, dst); break;
    case NK_TEMPLATE_LIT:  emit_template_lit(node, dst); break;
    case NK_AWAIT_EXPR:    emit_expr(n.data[0], dst); break;
    case NK_YIELD_EXPR:    emit_expr(n.data[0], dst); break;
    default:               emit_iABx(RegOp::LOADUNDEF, u8(dst), 0); break;
    }
}

// ─── Literals ───────────────────────────────────────────────────────────────

void AstEmitter::emit_numeric_lit(NodeIndex node, int dst) {
    auto sv = src_slice(tree_.span(node));

    // Strip underscore separators
    char buf[128];
    int len = 0;
    for (size_t i = 0; i < sv.size() && len < 127; i++)
        if (sv[i] != '_') buf[len++] = sv[i];
    buf[len] = '\0';

    double val;

    if (len >= 2 && buf[0] == '0') {
        if (buf[1] == 'x' || buf[1] == 'X') {
            // strtod handles 0x prefix natively with correct overflow (Infinity)
            char *end;
            val = std::strtod(buf, &end);
        } else if (buf[1] == 'o' || buf[1] == 'O' || buf[1] == 'b' || buf[1] == 'B') {
            int radix = (buf[1] == 'b' || buf[1] == 'B') ? 2 : 8;
            errno = 0;
            char *end;
            uint64_t u = std::strtoull(buf + 2, &end, radix);
            if (u == ULLONG_MAX && errno == ERANGE) {
                // Overflow — compute double iteratively
                val = 0;
                for (const char *p = buf + 2; p < end; p++)
                    val = val * radix + (*p - (radix == 8 && *p >= '8' ? 0 : '0'));
            } else {
                val = static_cast<double>(u);
            }
        } else {
            char *end;
            val = std::strtod(buf, &end);
        }
    } else {
        char *end;
        val = std::strtod(buf, &end);
    }

    int32_t iv = static_cast<int32_t>(val);
    if (val == static_cast<double>(iv) && iv >= -32768 && iv <= 32767) {
        emit_iAsBx(RegOp::LOADINT, u8(dst), s16(iv));
    } else {
        int ci = cpool_add(Value::float64(val));
        emit_iABx(RegOp::LOADK, u8(dst), u16(ci));
    }
}

void AstEmitter::emit_string_lit(NodeIndex node, int dst) {
    auto sv = src_slice(tree_.span(node));
    if (sv.size() >= 2 && (sv.front() == '"' || sv.front() == '\''))
        sv = sv.substr(1, sv.size() - 2);
    int ci = cpool_add(StrPrim::create(sv));
    emit_iABx(RegOp::LOADK, u8(dst), u16(ci));
}

void AstEmitter::emit_bool_lit(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    emit_iABx(n.data[0] ? RegOp::LOADTRUE : RegOp::LOADFALSE, u8(dst), 0);
}

void AstEmitter::emit_null_lit(int dst) {
    emit_iABx(RegOp::LOADNULL, u8(dst), 0);
}

void AstEmitter::emit_this_expr(int dst) {
    emit_iABC(RegOp::MOVE, u8(dst), 0, 0);
}

void AstEmitter::emit_ident_ref(NodeIndex node, int dst) {
    Atom name = atom_for_span(tree_.span(node));
    const Binding *b = bindings_.lookup_captured(name);
    if (b) {
        if (b->kind == BindKind::Upvalue)
            emit_iABC(RegOp::GETUPVAL, u8(dst), u8(b->slot), 0);
        else
            emit_iABC(RegOp::MOVE, u8(dst), u8(b->slot), 0);
        return;
    }
    int ci = cpool_add(e_->atom_to_value(name));
    emit_iABC(RegOp::GETFIELD, u8(dst), 0, u8(ci));
}

// ─── Binary expression ─────────────────────────────────────────────────────

void AstEmitter::emit_binary_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    auto bop = static_cast<BinOp>(n.data[2]);

    if (bop == BinInstanceof) {
        emit_expr(n.data[0], dst);           // obj → dst
        int ctor = alloc_temp();
        emit_expr(n.data[1], ctor);
        emit_iABC(RegOp::INSTANCEOF, u8(dst), u8(dst), u8(ctor));
        free_temp(ctor);
        return;
    }

    emit_expr(n.data[0], dst);               // left → dst
    int right = alloc_temp();
    emit_expr(n.data[1], right);
    RegOp rop = binop_to_reg(bop);
    emit_iABC(rop, u8(dst), u8(dst), u8(right));
    free_temp(right);
}

// ─── Logical expression ────────────────────────────────────────────────────

void AstEmitter::emit_logical_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    auto lop = static_cast<LogOp>(n.data[2]);

    emit_expr(n.data[0], dst);          // left → dst

    int end_lbl = new_label();
    RegOp jump_op;
    switch (lop) {
    case LogAnd:     jump_op = RegOp::IS_FALSE; break;
    case LogOr:      jump_op = RegOp::IS_TRUE; break;
    case LogNullish: jump_op = RegOp::IS_NULLISH; break;
    default:         jump_op = RegOp::IS_FALSE; break;
    }
    emit_jump(jump_op, end_lbl, u8(dst));

    int right = alloc_temp();
    emit_expr(n.data[1], right);
    emit_iABC(RegOp::MOVE, u8(dst), u8(right), 0);
    free_temp(right);

    bind_label(end_lbl);
}

// ─── Unary expression ──────────────────────────────────────────────────────

void AstEmitter::emit_unary_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    auto uop = static_cast<UnOp>(n.data[1]);

    if (uop == UnVoid) {
        int arg = alloc_temp();
        emit_expr(n.data[0], arg);
        free_temp(arg); // evaluate for side effects, discard
        emit_iABx(RegOp::LOADUNDEF, u8(dst), 0);
        return;
    }
    if (uop == UnDelete) {
        int arg = alloc_temp();
        emit_expr(n.data[0], arg);
        free_temp(arg); // discard
        emit_iABx(RegOp::LOADTRUE, u8(dst), 0);
        return;
    }
    if (uop == UnPlus) {
        emit_expr(n.data[0], dst);      // identity
        return;
    }

    emit_expr(n.data[0], dst);           // operand → dst
    RegOp opc;
    switch (uop) {
    case UnMinus:  opc = RegOp::NEG; break;
    case UnBang:   opc = RegOp::LNOT; break;
    case UnTilde:  opc = RegOp::BNOT; break;
    case UnTypeof: opc = RegOp::TYPEOF; break;
    default:       return;
    }
    emit_iABC(opc, u8(dst), u8(dst), 0);
}

// ─── Update expression (++, --) ────────────────────────────────────────────

void AstEmitter::emit_update_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    auto uop = static_cast<UpdOp>(n.data[1]);
    bool prefix = (n.data[2] & NF::Prefix) != 0;
    RegOp opc = (uop == UpdInc) ? RegOp::INC : RegOp::DEC;
    NodeIndex arg = n.data[0];

    int old_val = alloc_temp();
    emit_load(arg, old_val);            // load → old_val

    if (prefix) {
        emit_iABC(opc, u8(old_val), u8(old_val), 0);  // old_val = op(old_val)
        emit_store(arg, old_val);
        emit_iABC(RegOp::MOVE, u8(dst), u8(old_val), 0); // dst = new value
    } else {
        int new_val = alloc_temp();
        emit_iABC(opc, u8(new_val), u8(old_val), 0);  // new_val = op(old_val)
        emit_store(arg, new_val);
        emit_iABC(RegOp::MOVE, u8(dst), u8(old_val), 0); // dst = old value
        free_temp(new_val);
    }
    free_temp(old_val);
}

// ─── Assignment expression ─────────────────────────────────────────────────

void AstEmitter::emit_assignment_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    auto aop = static_cast<AsgnOp>(n.data[2]);
    NodeIndex left = n.data[0];
    NodeIndex right = n.data[1];

    if (aop == AsgnAssign) {
        emit_expr(right, dst);            // rhs → dst
        emit_store(left, dst);
        return;
    }

    RegOp cbop = compound_binop(aop);
    if (cbop != RegOp::NOP) {
        int rhs = alloc_temp();
        emit_expr(right, rhs);
        int lhs = alloc_temp();
        emit_load(left, lhs);
        emit_iABC(cbop, u8(lhs), u8(lhs), u8(rhs));
        emit_store(left, lhs);
        emit_iABC(RegOp::MOVE, u8(dst), u8(lhs), 0);
        free_temp(rhs);
        free_temp(lhs);
        return;
    }

    // Logical assignment: ??=, &&=, ||=
    int lhs = alloc_temp();
    emit_load(left, lhs);
    int end_lbl = new_label();
    RegOp jump_op;
    switch (aop) {
    case AsgnNullish: jump_op = RegOp::IS_NULLISH; break;
    case AsgnLand:    jump_op = RegOp::IS_FALSE; break;
    case AsgnLor:     jump_op = RegOp::IS_TRUE; break;
    default:          jump_op = RegOp::IS_FALSE; break;
    }
    emit_jump(jump_op, end_lbl, u8(lhs));

    emit_expr(right, lhs);               // rhs → lhs (overwrite)
    emit_store(left, lhs);

    bind_label(end_lbl);
    emit_iABC(RegOp::MOVE, u8(dst), u8(lhs), 0);
    free_temp(lhs);
}

// ─── Conditional expression (ternary) ──────────────────────────────────────

void AstEmitter::emit_conditional_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    emit_expr(n.data[0], dst);          // condition → dst
    int else_lbl = new_label();
    int end_lbl = new_label();

    emit_jump(RegOp::IS_FALSE, else_lbl, u8(dst));

    int then_val = alloc_temp();
    emit_expr(n.data[1], then_val);
    emit_iABC(RegOp::MOVE, u8(dst), u8(then_val), 0);
    free_temp(then_val);
    emit_jump(RegOp::JMP, end_lbl, 0);

    bind_label(else_lbl);
    int else_val = alloc_temp();
    emit_expr(n.data[2], else_val);
    emit_iABC(RegOp::MOVE, u8(dst), u8(else_val), 0);
    free_temp(else_val);

    bind_label(end_lbl);
}

// ─── Sequence expression ───────────────────────────────────────────────────

void AstEmitter::emit_sequence_expr(NodeIndex node, int dst) {
    auto range = tree_.range(node, 0);
    for (uint32_t i = 0; i < range.len; i++) {
        if (i + 1 < range.len) {
            int tmp = alloc_temp();         // intermediate: evaluate for side effects
            emit_expr(tree_.extras[range.start + i], tmp);
            free_temp(tmp);
        } else {
            emit_expr(tree_.extras[range.start + i], dst);  // last → dst
        }
    }
}

// ─── Member expression ─────────────────────────────────────────────────────

void AstEmitter::emit_member_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    uint32_t flags = n.data[2];
    bool computed = (flags & NF::Computed) != 0;
    bool optional = (flags & NF::Optional) != 0;

    emit_expr(n.data[0], dst);           // object → dst

    if (optional) {
        int end_lbl = new_label();
        emit_jump(RegOp::IS_NULLISH, end_lbl, u8(dst));
        if (computed) {
            int prop = alloc_temp();
            emit_expr(n.data[1], prop);
            emit_iABC(RegOp::GETELEM, u8(dst), u8(dst), u8(prop));
            free_temp(prop);
        } else {
            Atom prop_name = atom_for_span(tree_.span(n.data[1]));
            int ci = cpool_add(e_->atom_to_value(prop_name));
            emit_iABC(RegOp::GETFIELD, u8(dst), u8(dst), u8(ci));
        }
        bind_label(end_lbl);
        return;
    }

    if (computed) {
        int prop = alloc_temp();
        emit_expr(n.data[1], prop);
        emit_iABC(RegOp::GETELEM, u8(dst), u8(dst), u8(prop));
        free_temp(prop);
    } else {
        Atom prop_name = atom_for_span(tree_.span(n.data[1]));
        int ci = cpool_add(e_->atom_to_value(prop_name));
        emit_iABC(RegOp::GETFIELD, u8(dst), u8(dst), u8(ci));
    }
}

// ─── Call expression ────────────────────────────────────────────────────────

void AstEmitter::emit_call_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    NodeIndex callee = n.data[0];
    auto args_range = tree_.range(node, 1);
    bool is_method = (callee != NodeNull && tree_.kind(callee) == NK_MEMBER_EXPR);

    int func_reg = alloc_temp();
    emit_expr(callee, func_reg);

    int this_reg = -1;
    if (is_method) {
        this_reg = alloc_temp();
        Node &mem = tree_.nodes[callee];
        emit_expr(mem.data[0], this_reg);
    }

    int arg_base = next_temp_;
    int argc = 0;
    for (uint32_t i = 0; i < args_range.len; i++) {
        int arg_reg = alloc_temp();
        emit_expr(tree_.extras[args_range.start + i], arg_reg);
        argc++;
    }

    // Free in LIFO: args (reverse), optional this, func
    for (int i = next_temp_ - 1; i >= arg_base; i--) free_temp(i);
    if (is_method) free_temp(this_reg);
    free_temp(func_reg);

    if (is_method)
        emit_iABC(RegOp::CALL_M, u8(dst), u8(func_reg), u8(argc));
    else
        emit_iABC(RegOp::CALL, u8(dst), u8(func_reg), u8(argc));
}

// ─── New expression ────────────────────────────────────────────────────────

void AstEmitter::emit_new_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    NodeIndex callee = n.data[0];
    auto args_range = tree_.range(node, 1);

    int callee_reg = alloc_temp();
    emit_expr(callee, callee_reg);

    int arg_base = next_temp_;
    for (uint32_t i = 0; i < args_range.len; i++) {
        int arg_reg = alloc_temp();
        emit_expr(tree_.extras[args_range.start + i], arg_reg);
    }

    // Free in LIFO: args (reverse), callee
    for (int i = next_temp_ - 1; i >= arg_base; i--) free_temp(i);
    free_temp(callee_reg);

    int argc = static_cast<int>(args_range.len);
    emit_iABC(RegOp::CTOR, u8(dst), u8(callee_reg), u8(argc));
}

// ─── Array expression ──────────────────────────────────────────────────────

void AstEmitter::emit_array_expr(NodeIndex node, int dst) {
    auto range = tree_.range(node, 0);
    emit_iABC(RegOp::NEWARR, u8(dst), 0, 0);

    for (uint32_t i = 0; i < range.len; i++) {
        NodeIndex elem = tree_.extras[range.start + i];
        if (elem == NodeNull) continue;
        int val = alloc_temp();
        if (tree_.kind(elem) == NK_SPREAD)
            emit_expr(tree_.nodes[elem].data[0], val);
        else
            emit_expr(elem, val);
        emit_iABC(RegOp::APPEND, u8(dst), u8(val), 0);
        free_temp(val);
    }
}

// ─── Object expression ─────────────────────────────────────────────────────

void AstEmitter::emit_object_expr(NodeIndex node, int dst) {
    auto range = tree_.range(node, 0);
    emit_iABx(RegOp::NEWOBJ, u8(dst), 0);

    for (uint32_t i = 0; i < range.len; i++) {
        NodeIndex prop = tree_.extras[range.start + i];
        Node &pn = tree_.nodes[prop];
        uint32_t flags = pn.data[2];
        bool computed = (flags & NF::Computed) != 0;
        bool shorthand = (flags & NF::Shorthand) != 0;

        if (computed) {
            int key = alloc_temp();
            emit_expr(pn.data[0], key);
            int val = alloc_temp();
            emit_expr(pn.data[1], val);
            emit_iABC(RegOp::DEFINE_ELEM, u8(dst), u8(key), u8(val));
            free_temp(val);
            free_temp(key);
        } else {
            Atom key_name;
            if (shorthand)
                key_name = atom_for_span(tree_.span(pn.data[1]));
            else
                key_name = atom_for_span(tree_.span(pn.data[0]));
            int ci = cpool_add(e_->atom_to_value(key_name));
            int val = alloc_temp();
            emit_expr(pn.data[1], val);
            emit_iABC(RegOp::DEFINE_FIELD, u8(dst), u8(ci), u8(val));
            free_temp(val);
        }
    }
}

// ─── Function expression ───────────────────────────────────────────────────

void AstEmitter::emit_func_expr(NodeIndex node, int dst) {
    auto child = std::make_unique<AstEmitter>(e_, tree_, source_, source_len_);
    child->parent_ = this;
    child->bindings_.parent_table = &this->bindings_;
    Bytecode *bc = child->emit_function(node, true);

    int placeholder = cpool_add(Value::bytecode(nullptr));
    emit_iABx(RegOp::FCLOSURE, u8(dst), u16(placeholder));

    for (int i = 0; i < static_cast<int>(cpool_.size()); i++) {
        if (cpool_[i].is_bytecode() && cpool_[i].as<Bytecode>() == nullptr) {
            cpool_[i] = Value::bytecode(bc);
            break;
        }
    }
    child.release();
}

// ─── Arrow function ────────────────────────────────────────────────────────

void AstEmitter::emit_arrow_func(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    Span sp = tree_.span(node);
    NodeIndex func_node = tree_.alloc(NK_FUNCTION, sp,
        n.data[0], n.data[1], n.data[2]);
    emit_func_expr(func_node, dst);
}

// ─── Paren expression ──────────────────────────────────────────────────────

void AstEmitter::emit_paren_expr(NodeIndex node, int dst) {
    Node &n = tree_.nodes[node];
    emit_expr(n.data[0], dst);
}

// ─── Template literal ──────────────────────────────────────────────────────

void AstEmitter::emit_template_lit(NodeIndex node, int dst) {
    auto quasis_range = tree_.range(node, 0);
    auto exprs_range = tree_.range(node, 2);

    if (exprs_range.len == 0) {
        if (quasis_range.len > 0) {
            NodeIndex q = tree_.extras[quasis_range.start];
            Span qsp = tree_.span(q);
            auto sv = src_slice(qsp);
            int ci = cpool_add(StrPrim::create(sv));
            emit_iABx(RegOp::LOADK, u8(dst), u16(ci));
        } else {
            emit_iABx(RegOp::LOADNULL, u8(dst), 0);
        }
        return;
    }

    // First quasi → dst
    NodeIndex q = tree_.extras[quasis_range.start];
    Span qsp = tree_.span(q);
    int ci = cpool_add(StrPrim::create(src_slice(qsp)));
    emit_iABx(RegOp::LOADK, u8(dst), u16(ci));

    for (uint32_t i = 0; i < exprs_range.len; i++) {
        int expr_val = alloc_temp();
        emit_expr(tree_.extras[exprs_range.start + i], expr_val);
        emit_iABC(RegOp::ADD, u8(dst), u8(dst), u8(expr_val));
        free_temp(expr_val);

        if (i + 1 < quasis_range.len) {
            NodeIndex qn = tree_.extras[quasis_range.start + i + 1];
            auto sv = src_slice(tree_.span(qn));
            if (!sv.empty()) {
                ci = cpool_add(StrPrim::create(sv));
                int quasi_reg = alloc_temp();
                emit_iABx(RegOp::LOADK, u8(quasi_reg), u16(ci));
                emit_iABC(RegOp::ADD, u8(dst), u8(dst), u8(quasi_reg));
                free_temp(quasi_reg);
            }
        }
    }
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
        int obj = alloc_temp();
        emit_expr(n.data[0], obj);
        if (computed) {
            int prop = alloc_temp();
            emit_expr(n.data[1], prop);
            emit_iABC(RegOp::SETELEM, u8(obj), u8(prop), u8(value_reg));
            free_temp(prop);
        } else {
            Atom prop_name = atom_for_span(tree_.span(n.data[1]));
            int ci = cpool_add(e_->atom_to_value(prop_name));
            emit_iABC(RegOp::SETFIELD, u8(obj), u8(ci), u8(value_reg));
        }
        free_temp(obj);
        return;
    }
}

void AstEmitter::emit_load(NodeIndex target, int dst) {
    if (target == NodeNull) {
        emit_iABx(RegOp::LOADUNDEF, u8(dst), 0);
        return;
    }
    NodeKind k = tree_.kind(target);
    if (k == NK_IDENT_REF || k == NK_BINDING_IDENT) {
        emit_ident_ref(target, dst);
        return;
    }
    if (k == NK_MEMBER_EXPR) {
        emit_member_expr(target, dst);
        return;
    }
    emit_expr(target, dst);
}

bool AstEmitter::is_member_expr(NodeIndex node) const {
    return node != NodeNull && tree_.kind(node) == NK_MEMBER_EXPR;
}

RegOp AstEmitter::binop_to_reg(BinOp bop) const {
    return qjsp::binop_to_reg(bop);
}

} // namespace qjsp
