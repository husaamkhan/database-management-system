# Grammar

*The following document outlines the grammar rules that any queries to the DB must make in EBNF form.*

---

## Operator Precedence and Associativity

Below is a table listing operator precedence from lowest to highest.

| Level | Operator(s)                  | Type    | Associativity  |
|-------|------------------------------|---------|----------------|
| 1     | `union`, `intersect`, `minus`| binary  | left           |
| 2     | `times`, `join[c]`           | binary  | left           |
| 3     | `select[c]`, `project[a]`, `rename[n]` | unary | --   |
| 4     | relation name, `( expr )`    | atom    |         --      |

**Note:**
- Higher level indicates higher precedence. Operators with higher precedence will bind more tightly to their operands than other operators
- Left-associativity means `A <operator1> B <operator> C` is `(A <operator> B) <operator> C`
- The unary operators (`select`, `project`, `rename`) bind more tightly than any binary operator, so `select[c](R) union S` is
  `(select[c](R)) union S`, never `select[c](R union S)`
- Parentheses override precedence

---

## An ambiguity demonstration

**Naive Grammar**
```
Expr ::= Expr "union" Expr
       | Expr "minus" Expr
       | "(" Expr ")"
       | IDENT
```
Below are 2 parse trees for `A union B Minus C` given the above naive grammar

**Tree 1** `union` applied first: `A union B Minus C` or `(A union B) minus C`

```
        minus
       /     \
    union     C
   /     \
  A       B
```

**Tree 2**  `minus` applied first: `A union (B minus C)`

```
      union
     /     \
    A      minus
           /   \
          B     C
```

### Concrete Instance Showing Different Results

Let the three relations be (each with a single integer attribute `X`):

```
A = { 1, 2, 3 }
B = { 2, 3 }
C = { 1 }
```

**Tree 1: `(A union B) minus C`**

```
A union B = { 1, 2, 3 }   (union removes duplicates)
{ 1, 2, 3 } minus { 1 } = { 2, 3 }
```
Result: `{ 2, 3 }`

**Tree 2: `A union (B minus C)`**

```
B minus C = { 2, 3 }      (1 is not in B, so nothing is removed)
A union { 2, 3 } = { 1, 2, 3 }
```
Result: `{ 1, 2, 3 }`

The two trees produce different results, confirming the grammar is ambiguous.

### The Stratified Grammar That Removes the Ambiguity

The stratified grammar (see Section 5 for the full version) encodes precedence
and left-associativity by splitting `Expr` into one non-terminal per
precedence level:

```
Expr        ::= AddExpr
AddExpr     ::= AddExpr ("union" | "intersect" | "minus") MulExpr
              | MulExpr
MulExpr     ::= MulExpr ("times" | "join" "[" Condition "]") UnaryExpr
              | UnaryExpr
UnaryExpr   ::= "select"  "[" Condition    "]" "(" Expr ")"
              | "project" "[" AttrList     "]" "(" Expr ")"
              | "rename"  "[" Identifier   "]" "(" Expr ")"
              | Atom
Atom        ::= IDENT | "(" Expr ")"
```

**Which tree does it force?**

`A union B minus C` is parsed as `(A union B) minus C`  **Tree 1**.

Both `union` and `minus` live at the same precedence level (`AddExpr`).
Because the rule is left-recursive (`AddExpr ::= AddExpr op MulExpr`), the
parser always reduces the leftmost sub-expression first, producing a
left-leaning tree. The input is therefore read as
`(A union B) minus C`, not `A union (B minus C)`.

---

## EBNF

(* ===================== Top-Level Structure ===================== *)

program              = statement , { statement } ;

statement             = relation-definition
                      | query-expression ;


(* ===================== Relation Definitions ===================== *)

relation-definition   = identifier , "(" , attribute-name-list , ")" ,
                        "=" , "{" , { tuple-row } , "}" ;

attribute-name-list   = identifier , { "," , identifier } ;

tuple-row             = value , { "," , value } , newline ;

(* ================ Query Expressions ================ *)

query-expression      = additive-expr ;

(* Level 1  union / intersect / minus  left-associative *)

additive-expr         = multiplicative-expr , { additive-op , multiplicative-expr } ;

additive-op           = "union" | "intersect" | "minus" ;

(* Level 2  times / join  left-associative *)

multiplicative-expr   = unary-expr , { multiplicative-op , unary-expr } ;

multiplicative-op     = "times"
                      | "join" , "[" , condition , "]" ;

(* Level 3  unary prefix operators *)

unary-expr            = "select"  , "[" , condition          , "]" , "(" , query-expression , ")"
                      | "project" , "[" , attribute-name-list , "]" , "(" , query-expression , ")"
                      | "rename"  , "[" , identifier          , "]" , "(" , query-expression , ")"
                      | atom-expr ;

(* Level 4  atoms *)

atom-expr             = identifier
                      | "(" , query-expression , ")" ;


(* ===================== 6. Conditions ===================== *)

condition              = or-expr ;

or-expr                = and-expr , { "or" , and-expr } ;

and-expr               = not-expr , { "and" , not-expr } ;

not-expr               = "not" , not-expr
                       | "(" , condition , ")"
                       | comparison ;

comparison              = operand , comparison-op , operand ;

comparison-op            = "=" | "!=" | "<" | "<=" | ">" | ">=" ;

operand                  = qualified-attribute
                         | number
                         | string ;

qualified-attribute      = identifier , [ "." , identifier ] ;


(* ===================== 7. Values and Literals ===================== *)

value                 = number | string | identifier ;

number                = [ "-" ] , digit , { digit } , [ "." , digit , { digit } ] ;

string                = bare-string | quoted-string ;

bare-string           = letter , { letter | digit | "_" | "-" | "@" } ;

quoted-string         = "'" ,
                        { ( ? any character except "'" and newline ? ) | "''" } ,
                        "'" ;


(* ===================== 8. Identifiers and Base Rules ===================== *)

identifier             = letter , { letter | digit | "_" } ;

letter                 = ? any character "A"-"Z" or "a"-"z" ? ;

digit                  = ? any character "0"-"9" ? ;

newline                = ? newline character (U+000A) ? ;

---

## Keyword-Spelled Attribute Names
An attribute may be spelled the same as a keyword (test case 8, `select[union=3](R)`). The lexer classifies every keyword (`select`,
`project`, `rename`, `union`, `intersect`, `minus`, `times`, `join`, `and`, `or`, `not`) by spelling alone, independent of where it appears
in the input. This exception governs how the parser treats a keyword-typed token when it occurs where the grammar expects an
attribute name: `qualified-attribute` and `attribute-name-list`. At those two positions, and only those two positions, the parser accepts a
keyword token in place of an `identifier` token and uses its lexeme as the attribute name. Everywhere else in the grammar, a keyword-typed
token is parsed as the keyword it represents.

This exception does not extend to relation names. A relation name that matches a keyword spelling, whether in `relation-definition` or in
`atom-expr`, is rejected as a syntax error rather than accepted as an identifier.

## Parsing strategy
This project uses recursive descent.

Each precedence level is its own nonterminal, and every choice a parsing function has to make can be decided from a small, fixed number of tokens
of lookahead, with no ambiguity between alternatives.

### Left Recursion

Several rules in this grammar (`additive-expr`, `multiplicative-expr`, `or-expr`, and `and-expr`) would be left-recursive if written in their
most natural form, e.g.:

```ebnf
additive-expr = additive-expr , additive-op , multiplicative-expr | multiplicative-expr ;
```

A recursive descent function implementing this directly would call itself before consuming any input, which never terminates.

To avoid this, each of these four rules is instead written using EBNF's `{ }` repetition operator, which expresses "zero or more repetitions"
directly, without the rule referencing itself:

```ebnf
additive-expr       = multiplicative-expr , { additive-op , multiplicative-expr } ;
multiplicative-expr = unary-expr , { multiplicative-op , unary-expr } ;
or-expr              = and-expr , { "or" , and-expr } ;
and-expr              = not-expr , { "and" , not-expr } ;
```

By rewriting the rules to not reference themselves, I have removed the left recursion problem from the grammar.

## Semantic Constraints

These rules cannot be expressed in a context-free grammar and are enforced
during evaluation.

| Operator | Output schema and behaviour |
|---|---|
| `select[c]` | Same schema as the input. The condition may compare two attributes, not only an attribute against a constant. All attribute references in `c` must exist in the input schema. |
| `project[a₁,…,aₙ]` | Attributes `a₁…aₙ` in that order. All listed attributes must exist in the input schema. Duplicate tuples are removed from the result. |
| `rename[N]` | Same attributes under the new relation name `N`. Required to make self-joins expressible. |
| `times` | All attributes of both inputs, each qualified by their source relation name. If two qualified names would collide, that is an error. |
| `join[c]` | Equivalent to `select[c](L times R)`. Theta join  not a natural join. No implicit attribute matching occurs. |
| `union` | Schema of the left input. Both inputs must be union-compatible. |
| `intersect` | Schema of the left input. Both inputs must be union-compatible. |
| `minus` | Schema of the left input. Both inputs must be union-compatible. |

**Union compatibility** requires: the same number of attributes, the same
attribute names in the same order, and compatible types position by position.
Anything else is an error.

**Type error rule:** Comparing a number to a string in a condition is an
error, not a silent false.

---

## Sources
TODO - convert all to proper reference format
https://www.youtube.com/watch?v=IO5ie7GbJGI - Left recursion
https://www.youtube.com/watch?v=iddRD8tJi44 - Recursive descent parsers
Engineering a compiler third edition - Scanners, Parsers
