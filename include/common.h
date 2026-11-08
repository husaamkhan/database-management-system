#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

/* ==================================================
 * LOGGING
 * ================================================== */

#define LOG(fmt, ...)     printf(fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define SYNTAX_ERR(fmt, ...) LOG_ERR("[SYNTAX] " fmt, ##__VA_ARGS__)

#ifdef DEBUG
    #define LOG_DBG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
    #define LOG_DBG(fmt, ...) ((void)0)
#endif

/* ==================================================
 * UTILITY FUNCTIONS
 * ================================================== */

/*
 * Rounds a value up to the next multiple of the given alignment.
 *
 * Input:
 *   alignment - Required alignment.
 *   value     - Value to align.
 *
 * Output:
 *   None.
 *
 * Returns:
 *   The value rounded up to the next multiple of alignment.
 */
static inline size_t align_up(size_t alignment, size_t value)
{
	size_t r = value % alignment;
	if (r != 0)
	{
		value += alignment - r;
	}

	return value;
}

/*
 * Reads the contents of a file into a newly allocated buffer.
 *
 * Input:
 *   in - File stream to read from.
 *
 * Output:
 *   size_out - Set to the number of bytes read.
 *
 * Returns:
 *   A pointer to the allocated file contents, or NULL if reading fails.
 */
static inline char *read_file(FILE *in, size_t *size_out)
{
	fseek(in, 0, SEEK_END);

	long size = ftell(in);

	if (size < 0)
	{
		fclose(in);
		return NULL;
	}

	*size_out = (size_t)size;

	fseek(in, 0, SEEK_SET);

	char *buffer = malloc(*size_out);

	if (!fread(buffer, 1, *size_out, in))
	{
		LOG_ERR("Couldn't read file");
		free(buffer);
		return NULL;
	}

	return buffer;
}

/* ==================================================
 * DATA TYPES
 * ================================================== */

typedef struct
{
	int row;
	int col;
} FilePosition;

typedef enum
{
	LPAREN,
	RPAREN,
	LBRACKET,
	RBRACKET,
	LBRACE,
	RBRACE,
	DOT,
	COMMA,
	EQUAL,
	NOT_EQUAL,
	LESS_THAN,
	LESS_THAN_OR_EQUAL,
	GREATER_THAN,
	GREATER_THAN_OR_EQUAL,
	NUMBER,
	STRING,
	WORD,
	NEWLINE,
	COMMENT,
	SELECT,
	PROJECT,
	RENAME, // TODO: remove rename since its not a feature in sql
	UNION,
	INTERSECT,
	MINUS,
	TIMES,
	JOIN,
	JOIN_RELATIONS,
	AND,
	OR,
	NOT,
	IDENT,
	RELATION_DEFINITION,
	TUPLE
} Category;

typedef struct
{
	Category        category;
	const char     *lexeme_start;
	size_t          lexeme_length;
	FilePosition    pos;
} Token;

typedef struct
{
	char *base;
	size_t used;
	size_t capacity;
	size_t prev_used; // used for arena_pop because arena_push aligns upwards
} Arena;

typedef struct TreeNode
{
	struct TreeNode *parent;
	struct TreeNode *left_child;
	struct TreeNode *right_child;

	Token **token_arr;
	size_t token_count;
} TreeNode;

typedef struct StatementNode
{
	TreeNode *root;
	struct StatementNode *next;
} StatementNode;

typedef struct
{
	StatementNode *head;
	StatementNode *tail;
} Tree;

/* ==================================================
 * ARENA
 * ================================================== */

/*
 * Creates an empty arena.
 *
 * Input:
 *   initial_capacity - Initial capacity of the arena in bytes.
 *
 * Output:
 *   None.
 *
 * Returns:
 *   A newly initialized Arena.
 */
static inline Arena arena_create(size_t initial_capacity)
{
	// TODO: mmap is Linux/POSIX-specific and won't work on other platforms
	char *base = mmap(
		NULL,
		initial_capacity,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS,
		-1,
		0);

	if (base == MAP_FAILED)
	{
		LOG_ERR("Arena reservation failed!");
		exit(EXIT_FAILURE);
	}

	return (Arena) {
		.base      = base,
		.used      = 0,
		.capacity  = initial_capacity,
		.prev_used = 0
	};
}

/*
 * Releases the arena's memory back to the OS.
 *
 * Input:
 *   arena - Arena to destroy.
 */
static inline void arena_destroy(Arena *arena)
{
	if (arena->base == NULL)
	{
		LOG_ERR("Attempted to destroy an uninitialized or already destroyed arena!");
		return;
	}

	// TODO: munmap is Linux/POSIX-specific and won't work on other platforms
	if (munmap(arena->base, arena->capacity) != 0)
	{
		LOG_ERR("Arena release failed!");
	}

	arena->base = NULL;
	arena->used = 0;
	arena->capacity = 0;
	arena->prev_used = 0;
}

/*
 * Allocates memory from an arena.
 *
 * Input:
 *   arena     - Arena to allocate from.
 *   size      - Number of bytes to allocate.
 *   alignment - Required alignment of the allocation.
 *
 * Output:
 *   The arena's used space is advanced by the allocation size.
 *   The allocation fails if the arena does not have enough remaining capacity.
 *
 * Returns:
 *   A pointer to the allocated memory.
 */
static inline void *arena_push(Arena *arena, size_t size, size_t alignment)
{
	if (alignment == 0)
	{
		LOG_ERR("Arena allocation alignment must be nonzero!");
		exit(EXIT_FAILURE);
	}

	size_t aligned_used = align_up(alignment, arena->used);

	if (aligned_used > arena->capacity || size > arena->capacity - aligned_used)
	{
		LOG_ERR("Arena capacity exceeded!");
		exit(EXIT_FAILURE);
	}

	void *ptr = arena->base + aligned_used;
	arena->prev_used = arena->used; // tracks previous used for arena_pop
	arena->used = aligned_used + size;
	return ptr;
}


/*
 * Reverts the last arena allocation by resetting the used pointer to its previous location.
 *
 * Input:
 *   arena - Arena to pop from.
 *
 * Output:
 *   arena->used is reset to the value before the last arena_push.
 *
 * Returns:
 *   None.
 */
static inline void arena_pop(Arena *arena)
{
	/*
		TODO: In the future, i would change the arena implementation to allocate each block with
		a header that tracks what the prev_used was. That way, we can allow more than one pop
		at a time. I would then add a get() function that gets the data at a specific position
	*/
	// used instead of something like arena->used -= size of type as arena_push
	// aligns upwards. just subtracting the size of the type would not get rid
	// of any alignment padding from when the arena_push aligned upwards.
	arena->used = arena->prev_used;
}

#define token_new(arena) ((Token *)arena_push((arena), sizeof(Token), _Alignof(Token)))

/* ==================================================
 * TREE
 * ================================================== */

static inline TreeNode *tree_node_create(Arena *arena, size_t token_count, TreeNode *parent, TreeNode *left_child, TreeNode *right_child)
{
	TreeNode *node = arena_push(arena, sizeof(TreeNode), _Alignof(TreeNode));

	Token **token_arr = NULL;
	if (token_count > 0)
	{
		token_arr = arena_push(arena, sizeof(Token *) * token_count, _Alignof(Token *));
	}

	*node = (TreeNode){
		.parent = parent,
		.left_child = left_child,
		.right_child = right_child,
		.token_arr = token_arr,
		.token_count = token_count
	};

	return node;
}
