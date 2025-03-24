#include <iostream>
#include <vector>
#include <span>
#include <algorithm>
#include <fstream>
#include <functional>
#include <set>

#define ASSURE(condition, msg) \
  if (!(condition)) {              \
    std::cerr << msg << "\n  Line: " << __LINE__ << "\n"; \
    exit(1);                       \
  }

#define UNREACHABLE ASSURE(false, "Unreachable")

#if 0
    #define DEV_ASSURE(condition, msg) ASSURE(condition, msg)
    #define DEV_ONLY(statement) statement
    #define DEV_PRINT(msg) std::cerr << msg << "\n";
#else
    #define DEV_ASSURE(condition, msg)
    #define DEV_ONLY(statement)
    #define DEV_PRINT(msg)
#endif

auto start = std::chrono::high_resolution_clock::now();
void restartTime() { start = std::chrono::high_resolution_clock::now(); }

std::string duration() {
    auto elapsed = std::chrono::high_resolution_clock::now() - start;

    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()) + "μs";
}

#define SOLUTION_FOUND(assignment) std::cerr << "\n\nSolution Found after " << duration() << ":\n"; assignment.print(std::cout); exit(0);
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
        m_assignmentCount += 1;
    }

    size_t getAssignmentCount() { return m_assignmentCount; }

    // Assigns the literal, i.e. assumes that a = True or NOT(a) = True
    void assignLiteral(LiteralID literal, bool value, bool overwrite = false) {
        assignVariable(toVariable(literal), isNegated(literal) ? !value : value, overwrite);
    }

    void print(std::ostream& out) const {
        for (VariableID i = 1; i <= getMaxVariable(); i++) {
            auto assignment = getVariableAssignment(i);
            if (assignment.assigned) {
                out << (assignment.value ? "" : "-") << i << " ";
            }
        }

        out << "\n";
    }
private:
    size_t m_assignmentCount = 0;
    std::vector<bool> m_assignments;
};

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

        DEV_ONLY(m_dev_sorted = false;)
    }

    void setChildren(const std::span<Node>& children) {
        DEV_ASSURE(hasChildren(), "Invalid Node Type");
        m_child_begin = &*children.begin();
        m_child_end = &*children.end();

        DEV_ONLY(m_dev_sorted = false;)
    }

    // Orders literals in ascending order. Afterwards one can do binary search in them
    void orderLiterals() {
        DEV_ASSURE(hasLiterals(), "Invalid Node Type");
        std::sort(literals().begin(), literals().end(), [](LiteralID a, LiteralID b) {
            return b > a;
        });

        DEV_ONLY(m_dev_sorted = true;)
    }

    // Orders children by their child count, so that simpler predicates come first
    void orderChildren() {
        DEV_ASSURE(hasChildren(), "Invalid Node Type");
        std::sort(children().begin(), children().end(), [](const Node& a, const Node& b) {
            return b.childCount() > a.childCount();
        });

        DEV_ONLY(m_dev_sorted = true;)
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

    size_t childCount() const {
        return hasChildren() ? children().size() : literals().size();
    }

    void print(std::ostream& out, size_t depth = 0) const {
        out << std::setfill(' ') << std::setw(depth * 2) << "";
        out << m_type << "\n";
        for (const auto& child: children()) {
            if (child.isNoOp()) continue;
            child.print(out, depth + 1);
        }

        if (hasLiterals()) {
            out << std::setfill(' ') << std::setw((depth + 1) * 2) << "";
            out << "LIT:";
            for (const auto& literal: literals()) {
                if (literal == NO_LITERAL) continue;
                out << " " << literal;
            }
        }
        out << "\n";
    }

    // Assignment if the node has no children
    Assignment defaultAssignment() const {
        if (isAND()) {
            return { .assigned = true, .value = true };
        } else if(isOR()) {
            return { .assigned = true, .value = false };
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
            assignment.assigned = false;
            return;
        }

        if (isAND()) {
            if (!childAssignment.value) assignment.value = false;
        } else if (isOR()) {
            if (childAssignment.value) assignment.value = true;
        } else UNREACHABLE;
    }

    // Applies the assignment to the tree and returns the assignment of the node
    //
    // If assignLiterals is true, if a node is reduced to a single literal,
    //  the literal is assigned to the value to make the predicate evaluate to true.
    // This only makes sense if the predicate tree is in CNF
    Assignment apply(LiteralAssignment& literalAssignments, bool assignLiterals) {
        Assignment assignment = defaultAssignment();

        if (hasChildren()) {
            filter<Node>([&](Node& child) -> FilterResult {
                auto childAssignment = child.apply(literalAssignments, assignLiterals);
                updateAssignment(assignment, childAssignment);
                if (shortCircuit(assignment)) return { .exit = true };
                if (childAssignment.assigned) {
                    if (isOR() && !childAssignment.value) return { .keep = false };
                    if (isAND() && childAssignment.value) return { .keep = false };
                }
                return { .keep = true };
            });
        } else if (hasLiterals()) {
            size_t literalCountBefore = literals().size();
            size_t removedLiterals = 0;
            bool exit = filter<LiteralID>([&](LiteralID& literal) -> FilterResult {
                const auto literalAssignment = literalAssignments.getLiteralAssignment(literal);
                updateAssignment(assignment, literalAssignment);
                if (shortCircuit(assignment)) return { .exit = true };

                if (literalAssignment.assigned) {
                    if ((isOR() && !literalAssignment.value) || (isAND() && literalAssignment.value)) {
                        removedLiterals += 1;
                        return { .keep = false };
                    }
                }

                return { .keep = true };
            });
            if (exit) return assignment;

            // If only one literal is left, we know it must be true to fulfill
            // NOTE: This only holds true in normal form
            if (assignLiterals) {
                size_t literalCount = literalCountBefore - removedLiterals;
                if (literalCount == 1) {
                    for (auto literal: literals()) {
                        if (literal == NO_LITERAL) continue;
                        DEV_PRINT("  + Derived " << literal);
                        literalAssignments.assignLiteral(literal, true);
                        break;
                    }

                    return { .assigned = true, .value = true };
                }
            }
        }

        return assignment;
    }

    // Evaluates the predicate tree to the assignment
    Assignment evaluate(const LiteralAssignment& literalAssignments) const {
        Assignment assignment = defaultAssignment();
        for (const auto& literal: literals()) {
            if (literal == NO_LITERAL) continue;
            updateAssignment(assignment, literalAssignments.getLiteralAssignment(literal));
            if (shortCircuit(assignment)) return assignment;
        }

        for (const auto& child: children()) {
            if (child.isNoOp()) continue;
            updateAssignment(assignment, child.evaluate(literalAssignments));
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

    // Returns all literals that appear within the predicate
    std::set<LiteralID> getLiterals() const {
        std::set<LiteralID> literals;
        visitLiterals([&](LiteralID literal) { literals.emplace(literal); });
        return literals;
    }

    std::set<VariableID> getVariables() const {
        std::set<VariableID> variables;
        visitLiterals([&](LiteralID literal) { variables.emplace(toVariable(literal)); });
        return variables;
    }

    void visitNodes(std::function<void(Node& node)> visit) {
        for (auto& child: children()) {
            visit(child);
            child.visitNodes(visit);
        }
    }

    struct FilterResult { bool keep; bool exit = false; };

    template<typename T>
    inline bool filter(std::function<FilterResult(T& node)> keep)
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
            
            auto result = keep(*current);
            if (result.exit) return true;

            if (result.keep) break;
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
            
            auto result = keep(*current);
            if (result.exit) return true;

            if (target != current) {
                *target = *current;
            }

            if (result.keep) target++;
            current++;
        }

        m_child_end = static_cast<void*>(&*target);
        return false;
    }

private:
    NODE_TYPE m_type;
    void* m_child_begin;
    void* m_child_end;
    DEV_ONLY(bool m_dev_sorted {false};)
};


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
            out << "AST:\n";
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
            if (existingAssignment.assigned && !existingAssignment.value) {
                NO_SOLUTION(literal << " conflict");
                exit(1);
            }

            m_ctx.assignments.assignLiteral(literal, true);
            m_current.clear();
            return;
        }

        // Move temporary buffer into long term memory
        auto literals = m_ctx.literalMemory.allocate(m_current);
        m_current.clear();

        // Construct node with literals as children
        auto& node = m_ctx.nodeMemory.allocate(Node(NODE_TYPE::LITERAL_OR, literals));
        node.orderLiterals();
    }

    void finish() {
        ASSURE(m_current.empty(), "Unexpected end of input");
        m_ctx.root = &m_ctx.nodeMemory.allocate(Node(NODE_TYPE::AND, m_ctx.nodeMemory.all()));
        m_ctx.root->orderChildren();
    }

private:
    Context& m_ctx;
    std::vector<LiteralID> m_current;
};

// Builds a Context from a DIMACS CNF file
class FileParser: public ContextBuilder {
public:
    using ContextBuilder::ContextBuilder;

    void run(std::istream& in) {
        parseChunk(in);
        finish();
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

                addLiteral(toLiteral(digits, negate));
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
        size_t assignmentCount;

        do {
            assignmentCount = m_ctx.assignments.getAssignmentCount();
            DEV_PRINT("Simpliciation Round");
            result = m_ctx.root->apply(m_ctx.assignments, true);
        
        // Repeat simplification if a variable was assigned
        } while(!result.assigned && assignmentCount != m_ctx.assignments.getAssignmentCount());
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
        
            auto assignment = m_ctx.root->evaluate(m_ctx.assignments);
            if (assignment.assigned && assignment.value) {
                return assignment;
            }
        }
    }

private:
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
            FileParser(ctx).run(data);
        });

        runPhaseResult("Simplify", [&]() {
            return Simplifier(ctx).run();
        });

        runPhaseResult("BruteForce", [&]() {
            return BruteForce(ctx).run();
        });

        NO_SOLUTION("None found");
    }

    Context ctx;
};



int main(int argc, char* argv[]) {
    std::cerr << "SAT Solver (Jonas Wilms)\n";

    ASSURE(argc == 2, "Usage: ./sat <file>");
    char* filename = argv[1];

    std::fstream fs;
    fs.open(filename, std::fstream::in);

    // Exclude file opening time from measurements to make them more stable
    restartTime();

    auto solver = Solver();
    solver.run(fs);

    return 0;
}
