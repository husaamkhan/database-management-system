#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"
#include "parser.h"
#include "query_representation.h"

int main(int argc, char **argv)
{
	bool print_parse_tree = false;
	const char *query_file = NULL;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--tree") == 0)
		{
			print_parse_tree = true;
		}
		else if (query_file == NULL)
		{
			query_file = argv[i];
		}
		else
		{
			LOG_ERR("Usage: %s [--tree] [query-file]", argv[0]);
			return -1;
		}
	}

	size_t file_size = 0;
	char *file_contents = NULL;

	if (query_file == NULL)
	{
	    // TODO: why is this not a constant?
		size_t capacity = 4096;

		file_contents = malloc(capacity);

		if (file_contents == NULL)
		{
			LOG_ERR("Could not allocate input buffer");
			return -1;
		}

		int c;

		while ((c = fgetc(stdin)) != EOF)
		{
			if (file_size >= capacity)
			{
				capacity *= 2;

				char *new_buffer =
					realloc(file_contents, capacity);

				if (new_buffer == NULL)
				{
					LOG_ERR("Could not grow input buffer");
					free(file_contents);
					return -1;
				}

				file_contents = new_buffer;
			}

			file_contents[file_size++] = (char)c;
		}
	}
	else
	{
		FILE *in = fopen(query_file, "rb");

		if (in == NULL)
		{
			LOG_ERR("Could not open file '%s'", query_file);
			return -1;
		}

		file_contents = read_file(in, &file_size);

		fclose(in);

		if (file_contents == NULL)
		{
			return -1;
		}
	}

	Lexer lexer;
	lexer_init(
		&lexer,
		file_contents,
		file_size
	);

	// 64 KiB virtual address space allocation
	// TODO: initial capacity should be set as a defined constant
	Arena frontend_arena = arena_create(ARENA_INITIAL_CAPACITY);

	size_t token_count = 0;

	lex(
		&lexer,
		&frontend_arena,
		&token_count
	);

	if (lexer.has_error)
	{
		LOG_ERR("Errors occurred during lexing");

		free(file_contents);
		arena_destroy(&frontend_arena);

		return -1;
	}

	Parser parser;

	parser_init(
		&parser,
		&frontend_arena,
		token_count
	);

	Tree parse_tree = {
		.head = NULL,
		.tail = NULL
	};

	parse(
		&parser,
		&parse_tree
	);

	if (parser.has_error)
	{
		free(file_contents);
		arena_destroy(&frontend_arena);

		return -1;
	}

	if (print_parse_tree)
	{
		print_tree(&parse_tree);
	}

	RepresentationBuilder *builder = malloc(sizeof(RepresentationBuilder));

	// builder will allocate query representations in arena and store pointers
	// to each representation in the arraylist
    Arena representation_arena = arena_create(ARENA_INITIAL_CAPACITY);
    ArrayList *representation_list = array_list_create(sizeof(Query*));

    build(builder, &representation_arena, representation_list, &parse_tree);

    // file, tokens, and parse tree are removeable at this stage because any
    // data that was required has been memcpy'd into the query representations
	free(file_contents);
	arena_destroy(&frontend_arena);

	return 0;
}
