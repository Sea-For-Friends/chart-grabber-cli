// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (the RemObjects bytecode this replaces has no equivalent AST; legacy compiles
// straight to a VM program).
//
// Plain aggregates owned by an `Arena`, never `QObject`, never virtual: the interpreter (a later
// slice) dispatches on `Node::kind` with a `switch` - a visitor with 24 virtuals would be slower
// and longer, and idioms.md's "no abstraction without two users" applies.

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace cg::script {

// Owns every AST node allocated during one compile() call, so `CompiledScript` frees the whole
// tree in one call (spec 2.5) by destroying the `Arena` (indirectly, via `SymbolTable`, see
// parser.h). Deliberately not a `std::pmr::monotonic_buffer_resource`: several node kinds hold a
// `std::vector` member and a monotonic resource never runs destructors on release - this keeps
// the "free it all in one call" property without leaking each node's vector.
class Arena {
public:
    template <class T, class... Args> T* make(Args&&... args) {
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        cleanups_.push_back([kept = std::move(owned)]() mutable { kept.reset(); });
        return raw;
    }

    // Stamps position at construction (T-0013 rework F7): replaces the parser's repeated
    // `auto* n = make<T>(); n->line = line; n->column = column;` pair with one call. Overload
    // resolution prefers this fixed-arity template over the forwarding variadic one above for a
    // `make<T>(line, column)` call, since every AST node's default constructor takes no arguments.
    template <class T> T* make(int line, int column) {
        T* node = make<T>();
        node->line = line;
        node->column = column;
        return node;
    }

private:
    // `std::function` requires its target to be copy-constructible; the captured `unique_ptr<T>`
    // is not, so this needs the C++23 move-only variant.
    std::vector<std::move_only_function<void()>> cleanups_;
};

enum class NodeKind : std::uint8_t {
    // expressions
    IntLit,
    RealLit,
    StrLit,
    BoolLit,
    NilLit,
    VarRef,
    Unary,
    Binary,
    Call,
    Index,
    Field,
    ArrayLit,
    // statements
    Assign,
    IndexAssign,
    FieldAssign,
    CallStmt,
    If,
    Case,
    ForTo,
    While,
    RepeatUntil,
    TryFinally,
    Compound,
    Exit,
    Break,
    Continue,
    Empty,
    With,
};

struct Node {
    NodeKind kind;
    int line = 0;
    int column = 0;

    explicit Node(NodeKind aKind) : kind(aKind) {}
    // Never virtual (idioms.md/coding standard): `Arena::make<T>` keeps the exact derived
    // `unique_ptr<T>` alive for destruction, so nothing ever deletes a `Node` through this base.
};

// --- expressions -------------------------------------------------------------------------------
struct IntLit : Node {
    IntLit() : Node(NodeKind::IntLit) {}
    qint64 value = 0;
};
struct RealLit : Node {
    RealLit() : Node(NodeKind::RealLit) {}
    double value = 0.0;
};
struct StrLit : Node {
    StrLit() : Node(NodeKind::StrLit) {}
    QByteArray value; // already decoded, byte string
};
struct BoolLit : Node {
    BoolLit() : Node(NodeKind::BoolLit) {}
    bool value = false;
};
struct NilLit : Node {
    NilLit() : Node(NodeKind::NilLit) {}
};
struct VarRef : Node {
    VarRef() : Node(NodeKind::VarRef) {}
    int slot = -1; // resolved at compile time - see parser.h SymbolTable
};

enum class UnaryOp : std::uint8_t { Plus, Minus, Not };
enum class BinaryOp : std::uint8_t {
    Add,
    Sub,
    Mul,
    RealDiv,
    IntDiv,
    Mod,
    Shl,
    Shr,
    And,
    Or,
    Xor,
    Eq,
    Ne,
    Lt,
    Gt,
    Le,
    Ge,
};
// Where a Call's callee index (below) is looked up. `Method` calls encode the ScriptVars slot of
// the interface instance into `callee` (slot * kMethodSlotStride + methodIndex - see parser.cpp)
// since the v1 grammar has no user-declared interface-typed variables; the seven interfaces are
// only ever reached through one of the seven fixed ScriptVars slots.
enum class CallKind : std::uint8_t { Builtin, Registered, UserRoutine, Method };

struct Unary : Node {
    Unary() : Node(NodeKind::Unary) {}
    UnaryOp op = UnaryOp::Plus;
    Node* operand = nullptr;
};
struct Binary : Node {
    Binary() : Node(NodeKind::Binary) {}
    BinaryOp op = BinaryOp::Add;
    Node* lhs = nullptr;
    Node* rhs = nullptr;
};
struct Call : Node {
    Call() : Node(NodeKind::Call) {}
    int callee = -1;
    CallKind callKind = CallKind::Builtin;
    std::vector<Node*> args;
};
struct Index : Node { // s[i] and arr[i]
    Index() : Node(NodeKind::Index) {}
    Node* base = nullptr;
    Node* index = nullptr;
};
struct Field : Node {
    Field() : Node(NodeKind::Field) {}
    Node* base = nullptr;
    int fieldIndex = -1; // index into the base's RegisteredType field list
};
// `[e1, e2, ...]` (also `[]`): an open-array/set constructor. Corpus-driven addition beyond the
// ADR-0003/spec 3.1 grammar (Google/GoogleSat.zmp's `Format('...', [Random(4), ...])`) - flagged
// for the Architect to fold into spec 3.1/3.5 as a "G6" correction; see the T-0013 handoff.
struct ArrayLit : Node {
    ArrayLit() : Node(NodeKind::ArrayLit) {}
    std::vector<Node*> elements;
};

// --- statements --------------------------------------------------------------------------------
struct Assign : Node { // includes the implicit `Result := ...` inside a routine body
    Assign() : Node(NodeKind::Assign) {}
    int slot = -1;
    Node* value = nullptr;
};
struct IndexAssign : Node { // s[i] := c
    IndexAssign() : Node(NodeKind::IndexAssign) {}
    Node* target = nullptr;
    Node* index = nullptr;
    Node* value = nullptr;
};
struct FieldAssign : Node {
    FieldAssign() : Node(NodeKind::FieldAssign) {}
    Node* target = nullptr;
    int fieldIndex = -1;
    Node* value = nullptr;
};
struct CallStmt : Node {
    CallStmt() : Node(NodeKind::CallStmt) {}
    Call* call = nullptr;
};
struct If : Node {
    If() : Node(NodeKind::If) {}
    Node* cond = nullptr;
    Node* thenBranch = nullptr;
    Node* elseBranch = nullptr; // may be null
};
struct CaseLabel {
    qint64 lo = 0;
    qint64 hi = 0; // hi == lo for a single label
};
struct CaseArm {
    std::vector<CaseLabel> labels;
    Node* body = nullptr;
};
struct Case : Node {
    Case() : Node(NodeKind::Case) {}
    Node* subject = nullptr;
    std::vector<CaseArm> arms;
    Node* elseBranch = nullptr;
};
struct ForTo : Node {
    ForTo() : Node(NodeKind::ForTo) {}
    int slot = -1;
    Node* from = nullptr;
    Node* to = nullptr;
    bool downto = false;
    Node* body = nullptr;
};
struct While : Node {
    While() : Node(NodeKind::While) {}
    Node* cond = nullptr;
    Node* body = nullptr;
};
struct RepeatUntil : Node {
    RepeatUntil() : Node(NodeKind::RepeatUntil) {}
    std::vector<Node*> body;
    Node* cond = nullptr;
};
struct TryFinally : Node {
    TryFinally() : Node(NodeKind::TryFinally) {}
    std::vector<Node*> body;
    std::vector<Node*> handler;
    bool isExcept = false; // `except` swallows; `finally` always re-raises (spec 3.3 S9)
};
struct Compound : Node {
    Compound() : Node(NodeKind::Compound) {}
    std::vector<Node*> stmts;
};
struct Exit : Node {
    Exit() : Node(NodeKind::Exit) {}
};
struct Break : Node {
    Break() : Node(NodeKind::Break) {}
};
struct Continue : Node {
    Continue() : Node(NodeKind::Continue) {}
};
struct Empty : Node {
    Empty() : Node(NodeKind::Empty) {}
};
struct With : Node {
    With() : Node(NodeKind::With) {}
    Node* subject = nullptr;
    int scopeId = -1;
    Node* body = nullptr;
};

// --- declarations ------------------------------------------------------------------------------
enum class ParamMode : std::uint8_t { Value, Const, Var, Out };

struct Routine {
    QString name;
    int firstParamSlot = 0;
    int paramCount = 0;
    std::vector<ParamMode> paramModes;
    int resultSlot = -1; // -1 for a procedure
    int frameSize = 0;   // slots for params + locals + Result
    Node* body = nullptr;
};

struct Program {
    std::vector<Routine> routines; // in declaration order
    Node* main = nullptr;          // the final compound statement
    int globalFrameSize = 0;
};

} // namespace cg::script
