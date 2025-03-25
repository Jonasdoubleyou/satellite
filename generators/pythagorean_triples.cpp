#include "./generator.h"

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

    header(n, n * 2);

    for (int c = 1; c < n; c++) {
        for (int b = 1; b < c; b++) {
            int a_square = (c * c) - (b * b);
            int a = fast_sqrt(a_square);
            if (a * a == a_square && a <= b) {
                comment(a << "^2 + " << b << "^2 = " << c << "^2");
                clause(
                    "-" << a << " -" << b << " -" << c
                );
                clause(
                    a << " " << b << " " << c
                );
            }
        }
    }
}
