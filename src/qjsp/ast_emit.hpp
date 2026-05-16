#pragma once

#include "qjsp/ast.hpp"
#include "qjsp/binding.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/reg_opcode.hpp"
#include "qjsp/reg_opcode_info.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qjsp {

struct Engine;

// ─── Label / patch ──────────────────────────────────────────────────────────

struct EmitLabel {
    int pos = -1;
    int ref_count = 0;
};

struct EmitPatch {
    int instr_idx;
    int label_id;
};

// ─── Break/continue target ──────────────────────────────────────────────────

struct EmitBreakTarget {
    int break_label;
    int continue_label;
    Atom label;
};

// ─── AstEmitter ─────────────────────────────────────────────────────────────

class AstEmitter {
public:
    AstEmitter(Engine *e, AstTree &tree, const uint8_t *source, uint32_t source_len);
    ~AstEmitter();

    Bytecode *emit_program(NodeIndex root);

private:
    Engine *e_;
    AstTree &tree_;
    const uint8_t *source_;
    uint32_t source_len_;

    // ── Parent emitter (for nested functions) ───────────────────────────
    AstEmitter *parent_ = nullptr;

    // ── Instruction buffer ──────────────────────────────────────────────
    std::vector<uint32_t> code_;

    // ── Labels and patches ──────────────────────────────────────────────
    std::vector<EmitLabel> labels_;
    std::vector<EmitPatch> patches_;

    // ── Constant pool ───────────────────────────────────────────────────
    std::vector<Value> cpool_;

    // ── Register allocator ──────────────────────────────────────────────
    int next_temp_ = 0;
    int max_temp_ = 0;

    int this_reg() const { return 0; }
    int first_temp() const { return bindings_.frame_regs(); }

    int alloc_temp() {
        int r = next_temp_++;
        if (next_temp_ > max_temp_) max_temp_ = next_temp_;
        return r;
    }
    void free_temp() { next_temp_--; }
    void ensure_reg(int r) {
        if (r + 1 > max_temp_) max_temp_ = r + 1;
    }

    // ── Binding table ───────────────────────────────────────────────────
    BindingTable bindings_;

    // ── Break/continue stack ────────────────────────────────────────────
    std::vector<EmitBreakTarget> break_stack_;

    // ── Function metadata ───────────────────────────────────────────────
    Atom func_name_ = kAtomNull;
    bool is_expr_ = false;
    int eval_ret_reg_ = -1; // for top-level eval: last expr stmt → this reg

    // ── Instruction emission ────────────────────────────────────────────

    void emit_iABC(RegOp rop, uint8_t a, uint8_t b, uint8_t c) {
        code_.push_back(Instruction::iABC(qjsp::op(rop), a, b, c).raw);
    }
    void emit_iABx(RegOp rop, uint8_t a, uint16_t bx) {
        code_.push_back(Instruction::iABx(qjsp::op(rop), a, bx).raw);
    }
    void emit_iAsBx(RegOp rop, uint8_t a, int16_t sbx) {
        code_.push_back(Instruction::iAsBx(qjsp::op(rop), a, sbx).raw);
    }

    int instr_count() const { return static_cast<int>(code_.size()); }

    // Labels
    int new_label() {
        int id = static_cast<int>(labels_.size());
        labels_.push_back({});
        return id;
    }
    void bind_label(int id) {
        labels_[id].pos = instr_count();
    }
    int emit_jump(RegOp jump_op, int label_id, uint8_t reg_a) {
        if (label_id < 0) return -1;
        labels_[label_id].ref_count++;
        emit_iAsBx(jump_op, reg_a, 0);
        int idx = instr_count() - 1;
        patches_.push_back({idx, label_id});
        return idx;
    }
    void patch_labels() {
        for (auto &p : patches_) {
            int target = labels_[p.label_id].pos;
            int rel = target - p.instr_idx - 1;
            uint32_t raw = code_[p.instr_idx];
            RegOp opc = static_cast<RegOp>(raw & 0xFF);
            code_[p.instr_idx] = Instruction::iAsBx(static_cast<uint8_t>(opc), (raw >> 8) & 0xFF, static_cast<int16_t>(rel)).raw;
        }
    }

    // Constant pool
    int cpool_add(Value v) {
        for (int i = 0; i < static_cast<int>(cpool_.size()); i++)
            if (cpool_[i].data == v.data) return i;
        cpool_.push_back(v);
        return static_cast<int>(cpool_.size()) - 1;
    }

    // ── Variable resolution ─────────────────────────────────────────────
    // All variable lookups go through bindings_.lookup() / lookup_captured()

    // ── Scope management ────────────────────────────────────────────────
    // scope_level_ tracks emission-time scope nesting (no-op currently, for future PushScope)
    int scope_level_ = -1;
    void push_scope();
    void pop_scope();

    // ── Break/continue ──────────────────────────────────────────────────

    void push_break(int lbl, int lbc, Atom label = kAtomNull) {
        break_stack_.push_back({lbl, lbc, label});
    }
    void pop_break() { break_stack_.pop_back(); }
    int find_break_label(Atom label, bool is_continue) const;

    // ── Source text helpers ─────────────────────────────────────────────

    std::string_view src_slice(Span sp) const {
        return {reinterpret_cast<const char *>(source_) + sp.start, sp.end - sp.start};
    }
    Atom atom_for_span(Span sp) const;
    Atom atom_for_str(std::string_view sv) const;

    // ── Scope analysis ──────────────────────────────────────────────────

    void analyze_scope(NodeIndex body);
    void collect_vars(NodeIndex node, bool is_var);
    void collect_declarator(NodeIndex decl_node, uint32_t kind);

    // ── AST walk ────────────────────────────────────────────────────────

    Bytecode *emit_function(NodeIndex func_node, bool is_expr);
    void emit_body(IndexRange stmts);

    void emit_stmt(NodeIndex node);
    int emit_expr(NodeIndex node); // returns result register

    // Expression emitters
    int emit_numeric_lit(NodeIndex node);
    int emit_string_lit(NodeIndex node);
    int emit_bool_lit(NodeIndex node);
    int emit_null_lit();
    int emit_ident_ref(NodeIndex node);
    int emit_binary_expr(NodeIndex node);
    int emit_logical_expr(NodeIndex node);
    int emit_unary_expr(NodeIndex node);
    int emit_update_expr(NodeIndex node);
    int emit_assignment_expr(NodeIndex node);
    int emit_conditional_expr(NodeIndex node);
    int emit_sequence_expr(NodeIndex node);
    int emit_member_expr(NodeIndex node);
    int emit_call_expr(NodeIndex node);
    int emit_new_expr(NodeIndex node);
    int emit_array_expr(NodeIndex node);
    int emit_object_expr(NodeIndex node);
    int emit_func_expr(NodeIndex node);
    int emit_arrow_func(NodeIndex node);
    int emit_paren_expr(NodeIndex node);
    int emit_this_expr();
    int emit_template_lit(NodeIndex node);

    // Statement emitters
    void emit_expr_stmt(NodeIndex node);
    void emit_block_stmt(NodeIndex node);
    void emit_if_stmt(NodeIndex node);
    void emit_for_stmt(NodeIndex node);
    void emit_for_in_stmt(NodeIndex node);
    void emit_for_of_stmt(NodeIndex node);
    void emit_while_stmt(NodeIndex node);
    void emit_do_while_stmt(NodeIndex node);
    void emit_return_stmt(NodeIndex node);
    void emit_throw_stmt(NodeIndex node);
    void emit_try_stmt(NodeIndex node);
    void emit_var_decl(NodeIndex node);
    void emit_break_stmt(NodeIndex node);
    void emit_continue_stmt(NodeIndex node);
    void emit_labeled_stmt(NodeIndex node);
    void emit_switch_stmt(NodeIndex node);

    // LValue handling
    void emit_store(NodeIndex target, int value_reg);
    int emit_load(NodeIndex target);
    bool is_member_expr(NodeIndex node) const;

    // BinOp → RegOp
    RegOp binop_to_reg(BinOp bop) const;

    // Freeze into Bytecode
    Bytecode *freeze();
};

} // namespace qjsp
