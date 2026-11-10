
#include "common.h"
#include "query_representation.h"


/* =========================================================================
* GLOBALS
* ========================================================================== */
Arena *arena;
RepresentationBuilder *builder;

/* =========================================================================
* HELPERS
* ========================================================================== */
// set error marker to true and log the error
#define BUILDER_ERR(fmt, ...) \
    do \
    { \
        builder->has_error = true; \
        LOG_ERR(fmt, ##__VA_ARGS__); \
    } while (0)


/* =========================================================================
* OPERATION-SPECIFIC BUILDERS
* ========================================================================== */

static Query *build_filter(TreeNode *node);
static Query *build_join(TreeNode *node);
static Query *build_cross_join(TreeNode *node);
static Query *build_set_operation(TreeNode *node);

static Query *build_table(TreeNode *node)
{
    /* TODO: Construct QUERY_TABLE */
    return NULL;
}

/*
TODO: instead of malloc, its better to use arena here. freeing queryies is
becoming too complicated. to free a query, you'd need a helper that determines
the type of the query to understand its structure, then based off of that it
would need to recursively go through each sub-query of the representation and
free all of those. you'd need a separate helper to destroy expressions which hs
similar issues. you'd also need to have separate logic to free data inside
arraylists. much easier to just pop off of the arena. it will also be much
easier to just destroy an entire arena all together than to go through each
query and expression one by one, freeing every single little piece of data one
by one when the executor is done running.
*/
static Query *build_project(TreeNode *node)
{
    // record end of arena before representation construction
    size_t checkpoint = arena->used;

    Query *query = arena_push(arena, sizeof(Query), _Alignof(Query));
    query->type = QUERY_PROJECT;

    // build representation for the input relation to perform the project on
    Token *token = node->right_child->token_arr[0];
    switch (token->category)
    {
        case IDENT:
        {
            query->project.input = build_table(node->right_child);
            break;
        }

        case SELECT:
        {
            query->project.input = build_filter(node->right_child);
            break;
        }

        case PROJECT:
        {
            query->project.input = build_project(node->right_child);
            break;
        }

        case UNION:
        case INTERSECT:
        case MINUS:
        {
            query->project.input = build_set_operation(node->right_child);
            break;
        }

        case TIMES:
        {
            query->project.input = build_cross_join(node->right_child);
            break;
        }

        case JOIN:
        {
            query->project.input = build_join(node->right_child);
            break;
        }

        default:
            BUILDER_ERR(
                "Invalid input relation for project query at (%d:%d)",
                token->pos.row,
                token->pos.col
            );

            // free memory allocated for query
            arena->used = checkpoint;
            return NULL;
    }

    // each builder function above can return null if they failed
    if (query->project.input == NULL)
    {
        // free memory allocated for query
        arena->used = checkpoint;
        return NULL;
    }

    /*
    TODO: change columlist to be arena-backed
    TODO: since treenode stores how many tokens it has, we can modify this to initialize an arraylist of that same size to minimize resizing, as resizing with an arena backed arraylist will waste a lot of memory
    */
    // column_list stores pointers to strings that are allocated in the arena
    ArrayList *column_list = array_list_create(sizeof(char*));

    // populate column list with each column provided in the input
    for (int i = 0; i < node->left_child->token_count; i++)
    {
        Token *token = node->left_child->token_arr[i];

        // shouldnt really happen but just in case, report an error if the
        // token in the attributes array is not an identifier
        if (token->category != IDENT)
        {
            BUILDER_ERR(
                "Invalid column provided for project query attributes at (%d:%d). Must provide a valid identifier.",
                token->pos.row,
                token->pos.col
            );

            array_list_free(column_list);

            // free memory allocated for query
            arena->used = checkpoint;

            return NULL;
        }

        char *column_name = arena_push(
            arena,
            token->lexeme_length+1,
            _Alignof(char)
        );

        memcpy(
            column_name,
            token->lexeme_start,
            token->lexeme_length
        );

        column_name[token->lexeme_length] = '\0';

        array_list_append(column_list, column_name);
    }

    query->project.column_list = column_list;
    return query;
}

static Query *build_filter(TreeNode *node)
{
    /* TODO: Construct QUERY_FILTER */
    return NULL;
}

static Query *build_join(TreeNode *node)
{
    /* TODO: Construct QUERY_JOIN */
    return NULL;
}

static Query *build_cross_join(TreeNode *node)
{
    /* TODO: Construct QUERY_CROSS_JOIN */
    return NULL;
}

static Query *build_set_operation(TreeNode *node)
{
    /* TODO: Construct QUERY_UNION, QUERY_INTERSECT, or QUERY_EXCEPT */
    return NULL;
}

// Individual query construction
static Query *build_query(TreeNode *node)
{
    /*
    * Identify the base operation of this query
    * and dispatch to its corresponding builder.
    */
    Token *token = node->token_arr[0];
    switch (token->category)
    {
        case PROJECT:
        {
            return build_project(node);
        }

        case SELECT:
        {
            return build_filter(node);
        }

        case JOIN:
        {
            return build_join(node);
        }

        case TIMES:
        {
            return build_cross_join(node);
        }

        case UNION:
        case INTERSECT:
        case MINUS:
        {
            return build_set_operation(node);
        }

        default:
        {
            LOG_ERR(
                "Could not determine base operation of token at (%d:%d)",
                token->pos.row,
                token->pos.col
            );

            builder->has_error = true;

            return NULL;
        }
    }
}

/* =========================================================================
* MAIN ENTRY POINT
* ========================================================================== */

void build
(
    RepresentationBuilder *b,
    Arena *a,
    ArrayList *representation_list,
    Tree *parse_tree
)
{
    // initialize builder, arena, and arraylist
    builder = b;
    arena = a;

    builder->has_error = false;

    /*
    * Iterate through each individual query tree.
    * The traversal depends on how the top-level query list
    * is represented in our parser.
    */

    StatementNode *current_statement = parse_tree->head;
    while (current_statement != NULL)
    {
        Query *representation = build_query(current_statement->root);

        if (representation == NULL)
        {
            // TODO: should probably have some error reported rather than a
            // silent failure like this
            return;
        }

        array_list_append(representation_list, representation);

        current_statement = current_statement->next;
    }
}
