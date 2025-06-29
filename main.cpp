#include "common/utils.h"
#include <ranges>

#define SOLUTION_FOUND(assignment) \
  std::cerr << "\n\nSolution Found after " << duration() << ":\n"; \
  assignment.print(std::cout, true); \
  exit(0);

#define NO_SOLUTION(details) { std::cerr << "\n\nNo Solution possible after " << duration() << ": " << details << "\n"; std::cout << "UNSAT\n"; exit(1); }


// --------------------- Literals -----------------------------------------

// A Variable ID in [1, max_uint32)
using VariableID = uint32_t;
// A Literal ID in (-max_uint32, max_uint32)
// -N = NOT N
using LiteralID = int;
// A LiteralID that does not exist. Can be used to delete literals in place
constexpr LiteralID NO_LITERAL = 0;

template<typename T>
std::ostream& operator<<(std::ostream& out, const std::set<T>& lits) {
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

class Clause {
public:
    std::set<LiteralID> literals;

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


class Variable {
public:
    std::set<ClauseID> positiveClauses;
    std::set<ClauseID> negativeClauses;

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


class GraphContext {
public:
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

    ClauseID addClause(std::set<LiteralID> literals) {
        ClauseID clauseID = clauses.size() + 1;
        auto clause_it = clauses.try_emplace(clauseID);
        auto& clause = clause_it.first->second;
        clause.literals = std::move(literals);
        clause.assignment = Assignment::UNASSIGNED;

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

    bool hasClause(ClauseID id) const {
        return clauses.find(id) != clauses.end();
    }
    
    Clause& clause(ClauseID id) {
        auto it = clauses.find(id);
        DEV_ASSURE(it != clauses.end(), "Lost clause " << id)
        return it->second;
    }

    bool hasVariable(VariableID id) const {
        return variables.find(id) != variables.end();
    }

    Variable& variable(VariableID id) {
        auto it = variables.find(id);
        DEV_ASSURE(it != variables.end(), "Lost variable " << id)
        return it->second;
    }

    void assign(VariableID id, bool value) {
        auto& var = variable(id);
        if (var.assigned) {
            if (value != var.value) {
                NO_SOLUTION("Conflicting assignment for " << id);
            }
            return;
        }

        size_t count = unassignedVariables.erase(id);
        DEV_ASSURE(count == 1, "Unassigned Variable Chaos " << id);

        var.assigned = true;
        var.value = value;
    }

    void unassign(VariableID id) {
        auto& var = variable(id);
        DEV_ASSURE(var.assigned, "unassign assigned");

        unassignedVariables.insert(id);
        var.assigned = false;
    }


    std::map<ClauseID, Clause> clauses;
    std::map<VariableID, Variable> variables;

    // Collect during file parsing
    std::vector<ClauseID> unitClauses;
    std::set<VariableID> unassignedVariables;
};



// --------------------- Builder -----------------------------------------

// Builds a Context from a DIMACS CNF file
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
            ctx.addClause(std::move(currentLiterals));
            DEV_ASSURE(currentLiterals.empty(), "move");
        } else {
            currentLiterals.insert(literal);
        }
    }

private:
    std::set<LiteralID> currentLiterals;
};



class Simplifier {
public:
    Simplifier(GraphContext& ctx): ctx(ctx) {
       if (!ctx.unitClauses.empty()) {
            PRINT("Simplify Units");
            for (ClauseID id: ctx.unitClauses) {
                if (ctx.clauses.find(id) != ctx.clauses.end()) {
                    visitClause(id);
                }
            }
            
            ctx.unitClauses.clear();
            PRINT("= done after " << duration());
       }

       PRINT("Simplify with Pure Literal Elimination")
       /* auto unassigned = ctx.unassignedVariables;
       for (auto& id: unassigned) {
            if (ctx.variables.find(id) != ctx.variables.end()) {
                visitVariable(id);
            }
       } */
       PRINT("= done after " << duration());
    }

private:
    GraphContext& ctx;

    void visitClause(ClauseID id) {
        if (!ctx.hasClause(id)) return;

        DEV_PRINT("Visit Clause " << id);
        auto& it = ctx.clause(id);

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

    void removeFromVariable(LiteralID lit, ClauseID id) {
        auto& var = ctx.variable(toVariable(lit));
        if (var.assigned) return;

        DEV_PRINT("Remove from Variable " << lit << " " << id);

        if (isNegated(lit)) {
            size_t count = var.negativeClauses.erase(id);
            DEV_ASSURE(count == 1, lit << " C" << id);
        } else {
            size_t count = var.positiveClauses.erase(id);
            DEV_ASSURE(count == 1, lit << " C" << id);
        }
    }

    void removeFromClause(ClauseID id, LiteralID lit) {
        DEV_PRINT("Remove from Clause " << id << " " << lit);
        auto& it = ctx.clause(id).literals;
        size_t count = it.erase(lit);
        DEV_ASSURE(count == 1, "C" << id << " " << lit);
    }

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

    void assignVariable(VariableID id, bool value) {
        if (!ctx.hasVariable(id)) return;

        ctx.assign(id, value);

        auto& var  = ctx.variable(id);
        std::set<ClauseID> pos = std::move(var.positiveClauses);
        var.positiveClauses.clear();
        std::set<ClauseID> neg = std::move(var.negativeClauses);
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

class DPLL {
public:
    GraphContext& ctx;

    DPLL(GraphContext& ctx): ctx(ctx) {
        iterate();
    }

    void iterate() {
        std::set<VariableID> variables = ctx.unassignedVariables;
        if (variables.empty()) return;

        for (VariableID id: variables) {
            if (!ctx.variable(id).assigned) {
                bool conflict = assign(id, true, 0);
                if (conflict) {
                    unassign(id);
                    bool second_conflict = assign(id, false, 0);
                    DEV_ASSURE(!second_conflict, "Second conflict");
                }
                iterate();

                // TODO: Is it sufficient to only assign T?
            }
        }
    }

private:
    bool assign(VariableID id, bool value, ClauseID reason = 0) {
        auto& var = ctx.variable(id);
        if (var.assigned && var.value == value) return false;

        DEV_PRINT(id << " = " << (value ? "T" : "F"));
        ctx.assign(id, value);
        trail.push_back({ .var = id, .reason = reason });

        for (auto& clause: *(value ? &var.negativeClauses : &var.positiveClauses)) {
            auto& prevAssignment = ctx.clause(clause).assignment;
            if (prevAssignment == Assignment::SAT) continue;
            DEV_ASSURE(prevAssignment != Assignment::UNSAT, "How?");

            Assignment result = visitClause(clause, id);
            if (result == Assignment::UNSAT)
            {
                DEV_PRINT("Conflict C" << clause << " = " << ctx.clause(clause));
                DEV_ONLY(printConflicts(std::cerr));
                if (reason != 0)
                    return learnClause(clause);
                return false;
            }
        }

        if (ctx.unassignedVariables.size() == 0) {
            DEV_PRINT("All Variables assigned without conflicts");
            SOLUTION_FOUND(ctx);
        }

        return false;
    }

    void unassign(VariableID id) {
        auto& var = ctx.variable(id);
        DEV_ASSURE(var.assigned, "Double unassign");
        DEV_ASSURE(!trail.empty(), "Empty trail");
        DEV_ASSURE(trail.back().var == id, "Wrong unassign " << trail.back().var << " != " << id);
        trail.pop_back();

        DEV_PRINT(id << " = ?");
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

    bool learnClause(ClauseID clause) {
        std::set<LiteralID> learnedClause;
        std::set<VariableID> units;

        learnedClause = ctx.clause(clause).literals;
        
        while (!trail.empty()) {
            auto step = trail.back();

            // Always resolve the full clause
            if (step.reason == 0) break;

            unassign(step.var);

            units.insert(step.var);
            learnedClause.erase(toLiteral(step.var, true));
            learnedClause.erase(toLiteral(step.var, false));
            
            for (LiteralID lit: ctx.clause(step.reason).literals) {
                if (units.find(toVariable(lit)) == units.end()) {
                    learnedClause.insert(lit);
                } else {
                }
            }

            DEV_PRINT("learn " << ctx.clause(step.reason) << " learned clause: " << learnedClause << " units: " << units);

            if (learnedClause.empty()) {
                NO_SOLUTION("CDCL resolved to empty learned clause");
            }
        }

        LiteralID asserting; bool negativeAsserting;

        while (true) {
            ASSURE(!trail.empty(), "Missing asserting literal");

            auto& step = trail.back();
            asserting = toLiteral(step.var, ctx.variable(step.var).value);
            bool positiveAsserting = learnedClause.find(asserting) != learnedClause.end();
            negativeAsserting = learnedClause.find(-asserting) != learnedClause.end();

            if (positiveAsserting || negativeAsserting) {
                DEV_PRINT("Found asserting literal " << step.var);
                unassign(step.var);
                break;
            }

            DEV_PRINT("Unwind " << step.var);

            unassign(step.var);
        }

        PRINT("Learned clause: " << learnedClause);
        ClauseID id = ctx.addClause(learnedClause);

        /* if (negativeAsserting) {
            DEV_PRINT("Negatively asserting literal " << asserting);
            unassign(toVariable(asserting));
            trail.pop_back();
            assign(toVariable(asserting), isNegated(asserting), id);
        } */

        auto result = visitClause(id, trail.back().var, /* mustBeUnit */ true);
        return (result == Assignment::UNSAT);
    }

    Assignment visitClause(ClauseID id, VariableID fromVariable, bool mustBeUnit = false) {
        auto& it = ctx.clause(id);
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
                it.byVariable = toVariable(lit);
                // NOTE: Due to Unit Propagation, this might not be fromVariable

                return Assignment::SAT;
            }
        }

        if (unassignedCount == 0) {
            ASSURE(!mustBeUnit, "Expected Unit Propagation");
            it.assignment = Assignment::UNSAT;
            it.byVariable = fromVariable;

            return Assignment::UNSAT;
        }

        // Unit Propagation:
        if (unassignedCount == 1) {
            it.assignment = Assignment::SAT;
            it.byVariable = toVariable(unassigned);

            bool conflict = assign(toVariable(unassigned), !isNegated(unassigned), id);
            if (conflict) {
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
        VariableID var;
        ClauseID reason = 0;
    };

    std::vector<TrailStep> trail;

    void printTrail(std::ostream& os) {
        for (const auto& step: trail) {
            os << step.var << (step.reason != 0 ? "" : "u") << " ";
        }
        os << "\n";
    }

    void printConflicts(std::ostream& os) {
        for (const auto& step: std::views::reverse(trail)) {
            if (step.reason == 0) return;
            os << step.var << " C" << step.reason << " = " << ctx.clauses.find(step.reason)->second << "\n";
        }
    }
};


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

    DPLL{ctx};
    DEV_ONLY(ctx.print(std::cerr));
    NO_SOLUTION("DPLL exited");

    return 0;
}
