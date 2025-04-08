#pragma once

// Enables additional debug printing (reducing performance)
// #define DEBUG true

// What should the generators do?
// (1) Directly call Kissat via IPASIR and solve the problem
// #define SOLVE_WITH_KISSAT true
// (2) Produce a DIMARCS file with the problem
#define PRODUCE_DIMARCS true