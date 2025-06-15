#include "common/utils.h"

#define SOLUTION_FOUND(assignment) \
  std::cerr << "\n\nSolution Found after " << duration() << ":\n"; \
  assignment.print(std::cout, true); \
  exit(0);

#define NO_SOLUTION(details) std::cerr << "\n\nNo Solution possible after " << duration() << ": " << details << "\n"; std::cout << "UNSAT\n"; exit(1);

// --------------------- Memory Management -----------------------------------------

constexpr size_t BUFFER_SIZE = 1048576u;

/// Allocates instances of Type in fixed size chunks. This avoids many small allocations
/// TODO: Reclaim unused chunks
template<typename Type>
class Memory {
public:
    Memory() {
        m_memory.emplace_back().reserve(BUFFER_SIZE);
    }

    std::span<Type> allocate(size_t count) {
        DEV_ASSURE(count < BUFFER_SIZE, "Buffer too large");
        DEV_ASSURE(!m_memory.empty(), "Missing chunk");

        if (count + m_memory.back().size() > BUFFER_SIZE)
            m_memory.emplace_back().reserve(BUFFER_SIZE);
        
        auto& chunk = m_memory.back();
        chunk.resize(chunk.size() + count);

        return std::span<Type>(chunk.end() - count, chunk.end());
    }

    std::span<Type> allocate(const std::span<Type>& values)
    {
        auto target = allocate(values.size());
        copy(values.begin(), values.end(), /* into */ target.begin());
        return target;
    }

    Type& allocate(Type&& one) {
        if (1 + m_memory.back().size() > BUFFER_SIZE)
            m_memory.emplace_back().reserve(BUFFER_SIZE);
        
        auto& chunk = m_memory.back();
        return chunk.emplace_back(std::move(one));
    }

    std::span<Type> all() {
        ASSURE(m_memory.size() == 1, "Memory spans multiple pages");
        return std::span<Type>(m_memory.back().begin(), m_memory.back().end());
    }

private:
    std::vector<std::vector<Type>> m_memory;
};

// --------------------- Literals -----------------------------------------

// A Variable ID in [1, max_uint32)
using VariableID = uint32_t;
// A Literal ID in (-max_uint32, max_uint32)
// -N = NOT N
using LiteralID = int;
// A LiteralID that does not exist. Can be used to delete literals in place
constexpr LiteralID NO_LITERAL = 0;

VariableID toVariable(LiteralID literal) {
    return abs(literal);
}

LiteralID toLiteral(VariableID variable, bool negate) {
    return negate ? -variable : variable;
}

bool isNegated(LiteralID literal) {
    return literal < 0;
}

// An assignment of a Variable, Literal or Predicate
struct Assignment {
    // The value is assigned or unassigned
    bool assigned = false;
    // The truth value
    bool value;
};

std::ostream& operator<<(std::ostream& out, Assignment assignment) {
    if (assignment.assigned) {
        out << (assignment.value ? "T" : "F");
    } else out << "?";
    return out;
}

// Stores assignments for all literals (=variables)
// As storage a bitmask is used
class LiteralAssignment {
public:
    void setMaxVariable(VariableID max) {
        m_assignments.resize(max * 2);
    }

    VariableID getMaxVariable() const {
        return (m_assignments.size() / 2);
    }

    Assignment getVariableAssignment(VariableID id) const {
        if (id > getMaxVariable()) {
            return { .assigned = false, .value = false };
        }

        return {
            .assigned = m_assignments.at((id - 1) * 2),
            .value = m_assignments.at((id - 1) * 2 + 1)
        };
    }

    Assignment getLiteralAssignment(LiteralID literal) const {
        auto assignment = getVariableAssignment(toVariable(literal));
        return { .assigned = assignment.assigned, .value = isNegated(literal) ? !assignment.value : assignment.value };
    }

    void assignVariable(VariableID id, bool value, bool overwrite = false) {
        // TODO: By parsing the header, we can avoid growing here
        if (id > getMaxVariable()) setMaxVariable(id);

        DEV_ASSURE(overwrite || !getVariableAssignment(id).assigned, "Duplicate Assignment to " << id);
        m_assignments.at((id - 1) * 2) = true;
        m_assignments.at((id - 1) * 2 + 1) = value;
    }

    void unassignVariable(VariableID id) {
        DEV_ASSURE(getVariableAssignment(id).assigned, "Unassigning not assigned " << id);
        m_assignments.at((id - 1) * 2) = false;
    }

    // Assigns the literal, i.e. assumes that a = True or NOT(a) = True
    void assignLiteral(LiteralID literal, bool value, bool overwrite = false) {
        assignVariable(toVariable(literal), isNegated(literal) ? !value : value, overwrite);
    }

    void print(std::ostream& out, bool asAssignment = false) const {
        for (VariableID i = 1; i <= getMaxVariable(); i++) {
            auto assignment = getVariableAssignment(i);
            if (assignment.assigned) {
                out << (assignment.value ? "" : "-") << i << " ";
                if (asAssignment) out << "0 ";
            }
        }

        if (asAssignment) out << "\n\n";
    }
private:
    std::vector<bool> m_assignments;
};

std::ostream& operator<<(std::ostream& out, const LiteralAssignment& assignment) {
    assignment.print(out);
    return out;
}

// --------------------- Node -----------------------------------------

enum class NODE_TYPE {
    LITERAL_OR = 1,
    LITERAL_AND = 2,

    LITERAL_END = 3, // Marker - All Literals must be before

    OR = 4,
    AND = 5,

    NO_OP = 6,
};

std::ostream& operator<<(std::ostream& out, NODE_TYPE type) {
    switch(type) {
        case NODE_TYPE::LITERAL_OR:
        case NODE_TYPE::OR:
            out << "OR";
            break;
        case NODE_TYPE::LITERAL_AND:
        case NODE_TYPE::AND:
            out << "AND";
            break;
        default:
            UNREACHABLE;
    }

    return out;
}

template<typename T>
bool isNone(const T& it) { return it.isNoOp(); }

template<>
bool isNone<LiteralID>(const LiteralID& it) { return it == NO_LITERAL; }

// A Node in the predicate tree - It either has other Nodes or Literals as children
// Both children and literals are located in consecutive memory
class Node {
public:
    Node(NODE_TYPE type): m_type(type), m_child_begin(nullptr), m_child_end(nullptr) {}

    Node(NODE_TYPE type, const std::span<LiteralID>& literals): 
        m_type(type) {
        setLiterals(literals);
    }

    Node(NODE_TYPE type, const std::span<Node>& children): 
        m_type(type) {
        setChildren(children);
    }

    // Nodes can be deleted in place - this avoids vector relocations
    // TODO: Compress children with many NoOps / NO_LITERALs
    bool isNoOp() const { return m_type == NODE_TYPE::NO_OP; }
    void remove() { m_type = NODE_TYPE::NO_OP; }

    void setLiterals(const std::span<LiteralID>& literals) {
        DEV_ASSURE(hasLiterals(), "Invalid Node Type");
        m_child_begin = &*literals.begin();
        m_child_end = &*literals.end();
    }

    void setChildren(const std::span<Node>& children) {
        DEV_ASSURE(hasChildren(), "Invalid Node Type");
        m_child_begin = &*children.begin();
        m_child_end = &*children.end();
    }


    bool hasLiterals() const {
        return m_type < NODE_TYPE::LITERAL_END; 
    }

    bool isOR() const {
        return m_type == NODE_TYPE::OR || m_type == NODE_TYPE::LITERAL_OR;
    }

    bool isAND() const {
        return m_type == NODE_TYPE::AND || m_type == NODE_TYPE::LITERAL_AND;
    }

    bool hasChildren() const { return !hasLiterals(); }

    std::span<const LiteralID> literals() const {
        if (!hasLiterals()) return std::span<LiteralID>();
        return std::span<LiteralID>(static_cast<LiteralID*>(m_child_begin), static_cast<LiteralID*>(m_child_end));
    }
    
    std::span<const Node> children() const {
        if (!hasChildren()) return std::span<Node>();
        return std::span<Node>(static_cast<Node*>(m_child_begin), static_cast<Node*>(m_child_end));
    }

    std::span<LiteralID> literals() {
        if (!hasLiterals()) return std::span<LiteralID>();
        return std::span<LiteralID>(static_cast<LiteralID*>(m_child_begin), static_cast<LiteralID*>(m_child_end));
    }
    
    std::span<Node> children() {
        if (!hasChildren()) return std::span<Node>();
        return std::span<Node>(static_cast<Node*>(m_child_begin), static_cast<Node*>(m_child_end));
    }

    void print(std::ostream& out, size_t depth = 0) const {
        out << std::setfill(' ') << std::setw(depth * 2) << "";
        out << m_type;
        if (hasChildren()) {
            out << "\n";
            for (const auto& child: children()) {
                if (child.isNoOp()) continue;
                child.print(out, depth + 1);
                out << "\n";
            }
        }

        if (hasLiterals()) {
            for (const auto& literal: literals()) {
                if (literal == NO_LITERAL) continue;
                out << " " << literal;
            }
        }
    }

    // Assignment if the node has no children
    Assignment defaultAssignment() const {
        if (isAND()) {
            return { .assigned = true, .value = true };
        } else if(isOR()) {
            return { .assigned = false, .value = false };
        } else UNREACHABLE;
    }

    // Whether further iterating children/literals is useless as the assignment wont change
    bool shortCircuit(const Assignment& assignment) const {
        if (!assignment.assigned) return false;
        
        if (isAND()) return !assignment.value;
        else if (isOR()) return assignment.value;
        else UNREACHABLE;
    }

    // Add a child assignment to the assignment
    void updateAssignment(Assignment& assignment, const Assignment& childAssignment) const {
        // If the child is not determined, this node cannot be too
        if (!childAssignment.assigned) {
            if (isAND()) assignment.assigned = false;
            return;
        }

        if (isAND()) {
            if (!childAssignment.value) {
                assignment.assigned = true;
                assignment.value = false;
            }
        } else if (isOR()) {
            if (childAssignment.value) {
                assignment.assigned = true;
                assignment.value = true;
            }
        } else UNREACHABLE;
    }


    // Evaluates the predicate tree to the assignment
    Assignment evaluate(const LiteralAssignment& literalAssignments, std::function<bool (const Node&, LiteralID)> propagateUnit = [](const Node&, LiteralID) { return false; }) const {
        Assignment assignment = defaultAssignment();

        uint32_t unassignedCount = 0;
        LiteralID unitLiteral;

        for (const auto& literal: literals()) {
            if (literal == NO_LITERAL) continue;
            auto litAssignment = literalAssignments.getLiteralAssignment(literal);
            updateAssignment(assignment, litAssignment);
            if (shortCircuit(assignment)) return assignment;

            if (!litAssignment.assigned) {
                unassignedCount++;
                if (unassignedCount == 1) unitLiteral = literal;
            }
        }

        if (isOR() && unassignedCount == 0) {
            assignment.assigned = true;
        } 

        if (unassignedCount == 1) {
            if (propagateUnit(*this, unitLiteral)) return { .assigned = true, .value = true };
        }

        for (const auto& child: children()) {
            if (child.isNoOp()) continue;
            updateAssignment(assignment, child.evaluate(literalAssignments, propagateUnit));
            if (shortCircuit(assignment)) return assignment;
        }

        return assignment;
    }

    // Generic visitor to visit all literals below the node
    void visitLiterals(std::function<void(LiteralID literal)> visitor) const {
        for (const auto& child: children()) {
            if (child.isNoOp()) continue;
            child.visitLiterals(visitor);
        }

        for (const auto& literal: literals()) {
            if (literal == NO_LITERAL) continue;
            visitor(literal);
        }
    }

    std::set<VariableID> getVariables() const {
        std::set<VariableID> variables;
        visitLiterals([&](LiteralID literal) { variables.emplace(toVariable(literal)); });
        return variables;
    }

    std::set<VariableID> getUnassignedVariables(const LiteralAssignment& literalAssignments) const {
        std::set<VariableID> variables;
        visitLiterals([&](LiteralID literal) { 
            if (!literalAssignments.getLiteralAssignment(literal).assigned)
                variables.emplace(toVariable(literal));
        });
        return variables;
    }

    void visitNodes(std::function<void(Node& node)> visit) {
        for (auto& child: children()) {
            visit(child);
            child.visitNodes(visit);
        }
    }

    template<typename T>
    inline void filter(std::function<bool(T& node)> keep)
    {
        static_assert(std::is_same_v<T, Node> || std::is_same_v<T, LiteralID>);
        DEV_ASSURE((!std::is_same_v<T, Node> || hasChildren()), "Expected Children");
        DEV_ASSURE((!std::is_same_v<T, LiteralID>) || hasLiterals(), "Expected Literals");

        auto current = static_cast<T*>(m_child_begin);
        auto end = static_cast<T*>(m_child_end);

        // Move start pointer forward to remove nodes from the beginning
        while (current < end) {
            if (isNone(*current)) {
                current++;
                continue;
            }
            
            bool result = keep(*current);
            if (result) break;
            current++;
        }
        m_child_begin = static_cast<void*>(&*current);

        // Shift elements backward to remove nodes in the middle,
        // afterwards adjust the end
        auto target = current;
        while (current < end) {
            if (isNone(*current)) {
                current++;
                continue;
            }
            
            bool result = keep(*current);
            if (target != current) {
                *target = *current;
            }

            if (result) target++;
            current++;
        }

        m_child_end = static_cast<void*>(&*target);
    }

    void simplify(const LiteralAssignment& assignments) {
        if (hasChildren()) {
            filter<Node>([&](Node& child) -> bool {
                auto assigned = child.evaluate(assignments);
                if (assigned.assigned) {
                    if (isAND() && assigned.value) return false;
                    if (isOR() && !assigned.value) return false;
                }
                return true;
            });
            for (auto& child: children()) child.simplify(assignments);
        }

        if (hasLiterals()) {
            filter<LiteralID>([&](LiteralID& lit) -> bool {
                return !assignments.getLiteralAssignment(lit).assigned;
            });
        }
    }

private:
    NODE_TYPE m_type;
    void* m_child_begin;
    void* m_child_end;
};

std::ostream& operator<<(std::ostream& out, const Node& node) {
    node.print(out);
    return out;
}


// --------------------- Context -----------------------------------------

using LiteralMemory = Memory<LiteralID>;
using NodeMemory = Memory<Node>;

// The context stores a predicate (as literal memory, node memory and root node pointer),
//  and its assignments. The context is built and then modified by all phases, till a solution is found.
class Context {
public:
    Context() = default;
    Context(const Context& other) = delete;

    // Literals
    LiteralMemory literalMemory;

    // Nodes (conjunctions)
    NodeMemory nodeMemory;
    Node* root = nullptr;

    // Assignments
    LiteralAssignment assignments;

    void print(std::ostream& out) {
        out << "ASSIGNMENTS:\n  ";
        assignments.print(out);
        if (root) {
            out << "\nAST:\n";
            root->print(out, 1);
        }
        out << "\n\n";
    }
};

// --------------------- Builder -----------------------------------------

// Builds a Context from some input data
class ContextBuilder {
public:
    ContextBuilder(Context& ctx): m_ctx(ctx) {}

protected:
    void addLiteral(LiteralID literal) {
        if (literal == 0) {
            addConjunction();
        } else {
            m_current.push_back(literal);
        }
    }

    void addConjunction() {
        if (m_current.size() == 0) return;

        // Single literal conjunction -> Assignment
        if (m_current.size() == 1) {
            auto literal = m_current[0];
            
            auto existingAssignment = m_ctx.assignments.getLiteralAssignment(literal);
            if (existingAssignment.assigned) {
                if (!existingAssignment.value) {
                    NO_SOLUTION(literal << " conflict");
                    exit(1);
                }
            } else {
                m_ctx.assignments.assignLiteral(literal, true);
            }

            m_current.clear();
            return;
        }

        // Move temporary buffer into long term memory
        auto literals = m_ctx.literalMemory.allocate(m_current);
        m_current.clear();

        // Construct node with literals as children
        auto& node = m_ctx.nodeMemory.allocate(Node(NODE_TYPE::LITERAL_OR, literals));
    }

    void finish() {
        ASSURE(m_current.empty(), "Unexpected end of input");
        m_ctx.root = &m_ctx.nodeMemory.allocate(Node(NODE_TYPE::AND, m_ctx.nodeMemory.all()));
    }

private:
    Context& m_ctx;
    std::vector<LiteralID> m_current;
};

// Builds a Context from a DIMACS CNF file
template<typename Base>
class FileParser: public Base {
public:
    using Base::Base;

    void run(std::istream& in) {
        parseChunk(in);
        this->finish();
    }

private:
    void parseChunk(std::istream& in) {
        while (true) {
            char cursor;

            // Detect comment and header in first character
            if (!in.get(cursor)) return;
            if (cursor == 'c') {
                // Skip comment line
                in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }
            if (cursor == 'p') {
                // Skip meta information for now
                in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }

            // Read conjunctions
            do {
                bool negate = cursor == '-';
                if (negate) {
                    ASSURE(in.get(cursor), "Unexpected end of input");
                }

                uint32_t digits = 0;

                do {
                    ASSURE(cursor >= '0' && cursor <= '9', "Unexpected character: '" << cursor << "'");
                    digits = 10 * digits + (cursor - '0');
                } while (in.get(cursor) && cursor != ' ' && cursor != '\n');

                this->addLiteral(toLiteral(digits, negate));
                if (digits == 0) break;
            } while(in.get(cursor));
        }
    }
};

// --------------------- Phases -----------------------------------------

class Phase {
public:
    Phase(Context& ctx): m_ctx(ctx) {}

protected:
    Context& m_ctx;
};

// Simplifies the predicate by inserting known assignments, eliminating const true and false parts
class Simplifier: public Phase {
public:
    Assignment run() {
        Assignment result;
        bool assigned;

        do {
            assigned = false;
            
            DEV_PRINT(" - Simplify " << m_ctx.assignments);
            result = m_ctx.root->evaluate(m_ctx.assignments, [&](const Node& clause, LiteralID lit) {
                assigned = true;
                DEV_PRINT(clause << " => " << lit);
                m_ctx.assignments.assignLiteral(lit, true);
                return true;
            });
        
        // Repeat simplification if a variable was assigned
        } while(!result.assigned && assigned);

        if (!result.assigned) {
            DEV_PRINT("Simplify Tree");
            m_ctx.root->simplify(m_ctx.assignments);
        }

        return result;
    }
};

// Applies brute force: Iterates over all combinations of assignments
class BruteForce: public Phase {
public:
    Assignment run() {
        auto variables = m_ctx.root->getVariables();

        while (true) {
            bool done = true;
            for (VariableID variable: variables) {
                auto assignment = m_ctx.assignments.getVariableAssignment(variable);
                if (!assignment.assigned || !assignment.value) {
                    m_ctx.assignments.assignVariable(variable, true, true);
                    done = false;
                    break;
                }

                m_ctx.assignments.assignVariable(variable, false, true);
            }

            if (done) return { .assigned = true, .value = false };

            DEV_ONLY(
                std::cerr << "\n";
                for (VariableID variable: variables) { std::cerr << variable << " = " <<  m_ctx.assignments.getVariableAssignment(variable) << ", "; }
            )
        
            auto assignment = m_ctx.root->evaluate(m_ctx.assignments, [](const Node& node, auto lit) { return false; });
            if (assignment.assigned && assignment.value) {
                return assignment;
            }
        }
    }

private:
};

class DPLL: public Phase {
public:
    DPLL(Context& ctx): Phase(ctx) {}

    struct TrailStep { LiteralID assignment; bool unit; };
    std::vector<TrailStep> trail;
    uint64_t steps{0};

    void unwind() {
        do {
            TrailStep step = trail.back();
            m_ctx.assignments.unassignVariable(toVariable(step.assignment));
            trail.resize(trail.size() - 1);
            if (!step.unit) break;
            DEV_ASSURE(!trail.empty(), "unwinded past trail");
        } while(true);
    }

    void assign(LiteralID lit, bool unit) {
        trail.emplace_back(TrailStep{lit, unit});
        m_ctx.assignments.assignLiteral(lit, true);
    }

    void printTrail() {
        for (auto& step: trail) { std::cerr << step.assignment << (step.unit ? "u " : " "); }
        std::cerr << "\n";
    }

    Assignment step() {
        auto variables = m_ctx.root->getUnassignedVariables(m_ctx.assignments);
        if (variables.empty()) {
            // DEV_PRINT("no open variables");
            return { .assigned = true, .value = false };
        }

        for (VariableID variable: variables) {
            steps++;
            if (steps % 1'000 == 0) {
                PRINT("\033cDPLL " << duration());
                printTrail();
            }

            // DEV_PRINT(variable << " = T")
            assign(toLiteral(variable, false), false);
            DEV_ONLY(printTrail());
            auto assignment = m_ctx.root->evaluate(m_ctx.assignments, [&](const Node& node, LiteralID lit) {
                // DEV_PRINT("Propagate Unit " << lit);
                assign(lit, true);
                return true;
            });
            if (assignment.assigned && assignment.value) {
                DEV_PRINT("satisfied")
                return { .assigned = true, .value = true };
            }

            assignment = step();
            if (assignment.assigned && assignment.value) {
                DEV_PRINT("satisfied")
                return { .assigned = true, .value = true };
            }

            unwind();

            // DEV_PRINT(variable << " = F")
            assign(toLiteral(variable, true), false);
            DEV_ONLY(printTrail());
            assignment = m_ctx.root->evaluate(m_ctx.assignments, [&](const Node& node, LiteralID lit) {
                // DEV_PRINT("Propagate Unit " << lit);
                assign(lit, true);
                return true;
            });
            if (assignment.assigned && assignment.value) {
                DEV_PRINT("satisfied")
                return { .assigned = true, .value = true };
            }

            assignment = step();
            if (assignment.assigned && assignment.value) {
                DEV_PRINT("satisfied")
                return { .assigned = true, .value = true };
            }
    
            unwind();
        }

        return { .assigned = false, .value = false };
    }

    Assignment run() {
        return step();
    }
};

// --------------------- Solver -----------------------------------------

class Solver {
public:
    Solver() {}

    void runPhase(const char* name, std::function<void()> phase) {
        std::cerr << " ----- " << name << " ----- \n";
        phase();
        std::cerr << " = done at " << duration() << "\n\n";
        DEV_ONLY(ctx.print(std::cerr);)
    }

    void runPhaseResult(const char* name, std::function<Assignment()> phase) {
        runPhase(name, [&]() {
            Assignment assignment = phase();
            if (assignment.assigned) {
                if (assignment.value) {
                    SOLUTION_FOUND(ctx.assignments);
                } else NO_SOLUTION("Simplified to False");
            }
        });
    }

    void run(std::istream& data) {
        runPhase("Build", [&]() {
            FileParser<ContextBuilder>(ctx).run(data);
        });

        runPhaseResult("Simplify", [&]() {
            return Simplifier(ctx).run();
        });

        // runPhaseResult("BruteForce", [&]() {
        //     return BruteForce(ctx).run();
        // });

        runPhaseResult("DPLL", [&]() {
            return DPLL(ctx).run();
        });

        NO_SOLUTION("None found");
    }

    Context ctx;
};





// ------------ Bipartite Graph Solver ---------------------
class GraphSolver {
public:
    using ClauseID = uint32_t;
    
    static void run(std::istream& in) {
        auto solver = FileParser<GraphSolver>();
        solver.run(in);
    }

    void finish() {
        print(std::cout);

       ClauseID maxID = clauses.size();
       for (ClauseID id = 1; id <= maxID; id++) {
            if (clauses.find(id) != clauses.end())
                visitClause(id);
       }

        VariableID varMaxID = variables.size(); // FIXME
        for (VariableID varId = 1; varId <= varMaxID; varId++) {
            if (variables.find(varId) != variables.end())
                visitVariable(varId);
        }

        print(std::cout);
    }

    void visitClause(ClauseID id) {
        DEV_PRINT("Visit Clause " << id);
        auto& it = clause(id);

        if (it.literals.empty()) {
            NO_SOLUTION("Empty Clause");
        }

        // Unit Propagation
        if (it.literals.size() == 1) {
            LiteralID unit = *it.literals.begin();
            DEV_PRINT("Unit Propagation " << unit);
            assignVariable(toVariable(unit), !isNegated(unit));
        }
    }

    void removeFromVariable(LiteralID lit, ClauseID id) {
        DEV_PRINT("Remove from Variable " << lit << " " << id);
        auto& var = variable(toVariable(lit));
        if (isNegated(lit)) var.negativeClauses.erase(id); else var.positiveClauses.erase(id);
    }

    void removeFromClause(ClauseID id, LiteralID lit) {
        DEV_PRINT("Remove from Clause " << id << " " << lit);
        auto& it = clause(id).literals;
        it.erase(lit);
    }

    void removeClause(ClauseID id) {
        DEV_PRINT("Remove Clause " << id);

        for (const LiteralID lit: clause(id).literals) {
            removeFromVariable(lit, id);
            visitVariable(toVariable(lit));
        }

        clauses.erase(id);
        if (clauses.empty()) {
            SOLUTION_FOUND((*this));
        }
    }

    void assignVariable(VariableID id, bool value) {
        DEV_PRINT("Assign Variable " << id << " = " << (value ? "T" : "F"));
        auto& var = variable(id);
        if (var.assigned) {
            if (value != var.value) NO_SOLUTION("Conflicting assignment for " << id);
            return;
        }

        var.assigned = true;
        var.value = value;

        for (const auto& positive: var.positiveClauses) {
            removeFromClause(positive, id);
            if (value) removeClause(positive);
            else visitClause(positive);
        }

        for (const auto& negative: var.negativeClauses) {
            removeFromClause(negative, toLiteral(id, true));
            if (!value) removeClause(negative);
            else visitClause(negative);
        }
    }

    void visitVariable(VariableID id) {
        DEV_PRINT("Visit Variable " << id);
        auto& var = variable(id);
        if (var.assigned) return;

        // Pure Literal elimination
        if (var.negativeClauses.empty()) {
            DEV_PRINT("Pure Positive Variable " << id);
            assignVariable(id, true);
            return;
        }

        if (var.positiveClauses.empty()) {
            DEV_PRINT("Pure Negative Variable " << id);
            assignVariable(id, false);
            return;
        }
    }

    void print(std::ostream& out, bool asAssignment = false) {
        if (asAssignment) {
            for (const auto& [variableID, variable]: variables) {
                if (variable.assigned) {
                    out << (variable.value ? "" : "-") << variableID << " 0 ";
                }
            }
            return;
        }

        out << "CLAUSES:\n";
        for (const auto& [clauseID, clause]: clauses) {
            out << clauseID << " -> (";
            for (const auto& literalID: clause.literals) {
                out << literalID << ", ";
            }
            out << ")\n";
        }

        out << "VARIABLES:\n";
        for (const auto& [variableID, variable]: variables) {
            out << variableID;
            if (variable.assigned) {
                out << " = " << (variable.value ? "T" : "F");
            }
            out << " -> (";
            for (const auto& clauseID: variable.positiveClauses) {
                out << "+" << clauseID << ", ";
            }
            for (const auto& clauseID: variable.negativeClauses) {
                out << "-" << clauseID << ", ";
            }
            out << ")\n";
        }
    }

    void addLiteral(LiteralID literal) {
        if (literal == 0 && currentLiterals.size() > 0) {
            ClauseID clauseID = clauses.size() + 1;
            auto clause_it = clauses.try_emplace(clauseID);
            auto& clause = clause_it.first->second;
            clause.literals = currentLiterals;
            currentLiterals.clear();

            for (LiteralID lit: clause.literals) {
                auto var_it = variables.try_emplace(toVariable(lit));
                auto& var = var_it.first->second;
                if (isNegated(lit))
                    var.negativeClauses.insert(clauseID);
                else
                    var.positiveClauses.insert(clauseID);
            }
        } else {
            currentLiterals.insert(literal);
        }
    }

private:
    class Clause {
    public:
        std::set<LiteralID> literals;
    };

    class Variable {
    public:
        std::set<ClauseID> positiveClauses;
        std::set<ClauseID> negativeClauses;

        bool assigned = false;
        bool value = false;
    };

    Clause& clause(ClauseID id) {
        auto it = clauses.find(id);
        DEV_ASSURE(it != clauses.end(), "Lost clause " << id)
        return it->second;
    }

    Variable& variable(VariableID id) {
        auto it = variables.find(id);
        DEV_ASSURE(it != variables.end(), "Lost variable " << id)
        return it->second;
    }

    std::map<ClauseID, Clause> clauses;
    std::map<VariableID, Variable> variables;

    std::set<LiteralID> currentLiterals;
};




int main(int argc, char* argv[]) {
    std::cerr << "SAT Solver (Jonas Wilms)\n";

    ASSURE(argc <= 2, "Usage: ./sat <file?>");
    if (argc == 2) {
        char* filename = argv[1];

        std::fstream fs;
        fs.open(filename, std::fstream::in);

        // Exclude file opening time from measurements to make them more stable
        restartTime();

        auto solver = Solver();
        solver.run(fs);
        // GraphSolver::run(fs);
    } else {
        // Exclude file opening time from measurements to make them more stable
        restartTime();

        auto solver = Solver();
        solver.run(std::cin); 
        // GraphSolver::run(std::cin);
    }

    return 0;
}
