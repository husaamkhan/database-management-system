
#include "common.h"
#include "query_representation.h"


/* Operation-specific builders */

static Query *build_table(TreeNode *node)
{
    /* TODO: Construct QUERY_TABLE */
    return NULL;
}

static Query *build_project(TreeNode *node)
{
    /* TODO: Construct QUERY_PROJECT */
    return NULL;
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

Category get_base_operation(TreeNode *node)
{
    // TODO: Return the base operation based on the token_arr of the node
    return NULL;
}

// Individual query construction
static Query *build_query(TreeNode *node)
{
    /*
    * Identify the base operation of this query
    * and dispatch to its corresponding builder.
    */
    
    switch (get_base_operation(node))
    {
        case PROJECT:
        return build_project(node);
        
        case SELECT:
        return build_filter(node);
        
        case JOIN:
        return build_join(node);
        
        case TIMES:
        return build_cross_join(node);
        
        case UNION:
        case INTERSECT:
        case MINUS:
        return build_set_operation(node);
        
        default:
        return NULL;
    }
}

// Main entry point
void **build(
    Tree *parse_tree,
    Query **queries, // TODO: switch to using array list
    size_t *query_count
)
{
    size_t count = 0;
    
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
            /* Handle construction failure */
            return NULL;
        }
        
        /* Append representation to queries */
        queries = append_query(queries, representation, &count);
        
        current_statement = current_statement->next;
    }
    
    *query_count = count;
}

