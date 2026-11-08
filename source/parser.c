#include "common.h"
#include "parser.h"
#include <string.h>

static TreeNode *parse_condition(Parser *parser);
static TreeNode *parse_additive_expression(Parser *parser);
static TreeNode *parse_join(Parser *parser, TreeNode *left);

static Category get_word_category(const Token *token)
{
	if (token->lexeme_length == 6 &&
			strncmp(token->lexeme_start, "select", 6) == 0)
		return SELECT;

	if (token->lexeme_length == 7 &&
			strncmp(token->lexeme_start, "project", 7) == 0)
		return PROJECT;

	if (token->lexeme_length == 6 &&
			strncmp(token->lexeme_start, "rename", 6) == 0)
		return RENAME;

	if (token->lexeme_length == 5 &&
			strncmp(token->lexeme_start, "union", 5) == 0)
		return UNION;

	if (token->lexeme_length == 9 &&
			strncmp(token->lexeme_start, "intersect", 9) == 0)
		return INTERSECT;

	if (token->lexeme_length == 5 &&
			strncmp(token->lexeme_start, "minus", 5) == 0)
		return MINUS;

	if (token->lexeme_length == 5 &&
			strncmp(token->lexeme_start, "times", 5) == 0)
		return TIMES;

	if (token->lexeme_length == 4 &&
			strncmp(token->lexeme_start, "join", 4) == 0)
		return JOIN;

	if (token->lexeme_length == 3 &&
			strncmp(token->lexeme_start, "not", 3) == 0)
		return NOT;

	if (token->lexeme_length == 3 &&
			strncmp(token->lexeme_start, "and", 3) == 0)
		return AND;

	if (token->lexeme_length == 2 &&
			strncmp(token->lexeme_start, "or", 2) == 0)
		return OR;

	return IDENT;
}

static Token *peek(Parser *parser)
{
	if (parser->cur_pos >= parser->token_count)
	{
		return NULL;
	}
	return parser->cur_token;
}

static Token *peek_next(Parser *parser)
{
	if (parser->cur_pos + 1 >= parser->token_count)
	{
		return NULL;
	}

	return (Token *)parser->arena->base + parser->cur_pos + 1;
}

static Token *advance(Parser *parser)
{
	if (parser->cur_pos >= parser->token_count)
	{
		return NULL;
	}
	Token *token = parser->cur_token;
	parser->cur_token++;
	parser->cur_pos++;
	return token;
}

static bool check(Parser *parser, Category category)
{
	Token *token = peek(parser);
	return token != NULL && token->category == category;
}

void parser_init(Parser *parser, Arena *arena, size_t token_count)
{
	parser->arena = arena;
	parser->token_count = token_count;
	parser->cur_pos = 0;
	parser->cur_token = (Token *)arena->base;
	parser->has_error = false;
	parser->statement_has_error = false;
}

// TODO: getting rid of rename since its not included in sql
static TreeNode *parse_rename(Parser *parser)
{
	Token *rename_token = advance(parser);
	rename_token->category = RENAME;

	TreeNode *node = tree_node_create(
			parser->arena,
			1,
			NULL,
			NULL,
			NULL
			);

	node->token_arr[0] = rename_token;

	if (!check(parser, LBRACKET))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete rename expression at end of input: expected '[' identifier ']' and relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected '[' after 'rename' at %d:%d",
					token->pos.row,
					token->pos.col);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* '[' */

	Token *name = peek(parser);

	if (name == NULL)
	{
		SYNTAX_ERR("Incomplete rename expression at end of input: expected identifier and closing ']'");
		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	if (name->category != WORD)
	{
		SYNTAX_ERR("Expected identifier at %d:%d (got '%.*s')",
				name->pos.row,
				name->pos.col,
				(int)name->lexeme_length,
				name->lexeme_start);
		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	name->category = IDENT;

	TreeNode *new_name = tree_node_create(
			parser->arena,
			1,
			NULL,
			NULL,
			NULL
			);

	new_name->token_arr[0] = advance(parser);

	if (!check(parser, RBRACKET))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete rename expression at end of input: expected ']' and relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected ']' after rename identifier at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* ']' */

	if (!check(parser, LPAREN))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete rename expression at end of input: expected '(' and relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected '(' after rename identifier at %d:%d",
					token->pos.row,
					token->pos.col);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* '(' */

	TreeNode *expression = parse_additive_expression(parser);

	if (expression == NULL)
	{
		return node;
	}

	if (!check(parser, RPAREN))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete rename expression at end of input: expected ')'");
		}
		else
		{
			SYNTAX_ERR("Expected ')' at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* ')' */

	node->left_child = new_name;
	node->right_child = expression;

	new_name->parent = node;
	expression->parent = node;

	return node;
}

static TreeNode *parse_project(Parser *parser)
{
	Token *project_token = advance(parser);
	project_token->category = PROJECT;

	TreeNode *node = tree_node_create(
			parser->arena,
			1,
			NULL,
			NULL,
			NULL
			);

	node->token_arr[0] = project_token;

	if (!check(parser, LBRACKET))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete project expression at end of input: expected '[' attribute list ']' and relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected '[' after 'project' at %d:%d",
					token->pos.row,
					token->pos.col);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* '[' */

	Token *token = peek(parser);

	if (token == NULL)
	{
		SYNTAX_ERR("Incomplete project expression at end of input: expected attribute name and closing ']'");
		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	if (token->category != WORD)
	{
		SYNTAX_ERR("Expected attribute name at %d:%d (got '%.*s')",
				token->pos.row,
				token->pos.col,
				(int)token->lexeme_length,
				token->lexeme_start);
		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	size_t attribute_count = 1;
	size_t lookahead = parser->cur_pos + 1;

	while (lookahead < parser->token_count)
	{
		token = (Token *)parser->arena->base + lookahead;

		if (token->category != COMMA)
		{
			break;
		}

		lookahead++;

		if (lookahead >= parser->token_count)
		{
			SYNTAX_ERR("Incomplete project expression at end of input: expected attribute name and closing ']'");
			parser->has_error = true;
			parser->statement_has_error = true;
			return node;
		}

		token = (Token *)parser->arena->base + lookahead;

		if (token->category != WORD)
		{
			SYNTAX_ERR("Expected attribute name after ',' at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
			parser->has_error = true;
			parser->statement_has_error = true;
			return node;
		}

		attribute_count++;
		lookahead++;
	}

	TreeNode *attributes_node = tree_node_create(
			parser->arena,
			attribute_count,
			NULL,
			NULL,
			NULL
			);

	for (size_t i = 0; i < attribute_count; i++)
	{
		token = peek(parser);

		token->category = IDENT;
		attributes_node->token_arr[i] = advance(parser);

		if (i + 1 < attribute_count)
		{
			advance(parser); /* ',' */
		}
	}

	if (!check(parser, RBRACKET))
	{
		token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete project expression at end of input: expected ']'");
		}
		else
		{
			SYNTAX_ERR("Expected ']' after project attributes at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* ']' */

	if (!check(parser, LPAREN))
	{
		token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete project expression at end of input: expected '(' and relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected '(' after project attributes at %d:%d",
					token->pos.row,
					token->pos.col);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* '(' */

	TreeNode *expression = parse_additive_expression(parser);

	if (expression == NULL)
	{
		return node;
	}

	if (!check(parser, RPAREN))
	{
		token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete project expression at end of input: expected ')'");
		}
		else
		{
			SYNTAX_ERR("Expected ')' at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* ')' */

	node->left_child = attributes_node;
	node->right_child = expression;

	attributes_node->parent = node;
	expression->parent = node;

	return node;
}

static TreeNode *parse_select(Parser *parser)
{
	Token *select_token = advance(parser);
	select_token->category = SELECT;

	TreeNode *node = tree_node_create(
			parser->arena,
			1,
			NULL,
			NULL,
			NULL
			);

	node->token_arr[0] = select_token;

	if (!check(parser, LBRACKET))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete select expression at end of input: expected '[' condition ']' and relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected '[' after 'select' at %d:%d",
					token->pos.row,
					token->pos.col);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* '[' */

	TreeNode *condition = parse_condition(parser);

	if (condition == NULL)
	{
		return node;
	}

	if (!check(parser, RBRACKET))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete select expression at end of input: expected ']' and relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected ']' at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* ']' */

	if (!check(parser, LPAREN))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete select expression at end of input: expected '(' and relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected '(' after select condition at %d:%d",
					token->pos.row,
					token->pos.col);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* '(' */

	TreeNode *expression = parse_additive_expression(parser);

	if (expression == NULL)
	{
		return node;
	}

	if (!check(parser, RPAREN))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Expected ')' at end of input");
		}
		else
		{
			SYNTAX_ERR("Expected ')' at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return node;
	}

	advance(parser); /* ')' */

	node->left_child = condition;
	node->right_child = expression;

	condition->parent = node;
	expression->parent = node;

	return node;
}

static TreeNode *parse_atom_expression(Parser *parser)
{
	Token *token = peek(parser);

	if (token == NULL)
	{
		SYNTAX_ERR("Expected expression at end of input");
		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	if (token->category == LPAREN)
	{
		advance(parser);

		TreeNode *node = parse_additive_expression(parser);

		if (!check(parser, RPAREN))
		{
			token = peek(parser);

			if (token == NULL)
			{
				SYNTAX_ERR("Expected ')' at end of input");
			}
			else
			{
				SYNTAX_ERR("Expected ')' at %d:%d (got '%.*s')",
						token->pos.row,
						token->pos.col,
						(int)token->lexeme_length,
						token->lexeme_start);
			}

			parser->has_error = true;
			parser->statement_has_error = true;
			return node;
		}

		advance(parser);
		return node;
	}

	if (token->category == WORD)
	{
		token->category = IDENT;
		advance(parser);

		TreeNode *node = tree_node_create(
				parser->arena,
				1,
				NULL,
				NULL,
				NULL
				);

		node->token_arr[0] = token;

		return node;
	}

	SYNTAX_ERR("Expected expression at %d:%d (got '%.*s')",
			token->pos.row,
			token->pos.col,
			(int)token->lexeme_length,
			token->lexeme_start);
	parser->has_error = true;
	parser->statement_has_error = true;
	return NULL;
}

static TreeNode *parse_unary_expression(Parser *parser)
{
	Token *token = peek(parser);

	if (token == NULL)
	{
		return NULL;
	}

	if (token->category == WORD)
	{
		switch (get_word_category(token))
		{
			case SELECT:
				return parse_select(parser);

			case PROJECT:
				return parse_project(parser);

			// TODO: remove rename since its not a feature sql
			case RENAME:
				return parse_rename(parser);

			default:
				break;
		}
	}

	return parse_atom_expression(parser);
}

static TreeNode *parse_join(Parser *parser, TreeNode *left)
{
	Token *join_token = advance(parser);
	join_token->category = JOIN;

	if (!check(parser, LBRACKET))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete join expression at end of input: expected '[' condition ']' and right relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected '[' after 'join' at %d:%d",
					token->pos.row,
					token->pos.col);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	advance(parser); /* '[' */

	TreeNode *condition = parse_condition(parser);

	if (condition == NULL)
	{
		return NULL;
	}

	if (!check(parser, RBRACKET))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete join expression at end of input: expected ']' and right relation expression");
		}
		else
		{
			SYNTAX_ERR("Expected ']' after join condition at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	advance(parser); /* ']' */

	TreeNode *right = parse_unary_expression(parser);

	if (right == NULL)
	{
		SYNTAX_ERR("Expected identifier at end of input");
		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	Token *relations_token = token_new(parser->arena);
	*relations_token = (Token){
		.category = JOIN_RELATIONS,
		.lexeme_start = "JOIN_RELATIONS",
		.lexeme_length = 14,
		.pos = {0, 0}
	};

	TreeNode *relations = tree_node_create(
			parser->arena,
			1,
			NULL,
			left,
			right
			);

	relations->token_arr[0] = relations_token;

	TreeNode *node = tree_node_create(
			parser->arena,
			1,
			NULL,
			relations,
			condition
			);

	node->token_arr[0] = join_token;

	left->parent = relations;
	right->parent = relations;

	relations->parent = node;
	condition->parent = node;

	return node;
}

static TreeNode *parse_multiplicative_expression(Parser *parser)
{
	TreeNode *left = parse_unary_expression(parser);

	if (left == NULL)
	{
		return NULL;
	}

	while (check(parser, WORD))
	{
		Category category = get_word_category(parser->cur_token);

		if (category == TIMES)
		{
			Token *operator_token = advance(parser);
			operator_token->category = TIMES;

			TreeNode *right = parse_unary_expression(parser);

			if (right == NULL)
			{
				return NULL;
			}

			TreeNode *node = tree_node_create(
					parser->arena,
					1,
					NULL,
					left,
					right
					);

			node->token_arr[0] = operator_token;

			left->parent = node;
			right->parent = node;

			left = node;
		}
		else if (category == JOIN)
		{
			left = parse_join(parser, left);

			if (left == NULL)
			{
				return NULL;
			}
		}
		else
		{
			break;
		}
	}

	return left;
}

static TreeNode *parse_additive_expression(Parser *parser)
{
	TreeNode *left = parse_multiplicative_expression(parser);

	if (left == NULL)
	{
		return NULL;
	}

	while (check(parser, WORD))
	{
		Category category = get_word_category(parser->cur_token);

		if (category != UNION &&
				category != INTERSECT &&
				category != MINUS)
		{
			break;
		}

		Token *operator_token = advance(parser);

		if (category == UNION)
		{
			operator_token->category = UNION;
		}
		else if (category == INTERSECT)
		{
			operator_token->category = INTERSECT;
		}
		else
		{
			operator_token->category = MINUS;
		}

		TreeNode *right = parse_multiplicative_expression(parser);

		if (right == NULL)
		{
			return NULL;
		}

		TreeNode *node = tree_node_create(
				parser->arena,
				1,
				NULL,
				left,
				right
				);

		node->token_arr[0] = operator_token;

		left->parent = node;
		right->parent = node;

		left = node;
	}

	return left;
}

static TreeNode *parse_operand(Parser *parser)
{
	Token *token = peek(parser);

	if (token == NULL)
	{
		SYNTAX_ERR("Expected operand at end of input");
		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	if (token->category == NUMBER || token->category == STRING)
	{
		TreeNode *node = tree_node_create(
				parser->arena,
				1,
				NULL,
				NULL,
				NULL
				);

		node->token_arr[0] = advance(parser);

		return node;
	}

	if (token->category == WORD)
	{
		token->category = IDENT;
		Token *first = advance(parser);

		if (check(parser, DOT))
		{
			advance(parser);

			Token *second = peek(parser);

			if (second == NULL)
			{
				SYNTAX_ERR("Expected identifier after '.' at end of input");
				parser->has_error = true;
				parser->statement_has_error = true;
				return NULL;
			}

			if (second->category != WORD)
			{
				SYNTAX_ERR("Expected identifier after '.' at %d:%d (got '%.*s')",
						second->pos.row,
						second->pos.col,
						(int)second->lexeme_length,
						second->lexeme_start);
				parser->has_error = true;
				parser->statement_has_error = true;
				return NULL;
			}

			second->category = IDENT;
			advance(parser);

			TreeNode *node = tree_node_create(
					parser->arena,
					2,
					NULL,
					NULL,
					NULL
					);

			node->token_arr[0] = first;
			node->token_arr[1] = second;

			return node;
		}

		TreeNode *node = tree_node_create(
				parser->arena,
				1,
				NULL,
				NULL,
				NULL
				);

		node->token_arr[0] = first;

		return node;
	}

	SYNTAX_ERR("Expected operand at %d:%d (got '%.*s')",
			token->pos.row,
			token->pos.col,
			(int)token->lexeme_length,
			token->lexeme_start);
	parser->has_error = true;
	parser->statement_has_error = true;
	return NULL;
}

static TreeNode *parse_comparison(Parser *parser)
{
	TreeNode *left = parse_operand(parser);

	if (left == NULL)
	{
		return NULL;
	}

	Token *operator_token = peek(parser);

	if (operator_token == NULL ||
			(operator_token->category != EQUAL &&
			 operator_token->category != NOT_EQUAL &&
			 operator_token->category != LESS_THAN &&
			 operator_token->category != LESS_THAN_OR_EQUAL &&
			 operator_token->category != GREATER_THAN &&
			 operator_token->category != GREATER_THAN_OR_EQUAL))
	{
		if (operator_token == NULL)
		{
			SYNTAX_ERR("Expected comparison operator at end of input");
		}
		else
		{
			SYNTAX_ERR("Expected comparison operator at %d:%d (got '%.*s')",
					operator_token->pos.row,
					operator_token->pos.col,
					(int)operator_token->lexeme_length,
					operator_token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return left;
	}

	advance(parser);

	TreeNode *right = parse_operand(parser);

	if (right == NULL)
	{
		return NULL;
	}

	TreeNode *node = tree_node_create(
			parser->arena,
			1,
			NULL,
			left,
			right
			);

	node->token_arr[0] = operator_token;

	left->parent = node;
	right->parent = node;

	return node;
}

static TreeNode *parse_not_expression(Parser *parser)
{
	if (check(parser, WORD) && get_word_category(parser->cur_token) == NOT)
	{
		Token *operator_token = advance(parser);
		operator_token->category = NOT;

		TreeNode *child = parse_not_expression(parser);

		if (child == NULL)
		{
			return NULL;
		}

		TreeNode *node = tree_node_create(
				parser->arena,
				1,
				NULL,
				child,
				NULL
				);

		node->token_arr[0] = operator_token;
		child->parent = node;

		return node;
	}

	if (check(parser, LPAREN))
	{
		advance(parser);

		TreeNode *node = parse_condition(parser);

		if (!check(parser, RPAREN))
		{
			Token *token = peek(parser);

			if (token == NULL)
			{
				SYNTAX_ERR("Expected ')' at end of input");
			}
			else
			{
				SYNTAX_ERR("Expected ')' at %d:%d (got '%.*s')",
						token->pos.row,
						token->pos.col,
						(int)token->lexeme_length,
						token->lexeme_start);
			}

			parser->has_error = true;
			parser->statement_has_error = true;
			return node;
		}

		advance(parser);
		return node;
	}

	return parse_comparison(parser);
}

static TreeNode *parse_and_expression(Parser *parser)
{
	TreeNode *left = parse_not_expression(parser);

	while (check(parser, WORD) &&
			get_word_category(parser->cur_token) == AND)
	{
		Token *operator_token = advance(parser);
		operator_token->category = AND;

		TreeNode *right = parse_not_expression(parser);

		if (right == NULL)
		{
			return NULL;
		}

		TreeNode *node = tree_node_create(
				parser->arena,
				1,
				NULL,
				left,
				right
				);

		node->token_arr[0] = operator_token;

		left->parent = node;
		right->parent = node;

		left = node;
	}

	return left;
}

static TreeNode *parse_or_expression(Parser *parser)
{
	TreeNode *left = parse_and_expression(parser);

	while (check(parser, WORD) &&
			parser->cur_token->lexeme_length == 2 &&
			strncmp(parser->cur_token->lexeme_start, "or", 2) == 0)
	{
		Token *operator_token = advance(parser);
		operator_token->category = OR;

		TreeNode *right = parse_and_expression(parser);

		if (right == NULL)
		{
			return NULL;
		}

		TreeNode *node = tree_node_create(
				parser->arena,
				1,
				NULL,
				left,
				right
				);

		node->token_arr[0] = operator_token;

		left->parent = node;
		right->parent = node;

		left = node;
	}

	return left;
}

static TreeNode *parse_condition(Parser *parser)
{
	return parse_or_expression(parser);
}

static TreeNode *parse_relation_definition_attribute_list(
		Parser *parser, size_t *attribute_count)
{
	Token *token = peek(parser);

	if (token == NULL)
	{
		SYNTAX_ERR("Incomplete relation definition at end of input: expected attribute");
		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	if (check(parser, RPAREN))
	{
		SYNTAX_ERR("Expected attribute at %d:%d (got ')')",
				token->pos.row,
				token->pos.col);
		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	if (token->category != WORD)
	{
		SYNTAX_ERR("Expected attribute at %d:%d (got '%.*s')",
				token->pos.row,
				token->pos.col,
				(int)token->lexeme_length,
				token->lexeme_start);
		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	size_t count = 1;
	size_t lookahead = parser->cur_pos + 1;

	while (lookahead < parser->token_count)
	{
		token = (Token *)parser->arena->base + lookahead;

		if (token->category != COMMA)
		{
			break;
		}

		lookahead++;

		if (lookahead >= parser->token_count)
		{
			SYNTAX_ERR("Incomplete relation definition at end of input: expected attribute after ','");
			parser->has_error = true;
			parser->statement_has_error = true;
			return NULL;
		}

		token = (Token *)parser->arena->base + lookahead;

		if (token->category != WORD)
		{
			SYNTAX_ERR("Expected attribute after ',' at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
			parser->has_error = true;
			parser->statement_has_error = true;
			return NULL;
		}

		count++;
		lookahead++;
	}

	TreeNode *attributes_node = tree_node_create(
			parser->arena,
			count,
			NULL,
			NULL,
			NULL
			);

	for (size_t i = 0; i < count; i++)
	{
		token = peek(parser);

		/* Attributes are identifiers even when their spelling matches a keyword. */
		token->category = IDENT;
		attributes_node->token_arr[i] = advance(parser);

		if (i + 1 < count)
		{
			advance(parser); /* ',' */
		}
	}

	if (!check(parser, RPAREN))
	{
		token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete relation definition at end of input: expected ')'");
		}
		else
		{
			SYNTAX_ERR("Expected ')' after relation attributes at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	advance(parser); /* ')' */

	*attribute_count = count;

	return attributes_node;
}

static TreeNode *parse_relation_definition_tuple(
		Parser *parser, size_t attribute_count)
{
	TreeNode *tuple = tree_node_create(
			parser->arena,
			attribute_count + 1,
			NULL,
			NULL,
			NULL
			);

	Token *tuple_token = token_new(parser->arena);
	*tuple_token = (Token){
		.category = TUPLE,
		.lexeme_start = NULL,
		.lexeme_length = 0,
		.pos = {0, 0}
	};

	tuple->token_arr[0] = tuple_token;

	for (size_t i = 0; i < attribute_count; i++)
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete tuple at end of input: expected value");
			parser->has_error = true;
			parser->statement_has_error = true;
			return tuple;
		}

		if (token->category != WORD &&
				token->category != NUMBER &&
				token->category != STRING)
		{
			SYNTAX_ERR("Expected tuple value at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
			parser->has_error = true;
			parser->statement_has_error = true;
			return tuple;
		}

		tuple->token_arr[i + 1] = advance(parser);

		if (i + 1 < attribute_count)
		{
			if (!check(parser, COMMA))
			{
				token = peek(parser);

				if (token == NULL)
				{
					SYNTAX_ERR("Incomplete tuple: expected %zu values, got %zu",
							attribute_count,
							i + 1);
				}
				else
				{
					SYNTAX_ERR("Expected ',' between tuple values at %d:%d (got '%.*s')",
							token->pos.row,
							token->pos.col,
							(int)token->lexeme_length,
							token->lexeme_start);
				}

				parser->has_error = true;
				parser->statement_has_error = true;
				return tuple;
			}

			advance(parser); /* ',' */
		}
	}

	/*
	 * If another comma follows the expected number of values, then
	 * this tuple contains too many values.
	 */
	if (check(parser, COMMA))
	{
		Token *token = peek(parser);

		SYNTAX_ERR("Too many values in tuple at %d:%d",
				token->pos.row,
				token->pos.col);
		parser->has_error = true;
		parser->statement_has_error = true;
		return tuple;
	}

	return tuple;
}

static TreeNode *parse_relation_definition_tuple_list(
		Parser *parser, size_t attribute_count)
{
	if (!check(parser, LBRACE))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete relation definition at end of input: expected '{'");
		}
		else
		{
			SYNTAX_ERR("Expected '{' after '=' at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	advance(parser); /* '{' */

	while (check(parser, NEWLINE))
	{
		advance(parser);
	}

	if (check(parser, RBRACE))
	{
		advance(parser);
		return NULL;
	}

	TreeNode *tuple_list = NULL;
	TreeNode *last_tuple = NULL;

	while (peek(parser) != NULL && !check(parser, RBRACE))
	{
		TreeNode *tuple = parse_relation_definition_tuple(
				parser,
				attribute_count);

		if (tuple == NULL)
		{
			return tuple_list;
		}

		if (tuple_list == NULL)
		{
			tuple_list = tuple;
		}
		else
		{
			last_tuple->left_child = tuple;
			tuple->parent = last_tuple;
		}

		last_tuple = tuple;

		bool had_newline = false;

		while (check(parser, NEWLINE))
		{
			advance(parser);
			had_newline = true;
		}

		if (check(parser, RBRACE))
		{
			break;
		}

		/*
		 * Each tuple must be separated from the next tuple.
		 * A newline is the separator between tuples.
		 */
		if (!had_newline)
		{
			Token *token = peek(parser);

			SYNTAX_ERR("Expected newline between relation definition tuples at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
			parser->has_error = true;
			parser->statement_has_error = true;
			return tuple_list;
		}
	}

	if (!check(parser, RBRACE))
	{
		SYNTAX_ERR("Expected '}' at end of relation definition tuple list");
		parser->has_error = true;
		parser->statement_has_error = true;
		return tuple_list;
	}

	advance(parser); /* '}' */

	return tuple_list;
}

static TreeNode *parse_relation_definition(Parser *parser)
{
	Token *relation_token = advance(parser);
	relation_token->category = IDENT;

	advance(parser); /* '(' */

	size_t attribute_count = 0;

	TreeNode *attributes_node =
		parse_relation_definition_attribute_list(
				parser,
				&attribute_count);

	if (attributes_node == NULL)
	{
		return NULL;
	}

	Token *definition_token = token_new(parser->arena);
	*definition_token = (Token){
		.category = RELATION_DEFINITION,
		.lexeme_start = NULL,
		.lexeme_length = 0,
		.pos = {0, 0}
	};

	TreeNode *definition_node = tree_node_create(
			parser->arena,
			1,
			NULL,
			NULL,
			NULL
			);

	definition_node->token_arr[0] = definition_token;

	TreeNode *relation_node = tree_node_create(
			parser->arena,
			1,
			NULL,
			NULL,
			NULL
			);

	relation_node->token_arr[0] = relation_token;

	definition_node->left_child = relation_node;
	definition_node->right_child = attributes_node;

	relation_node->parent = definition_node;
	attributes_node->parent = definition_node;

	if (!check(parser, EQUAL))
	{
		Token *token = peek(parser);

		if (token == NULL)
		{
			SYNTAX_ERR("Incomplete relation definition at end of input: expected '='");
		}
		else
		{
			SYNTAX_ERR("Expected '=' after relation definition at %d:%d (got '%.*s')",
					token->pos.row,
					token->pos.col,
					(int)token->lexeme_length,
					token->lexeme_start);
		}

		parser->has_error = true;
		parser->statement_has_error = true;
		return definition_node;
	}

	Token *equals_token = advance(parser);

	TreeNode *equals_node = tree_node_create(
			parser->arena,
			1,
			NULL,
			definition_node,
			NULL
			);

	equals_node->token_arr[0] = equals_token;

	definition_node->parent = equals_node;

	TreeNode *tuple_list =
		parse_relation_definition_tuple_list(
				parser,
				attribute_count);

	/*
	 * Once '=' has been consumed, the '=' node is the statement
	 * root, even when parsing the tuple list reports an error.
	 */
	equals_node->right_child = tuple_list;

	if (tuple_list != NULL)
	{
		tuple_list->parent = equals_node;
	}

	return equals_node;
}

static TreeNode *parse_statement(Parser *parser)
{
	Token *token = peek(parser);

	if (token == NULL)
	{
		return NULL;
	}

	if (token->category == WORD)
	{
		Token *next = peek_next(parser);

		if (get_word_category(token) == IDENT &&
				next != NULL &&
				next->category == LPAREN)
		{
			return parse_relation_definition(parser);
		}
	}

	if (token->category != WORD && token->category != LPAREN)
	{
		SYNTAX_ERR("Expected identifier, keyword, or '(' at %d:%d (got '%.*s')",
				token->pos.row,
				token->pos.col,
				(int)token->lexeme_length,
				token->lexeme_start);
		parser->has_error = true;
		parser->statement_has_error = true;
		return NULL;
	}

	return parse_additive_expression(parser);
}

static const char *category_names[] = {
	[IDENT] = "IDENT",
	[NUMBER] = "NUMBER",
	[STRING] = "STRING",
	[SELECT] = "SELECT",
	[PROJECT] = "PROJECT",
	[RENAME] = "RENAME",
	[UNION] = "UNION",
	[INTERSECT] = "INTERSECT",
	[MINUS] = "MINUS",
	[TIMES] = "TIMES",
	[JOIN] = "JOIN",
	[JOIN_RELATIONS] = "JOIN_RELATIONS",
	[NOT] = "NOT",
	[AND] = "AND",
	[OR] = "OR",
	[RELATION_DEFINITION] = "RELATION_DEFINITION",
	[TUPLE] = "TUPLE",
	[LPAREN] = "LPAREN",
	[RPAREN] = "RPAREN",
	[LBRACKET] = "LBRACKET",
	[RBRACKET] = "RBRACKET",
	[LBRACE] = "LBRACE",
	[RBRACE] = "RBRACE",
	[COMMA] = "COMMA",
	[EQUAL] = "EQUAL",
	[NEWLINE] = "NEWLINE",
};

static void print_tree_node(const TreeNode *node, const char *prefix, bool is_last)
{
    printf("%s%s", prefix, is_last ? "└── " : "├── ");

    if (node == NULL)
    {
        printf("(error)\n");
        return;
    }

    for (size_t i = 0; i < node->token_count; i++)
    {
        Token *token = node->token_arr[i];

        if (token == NULL)
        {
            printf("<missing>");
        }
        else if (token->lexeme_start == NULL)
        {
            printf("<%s>", category_names[token->category]);
        }
        else
        {
            printf("%.*s",
                    (int)token->lexeme_length,
                    token->lexeme_start);
        }

        if (i + 1 < node->token_count)
        {
            printf(" ");
        }
    }

    printf("\n");

    char child_prefix[256];
    snprintf(child_prefix, sizeof(child_prefix), "%s%s",
            prefix,
            is_last ? "    " : "│   ");

    if (node->left_child != NULL)
    {
        bool left_is_last = node->right_child == NULL;
        print_tree_node(node->left_child, child_prefix, left_is_last);
    }

    if (node->right_child != NULL)
    {
        print_tree_node(node->right_child, child_prefix, true);
    }
}

void print_tree(const Tree *tree)
{
	if (tree == NULL || tree->head == NULL)
	{
		printf("PROGRAM\n(empty)\n");
		return;
	}

	printf("PROGRAM\n");

	for (StatementNode *stmt = tree->head;
			stmt != NULL;
			stmt = stmt->next)
	{
		bool is_last = (stmt->next == NULL);

		if (stmt->root == NULL)
		{
			printf("%s(error)\n", is_last ? "└── " : "├── ");
			continue;
		}

		print_tree_node(stmt->root, "", is_last);
	}
}

void parse(Parser *parser, Tree *tree)
{
	tree->head = NULL;
	tree->tail = NULL;

	while (peek(parser) != NULL)
	{
		parser->statement_has_error = false;

		while (check(parser, NEWLINE))
		{
			advance(parser);
		}

		if (peek(parser) == NULL)
		{
			break;
		}

		TreeNode *root = parse_statement(parser);

		Token *next = peek(parser);

		/*
		 * Only report an unexpected token here if the parser has not
		 * already reported a syntax error for this statement.
		 */
		if (!parser->statement_has_error &&
				next != NULL &&
				next->category != NEWLINE)
		{
			SYNTAX_ERR("Expected end of statement at %d:%d (got '%.*s')",
					next->pos.row,
					next->pos.col,
					(int)next->lexeme_length,
					next->lexeme_start);
			parser->has_error = true;
			parser->statement_has_error = true;
		}

		StatementNode *stmt = arena_push(
				parser->arena,
				sizeof(StatementNode),
				_Alignof(StatementNode));

		stmt->root = root;
		stmt->next = NULL;

		if (tree->tail == NULL)
		{
			tree->head = stmt;
		}
		else
		{
			tree->tail->next = stmt;
		}

		tree->tail = stmt;

		if (parser->statement_has_error)
		{
			while (peek(parser) != NULL &&
					!check(parser, NEWLINE))
			{
				advance(parser);
			}
		}

		if (check(parser, NEWLINE))
		{
			advance(parser);
		}
	}
}
