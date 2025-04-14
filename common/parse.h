#include "./utils.h"

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
