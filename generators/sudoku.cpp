#include "../common/utils.h"
#include "../common/generate.h"
#include "../common/field.h"

uint32_t region_size = 0;
auto field = Field2D();


void parse(std::istream &in)
{
    region_size = readDigits(in);
    PRINT("Sudoku " << region_size << " x " << region_size);
    field.init(region_size * region_size, region_size * region_size);
    field.read(in);
}


void run() {
    field.print();

    // Minimal Clauses according to https://sat.inesc-id.pt/~ines/publications/aimath06.pdf
    // Extended Clauses commented out

    problem.add_header(field.variable_count(), 0);

    DEV_PRINT("-- Cells")
    for (uint32_t x: field.columns()) {
        for (uint32_t y: field.rows()) {
            DEV_PRINT("- Cell " << x << "|" << y << " must have at least one value");
            for (uint32_t value: field.values()) {
                problem.add_literal(field.field_value(x, y, value));
            }
            problem.end_clause();
        }
    } 

    DEV_PRINT("-- Rows")
    for (uint32_t row: field.rows()) {
        for (uint32_t value: field.values()) {
            /* DEV_PRINT("- There must be a " << value << " in row " << row);
            for (uint32_t col = 0; col < row_size; col++) {
                problem.add_literal(field_value(row, col, value));
            }
            problem.end_clause(); */

            DEV_PRINT("- There must be only one " << value << " in row " << row);
            for (uint32_t col: field.columns()) {
                for (uint32_t col2: field.columns(col + 1)) {
                    // not((0|0) = 1 and (0|1) = 1) => (not((0|0) = 1) or not((0|1) = 1))
                    problem.add_clause(negate(field.field_value(row, col, value)), negate(field.field_value(row, col2, value)));
                }
            }
            
        }
    }

    DEV_PRINT("-- Columns");
    for (uint32_t col: field.columns()) {
        for (uint32_t value: field.values()) {
            /* DEV_PRINT("-- There must be a " << value << " in col " << col);
            for (uint32_t row = 0; row < row_size; row++) {
                problem.add_literal(field_value(row, col, value));
            }
            problem.end_clause(); */

            DEV_PRINT("- There must be only one " << value << " in col " << col);
            for (uint32_t row: field.rows()) {
                for (uint32_t row2: field.rows(row + 1)) {
                    problem.add_clause(negate(field.field_value(row, col, value)), negate(field.field_value(row2, col, value)));
                }
            }
        }
    }

    DEV_PRINT("-- Regions");
    for (uint32_t region_x = 0; region_x < region_size; region_x++) {
        for (uint32_t region_y = 0; region_y < region_size; region_y++) {
            for (uint32_t value: field.values()) {
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
                                    negate(field.field_value(x, y, value)),
                                    negate(field.field_value(x2, y2, value)));

                            }
                        }
                    }
                }
            }
        }
    }

    DEV_PRINT("-- Assignments")
    field.assign_fields(problem);

    auto solution = problem.solve();
    if (solution != Result::SAT) {
        field.print();
        PRINT("Unsolvable");
        return;
    }
    
    PRINT("Solved in " << duration());
    
    for (uint32_t x: field.columns()) {
        for (uint32_t y: field.rows()) {
            uint8_t existing_value = field.field(x, y);
            if (existing_value <= 0) {
                for (uint32_t value: field.values()) {
                    if (problem.get_assignment(field.field_value(x, y, value))) {
                        ASSURE(field.field(x, y) == 0, "Duplicate assignment to " << x << "|" << y << " = " << value << " = " << (int)field.field(x, y));
                        field.field(x, y) = value;
                        
                    }
                }
            }
        }
    }

    field.print();    
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