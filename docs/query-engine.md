# Query Engine
## Overview
The query engine is responsible for interpreting and executing queries against the database. It currently supports a custom relational algebra language, providing operations for retrieving and transforming relational data.

The project initially began as a smaller university assignment for a databases course, where the objective was to implement a relational algebra engine. However, I wanted to take the project further and develop it into a larger personal project.

As the scope of the project expands, the query engine will evolve alongside it. The eventual goal is to introduce support for a subset of SQL, providing a more practical interface for interacting with the database while retaining the underlying query processing functionality.

## Query Processing
The current query engine follows a traditional two-stage process: lexical analysis and syntactic analysis.

During lexical analysis, the lexer converts the input query into a sequence of tokens. The token stream is given to the parser to perform syntactic analysis, in which it interprets those tokens according to the language grammar and constructs a parse tree.

### Lexical Analysis
The lexer's responsibility is to read the input character by character and identify the individual components of a query.

recognizes several categories of tokens, listed below:

| Category | Examples |
|----------|----------|
| Words    | Employees, select, salary |
| Numbers  | 100, 25.5, -10 |
| Strings  | 'John', 'Engineering' |
| Comparison operators | =, !=, <, >, <=, >= |
| Delimiters | (, ), [, ], {, } |
| Separators | ,, . |
| Newlines | Statement delimiters |

Words are initially classified generically. The parser subsequently determines whether a word represents a relational algebra operator, a logical operator, or an identifier based on its context. This means there aren't any words that are strictly reserved for certain uses, allowing the user to use them more freely.

For example, consider the following query:

```
select[salary > 50000](Employees)
```

The lexer produces a sequence of tokens representing the individual components:

```
WORD("select")
LBRACKET
WORD("salary")
GREATER_THAN
NUMBER("50000")
RBRACKET
LPAREN
WORD("Employees")
RPAREN
```

Tokenization also handles comments, string literals, and malformed input, reporting lexical errors when unexpected characters or unterminated strings are encountered.

### Syntactic Analysis
The parser consumes the token stream produced by the lexer and constructs a parse tree according to the relational algebra grammar.

The parser uses a recursive-descent parsing approach, with individual functions responsible for recognizing different grammatical constructs.

For example:

* parse_select() handles selection expressions.
* parse_project() handles projection expressions.
* parse_rename() handles rename expressions.
* parse_condition() handles logical conditions.
* parse_additive_expression() handles set operations.
* parse_multiplicative_expression() handles Cartesian products and joins.

These functions call one another recursively to process nested expressions.

#### Parse Tree Representation
The parser produces a tree that represents each query in such a way that the system can later traverse it to understand the program's operations and their structure.

Each TreeNode contains an array of tokens, along with pointers to its left and right children and its parent. This allows the tree to represent both unary and binary operations, as well as constructs containing multiple attributes or values.

The parser also maintains a linked list of statements, allowing multiple expressions or relation definitions to be represented within a single input.

Nodes are allocated using the project's arena allocator, keeping memory management centralized and avoiding individual allocations for every node

##### Selection
Consider the following relational algebra expression:
```
select[salary > 50000](Employees)
```

The expression selects all tuples from Employees where the salary exceeds 50,000.

Its parse tree is structured as follows:
```
PROGRAM
└── select
    ├── >
    │   ├── salary
    │   └── 50000
    └── Employees
```

The root node represents the selection operation.

Its left child represents the selection condition, which is itself a comparison expression. Its right child represents the relation being operated on.

This structure allows the query engine to distinguish between the condition being evaluated and the relation against which it is evaluated.

##### Projection and Selection
Consider a slightly more complex expression:
```
project[name, salary](
    select[salary > 50000](Employees)
)
```

This expression first selects employees earning more than 50,000 and then projects their names and salaries.

Its parse tree illustrates how nested relational algebra operations are represented:
```
PROGRAM
└── project
    ├── name salary
    └── select
        ├── >
        │   ├── salary
        │   └── 50000
        └── Employees
```

The projection node contains an attribute list as its left child and the nested selection expression as its right child.

The tree therefore preserves the order and structure of the operations specified by the query.

##### Binary Operations
Binary relational algebra operations, such as union and intersection, use their left and right children to represent their respective operands.

For example:
```
Employees union Contractors
```

Produces the following structure:
```
PROGRAM
└── union
    ├── Employees
    └── Contractors
```

##### Relation Definitions
In addition to query expressions, the parser supports relation definitions containing attribute lists and tuple data.

For example:
```
Employees(id, name, salary) = {
    1, 'Alice', 75000
    2, 'Bob', 60000
}
```

Relation definitions are represented differently from ordinary relational algebra expressions.

The root represents the assignment operator, with the relation name and its attributes grouped under a relation definition node. The tuple data is represented using tuple nodes linked through their left-child pointers.

This allows relation definitions and query expressions to coexist within the same parsed program.

## Future Direction
The current implementation is designed specifically around relational algebra. Although this provides a useful foundation for query processing, the long-term objective is to support a subset of SQL.

Introducing SQL will require additional language constructs and a different grammar. However, the intention is to avoid coupling query execution to any particular query language.

The current plan is to introduce an intermediate logical representation between parsing and execution. Individual language frontends could translate their respective parse trees into this shared representation, allowing the query execution engine to operate independently of the original query syntax.

The initial relational algebra implementation therefore serves as both a foundation for the current query engine and an opportunity to explore the design of a more extensible query processing architecture.