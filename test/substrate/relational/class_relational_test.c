// class_relational_test/test.c — verify the Relational spotlight (SymbolTable).
#include <stdio.h>
#include <string.h>

#include "substrate/relational/symbol_table.h"
#include "substrate/relational/relational.h"
#include "nio/mem.h"

static int g_failures = 0;
#define CHECK(cond) do{ if(!(cond)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); g_failures++; } }while(0)

// Exercises relational name/value/function lookup, ranked scopes, and rename behavior.
static fn testRelationalSpotlight(){
    SymbolTable global = SymbolTable();
    SymbolTable local = SymbolTable();
    CHECK(global.active);
    CHECK(local.active);
    Relational_setString(&global, "health", "100");
    Relational_setString(&global, "health_ui", "panel");
    Relational_setString(&global, "health_progress_bar", "bar");
    Relational_setString(&local, "hp_text", "42");
    Relational_setString(&local, "mana", "50");

    i32 out[10];
    bytes n = Relational_search(&global, "health", out, 10);
    CHECK(n==3);
    CHECK(strcmp(SymbolTable_getName(&global, out[0]), "health")==0);

    n = Relational_search(&global, "health_", out, 10);
    CHECK(n==2);

    i32 outAll[10];
    n = Relational_searchAll(&global, &local, "health", outAll, 10);
    CHECK(n==3);
    n = Relational_search(&global, "HEALTH", out, 10);
    CHECK(n==3);

    any p = Relational_getValue(&global, "health_ui");
    CHECK(p != nullptr);
    CHECK(strcmp(Relational_getString(&global, "health_ui"), "panel")==0);
    any fn = (any) 0x12345;
    Relational_setFunction(&global, "onUpdate", fn);
    CHECK(Relational_getFunction(&global, "onUpdate")==fn);

    CHECK(Relational_setName(&global, "mana", "mana2")==false);
    CHECK(Relational_setName(&local, "mana", "mana2")==true);
    CHECK(Relational_getId(&local, "mana2")>=0);

    SymbolTable_shutdown(&global);
    SymbolTable_shutdown(&local);
}

// Runs the relational spotlight scenario and returns its accumulated result.
int main(){
    if (!Memory_init(0u)) return 1;
    testRelationalSpotlight();
    if(g_failures==0) printf("class_relational_test: all checks passed\n");
    else printf("class_relational_test: %d FAILURES\n", g_failures);
    return g_failures==0?0:1;
}
