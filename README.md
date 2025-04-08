# SATellite

A naive solver for the boolean satisfiability problem (SAT).

To build locally, install Clang then run `./build`.
Run `./test` to run the SAT solver on all CNF files in the tests/ folder, writing results to .out files.

## Generators

The `./generators` folder contains scripts that generates DIMACS files for mathematical problems that can be mapped to a SAT problem. Run them with e.g. `./generators/pythagorean_triples 100 | ./sat` to generate and solve them.

### ./generators/pythagoran_triples <n>

Given all triples `{ (a, b, c) | c ∈ (1, n), a^2 + b^2 = c^2 }`, find a black/white coloring for the numbers a, b and c so that for every triple, not all three numbers have the same color.

### ./generators/graph_coloring <DIMACS graph file>

Colors the graph using the least number of colors such that all neighbouring nodes have a different color.

### ./generators/sudoku <sudoku file>

Encodes a variable size sudoku into SAT.
