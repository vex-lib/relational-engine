// class_relational_test/test.c — verify the Relational spotlight (SymbolTable).
#include <stdio.h>
#include <string.h>

#include "oop/type.h"
#include "relational/symbol_table.h"
#include "relational/relational.h"
#include "primitive/vex-string.h"
#include "nio/mem.h"
#include "nio/mem.h"

static int g_failures = 0;
#define CHECK(cond) do{ if(!(cond)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); g_failures++; } }while(0)

// Exercises relational name/value/function lookup, ranked scopes, and rename behavior.
static void testRelationalSpotlight(void){
    SymbolTable global = SymbolTable();
    SymbolTable local = SymbolTable();
    CHECK(global.active);
    CHECK(local.active);
    // user vars — health family
    Relational_setString(&global, "health", "100");
    Relational_setString(&global, "health_ui", "panel");
    Relational_setString(&global, "health_progress_bar", "bar");
    Relational_setString(&local, "hp_text", "42");
    Relational_setString(&local, "mana", "50");

    // exact
    int32_t out[10];
    size_t n = Relational_search(&global, "health", out, 10);
    CHECK(n==3); // health, health_ui, health_progress_bar (ranked exact > prefix > substring)
    // verify first is exact "health"
    CHECK(strcmp(SymbolTable_getName(&global, out[0]), "health")==0);

    // prefix vs substring
    n = Relational_search(&global, "health_", out, 10);
    CHECK(n==2); // health_ui, health_progress_bar (prefix)

    // searchAll merges global+local, first 100 cap
    int32_t outAll[10];
    n = Relational_searchAll(&global, &local, "health", outAll, 10);
    // global 3 + local 0 (hp_text doesn't contain health) =3
    CHECK(n==3);
    // case-insensitive
    n = Relational_search(&global, "HEALTH", out, 10);
    CHECK(n==3);

    // getValue / getFunction
    void *p = Relational_getValue(&global, "health_ui");
    CHECK(p != nullptr);
    CHECK(strcmp(string_get((uint8_t*) p), "panel")==0);
    // function pointer round-trip
    void *fn = (void*) 0x12345;
    Relational_setFunction(&global, "onUpdate", fn);
    CHECK(Relational_getFunction(&global, "onUpdate")==fn);

    // getName/setName
    CHECK(Relational_setName(&global, "mana", "mana2")==false); // mana in local, not global
    CHECK(Relational_setName(&local, "mana", "mana2")==true);
    CHECK(Relational_getId(&local, "mana2")>=0);

    SymbolTable_shutdown(&global);
    SymbolTable_shutdown(&local);
}

// Runs the relational spotlight scenario and returns its accumulated result.
int main(void){
    if (!Memory_init(0u)) return 1;
    testRelationalSpotlight();
    if(g_failures==0) printf("class_relational_test: all checks passed\n");
    else printf("class_relational_test: %d FAILURES\n", g_failures);
    return g_failures==0?0:1;
}
