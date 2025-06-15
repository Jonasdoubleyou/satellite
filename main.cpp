#include "common/utils.h"

#define SOLUTION_FOUND(assignment) \
  std::cerr << "\n\nSolution Found after " << duration() << ":\n"; \
  assignment.print(std::cout, true); \
  exit(0);

#define NO_SOLUTION(details) std::cerr << "\n\nNo Solution possible after " << duration() << ": " << details << "\n"; std::cout << "UNSAT\n"; exit(1);


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



// --------------------- Builder -----------------------------------------

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

        GraphSolver::run(fs);
    } else {
        // Exclude file opening time from measurements to make them more stable
        restartTime();

        GraphSolver::run(std::cin);
    }

    return 0;
}
