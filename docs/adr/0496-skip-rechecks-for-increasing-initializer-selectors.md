# Skip rechecks for increasing initializer selectors

The generated kernel symbol table contains more than 32,000 consecutive
initializer clauses. CupidC had checked each new selector against every
earlier selector during parsing, validation and emission.

Track the largest selector seen in each list. A larger selector is unique
by construction. Smaller or equal selectors still use the duplicate check,
and all clauses keep their original order. The same rule applies to the
typed frontend, automatic aggregate lowering and static object emission.

The production symbol table compiles to the same 132,492-byte object. A
separate test covers out-of-order selectors and checks that a duplicate
preserves the previous object.
