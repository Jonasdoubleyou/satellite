#include "../common/utils.h"
#include "../common/generate.h"

struct pair_hash {
  size_t operator() (const std::pair<uint32_t, uint32_t>& p) const {
      return ((size_t)p.first << 32) | p.second;
  }
};

uint32_t max_node = 0;
std::unordered_set<std::pair<uint32_t, uint32_t>, pair_hash> edges;

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
    while (true)
    {
        char cursor;

        if (!in.get(cursor))
            break;
        // Detect non edges
        if (cursor != 'e')
        {
            // Skip comment line
            in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }

        ASSURE(in.get(cursor), "missing space after e");
        ASSURE(cursor == ' ', "Expected space after e, got: " << cursor);

        // Read edges
        uint32_t from = readDigits(in);
        uint32_t to = readDigits(in);
        if (from > max_node) max_node = from;
        if (to > max_node) max_node = to;

        if (edges.find(std::pair(from, to)) == edges.end()) {
            edges.emplace(std::pair(to, from));
        }
    }
}

uint32_t node_color(uint32_t node, uint32_t color) {
    return color * max_node + node;
}

void run() {
    DEV_PRINT("edges: " << edges.size() << " nodes: " << max_node << "\n");   

    for (uint32_t color_count = 2; color_count <= max_node; color_count += 1) {
        problem.add_header(node_color(max_node, color_count), 1);
        DEV_PRINT("--- " << color_count << " colors\n");
        
        DEV_PRINT("-- nodes must have a color");
        
        problem.add_clause(1);

        for (uint32_t node = 2; node <= max_node; node++) {
            DEV_PRINT("- node " << node);
            for (uint32_t color = 0; color < color_count; color++) {
                problem.add_literal(node_color(node, color));
            }
            problem.end_clause();
        }

        DEV_PRINT("-- neighbouring nodes must have a different color");
        for (const auto& [from, to]: edges) {
            DEV_PRINT("- " << from << " -> " << to);
            for (uint32_t color = 0; color < color_count; color++) {
                problem.add_clause(negate(node_color(from, color)), negate(node_color(to, color)));
            }
        }

        // -> both rules imply that no node has two colors

        auto result = problem.solve();
        if (result == Result::SAT) {
            PRINT("Solved with " << color_count << " colors in " << duration());
            for (uint32_t node = 1; node <= max_node; node++) {
                for (uint32_t color = 0; color < color_count; color++) {
                    bool assigned = problem.get_assignment(node_color(node, color));
                    if (assigned) {
                        PRINT("Node " << node << " has Color " << color);
                    }
                }
            }
            return;
        }

        ASSURE(result == Result::UNSAT, "Unexpected termination");
        PRINT("Unsolvable with " << color_count << " colors, repeating");

        problem.clear();
    }
}

int main(int argc, char* argv[]) {
    std::cerr << "Graph Coloring\n";

    ASSURE(argc <= 2, "Usage: ./graph_coloring <dimacs file?>");
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