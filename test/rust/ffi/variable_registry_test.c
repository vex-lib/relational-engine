// Real C client of Rust stable variable rows and engine-owned C name search.
#include "relational_engine/variable_registry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    ReVariableRegistry *owner = nullptr;
    assert(re_variables_new(0, &owner) == 1 && owner == nullptr);
    assert(re_variables_new(2, &owner) == 0);
    uint8_t gold = 42;
    size_t index = 99;
    assert(re_variables_add(owner, (const uint8_t*) "Gold", 4, &gold, &index) == 0 && index == 0);
    const ReVariableSlot *slot = nullptr;
    assert(re_variables_slot(owner, 0, &slot) == 0);
    assert(memcmp((*slot).name, "gold", 4) == 0 && (*slot).name[4] == 0);
    assert((*slot).pointer == &gold);
    const ReVariableSlot *original = slot;
    for (size_t i = 1; i < 1025; ++i) {
        char name[24];
        int length = snprintf(name, sizeof(name), "field%zu", i);
        assert(length > 0 && length < 24);
        assert(re_variables_add(owner, (const uint8_t*) name, (size_t) length, nullptr, &index) == 0 && index == i);
    }
    assert(re_variables_slot(owner, 0, &slot) == 0 && slot == original);
    assert((*slot).pointer == &gold && *(*slot).pointer == 42);
    assert(re_variables_find(owner, (const uint8_t*) "FIELD1024", 9, &index) == 0 && index == 1024);
    index = 99;
    assert(re_variables_add(owner, (const uint8_t*) "gold", 4, nullptr, &index) == 4 && index == 99);
    assert(re_variables_find(owner, (const uint8_t*) "missing", 7, &index) == 3 && index == 99);
    assert(re_variables_add(owner, (const uint8_t*) "a\0b", 3, nullptr, &index) == 1 && index == 99);
    assert(re_variables_slot(owner, SIZE_MAX, &slot) == 3 && slot == original);
    assert(re_variables_set_pointer(owner, 0, nullptr) == 0 && (*slot).pointer == nullptr);
    assert(re_variables_set_pointer(owner, 0, &gold) == 0 && (*slot).pointer == &gold);
    re_variables_drop(owner);
    assert(gold == 42);
    re_variables_drop(nullptr);
    return 0;
}
