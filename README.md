# SATellite

A naive solver for the boolean satisfiability problem (SAT) as well as various generators for SAT problems.
Also embeds [Kissat](https://github.com/arminbiere/kissat/) for solving the problems using a not-so-naive solver.

To build locally, install Clang then run `./install` (which clones and builds Kissat), then `./build`.
Run `./test` to run the SAT solver on all CNF files in the tests/ folder, writing results to .out files.

## Generators

The `./generators` folder contains scripts that generates SAT problems for other mathematical problems. They can be built in two flavours (switch in [config.h](./common/config.h), then re`./build`):
- When generating DIMARC files they produce a file that can be used as input for a SAT solver, e.g. `./generators/pythagorean_triples 100 | ./sat`. This does not work for iterative problems and only produces the first iteration
- It directly runs the Kissat Solver to solve the problem

### `./generators/pythagoran_triples <n>`

Given all triples `{ (a, b, c) | c ∈ (1, n), a^2 + b^2 = c^2 }`, find a black/white coloring for the numbers a, b and c so that for every triple, not all three numbers have the same color.

### `./generators/graph_coloring <DIMACS graph file>`

Colors the graph using the least number of colors such that all neighbouring nodes have a different color.

### `./generators/sudoku <sudoku file>`

Encodes a variable size sudoku into SAT.
