#pragma once

#include "./parse.h"

class ValueIterator {
public:
    ValueIterator(uint32_t value):
        value(value) {};
    
    uint32_t operator*() { return value; }
    void operator++() { value += 1; }
    bool operator!=(const ValueIterator& other) { return value != other.value; }
private:
    uint32_t value;
};

class RangeIterator {
public:
    RangeIterator(uint32_t start, uint32_t finish):
        start(std::min(finish, start)), finish(finish) {}

    auto begin() { return ValueIterator(start); }
    auto end() { return ValueIterator(finish); }
private:
    uint32_t start;
    uint32_t finish;
};

class Field2D {
public:
    void init(uint32_t value_count, uint32_t n) {
        this->value_count = value_count;

        row_size = n;
        field_size = row_size * row_size;
        fields.resize(field_size);
    }

    uint8_t& field(uint32_t x, uint32_t y) {
        return fields[y * row_size + x];
    }

    auto rows(uint32_t start = 0) { return RangeIterator(start, row_size); }
    auto columns(uint32_t start = 0) { return RangeIterator(start, row_size); }
    auto values() { return RangeIterator(1, value_count + 1); }

    // Field and Value -> SAT Variable
    uint32_t field_value(uint32_t x, uint32_t y, uint8_t value) {
        DEV_ASSURE(value <= value_count, "");
        DEV_ASSURE(value > 0, "");
        return (y * row_size + x) * value_count + (uint32_t) value;
    }

    void read(std::istream& in) {
        for (size_t i = 0; i < field_size; i++)
        {
            fields[i] = readDigits(in);
        }
    }

    void print() {
        for (uint32_t x = 0; x < row_size; x++) {
            for (uint32_t y = 0; y < row_size; y++) {
                std::cerr << (int) field(x, y) << " ";
            }
            std::cerr << "\n\n";
        }
    }

private:
    std::vector<uint8_t> fields;
    uint32_t value_count = 0;
    uint32_t row_size = 0;
    uint32_t field_size = 0;
};
