#include "./utils.h"

bool isDigit(char cursor) {
    return cursor >= '0' && cursor <= '9';
}

bool isWhitespace(char cursor) {
    return cursor == ' ' || cursor == '\n';
}

uint32_t readDigits(std::istream &in)
{
    char cursor;

    do {
        ASSURE(in.get(cursor), "Expected digits");
    } while(isWhitespace(cursor));

    uint32_t digits = 0;

    do
    {
        ASSURE(isDigit(cursor), "Unexpected character: '" << cursor << "'");
        digits = 10 * digits + (cursor - '0');
    } while (in.get(cursor) && !isWhitespace(cursor));

    return digits;
}
