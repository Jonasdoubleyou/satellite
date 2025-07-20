// SAT Solver by Jonas Wilms, implements CDCL with Unit Propagation + Pure Literal Elimination as preprocessing

#include "common/utils.h"
#include <ranges>

#define SOLUTION_FOUND(assignment) \
  std::cerr << "\n\nSolution Found after " << duration() << ":\n"; \
  assignment.print(std::cout, true); \
  exit(0);

#define NO_SOLUTION(details) { std::cerr << "\n\nNo Solution possible after " << duration() << ": " << details << "\n"; std::cout << "UNSAT\n"; exit(1); }


// --------------------- Literals / Clauses -----------------------------------------

/// A Variable ID in [1, max_uint32)
using VariableID = uint32_t;
/// A Literal ID in (-max_uint32, max_uint32)
/// -N = NOT N
using LiteralID = int;
/// A LiteralID that does not exist. Can be used to delete literals in place
constexpr LiteralID NO_LITERAL = 0;

template<typename T>
std::ostream& operator<<(std::ostream& out, const std::unordered_set<T>& lits) {
    out << "{ ";
    for (LiteralID lit: lits)
        out << lit << ", ";
    out << "}";
    return out;
}

VariableID toVariable(LiteralID literal) {
    return abs(literal);
}

LiteralID toLiteral(VariableID variable, bool negate) {
    return negate ? -variable : variable;
}

bool isNegated(LiteralID literal) {
    return literal < 0;
}

enum Assignment {
    UNASSIGNED = 0,
    SAT = 1,
    UNSAT = 2,
};

using ClauseID = uint32_t;

// Slightly easier to debug if ordered, but faster if unordered:
// using LiteralSet = std::set<LiteralID>; 
using LiteralSet = std::unordered_set<LiteralID>;

class Clause {
public:
    LiteralSet literals;

    /// Cache of Clause assignment for speed
    /// (probably watched literals would be more efficient)
    Assignment assignment {Assignment::UNASSIGNED};
    VariableID byVariable{0};
};

std::ostream& operator<<(std::ostream& os, const Clause& clause) {
    os << "(";
    for (const auto& literalID: clause.literals) {
        os << literalID << ", ";
    }
    os << ")";
    return os;
}

using ClauseSet = std::unordered_set<ClauseID>;

class Variable {
public:
    // Backreference to Clauses that contain this Variable
    //  -> Together Clauses and Variables form a bipartite graph

    /// Clauses that get satisfied if the Variable is set to true
    ClauseSet positiveClauses;
    /// Clauses that get satisfied if the Variable is set to false
    ClauseSet negativeClauses;

    /// Heuristic for guessing influential variables
    size_t score() {
        return std::max(positiveClauses.size(), negativeClauses.size());
    }

    /// Assignment of the Variable
    bool assigned = false;
    bool value = false;
};

std::ostream& operator<<(std::ostream& os, const Variable& variable) {
    if (variable.assigned) {
        os << (variable.value ? "T" : "F");
    } else os << "?";
    
    os << " -> (";
    for (const auto& clauseID: variable.positiveClauses) {
        os << "+C" << clauseID << ", ";
    }
    for (const auto& clauseID: variable.negativeClauses) {
        os << "-C" << clauseID << ", ";
    }
    os << ")";
    return os;
}

// Stores Clauses and Variables, adds some utilities to modify the graph
class GraphContext {
public:
    /// Prints the Graph
    /// \param asAssignment only print assigned variables (i.e. the solution)
    void print(std::ostream& out, bool asAssignment = false) {
        if (asAssignment) {
            for (const auto& [variableID, variable]: variables) {
                if (variable.assigned) {
                    out << (variable.value ? "" : "-") << variableID << " 0 ";
                }
            }
            out << "\n";
            return;
        }

        out << "CLAUSES:\n";
        for (const auto& [clauseID, clause]: clauses) {
            out << "C" << clauseID << " -> " << clause << "\n";
        }

        out << "VARIABLES:\n";
        for (const auto& [variableID, variable]: variables) {
            out << variableID << variable << "\n";
        }
    }

    /// Prints unassigned clauses
    void printClauses(std::ostream& out) {
        for (const auto& [clauseID, clause]: clauses) {
            bool isSAT = false;
            for (auto& lit: clause.literals) {
                auto& var = variables[toVariable(lit)];
                if (var.assigned && isNegated(lit) != var.value) {
                    isSAT = true;
                    break;
                }
            }

            if (!isSAT) {
                out << "C" << clauseID << " not satisfied:\n";
                for (auto& lit: clause.literals) {
                    out << lit;
                    auto& var = variables[toVariable(lit)];
                    if (var.assigned) {
                        out << "=" << (isNegated(lit) == var.value ? "F" : "T");
                    }
                    out << ", ";
                }
                out << "\n";
            }
        }
    }

    /// Adds a Clause to the Graph, and links it to the Variables
    ClauseID addClause(LiteralSet literals) {
        DEV_ONLY(
            for (auto lit: literals) {
                ASSURE(literals.count(-lit) == 0, "Tautology clause");
            }
        );

        ClauseID clauseID = ++clauseCounter;
        auto& clause = clauses.emplace(clauseID, Clause {
            .assignment = Assignment::UNASSIGNED,
            .byVariable = 0,
            .literals = std::move(literals)
        }).first->second;

        for (LiteralID lit: clause.literals) {
            auto var_it = variables.try_emplace(toVariable(lit));
            auto& var = var_it.first->second;
            
            if (!var.assigned)
                unassignedVariables.insert(toVariable(lit));

            if (isNegated(lit))
                var.negativeClauses.insert(clauseID);
            else
                var.positiveClauses.insert(clauseID);
        }

        if (clause.literals.size() == 1) {
            unitClauses.emplace_back(clauseID);
        }

        return clauseID;
    }

    /// Checks wether a clause exists - should usually be true unless the Graph is currently modified
    bool hasClause(ClauseID id) const {
        return clauses.find(id) != clauses.end();
    }
    
    /// Retrieves Clause by ID
    Clause& clause(ClauseID id) {
        // NOTE: It would probably faster to keep Clause* instead of ClauseIDs, but this is easier to debug
        auto it = clauses.find(id);
        DEV_ASSURE(it != clauses.end(), "Lost clause " << id)
        return it->second;
    }

    /// Checks wether a variable exists - should usually be true unless the Graph is currently modified
    bool hasVariable(VariableID id) const {
        return variables.find(id) != variables.end();
    }

    /// Retrieves Variable by ID
    Variable& variable(VariableID id) {
        auto it = variables.find(id);
        DEV_ASSURE(it != variables.end(), "Lost variable " << id)
        return it->second;
    }

    /// Assigns a value to a Variable.
    /// Ignores duplicate assignments, unless the assignment causes a conflict
    /// @param override assign even if it causes a conflict
    void assign(VariableID id, bool value, bool override = false) {
        auto& var = variable(id);
        if (var.assigned) {
            if (value != var.value) {
                if (override) {
                    unassign(id);
                } else {
                    NO_SOLUTION("Conflicting assignment for " << id);
                }
            }
            return;
        }

        size_t count = unassignedVariables.erase(id);
        DEV_ASSURE(count == 1, "Unassigned Variable Chaos " << id);

        var.assigned = true;
        var.value = value;
    }

    /// Unassigns a Variable
    void unassign(VariableID id) {
        auto& var = variable(id);
        DEV_ASSURE(var.assigned, "unassign assigned");

        unassignedVariables.insert(id);
        var.assigned = false;
    }

    /// Checks whether the Graph is in a consistent state
    void consistencyCheck() {
        for (auto& [clauseID, clause]: clauses) {
            ASSURE(clause.assignment != Assignment::UNSAT, "UNSAT clause");

            if (clause.assignment == Assignment::SAT) {
                ASSURE(clause.byVariable != 0, "Inconsistent byVariable");
                ASSURE(variables[clause.byVariable].assigned, "Inconsistent byVariable");
                if (variables[clause.byVariable].value) {
                    ASSURE(clause.literals.count(clause.byVariable) == 1, "Inconsistent byVariable");
                } else {
                    ASSURE(clause.literals.count(-clause.byVariable) == 1, "Inconsistent byVariable");
                }
            }

            for (auto lit: clause.literals) {
                auto& var = variables[toVariable(lit)];
                if (lit > 0) {
                    ASSURE(var.positiveClauses.count(clauseID) == 1, "Inconsistent graph");
                } else {
                    ASSURE(var.negativeClauses.count(clauseID) == 1, "Inconsistent graph");
                }
            }
        }

        for (auto [varID, variable]: variables) {
            for (auto pos: variable.positiveClauses) {
                ASSURE(clauses[pos].literals.count(varID) == 1, "Inconsistent graph");
            }
            for (auto neg: variable.negativeClauses) {
                ASSURE(clauses[neg].literals.count(-varID) == 1, "Inconsistent graph");
            }
        }
    }

    std::map<ClauseID, Clause> clauses;
    std::map<VariableID, Variable> variables;

    /// As we remove clauses and variables, keep a consistent ID:
    size_t clauseCounter = 0;

    /// Collect unit clauses for faster unit propagation
    std::vector<ClauseID> unitClauses;
    /// Keep unassigned variables for faster CDCL guessing
    std::set<VariableID> unassignedVariables;
};



// --------------------- File Parsing -----------------------------------------

/// Builds a GraphContext from a DIMACS CNF file
class FileParser {
public:
    GraphContext& ctx;

    FileParser(GraphContext& ctx, std::istream& in): ctx(ctx) {
        parseChunk(in);
        ASSURE(currentLiterals.empty(), "Unterminated Clause");
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
                if (digits == 0) break;
            } while(in.get(cursor));
        }
    }

    void addLiteral(LiteralID literal) {
        if (literal == 0 && currentLiterals.size() > 0) {
            for (auto lit: currentLiterals) {
                // Tautology: (a v -a) = T
                if (currentLiterals.count(-lit) > 0) {
                    DEV_PRINT("Tautology clause");
                    return;
                }
            }
            ctx.addClause(std::move(currentLiterals));
            DEV_ASSURE(currentLiterals.empty(), "move");
        } else {
            currentLiterals.insert(literal);
        }
    }

private:
    LiteralSet currentLiterals;
};

// --------------------- Simplifier -----------------------------------------

/// Simplifies a GraphContext using Unit Propagation + Pure Literal Elimination
/// This modifies the Graph, to speed up further CDCL processing,
/// and might already determine a solution.
class Simplifier {
public:
    Simplifier(GraphContext& ctx): ctx(ctx) {
       if (!ctx.unitClauses.empty()) {
            PRINT("Simplify Units");
            PRINT("+ Clauses before: " << ctx.clauses.size());
            for (ClauseID id: ctx.unitClauses) {
                if (ctx.clauses.find(id) != ctx.clauses.end()) {
                    visitClause(id);
                }
            }
            
            ctx.unitClauses.clear();
            DEV_ONLY(ctx.consistencyCheck());
            PRINT("+ Clauses after: " << ctx.clauses.size());
            PRINT("= done after " << duration());
       }

       PRINT("\nSimplify with Pure Literal Elimination")
       auto unassigned = ctx.unassignedVariables;
       for (auto& id: unassigned) {
            if (ctx.variables.find(id) != ctx.variables.end()) {
                visitVariable(id);
            }
       }

       DEV_ONLY(ctx.consistencyCheck());
       PRINT("+ Clauses after: " << ctx.clauses.size());
       PRINT("= done after " << duration());
    }

private:
    GraphContext& ctx;

    /// Visits a Clause and performs Unit Propagation if it is a unit clause
    void visitClause(ClauseID id) {
        // During removal we might be in a cycle and arrive at a half removed clause:
        if (!ctx.hasClause(id)) return;

        DEV_PRINT("Visit Clause C" << id);
        auto& it = ctx.clause(id);

        // ((false v ) ^ ...) = F
        if (it.literals.empty()) {
            NO_SOLUTION("Empty Clause");
        }

        // Unit Propagation
        if (it.literals.size() == 1) {
            LiteralID unit = *it.literals.begin();
            DEV_PRINT("C" << id << " -> " << unit);
            assignVariable(toVariable(unit), !isNegated(unit));
        }
    }

    /// Removes a clause from a variable
    void removeFromVariable(LiteralID lit, ClauseID id) {
        auto& var = ctx.variable(toVariable(lit));
        if (var.assigned) return;

        DEV_PRINT("Remove from Variable L" << lit << " " << id);

        if (isNegated(lit)) {
            size_t count = var.negativeClauses.erase(id);
            DEV_ASSURE(count == 1, lit << " C" << id);
        } else {
            size_t count = var.positiveClauses.erase(id);
            DEV_ASSURE(count == 1, lit << " C" << id);
        }
    }

    /// Removes a variable from a clause
    void removeFromClause(ClauseID id, LiteralID lit) {
        DEV_PRINT("Remove from Clause " << id << " " << lit);
        auto& it = ctx.clause(id).literals;
        size_t count = it.erase(lit);
        DEV_ASSURE(count == 1, "C" << id << " " << lit);
    }

    /// Removes a clause, recurses into removed variables
    void removeClause(ClauseID id) {
        DEV_PRINT("Remove Clause " << id);

        if (!ctx.hasClause(id)) return;

        Clause clause = ctx.clause(id);
        for (const LiteralID lit: clause.literals) {
            if (!ctx.hasVariable(toVariable(lit))) continue;
            removeFromVariable(lit, id);
            visitVariable(toVariable(lit));
        }

        ctx.clauses.erase(id);
        if (ctx.clauses.empty()) {
            SOLUTION_FOUND(ctx);
        }
    }

    /// Assigns a value to a variable (coming from unit propagation or pure literal),
    ///  recurses into clauses and simplifies them, potentially recursing into unit propagation
    void assignVariable(VariableID id, bool value) {
        if (!ctx.hasVariable(id)) return;

        ctx.assign(id, value);

        auto& var  = ctx.variable(id);
        ClauseSet pos = std::move(var.positiveClauses);
        var.positiveClauses.clear();
        ClauseSet neg = std::move(var.negativeClauses);
        var.negativeClauses.clear();

        for (const auto& positive: pos) {
            DEV_PRINT("Clause " << positive);
            if (!ctx.hasClause(positive)) continue;

            removeFromClause(positive, id);
            
            if (value) removeClause(positive);
            else visitClause(positive);
        }

        for (const auto& negative: neg) {
            DEV_PRINT("Clause " << negative);
            if (!ctx.hasClause(negative)) continue;

            removeFromClause(negative, toLiteral(id, true));
            
            if (!value) removeClause(negative);
            else visitClause(negative);
        }

    }

    /// Visits a variable, perform pure literal elimination
    void visitVariable(VariableID id) {
        if (!ctx.hasVariable(id)) return;

        DEV_PRINT("Visit Variable " << id);
        auto& var = ctx.variable(id);
        if (var.assigned) return;

        // Pure Literal elimination
        if (var.negativeClauses.empty()) {
            DEV_ASSURE(!var.positiveClauses.empty(), id << " no clauses");
            DEV_PRINT("Pure Positive Variable " << id);
            assignVariable(id, true);
            return;
        } else if (var.positiveClauses.empty()) {
            DEV_PRINT("Pure Negative Variable " << id);
            assignVariable(id, false);
            return;
        }
    }
};

// --------------------- CDCL -----------------------------------------

/// A simple CDCL implementation - Guess variable assignments,
/// perform unit propagation, when running into an unsatisfied clause compute a reason clause
/// and backtrack.
class CDCL {
public:
    GraphContext& ctx;

    /// Performance Counters to look into the algorithm
    struct PerfCounters {
        size_t learnedClauses = 0;
        size_t unitProps = 0;
        size_t guesses = 0;
    };
    PerfCounters perf;

    CDCL(GraphContext& ctx): ctx(ctx) {
        iterate();
    }

    void iterate() {
        PRINT("\nCDCL with " << ctx.clauses.size() << " clauses");

        // Loop unassigned Variables (skip ones assigned by the Simplifier),
        //  in descending order according to the heuristic
        std::vector<VariableID> variables = std::vector(ctx.unassignedVariables.begin(), ctx.unassignedVariables.end());
        if (variables.empty()) return;

        std::sort(variables.begin(), variables.end(), [&](VariableID a, VariableID b) {
            return ctx.variable(b).score() > ctx.variable(a).score();
        });

        // Due to CDCL we eventually terminate during clause learning, thus we have a
        // seemingly infinite loop here

        // During clause learning we back track and unwind the stack - but we do not jump back
        // here - thus we need to potentially loop the variables multiple times to assign them all
        while (true) {
            for (VariableID id: variables) {
                // This is easier than looping unassignedVariables (which is mutated)
                if (!ctx.variable(id).assigned) {
                    // Guess v = T, recurse into unit propagation,
                    //  potentially back track through a conflict clause
                    perf.guesses++;
                    bool conflict = assign(id, true, 0);

                    // Only try v = F if v = T lead into a conflict - the conflict
                    // is no longer there due to unwinding during clause learning
                    // if (conflict) {
                    //     unassign(id);
                    //     perf.guesses++;
                    //     bool second_conflict = assign(id, false, 0);
                    //     ASSURE(!second_conflict, "Second conflict");
                    // }

                    // The SAT case: All variables are assigned to a value.
                    // NOTE: This also potentially assigns a few irrelevant variables,
                    //  it could be more efficient to track unsatisfied clauses instead
                    
                    // We need to check this here, as within assign(...) we might still find a conflict
                    // while unwinding from recursion
                    if (ctx.unassignedVariables.size() == 0) {
                        PRINT(" = All Variables assigned without conflicts");
                        PRINT(" + Learned Clauses: " << perf.learnedClauses);
                        PRINT(" + Unit Propagations: " << perf.unitProps);
                        PRINT(" + Guesses: " << perf.guesses);
                        PRINT(" = done after " << duration());

                        DEV_ONLY(ctx.consistencyCheck());
                        DEV_ONLY(ctx.printClauses(std::cerr));

                        SOLUTION_FOUND(ctx);
                    }
                }
            }
        }
    }

private:
    /// Assigns a Variable and tracks the assignment on the trail,
    ///  revisits clauses for unit propagation
    [[nodiscard]] bool assign(VariableID id, bool value, ClauseID reason = 0, bool noTrail = false) {
        auto& var = ctx.variable(id);
        if (var.assigned && var.value == value) return false;

        DEV_PRINT("V" << id << " = " << (value ? "T" : "F"));
        ctx.assign(id, value, noTrail);
        if (!noTrail) trail.push_back({ .var = id, .reason = reason });

        for (auto& clause: *(value ? &var.negativeClauses : &var.positiveClauses)) {
            auto& prevAssignment = ctx.clause(clause).assignment;
            // Skip clauses that are already satisfied (= speedup through caching)
            if (prevAssignment == Assignment::SAT) continue;
            DEV_ASSURE(prevAssignment != Assignment::UNSAT, "How?");

            Assignment result = visitClause(clause, id);
            if (result == Assignment::UNSAT)
            {
                DEV_PRINT("Conflict C" << clause << " = " << ctx.clause(clause));
                DEV_ONLY(printConflicts(std::cerr));
                return learnClause(clause);
            }
        }

        return false;
    }

    /// Unassigns the most recent variable from the trail,
    ///  potentially clears cached assignments of clauses satisfied by the variable
    void unassign(VariableID id) {
        auto& var = ctx.variable(id);
        DEV_ASSURE(var.assigned, "Double unassign");
        DEV_ASSURE(!trail.empty(), "Empty trail");
        DEV_ASSURE(trail.back().var == id, "Wrong unassign " << trail.back().var << " != " << id);
        trail.pop_back();

        DEV_PRINT("V" << id << " = ?");
        ctx.unassign(id);

        for (auto c: var.positiveClauses) {
            auto& clause = ctx.clause(c);
            if (clause.byVariable == id)
                clause.assignment = Assignment::UNASSIGNED;
        }

        for (auto c: var.negativeClauses) {
            auto& clause = ctx.clause(c);
            if (clause.byVariable == id)
                clause.assignment = Assignment::UNASSIGNED;
        }
    }

    /// Learn from the conflict clause by performing CDCL.
    /// Unwinds the stack on the current level, and compute the resolvent over all reason clauses.
    /// Afterwards unwinds until the learned clause becomes not asserting, then adds the clause
    ///  to the trail
    bool learnClause(ClauseID conflictClause) {
        LiteralSet learnedClause;
        learnedClause = ctx.clause(conflictClause).literals;

        // (1) Learn a clause from the reason clauses of the current level
        while (!trail.empty()) {
            auto step = trail.back();

            // Always resolve the current level fully
            if (step.reason == 0) break;

            unassign(step.var);

            auto posLit = toLiteral(step.var, true);
            auto negLit = toLiteral(step.var, false);

            const auto& reasonClause = ctx.clause(step.reason).literals;
            bool posRes = learnedClause.count(posLit) == 1 && reasonClause.count(negLit) == 1;
            bool negRes = learnedClause.count(negLit) == 1 && reasonClause.count(posLit) == 1;

            // double unit propagation A -> B, A -> C, only resolve via topmost clause on the trail and skip further ones
            if (!posRes && !negRes) {
                continue;
            }

            // Resolution of learnedClause and reasonClause via step.var
            // (learnedClause v lit) ^ (reasonClause v -lit) => (learnedClause v reasonClause)

            // (1) Remove lit from learnedClause
            learnedClause.erase(posLit);
            learnedClause.erase(negLit);

            // (2) Add reasonClause to learnedClause ...
            for (LiteralID lit: reasonClause) {
                // ... and remove lit
                if (!(lit == posLit || lit == negLit)) {
                    // I don't think a tautology can happen here ...
                    DEV_ASSURE(learnedClause.count(-lit) == 0, "Tautology via " << lit << ": " << learnedClause);
                    learnedClause.insert(lit);
                }
            }

            // DEV_PRINT("learn " << ctx.clause(step.reason) << " learned clause: " << learnedClause);

            if (learnedClause.empty()) {
                // UNSAT Case: CDCL resolves an empty clause
                NO_SOLUTION("CDCL resolved to empty learned clause");
            }

            // NOTE: This loop should in theory always lead to a 1-UIP clause,
            //  but I am not sure this is always the case
        }

        // As we resolve via the assigned unit literals and the conflict clause,
        //  there should be no assigned literal in the learned clause.
        DEV_ONLY(
            for (LiteralID lit: learnedClause) {
                auto& var = ctx.variable(toVariable(lit));
                ASSURE(var.assigned, "Unassigned Variable in learned clause " << lit);
                ASSURE(var.value == isNegated(lit), "Assigning Variable in learned clause " << lit);
            }
        )

        LiteralID asserting;

        // (2) unwind till the learned clause becomes unassigned
        // size_t unassignedCount = 0;
        while (true) {
            ASSURE(!trail.empty(), "Missing asserting literal");

            auto& step = trail.back();
            asserting = toLiteral(step.var, ctx.variable(step.var).value);
            bool positiveAsserting = learnedClause.find(asserting) != learnedClause.end();

            DEV_ASSURE(learnedClause.count(-asserting) == 0, "Positive Asserting " << asserting);

            // Found a Variable that currently has a truth value so that the clause is not satisfied.
            // After we unassign it, the clause has one unassigned variable, and visiting it causes unit propagation
            if (positiveAsserting) {
                // NOTE: An alternative approach would be to unwind just before the second assignment.
                // This undoes unrelated unit propagation steps and unrelated guesses - but this seems to be counter productive
                // unassignedCount += 1;
                // if (unassignedCount == 2) break;

                // DEV_PRINT("Found asserting literal " << step.var);

                // Instead just stop unwinding here:
                unassign(step.var);
                break;
            }

            // DEV_PRINT("Unwind " << step.var);
            unassign(step.var);
        }

        DEV_PRINT("Learned clause: " << learnedClause);
        perf.learnedClauses++;

        // Add the learned clause to the Graph
        ClauseID id = ctx.addClause(learnedClause);
        DEV_ONLY(ctx.consistencyCheck());

        // Directly visit it, to trigger unit propagation of the unwound variable,
        // potentially recursing into unit propagation and further conflicts
        auto result = visitClause(id, trail.back().var, /* mustBeUnit */ true);
        return (result == Assignment::UNSAT);
    }

    /// Visits a clause and checks whether the clause is assigned to true or false.
    /// If it is not assigned and only one unassigned variable is left, perform unit propagation
    Assignment visitClause(ClauseID id, VariableID fromVariable, bool mustBeUnit = false) {
        auto& it = ctx.clause(id);

        // Already cached, short circuit
        if (it.assignment != Assignment::UNASSIGNED) {
            DEV_ASSURE(!mustBeUnit, "What?");
            return it.assignment;
        }

        LiteralID unassigned = NO_LITERAL;
        size_t unassignedCount = 0;
        for (const auto& lit: it.literals) {
            auto& var = ctx.variable(toVariable(lit));
            if (!var.assigned) {
                unassignedCount++;
                unassigned = lit;
            } else if (var.value == !isNegated(lit)) {
                ASSURE(!mustBeUnit, "Expected Unit Propagation");

                // Cache in the Clause
                it.assignment = Assignment::SAT;
                // NOTE: Due to Unit Propagation, this might not be fromVariable
                it.byVariable = toVariable(lit);
                DEV_PRINT("C" << id << " sat by L" << lit);

                return Assignment::SAT;
            }
        }

        if (unassignedCount == 0) {
            DEV_PRINT("C" << id << " unsat");
            ASSURE(!mustBeUnit, "Expected Unit Propagation");

            // Cache in the clause
            it.assignment = Assignment::UNSAT;
            it.byVariable = fromVariable;

            return Assignment::UNSAT;
        }

        // Unit Propagation:
        if (unassignedCount == 1) {
            perf.unitProps++;
            it.assignment = Assignment::SAT;
            it.byVariable = toVariable(unassigned);
            DEV_PRINT("C" << id << " sat by unit prop " << unassigned);

            // Assign the unit variable, potentially recursing into unit propagation 
            bool conflict = assign(toVariable(unassigned), !isNegated(unassigned), id);

            if (conflict) {
                // Handle the case that we learned a unit clause that conflicts
                // NOTE: I am not sure anymore if this handling is still necessary
                if (it.literals.size() == 1) {
                    NO_SOLUTION("Conflict during Unit Propagation " << unassigned);
                }
                unassign(toVariable(unassigned));
                return Assignment::UNSAT;
            }

            return Assignment::SAT;
        }

        ASSURE(!mustBeUnit, "Expected Unit Propagation");
        return Assignment::UNASSIGNED;
    }

    struct TrailStep {
        /// The variable assigned in this step
        VariableID var;
        /// The reason clause for unit propagation. During CDCL we resolve along this.
        /// If 0, the variable was assigned by guessing
        ClauseID reason = 0;
    };

    /// The trail for unwinding and conflict clause learning
    ///
    /// During unit propagation the trail is depth first,
    /// e.g. we propagate A -> B, B -> C, A -> D, then the trail is A, B, C, D.
    /// Therefore there might not be a resolvent between consecutive entries on the stack.
    std::vector<TrailStep> trail;

    /// Prints the whole trail
    void printTrail(std::ostream& os) {
        for (const auto& step: trail) {
            os << step.var << (step.reason != 0 ? "" : "u") << " ";
        }
        os << "\n";
    }

    /// Prints the part of the trail used for conflict learning
    void printConflicts(std::ostream& os) {
        for (const auto& step: std::views::reverse(trail)) {
            if (step.reason == 0) return;
            os << step.var << " C" << step.reason << " = " << ctx.clauses.find(step.reason)->second << "\n";
        }
    }
};

// --------------------- MAIN -----------------------------------------

int main(int argc, char* argv[]) {
    std::cerr << "SAT Solver (Jonas Wilms)\n";

    GraphContext ctx;

    ASSURE(argc <= 2, "Usage: ./sat <file?>");
    if (argc == 2) {
        char* filename = argv[1];

        std::fstream fs;
        fs.open(filename, std::fstream::in);

        FileParser(ctx, fs);
    } else {
        FileParser(ctx, std::cin);
    }

    // Exclude file opening time from measurements to make them more stable
    restartTime();

    DEV_ONLY(ctx.print(std::cerr));

    Simplifier{ctx};
    DEV_ONLY(ctx.print(std::cerr));

    CDCL{ctx};
    DEV_ONLY(ctx.print(std::cerr));
    ASSURE(false, "CDCL exited");

    return 0;
}
