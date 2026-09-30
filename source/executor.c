#include "executor.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static bool execute_query(Executor *executor, TreeNode *node, Relation *result);
static bool evaluate_condition(TreeNode *node, Relation *relation, Tuple *tuple);

static bool relation_append_tuple(Relation *relation, Tuple *tuple)
{
    Tuple *tuples = malloc(sizeof(Tuple) * (relation->tuple_count + 1));

    if (tuples == NULL)
    {
        return false;
    }

    if (relation->tuples != NULL)
    {
        memcpy(tuples, relation->tuples, sizeof(Tuple) * relation->tuple_count);
        free(relation->tuples);
    }

    tuples[relation->tuple_count] = *tuple;
    relation->tuples = tuples;
    relation->tuple_count++;

    return true;
}

static bool compare_values(
	Value *left,
	Value *right,
	int *result)
{
	if (left->type != right->type)
	{
		return false;
	}

	if (left->type == VALUE_NUMBER)
	{
		if (left->number < right->number)
		{
			*result = -1;
		}
		else if (left->number > right->number)
		{
			*result = 1;
		}
		else
		{
			*result = 0;
		}

		return true;
	}

	int comparison = strcmp(
		left->string,
		right->string
		);

	if (comparison < 0)
	{
		*result = -1;
	}
	else if (comparison > 0)
	{
		*result = 1;
	}
	else
	{
		*result = 0;
	}

	return true;
}

static bool relation_contains_tuple(
	Relation *relation,
	Tuple *tuple)
{
	for (size_t i = 0;
		i < relation->tuple_count;
		i++)
	{
		Tuple *existing = &relation->tuples[i];

		if (existing->value_count != tuple->value_count)
		{
			continue;
		}

		bool equal = true;

		for (size_t j = 0;
			j < tuple->value_count;
			j++)
		{
			int comparison;

			if (!compare_values(&existing->values[j], &tuple->values[j], &comparison) || comparison != 0)
			{
				equal = false;
				break;
			}
		}

		if (equal)
		{
			return true;
		}
	}

	return false;
}

static double token_to_number(const Token *token)
{
	char buffer[64];

    /*
     * Token lexemes are stored as a pointer and a length, rather than
     * as null-terminated strings. strtod() expects a null-terminated
     * string, so copy the numeric lexeme into a buffer and add the
     * terminating '\0' before passing it to strtod().
     */
	memcpy(
		buffer,
		token->lexeme_start,
		token->lexeme_length
		);

	buffer[token->lexeme_length] = '\0';

	return strtod(buffer, NULL);
}


static void token_to_string(
	const Token *token,
	char *string)
{
	/*
	 * STRING tokens include surrounding single quotes.
	 * Remove the quotes and convert doubled single quotes
	 * into a single quote.
	 */
	if (token->category == STRING)
	{
		size_t j = 0;

		for (size_t i = 1;
			i < token->lexeme_length - 1;
			i++)
		{
			if (token->lexeme_start[i] == '\'' &&
				token->lexeme_start[i + 1] == '\'')
			{
				string[j++] = '\'';
				i++;
			}
			else
			{
				string[j++] = token->lexeme_start[i];
			}
		}

		string[j] = '\0';
	}
	else
	{
		/*
		 * Bare strings are represented by WORD tokens.
		 * They do not require any conversion, so copy the
		 * lexeme directly.
		 */
		memcpy(
			string,
			token->lexeme_start,
			token->lexeme_length
			);

		string[token->lexeme_length] = '\0';
	}
}


static void token_to_value(
	const Token *token,
	Value *value,
	char *string)
{
	if (token->category == NUMBER)
	{
		value->type = VALUE_NUMBER;
		value->number = token_to_number(token);
	}
	else
	{
		value->type = VALUE_STRING;
		value->string = string;

		token_to_string(
			token,
			string
			);
	}
}


static Relation *find_relation(
	Executor *executor,
	const char *name,
	size_t name_length)
{
	for (size_t i = 0;
		i < executor->relation_count;
		i++)
	{
		Relation *relation = &executor->relations[i];

		if (relation->name_length == name_length &&
			strncmp(
				relation->name,
				name,
				name_length
				) == 0)
		{
			return relation;
		}
	}

	return NULL;
}

static Relation *execute_relation_definition(
	Executor *executor,
	TreeNode *root)
{
	// Needed due to poor arena_pop mechanism that will be updated in the future
	// TODO: Update this when arena_pop is fixed
	size_t arena_mark_used = executor->arena->used;
	size_t arena_mark_prev_used = executor->arena->prev_used;

	TreeNode *definition = root->left_child;

	TreeNode *relation_node = definition->left_child;
	TreeNode *attributes_node = definition->right_child;

	Token *relation_token = relation_node->token_arr[0];

	Relation *existing = find_relation(
		executor,
		relation_token->lexeme_start,
		relation_token->lexeme_length
		);

	if (existing != NULL)
	{
		DEFINITION_ERR(
			"Relation '%.*s' already exists",
			(int)relation_token->lexeme_length,
			relation_token->lexeme_start
			);

		return NULL;
	}

	Relation *relation = arena_push(
		executor->arena,
		sizeof(Relation),
		_Alignof(Relation)
		);

	relation->name = relation_token->lexeme_start;
	relation->name_length = relation_token->lexeme_length;

	relation->attribute_count = attributes_node->token_count;

	relation->attributes = arena_push(
		executor->arena,
		sizeof(Attribute) * relation->attribute_count,
		_Alignof(Attribute)
		);

	for (size_t i = 0;
		i < relation->attribute_count;
		i++)
	{
		Token *attribute_token =
			attributes_node->token_arr[i];

		relation->attributes[i].name =
			attribute_token->lexeme_start;

		relation->attributes[i].name_length =
			attribute_token->lexeme_length;
	}

	// Count tuples in list to know how much memory to malloc
	relation->tuple_count = 0;

	for (TreeNode *tuple_node = root->right_child;
		tuple_node != NULL;
		tuple_node = tuple_node->left_child)
	{
		relation->tuple_count++;
	}

	if (relation->tuple_count == 0)
	{
		relation->tuples = NULL;
	}
	else
	{
		relation->tuples = malloc(
			sizeof(Tuple) * relation->tuple_count
			);

		if (relation->tuples == NULL)
		{
			// Reset arena if tuple malloc fails
			executor->arena->used = arena_mark_used;
			executor->arena->prev_used = arena_mark_prev_used;
			return NULL;
		}
	}

	size_t tuple_index = 0;

	for (TreeNode *tuple_node = root->right_child;
		tuple_node != NULL;
		tuple_node = tuple_node->left_child)
	{
		Tuple *tuple = &relation->tuples[tuple_index];

		/*
		 * token_arr[0] is the synthetic TUPLE token.
		 * The actual tuple values begin at token_arr[1].
		 */
		tuple->value_count = tuple_node->token_count - 1;

		tuple->values = arena_push(
			executor->arena,
			sizeof(Value) * tuple->value_count,
			_Alignof(Value)
			);

		for (size_t i = 0;
			i < tuple->value_count;
			i++)
		{
			Token *value_token =
				tuple_node->token_arr[i + 1];

			Value *value =
				&tuple->values[i];

			if (value_token->category == NUMBER)
			{
				token_to_value(
					value_token,
					value,
					NULL
					);
			}
			else
			{
				char *string = arena_push(
					executor->arena,
					value_token->lexeme_length + 1,
					_Alignof(char)
					);

				token_to_value(
					value_token,
					value,
					string
					);
			}

			if (tuple_index == 0)
			{
				relation->attributes[i].type =
					tuple->values[i].type;
			}
			else if (tuple->values[i].type !=
				relation->attributes[i].type)
			{
				TYPE_ERR(
					"Type mismatch for attribute '%.*s' in relation '%.*s'",
					(int)relation->attributes[i].name_length,
					relation->attributes[i].name,
					(int)relation->name_length,
					relation->name
					);

				free(relation->tuples);

				executor->arena->used = arena_mark_used;
				executor->arena->prev_used = arena_mark_prev_used;

				return NULL;
			}
		}

		tuple_index++;
	}

	/*
	 * Add the newly created relation to the executor's
	 * relation list.
	 */
	Relation *relations = arena_push(
		executor->arena,
		sizeof(Relation) * (executor->relation_count + 1),
		_Alignof(Relation)
		);

	if (executor->relation_count > 0)
	{
		memcpy(
			relations,
			executor->relations,
			sizeof(Relation) * executor->relation_count
			);
	}

	relations[executor->relation_count] = *relation;

	executor->relations = relations;
	executor->relation_count++;

	return &relations[executor->relation_count - 1];
}

static bool execute_rename(
	Executor *executor,
	TreeNode *node,
	Relation *result)
{
	TreeNode *name_node = node->left_child;
	TreeNode *expression = node->right_child;

	Relation input;

	if (!execute_query(
		executor,
		expression,
		&input
		))
	{
		return false;
	}

	Token *name_token = name_node->token_arr[0];

	*result = input;
	result->name = name_token->lexeme_start;
	result->name_length = name_token->lexeme_length;
	result->tuples = NULL;
	result->tuple_count = 0;

	for (size_t i = 0; i < input.tuple_count; i++)
	{
		if (!relation_append_tuple(
			result,
			&input.tuples[i]
			))
		{
			// input.tuples are malloced in execute_query
			free(input.tuples);
			free(result->tuples);
			result->tuples = NULL;
			result->tuple_count = 0;
			return false;
		}
	}

	// input.tuples are malloced in execute_query
	free(input.tuples);
	return true;
}

static bool evaluate_comparison(
	TreeNode *node,
	Relation *relation,
	Tuple *tuple)
{
	TreeNode *left = node->left_child;
	TreeNode *right = node->right_child;
	Token *operator_token = node->token_arr[0];

	Value left_value;
	Value right_value;

	char left_string[left->token_arr[0]->lexeme_length + 1];
	char right_string[right->token_arr[0]->lexeme_length + 1];

	/* Resolve left operand. */
	if (left->token_arr[0]->category == NUMBER ||
		left->token_arr[0]->category == STRING)
	{
		token_to_value(
			left->token_arr[0],
			&left_value,
			left->token_arr[0]->category == STRING ?
				left_string : NULL
			);
	}
	else
	{
		Token *attribute_token =
			left->token_arr[left->token_count - 1];

		size_t attribute_index =
			relation->attribute_count;

		for (size_t i = 0;
			i < relation->attribute_count;
			i++)
		{
			Attribute *attribute =
				&relation->attributes[i];

			// Unqualified attribute
			if (left->token_count == 1)
			{
				if (attribute_token->lexeme_length ==
						attribute->name_length &&
					strncmp(
						attribute_token->lexeme_start,
						attribute->name,
						attribute->name_length
						) == 0)
				{
					attribute_index = i;
					break;
				}
			}
			// Qualified attribute (Relation.Attribute)
			// token_arr[0] is relation name, token_arr[1] is attribute name
			else
			{
				Token *relation_token =
					left->token_arr[0];

				if (relation_token->lexeme_length ==
						relation->name_length &&
					strncmp(
						relation_token->lexeme_start,
						relation->name,
						relation->name_length
						) == 0 &&
					attribute_token->lexeme_length ==
						attribute->name_length &&
					strncmp(
						attribute_token->lexeme_start,
						attribute->name,
						attribute->name_length
						) == 0)
				{
					attribute_index = i;
					break;
				}
			}
		}

		if (attribute_index == relation->attribute_count)
		{
			NAME_ERR(
				"Unknown attribute '%.*s'",
				(int)attribute_token->lexeme_length,
				attribute_token->lexeme_start
				);

			return false;
		}

		left_value =
			tuple->values[attribute_index];
	}

	/* Resolve right operand. */
	if (right->token_arr[0]->category == NUMBER ||
		right->token_arr[0]->category == STRING)
	{
		token_to_value(
			right->token_arr[0],
			&right_value,
			right->token_arr[0]->category == STRING ?
				right_string : NULL
			);
	}
	else
	{
		Token *attribute_token =
			right->token_arr[right->token_count - 1];

		size_t attribute_index =
			relation->attribute_count;

		for (size_t i = 0;
			i < relation->attribute_count;
			i++)
		{
			Attribute *attribute =
				&relation->attributes[i];

			// Unqualified attribute
			if (right->token_count == 1)
			{
				if (attribute_token->lexeme_length ==
						attribute->name_length &&
					strncmp(
						attribute_token->lexeme_start,
						attribute->name,
						attribute->name_length
						) == 0)
				{
					attribute_index = i;
					break;
				}
			}
			// Qualified attribute (Relation.Attribute)
			// token_arr[0] is relation name, token_arr[1] is attribute name
			else
			{
				Token *relation_token =
					right->token_arr[0];

				if (relation_token->lexeme_length ==
						relation->name_length &&
					strncmp(
						relation_token->lexeme_start,
						relation->name,
						relation->name_length
						) == 0 &&
					attribute_token->lexeme_length ==
						attribute->name_length &&
					strncmp(
						attribute_token->lexeme_start,
						attribute->name,
						attribute->name_length
						) == 0)
				{
					attribute_index = i;
					break;
				}
			}
		}

		if (attribute_index == relation->attribute_count)
		{
			NAME_ERR(
				"Unknown attribute '%.*s'",
				(int)attribute_token->lexeme_length,
				attribute_token->lexeme_start
				);

			return false;
		}

		right_value =
			tuple->values[attribute_index];
	}

	int result;

	if (!compare_values(
		&left_value,
		&right_value,
		&result))
	{
		TYPE_ERR(
			"Cannot compare values of different types"
			);

		return false;
	}

	switch (operator_token->category)
	{
	case EQUAL:
		return result == 0;

	case NOT_EQUAL:
		return result != 0;

	case LESS_THAN:
		return result < 0;

	case LESS_THAN_OR_EQUAL:
		return result <= 0;

	case GREATER_THAN:
		return result > 0;

	case GREATER_THAN_OR_EQUAL:
		return result >= 0;

	default:
		SYNTAX_ERR(
			"Invalid comparison operator"
			);

		return false;
	}
}

static bool evaluate_condition(
	TreeNode *node,
	Relation *relation,
	Tuple *tuple)
{
	if (node == NULL || node->token_count == 0)
	{
		return false;
	}

	Token *token = node->token_arr[0];

	switch (token->category)
	{
	case AND:
		return evaluate_condition(
			node->left_child,
			relation,
			tuple
			)
		&& evaluate_condition(
			node->right_child,
			relation,
			tuple
			);

	case OR:
		return evaluate_condition(
			node->left_child,
			relation,
			tuple
			)
		|| evaluate_condition(
			node->right_child,
			relation,
			tuple
			);

	case NOT:
		return !evaluate_condition(
			node->left_child,
			relation,
			tuple
			);

	case EQUAL:
	case NOT_EQUAL:
	case LESS_THAN:
	case LESS_THAN_OR_EQUAL:
	case GREATER_THAN:
	case GREATER_THAN_OR_EQUAL:
		return evaluate_comparison(
			node,
			relation,
			tuple
			);

	default:
		return false;
	}
}

static bool execute_select(
	Executor *executor,
	TreeNode *node,
	Relation *result)
{
	TreeNode *condition = node->left_child;
	TreeNode *expression = node->right_child;

	Relation input;

	if (!execute_query(
		executor,
		expression,
		&input
		))
	{
		return false;
	}

	*result = input;
	result->tuples = NULL;
	result->tuple_count = 0;

	for (size_t i = 0; i < input.tuple_count; i++)
	{
		Tuple *tuple = &input.tuples[i];

		if (evaluate_condition(
			condition,
			&input,
			tuple
			))
		{
			if (!relation_append_tuple(result, tuple))
			{
				// input.tuples are malloced in execute_query
				free(input.tuples);
				free(result->tuples);
				result->tuples = NULL;
				result->tuple_count = 0;
				return false;
			}
		}
	}

	// input.tuples are malloced in execute_query
	free(input.tuples);
	return true;
}

static bool execute_project(
	Executor *executor,
	TreeNode *node,
	Relation *result)
{
	TreeNode *attributes_node = node->left_child;
	TreeNode *expression = node->right_child;

	Relation input;

	if (!execute_query(
		executor,
		expression,
		&input
		))
	{
		return false;
	}

	*result = input;
	result->attributes = NULL;
	result->attribute_count = attributes_node->token_count;
	result->tuples = NULL;
	result->tuple_count = 0;

	result->attributes = malloc(
		sizeof(Attribute) * result->attribute_count
		);

	if (result->attributes == NULL)
	{
		free(input.tuples);
		return false;
	}

	size_t attribute_indices[result->attribute_count];

	for (size_t i = 0;
		i < result->attribute_count;
		i++)
	{
		Token *attribute_token =
			attributes_node->token_arr[i];

		attribute_indices[i] = input.attribute_count;

		for (size_t j = 0;
			j < input.attribute_count;
			j++)
		{
			Attribute *attribute =
				&input.attributes[j];

			if (attribute_token->lexeme_length ==
					attribute->name_length &&
				strncmp(
					attribute_token->lexeme_start,
					attribute->name,
					attribute->name_length
					) == 0)
			{
				attribute_indices[i] = j;
				break;
			}
		}

		if (attribute_indices[i] == input.attribute_count)
		{
			NAME_ERR(
				"Unknown attribute '%.*s'",
				(int)attribute_token->lexeme_length,
				attribute_token->lexeme_start
				);

			free(input.tuples);
			free(result->attributes);

			result->attributes = NULL;
			result->attribute_count = 0;

			return false;
		}

		result->attributes[i] =
			input.attributes[attribute_indices[i]];
	}

	for (size_t i = 0;
		i < input.tuple_count;
		i++)
	{
		Tuple projected;

		projected.value_count = result->attribute_count;

		projected.values = malloc(
			sizeof(Value) * projected.value_count
			);

		if (projected.values == NULL)
		{
			free(input.tuples);
			free(result->tuples);
			free(result->attributes);

			result->tuples = NULL;
			result->attributes = NULL;
			result->tuple_count = 0;
			result->attribute_count = 0;

			return false;
		}

		for (size_t j = 0;
			j < result->attribute_count;
			j++)
		{
			projected.values[j] =
			input.tuples[i].values[attribute_indices[j]];
		}

		if (!relation_contains_tuple(
			result,
			&projected
			))
		{
			if (!relation_append_tuple(
				result,
				&projected
				))
			{
				free(projected.values);
				free(input.tuples);
				free(result->tuples);
				free(result->attributes);

				result->tuples = NULL;
				result->attributes = NULL;
				result->tuple_count = 0;
				result->attribute_count = 0;

				return false;
			}
		}
		else
		{
			free(projected.values);
		}
	}

	free(input.tuples);

	return true;
}

static bool execute_query(
	Executor *executor,
	TreeNode *node,
	Relation *result)
{
	if (node == NULL || node->token_count == 0)
	{
		return false;
	}

	Token *token = node->token_arr[0];

	switch (token->category)
	{
	case IDENT:
	{
		Relation *relation = find_relation(
			executor,
			token->lexeme_start,
			token->lexeme_length
			);

		if (relation == NULL)
		{
			NAME_ERR(
				"Relation '%.*s' does not exist",
				(int)token->lexeme_length,
				token->lexeme_start
				);

			return false;
		}

		*result = *relation;
		result->tuples = NULL;
		result->tuple_count = 0;

		// TODO: It constructs a result here for this query because with the current
		// ownership model, the result will be free'd after this. Ideally it should
		// not iterate over the relation to construct the result, as this should be
		// a simple "return a pointer to the relation" scenario. In the future, this
		// needs to be optimized
		for (size_t i = 0; i < relation->tuple_count; i++)
		{
			if (!relation_append_tuple(
				result,
				&relation->tuples[i]
				))
			{
				free(result->tuples);
				result->tuples = NULL;
				result->tuple_count = 0;

				return false;
			}
		}

		return true;
	}

	case RENAME:
		return execute_rename(
			executor,
			node,
			result
			);

	case SELECT:
		return execute_select(
			executor,
			node,
			result
			);

case PROJECT:
		return execute_project(
			executor,
			node,
			result
			);

	default:
		return false;
	}
}

static void print_result(const Relation *relation)
{
	printf(
		"Relation: %.*s\n",
		(int)relation->name_length,
		relation->name
		);

	printf("Attributes: ");

	for (size_t i = 0;
		i < relation->attribute_count;
		i++)
	{
		if (i > 0)
		{
			printf(", ");
		}

		printf("%.*s", (int)relation->attributes[i].name_length,
			relation->attributes[i].name);
	}

	printf("\n");

	printf("Tuples:\n");

	for (size_t i = 0;
		i < relation->tuple_count;
		i++)
	{
		const Tuple *tuple = &relation->tuples[i];

		printf("  (");

		for (size_t j = 0;
			j < tuple->value_count;
			j++)
		{
			if (j > 0)
			{
				printf(", ");
			}

			const Value *value = &tuple->values[j];

			if (value->type == VALUE_NUMBER)
			{
				printf("%g", value->number);
			}
			else
			{
				printf("'%s'", value->string);
			}
		}

		printf(")\n");
	}

	printf("\n");
}

static void execute_statement(
	Executor *executor,
	TreeNode *root)
{
	if (root == NULL || root->token_count == 0)
	{
		return;
	}

	switch (root->token_arr[0]->category)
	{
	case EQUAL:
		{
			Relation *relation =
			execute_relation_definition(
				executor,
				root
				);

			if (relation != NULL)
			{
				printf(
					"Successfully created relation %.*s\n",
					(int)relation->name_length,
					relation->name
					);
			}

			break;
		}

	default:
		{
			// Result is only a temporary relation, therefore it should only have
			// a temporary lifespan. So executer mallocs it rather than taking space
			// from the arena, which would make it permanent
			Relation *result = malloc(sizeof(*result));

			if (result == NULL)
			{
				return;
			}

			if (execute_query(
				executor,
				root,
				result
				))
			{
				print_result(result);
			}

			// Free temporary result data
			free(result->tuples);
			// free(result->attributes);
			free(result);

			break;
		}
	}
}


void executor_init(
	Executor *executor,
	Arena *arena)
{
	*executor = (Executor){
		.arena = arena,
		.relations = NULL,
		.relation_count = 0,
		.stats = {
			.join_comparisons = 0,
			.select_tuples_examined = 0
		}
	};
}


void execute(
	Executor *executor,
	Tree *tree)
{
	for (StatementNode *statement = tree->head;
		statement != NULL;
		statement = statement->next)
	{
		execute_statement(
			executor,
			statement->root
			);
	}
}

