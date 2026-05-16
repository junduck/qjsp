#include "qjsp/binding.hpp"
#include <algorithm>

namespace qjsp {

// ─── Scope management ───────────────────────────────────────────────────────

void BindingTable::begin_scope() {
    int idx = (int)scopes_.size();
    int parent = (idx > 0) ? current_scope_ : -1;
    int level = (parent >= 0) ? scopes_[parent].level + 1 : 0;
    scopes_.push_back({parent, -1, level});
    current_scope_ = idx;
}

void BindingTable::end_scope() {
    if (current_scope_ >= 0)
        current_scope_ = scopes_[current_scope_].parent_idx;
}

// ─── Internal helpers ───────────────────────────────────────────────────────

bool BindingTable::has_in_scope(Atom name, int scope_idx) const {
    // Walk the linked list of entries in the given scope.
    // If scope_idx == -1, treat as lookup failed (edge case for empty table).
    if (scope_idx < 0 || scope_idx >= (int)scopes_.size()) return false;
    int cur = scopes_[scope_idx].first_entry;
    while (cur >= 0) {
        if (entries_[cur].name == name) return true;
        cur = entries_[cur].scope_next;
    }
    return false;
}

int BindingTable::add_entry(Atom name, BindKind kind, int scope_idx) {
    int slot;
    if (kind == BindKind::Arg) {
        slot = arg_slot(arg_count_);
        arg_count_++;
    } else {
        slot = var_slot(var_count_);
        var_count_++;
    }

    int ei = (int)entries_.size();
    entries_.push_back({name, kind, slot, -1});

    // Link into scope's linked list
    if (scope_idx >= 0 && scope_idx < (int)scopes_.size()) {
        entries_[ei].scope_next = scopes_[scope_idx].first_entry;
        scopes_[scope_idx].first_entry = ei;
    }

    return ei;
}

// ─── Registration ───────────────────────────────────────────────────────────

bool BindingTable::add_argument(Atom name) {
    // Check for duplicate arg names
    for (int i = 0; i < arg_count_; i++) {
        if (entries_[i].name == name) return false;
    }
    // If no scopes yet, create the root scope
    if (scopes_.empty()) begin_scope();
    add_entry(name, BindKind::Arg, 0);
    return true;
}

bool BindingTable::add_var(Atom name) {
    // Var declarations are function-scoped (always scope 0).
    // Skip if already declared in function scope.
    if (has_in_scope(name, 0)) return false;

    // Also check arguments
    for (int i = 0; i < arg_count_; i++) {
        if (entries_[i].name == name) return false;
    }

    // Ensure scope 0 exists
    if (scopes_.empty()) begin_scope();

    add_entry(name, BindKind::Var, 0);
    return true;
}

bool BindingTable::add_let(Atom name) {
    if (has_in_scope(name, current_scope_)) return false;
    add_entry(name, BindKind::Let, current_scope_);
    return true;
}

bool BindingTable::add_const(Atom name) {
    if (has_in_scope(name, current_scope_)) return false;
    add_entry(name, BindKind::Const, current_scope_);
    return true;
}

bool BindingTable::add_function(Atom name) {
    return add_var(name);
}

// ─── seal ───────────────────────────────────────────────────────────────────

void BindingTable::seal() {
    sealed_ = true;
    name_map_.clear();
    for (int i = 0; i < (int)entries_.size(); i++) {
        // Latest entry wins for shadowing (vars added later override earlier)
        name_map_[entries_[i].name] = i;
    }
    for (int i = 0; i < (int)upvalue_entries_.size(); i++) {
        name_map_[upvalue_entries_[i].name] = i + (int)entries_.size();
    }
}

// ─── Lookup ─────────────────────────────────────────────────────────────────

bool BindingTable::has(Atom name) const {
    for (const auto& e : entries_)
        if (e.name == name) return true;
    for (const auto& e : upvalue_entries_)
        if (e.name == name) return true;
    return false;
}

const Binding* BindingTable::lookup(Atom name) const {
    auto it = name_map_.find(name);
    if (it == name_map_.end()) return nullptr;

    int idx = it->second;
    if (idx < (int)entries_.size())
        return &entries_[idx];
    return &upvalue_entries_[idx - (int)entries_.size()];
}

const Binding* BindingTable::lookup_captured(Atom name) {
    // Try local first
    const Binding* b = lookup(name);
    if (b) return b;

    // Walk parent chain for upvalues
    BindingTable* cur = parent_table;
    while (cur) {
        const Binding* pb = cur->lookup(name);
        if (pb) {
            // Found in parent — check for existing upvalue entry
            for (size_t i = 0; i < upvalue_entries_.size(); i++) {
                if (upvalue_entries_[i].name == name)
                    return &upvalue_entries_[i];
            }
            for (size_t i = 0; i < upvalues_.size(); i++) {
                if (upvalues_[i].var_name == name)
                    return &upvalue_entries_[i];
            }

            // Create upvalue
            int uv_idx = next_upval_++;

            // Find the parent entry index (for ClosureVar.var_idx)
            int parent_entry_idx = -1;
            auto& parent_entries = cur->entries_;
            for (int i = 0; i < (int)parent_entries.size(); i++) {
                if (parent_entries[i].name == name && parent_entries[i].kind == pb->kind) {
                    parent_entry_idx = i;
                    break;
                }
            }

            ClosureVar cv{};
            cv.var_name = name;
            cv.var_idx = (uint16_t)(parent_entry_idx >= 0 ? parent_entry_idx : 0);
            cv.set_closure_type(
                pb->kind == BindKind::Arg ? ClosureType::arg : ClosureType::local);
            cv.set_is_const(is_const_kind(pb->kind));
            cv.set_is_lexical(is_lexical_kind(pb->kind));
            upvalues_.push_back(cv);

            Binding uv_binding{name, BindKind::Upvalue, uv_idx, -1};
            upvalue_entries_.push_back(uv_binding);

            // Rebuild name map to include new upvalue entry
            if (sealed_) seal();

            return &upvalue_entries_.back();
        }
        cur = cur->parent_table;
    }

    return nullptr; // not found — global reference
}

} // namespace qjsp
