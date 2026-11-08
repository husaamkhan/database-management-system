#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"
#include "parser.h"
#include "executor.h"

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
	Arena arena = arena_create(64 * 1024);

	size_t token_count = 0;

	lex(
		&lexer,
		&arena,
		&token_count
	);

	if (lexer.has_error)
	{
		LOG_ERR("Errors occurred during lexing");

		free(file_contents);
		arena_destroy(&arena);

		return -1;
	}

	Parser parser;

	parser_init(
		&parser,
		&arena,
		token_count
	);

	Tree tree = {
		.head = NULL,
		.tail = NULL
	};

	parse(
		&parser,
		&tree
	);

	if (parser.has_error)
	{
		free(file_contents);
		arena_destroy(&arena);

		return -1;
	}

	if (print_parse_tree)
	{
		print_tree(&tree);
	}

	Executor executor;

	executor_init(
		&executor,
		&arena
	);

	execute(
		&executor,
		&tree
	);

	free(file_contents);
	arena_destroy(&arena);

	return 0;
}

