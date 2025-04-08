#include "../common/generate.h"

// --- SQRT of an integer (https://stackoverflow.com/a/63457507/5260024) ---
unsigned char bit_width(unsigned long long x) {
    return x == 0 ? 1 : 64 - __builtin_clzll(x);
}

template <typename Int>
Int fast_sqrt(const Int n) {
    unsigned char shift = bit_width(n);
    shift += shift & 1; // round up to next multiple of 2

    Int result = 0;

    do {
        shift -= 2;
        result <<= 1; // make space for the next guessed bit
        result |= 1;  // guess that the next bit is 1
        result ^= result * result > (n >> shift); // revert if guess too high
    } while (shift != 0);

    return result;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: ./pythagorean_triples <N>";
        return 1;
    }

    int n = std::stoi(argv[1]);

    problem.add_header(n, n * 2);

    for (int c = 1; c < n; c++) {
        for (int b = 1; b < c; b++) {
            int a_square = (c * c) - (b * b);
            int a = fast_sqrt(a_square);
            if (a * a == a_square && a <= b) {
                DEV_PRINT(a << "^2 + " << b << "^2 = " << c << "^2");
                problem.add_clause(negate(a),  negate(b), negate(c));
                problem.add_clause(a, b, c);
            }
        }
    }

    auto solution = problem.solve();
    if (solution != Result::SAT) {
        PRINT("Unsolvable");
        return 0;
    }

    PRINT("Found solution in " << duration());
    for (int c = 1; c < n; c++) {
        if (problem.get_assignment(c)) {
            PRINT(c << " is black");
        } else {
            PRINT(c << " is white");
        }
    }
}
