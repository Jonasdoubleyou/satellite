#pragma once

#include "./utils.h"
#include "./kissat.h"

#define negate(var) -(int)(var)

template<typename Base>
class ProblemBase: public Base {
public:
    template<typename ...Types>
    void add_literal(int lit, Types... literals) {
        this->add_one_literal(lit);
        if constexpr (sizeof...(literals) > 0)
            this->add_literal(literals...);
    }

    template<typename ...Types>
    void add_clause(Types... literals) {
        DEV_PRINT("add clause");
        this->add_literal(literals...);
        this->end_clause();
    }
};

enum Result {
    SAT = 10,
    UNSAT = 20,
    TERMINATE = 0
};

class DIMACSProblem {
public:
    void add_header(uint32_t variable_count, uint32_t clause_count) {
        std::cout << "p cnf " << variable_count << " " << clause_count << "\n";
    }
 
    void add_one_literal(int lit) { std::cout << lit << " "; }
    void end_clause() { std::cout << "0\n"; }

    Result solve() {
        exit(0); 
        return Result::TERMINATE;
    }

    void clear() {
        ASSURE(false, "not implemented");
    }

    bool get_assignment(int lit) {
        ASSURE(false, "not implemented");
        return false;
    }
};

class KISSATProblem {
public:
    KISSATProblem() { 
        DEV_PRINT("Initializing Kissat");
        instance = kissat_init();
        DEV_PRINT("Initializing Kissat done");
    }

    void add_header(uint32_t variable_count, uint32_t clause_count) {
    }
 
    void add_one_literal(int lit) {
        DEV_ASSURE(lit != 0, "");
        DEV_ONLY(std::cout << lit << " ";);
        kissat_add(instance, lit);
    }

    void end_clause() {
        DEV_ONLY(std::cout << "0\n";);
        kissat_add(instance, 0);
    }

    Result solve() {
        DEV_PRINT("Solve ");
        return Result(kissat_solve(instance));  
    }

    void clear() {
        kissat_release(instance);
        instance = kissat_init();
    }

    bool get_assignment(int lit) {
        DEV_ASSURE(lit != 0, "");
        return kissat_value(instance, lit) > 0;
    }

private:
    kissat* instance;
};

auto problem = ProblemBase<KISSATProblem>();

