#pragma once

#include <string>
#include <iostream>
#include <type_traits>

#define header(variables, clauses) std::cout << "p cnf " << variables << " " << clauses << "\n";
#define comment(str) std::cout << "c " << str << "\n"
#define clause(str) std::cout << str << " 0\n"