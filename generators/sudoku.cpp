#include "../common/utils.h"
#include "../common/generate.h"

std::vector<uint8_t> fields;
uint32_t region_size = 0;
uint32_t row_size = 0;
uint32_t field_size = 0;

uint8_t& field(uint32_t x, uint32_t y) {
    return fields[y * row_size + x];
}

// Field and Value -> SAT Variable
uint32_t field_value(uint32_t x, uint32_t y, uint8_t value) {
    return (y * row_size + x) * row_size + (uint32_t) value;
}

void printField() {
    for (uint32_t x = 0; x < row_size; x++) {
        for (uint32_t y = 0; y < row_size; y++) {
            std::cerr << (int) field(x, y) << " ";
        }
        std::cerr << "\n\n";
    }
}

uint32_t readDigits(std::istream &in)
{
    char cursor;
    ASSURE(in.get(cursor), "");

    uint32_t digits = 0;

    do
    {
        ASSURE(cursor >= '0' && cursor <= '9', "Unexpected character: '" << cursor << "'");
        digits = 10 * digits + (cursor - '0');
    } while (in.get(cursor) && cursor != ' ' && cursor != '\n');

    return digits;
}

void parse(std::istream &in)
{
    region_size = readDigits(in);
    PRINT("Sudoku " << region_size << " x " << region_size);
    row_size = region_size * region_size;
    field_size = row_size * row_size;

    fields.resize(field_size);

    for (size_t i = 0; i < field_size; i++)
    {
        fields[i] = readDigits(in);
    }
}


void run() {
    printField();

    // Minimal Clauses according to https://sat.inesc-id.pt/~ines/publications/aimath06.pdf
    // Extended Clauses commented out

    DEV_PRINT("-- Cells")
    for (uint32_t x = 0; x < row_size; x++) {
        for (uint32_t y = 0; y < row_size; y++) {
            DEV_PRINT("- Cell " << x << "|" << y << " must have at least one value");
            for (uint32_t value = 1; value <= row_size; value++) {
                problem.add_literal(field_value(x, y, value));
            }
            problem.end_clause();
        }
    } 

    DEV_PRINT("-- Rows")
    for (uint32_t row = 0; row < row_size; row++) {
        for (uint32_t value = 1; value <= row_size; value++) {
            /* DEV_PRINT("- There must be a " << value << " in row " << row);
            for (uint32_t col = 0; col < row_size; col++) {
                problem.add_literal(field_value(row, col, value));
            }
            problem.end_clause(); */

            DEV_PRINT("- There must be only one " << value << " in row " << row);
            for (uint32_t col = 0; col < row_size; col++) {
                for (uint32_t col2 = col + 1; col2 < row_size; col2++) {
                    // not((0|0) = 1 and (0|1) = 1) => (not((0|0) = 1) or not((0|1) = 1))
                    problem.add_clause(negate(field_value(row, col, value)), negate(field_value(row, col2, value)));
                }
            }
            
        }
    }

    DEV_PRINT("-- Columns");
    for (uint32_t col = 0; col < row_size; col++) {
        for (uint32_t value = 1; value <= row_size; value++) {
            /* DEV_PRINT("-- There must be a " << value << " in col " << col);
            for (uint32_t row = 0; row < row_size; row++) {
                problem.add_literal(field_value(row, col, value));
            }
            problem.end_clause(); */

            DEV_PRINT("- There must be only one " << value << " in col " << col);
            for (uint32_t row = 0; row < row_size; row++) {
                for (uint32_t row2 = row + 1; row2 < row_size; row2++) {
                    problem.add_clause(negate(field_value(row, col, value)), negate(field_value(row2, col, value)));
                }
            }
        }
    }

    DEV_PRINT("-- Regions");
    for (uint32_t region_x = 0; region_x < region_size; region_x++) {
        for (uint32_t region_y = 0; region_y < region_size; region_y++) {
            for (uint32_t value = 1; value <= row_size; value++) {
                /* DEV_PRINT("Region " << region_x << "|" << region_y << " must contain " << value);
                for (uint32_t inner_x = 0; inner_x < region_size; inner_x++) {
                    for (uint32_t inner_y = 0; inner_y < region_size; inner_y++) {
                        uint32_t y = region_y * region_size + inner_y;
                        uint32_t x = region_x * region_size + inner_x;

                        problem.add_literal(field_value(x, y, value));
                    }
                }

                problem.end_clause(); */

                DEV_PRINT("Region " << region_x << "|" << region_y << " must only contain one " << value);
                for (uint32_t inner_x = 0; inner_x < region_size; inner_x++) {
                    for (uint32_t inner_y = 0; inner_y < region_size; inner_y++) {
                        for (uint32_t inner_x2 = inner_x + 1; inner_x2 < region_size; inner_x2++) {
                            for (uint32_t inner_y2 = inner_y + 1; inner_y2 < region_size; inner_y2++) {
                                uint32_t y = region_y * region_size + inner_y;
                                uint32_t x = region_x * region_size + inner_x;
                                uint32_t y2 = region_y * region_size + inner_y2;
                                uint32_t x2 = region_x * region_size + inner_x2;

                                problem.add_clause(
                                    negate(field_value(x, y, value)),
                                    negate(field_value(x2, y2, value)));

                            }
                        }
                    }
                }
            }
        }
    }

    DEV_PRINT("-- Assignments")
    for (uint32_t x = 0; x < row_size; x++) {
        for (uint32_t y = 0; y < row_size; y++) {
            uint8_t value = field(x, y);
            if (value > 0) {
                problem.add_clause(field_value(x, y, value));
            }
        }
    }

    auto solution = problem.solve();
    if (solution != Result::SAT) {
        printField();
        PRINT("Unsolvable");
        return;
    }
    
    PRINT("Solved in " << duration());
    
    for (uint32_t x = 0; x < row_size; x++) {
        for (uint32_t y = 0; y < row_size; y++) {
            uint8_t existing_value = field(x, y);
            if (existing_value <= 0) {
                for (uint32_t value = 1; value <= row_size; value++) {
                    if (problem.get_assignment(field_value(x, y, value))) {
                        ASSURE(field(x, y) == 0, "Duplicate assignment to " << x << "|" << y << " = " << value << " = " << (int)field(x, y));
                        field(x, y) = value;
                        
                    }
                }
            }
        }
    }

    printField();    
}

int main(int argc, char* argv[]) {
    PRINT("Sudoku");
    ASSURE(argc <= 2, "Usage: ./sudoku <sudoku file?>");
    if (argc == 2) {
        char* filename = argv[1];

        std::fstream fs;
        fs.open(filename, std::fstream::in);
        parse(fs);
    } else {
        parse(std::cin); 
    }

    run();

    return 0;
}