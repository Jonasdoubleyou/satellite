#include "../common/field.h"

auto field = Field2D();
auto regions = Field2D();

void parse(std::istream &in)
{
    size_t size = readDigits(in);
    PRINT("Suguru " << size << " x " << size);
    field.init(size, size);
    field.read(in);

    regions.init(size, size);
    regions.read(in);
}

void run() {
    PRINT("Field: ")
    field.print();
    PRINT("Regions: ")
    regions.print();


}

int main(int argc, char* argv[]) {
    PRINT("Suguru");
    ASSURE(argc <= 2, "Usage: ./suguru <suguru file?>");
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