#include "vex.h"
#include "re.h"

#include <stdio.h>

static i32 failures = 0;
static i32 checks = 0;

static fn check(bool condition, const char *label)
{
    ++checks;
    if (!condition)
    {
        printf("FAIL %s\n", label);
        ++failures;
    }
}

i32 main()
{
    CharSequence alpha = CharSequence_cstr("alpha");
    CharSequence alphaCopy = CharSequence_cstr("alpha");
    CharSequence beta = CharSequence_cstr("beta");
    CharSequence empty = CharSequence_zero();

    check(CharSequence_length(alpha) == 5, "length");
    check(CharSequence_isEmpty(empty), "empty view");
    check(!CharSequence_isEmpty(alpha), "non-empty view");
    check(CharSequence_compare(alpha, alphaCopy) == 0, "equal compare");
    check(CharSequence_compare(alpha, beta) < 0, "shorter prefix sorts first");
    check(CharSequence_compare(beta, alpha) > 0, "reverse compare");
    check(CharSequence_compare(empty, empty) == 0, "two empty views");
    check(CharSequence_compare(empty, alpha) < 0, "empty before text");
    check(CharSequence_equals(alpha, alphaCopy), "equals");
    check(!CharSequence_equals(alpha, beta), "not equals");

    // A prefix that is also a prefix of the longer string.
    check(CharSequence_compare(CharSequence_cstr("ab"), CharSequence_cstr("abc")) < 0,
          "prefix is less, not equal");

    check(CharSequence_startsWith(alpha, CharSequence_cstr("alp")), "starts with");
    check(!CharSequence_startsWith(alpha, CharSequence_cstr("lph")), "not starts with");
    check(CharSequence_endsWith(alpha, CharSequence_cstr("pha")), "ends with");
    check(!CharSequence_endsWith(alpha, CharSequence_cstr("alpha!")) , "suffix longer than view");
    check(CharSequence_hasPrefix(alpha, "al"), "has prefix literal");

    check(CharSequence_find(alpha, CharSequence_cstr("ph")) == 2, "find substring");
    check(CharSequence_find(alpha, CharSequence_cstr("zz")) == SIZE_MAX, "find absent");
    check(CharSequence_find(alpha, empty) == 0, "find empty needle");
    check(CharSequence_findChar(alpha, 'h') == 3, "find char");
    check(CharSequence_findChar(alpha, 'z') == SIZE_MAX, "find absent char");

    // Bytes with no terminator: the reason this class exists.
    const char raw[3] = {'x', 'y', 'z'};
    CharSequence unterminated = CharSequence_2(raw, 3);
    check(CharSequence_length(unterminated) == 3, "unterminated length");
    check(CharSequence_equals(unterminated, CharSequence_cstr("xyz")),
          "unterminated compares equal to a C string");
    check(CharSequence_find(unterminated, CharSequence_cstr("yz")) == 1,
          "unterminated search stays inside the bound");

    // A view past a C string's length must not see the terminator or beyond.
    CharSequence head = CharSequence_2("abc", 2);
    check(CharSequence_compare(head, CharSequence_cstr("abc")) < 0, "bounded view is shorter");
    check(CharSequence_findChar(head, 'c') == SIZE_MAX, "search honours the bound");

    if (failures == 0)
    {
        printf("primitive/char_sequence: %d checks passed\n", checks);
        return 0;
    }
    printf("primitive/char_sequence: %d of %d check(s) failed\n", failures, checks);
    return 1;
}
