#pragma once

#include "qjsp/atom.hpp"
#include "qjsp/bytecode.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace qjsp {

// ─── Binding kind ────────────────────────────────────────────────────────────

enum class BindKind : uint8_t {
    Arg,     // function parameter — slot is register
    Var,     // var declaration (function-scoped) — slot is register
    Let,     // let declaration (block-scoped, mutable) — slot is register
    Const,   // const declaration (block-scoped, immutable) — slot is register
    Upvalue, // captured from enclosing scope — slot is upvalue index
};

inline bool is_lexical_kind(BindKind k) { return k == BindKind::Let || k == BindKind::Const; }
inline bool is_const_kind(BindKind k)  { return k == BindKind::Const; }
inline bool is_local_kind(BindKind k)  { return k != BindKind::Upvalue; }

// ─── Binding — maps a name to a VM slot ──────────────────────────────────────

struct Binding {
    Atom name;
    BindKind kind;
    int slot;        // register for Arg/Var/Let/Const; upvalue index for Upvalue
    int scope_next;  // linked list: next binding in same scope (-1 = end)
};

// ─── BindScope — a block or function scope ───────────────────────────────────

struct BindScope {
    int parent_idx;  // index of enclosing scope (-1 = root)
    int first_entry; // index of first binding in linked list (-1 = empty)
    int level;       // nesting depth (0 = function/top-level)
};

// ─── BindingTable ────────────────────────────────────────────────────────────
//
// Built during the scope analysis pass, queried during code generation.
//
// Lifecycle:
//   1. add_argument() for each formal parameter
//   2. Scope-analysis walk: begin_scope() / end_scope() around blocks,
//      add_var() / add_let() / add_const() / add_function() for declarations
//   3. seal() — locks the table, builds O(1) name lookup
//   4. lookup(name)  — used during code emission for local variable access
//   5. lookup_captured(name) — walks parent tables for upvalues
//
// Register layout:
//   R[0]               = this
//   R[1 .. 1+a-1]      = arguments (a = arg_count)
//   R[1+a .. 1+a+v-1]  = local variables (v = var_count)

class BindingTable {
public:
    BindingTable() = default;

    // ── Registration (scope analysis pass) ─────────────────────────────

    void begin_scope();
    void end_scope();

    bool add_argument(Atom name);   // false if duplicate arg name
    bool add_var(Atom name);        // false if name already function-scoped
    bool add_let(Atom name);        // false if name exists in current scope
    bool add_const(Atom name);      // false if name exists in current scope
    bool add_function(Atom name);   // same as add_var

    // Lock table and build O(1) name map. Must be called before lookup().
    void seal();

    // ── Query (code generation pass) ────────────────────────────────────

    // Check if a name has been declared in this table (works before seal()).
    bool has(Atom name) const;

    // Look up a name in this function's scope chain.
    // Returns nullptr if not found (caller should emit global access).
    const Binding* lookup(Atom name) const;

    // Look up a name, walking parent tables for upvalues.
    // Creates ClosureVar entries in the current table as needed.
    // Returns nullptr if not found anywhere (caller should emit global access).
    const Binding* lookup_captured(Atom name);

    // ── Accessors ───────────────────────────────────────────────────────

    int arg_count()       const { return arg_count_; }
    int var_count()       const { return var_count_; }
    int entry_count()     const { return (int)entries_.size(); }
    int upvalue_count()   const { return (int)upvalues_.size(); }
    int current_scope()   const { return current_scope_; }

    int arg_slot(int arg_idx) const { return 1 + arg_idx; }
    int var_slot(int var_idx) const { return 1 + arg_count_ + var_idx; }

    // Total non-temp registers: 1 (this) + args + vars
    int frame_regs() const { return 1 + arg_count_ + var_count_; }

    const std::vector<Binding>&    entries()      const { return entries_; }
    const std::vector<ClosureVar>& closure_vars() const { return upvalues_; }

    // Parent table — set by AstEmitter when nesting functions
    BindingTable* parent_table = nullptr;

private:
    std::vector<Binding> entries_;
    std::vector<BindScope> scopes_;
    std::vector<ClosureVar> upvalues_;
    std::vector<Binding> upvalue_entries_; // parallel entries for Upvalue bindings
    std::unordered_map<Atom, int> name_map_;
    int arg_count_ = 0;
    int var_count_ = 0;
    int next_upval_ = 0;
    bool sealed_ = false;
    int current_scope_ = -1;

    int  add_entry(Atom name, BindKind kind, int scope_idx);
    bool has_in_scope(Atom name, int scope_idx) const;
};

} // namespace qjsp
