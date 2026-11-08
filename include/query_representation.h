#include <stdbool.h>
#include <stddef.h>


typedef enum
{
    VALUE_FLOAT,
    VALUE_INT,
    VALUE_STR
} ValueType;

typedef struct
{
    ValueType type;

    union
    {
        long long integer;
        double floating_point;
        char *string;
    } data;
} Value;

typedef enum {
    EXPR_COLUMN,
    EXPR_LITERAL,
    EXPR_COMPARISON,
    EXPR_AND,
    EXPR_OR,
    EXPR_NOT
} ExpressionType;

typedef enum {
    COMP_EQUAL,
    COMP_NOT_EQUAL,
    COMP_LESS,
    COMP_GREATER,
    COMP_LESS_EQUAL,
    COMP_GREATER_EQUAL
} ComparisonOperator;

typedef struct Expression Expression;
struct Expression {
    ExpressionType type;

    union {
        char *column_name;
        Value literal;

        struct {
            Expression *left;
            Expression *right;
            ComparisonOperator operator;
        } comparison;

        struct {
            Expression *left;
            Expression *right;
        } binary;

        Expression *unary;
    } data;
};

typedef struct
{
    bool all_columns;
    char **column_names;
    size_t size;
} ColumnList;

typedef enum {
    QUERY_TABLE,
    QUERY_PROJECT,
    QUERY_FILTER,
    QUERY_JOIN,
    QUERY_CROSS_JOIN,
    QUERY_UNION,
    QUERY_INTERSECT,
    QUERY_EXCEPT
} QueryNodeType;

typedef struct QueryNode QueryNode;
struct QueryNode {
    QueryNodeType type;

    union {
        struct {
            QueryNode *input;
            ColumnList columns;
        } project;

        struct {
            QueryNode *input;
            Expression *condition;
        } filter;

        struct {
            QueryNode *left;
            QueryNode *right;
            Expression *condition;
        } join;

        struct {
            QueryNode *left;
            QueryNode *right;
        } cross_join;

        struct {
            char *name;
        } table;

        struct {
            QueryNode *left;
            QueryNode *right;
        } set_operation;
    };
};