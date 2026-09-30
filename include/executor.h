#pragma once

#include "common.h"

#define NAME_ERR(fmt, ...) LOG_ERR("[NAME] " fmt, ##__VA_ARGS__)
#define DEFINITION_ERR(fmt, ...) LOG_ERR("[DEFINITION] " fmt, ##__VA_ARGS__)
#define SCHEMA_ERR(fmt, ...) LOG_ERR("[SCHEMA] " fmt, ##__VA_ARGS__)
#define TYPE_ERR(fmt, ...) LOG_ERR("[TYPE] " fmt, ##__VA_ARGS__)
#define SYNTAX_ERR(fmt, ...) LOG_ERR("[SYNTAX] " fmt, ##__VA_ARGS__)

typedef enum
{
	VALUE_NUMBER,
	VALUE_STRING
} ValueType;

typedef struct
{
	ValueType type;

	union
	{
		double number;
		const char *string;
	};
} Value;

typedef struct
{
	Value *values;
	size_t value_count;
} Tuple;

typedef struct
{
    const char *name;
    size_t name_length;
    ValueType type;
} Attribute;

typedef struct
{
    const char *name;
    size_t name_length;

    Attribute *attributes;
    size_t attribute_count;

    Tuple *tuples;
    size_t tuple_count;
} Relation;

typedef struct
{
	size_t join_comparisons;
	size_t select_tuples_examined;
} ExecutorStats;

typedef struct
{
	Arena *arena;

	Relation *relations;
	size_t relation_count;

	ExecutorStats stats;
} Executor;

void executor_init(Executor *executor, Arena *arena);
void execute(Executor *executor, Tree *tree);

