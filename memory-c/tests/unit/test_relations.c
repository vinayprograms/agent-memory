/*
 * Memory Service - Relations Storage Unit Tests
 */

#include "../test_framework.h"
#include "../../src/storage/relations.h"
#include "../../include/error.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static void cleanup_dir(const char* dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

/* Test relations creation */
TEST(relations_create_basic) {
    const char* dir = "/tmp/test_relations_create";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    relations_store_t* store = NULL;
    mem_error_t err = relations_create(&store, dir, 1000);

    ASSERT_OK(err);
    ASSERT_NOT_NULL(store);
    ASSERT_EQ(relations_count(store), 0);

    relations_close(store);
    cleanup_dir(dir);
}

/* Test node allocation */
TEST(relations_alloc_node) {
    const char* dir = "/tmp/test_relations_alloc";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    relations_store_t* store = NULL;
    ASSERT_OK(relations_create(&store, dir, 100));

    node_id_t id1, id2, id3;
    ASSERT_OK(relations_alloc_node(store, &id1));
    ASSERT_OK(relations_alloc_node(store, &id2));
    ASSERT_OK(relations_alloc_node(store, &id3));

    ASSERT_EQ(id1, 0);
    ASSERT_EQ(id2, 1);
    ASSERT_EQ(id3, 2);
    ASSERT_EQ(relations_count(store), 3);

    relations_close(store);
    cleanup_dir(dir);
}

/* Test parent-child relationships */
TEST(relations_parent_child) {
    const char* dir = "/tmp/test_relations_parent";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    relations_store_t* store = NULL;
    ASSERT_OK(relations_create(&store, dir, 100));

    /* Create nodes: parent -> child1, child2 */
    node_id_t parent, child1, child2;
    ASSERT_OK(relations_alloc_node(store, &parent));
    ASSERT_OK(relations_alloc_node(store, &child1));
    ASSERT_OK(relations_alloc_node(store, &child2));

    /* Set relationships */
    ASSERT_OK(relations_set_parent(store, child1, parent));
    ASSERT_OK(relations_set_parent(store, child2, parent));
    ASSERT_OK(relations_set_first_child(store, parent, child1));
    ASSERT_OK(relations_set_next_sibling(store, child1, child2));

    /* Verify */
    ASSERT_EQ(relations_get_parent(store, child1), parent);
    ASSERT_EQ(relations_get_parent(store, child2), parent);
    ASSERT_EQ(relations_get_first_child(store, parent), child1);
    ASSERT_EQ(relations_get_next_sibling(store, child1), child2);
    ASSERT_EQ(relations_get_next_sibling(store, child2), NODE_ID_INVALID);

    relations_close(store);
    cleanup_dir(dir);
}

/* Test level assignment */
TEST(relations_levels) {
    const char* dir = "/tmp/test_relations_levels";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    relations_store_t* store = NULL;
    ASSERT_OK(relations_create(&store, dir, 100));

    node_id_t id;
    ASSERT_OK(relations_alloc_node(store, &id));

    ASSERT_OK(relations_set_level(store, id, LEVEL_SESSION));
    ASSERT_EQ(relations_get_level(store, id), LEVEL_SESSION);

    ASSERT_OK(relations_set_level(store, id, LEVEL_STATEMENT));
    ASSERT_EQ(relations_get_level(store, id), LEVEL_STATEMENT);

    relations_close(store);
    cleanup_dir(dir);
}

/* Test get_children */
TEST(relations_get_children) {
    const char* dir = "/tmp/test_relations_children";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    relations_store_t* store = NULL;
    ASSERT_OK(relations_create(&store, dir, 100));

    /* Create: parent -> [c1, c2, c3] */
    node_id_t parent, c1, c2, c3;
    ASSERT_OK(relations_alloc_node(store, &parent));
    ASSERT_OK(relations_alloc_node(store, &c1));
    ASSERT_OK(relations_alloc_node(store, &c2));
    ASSERT_OK(relations_alloc_node(store, &c3));

    ASSERT_OK(relations_set_first_child(store, parent, c1));
    ASSERT_OK(relations_set_next_sibling(store, c1, c2));
    ASSERT_OK(relations_set_next_sibling(store, c2, c3));

    node_id_t children[10];
    size_t count = relations_get_children(store, parent, children, 10);

    ASSERT_EQ(count, 3);
    ASSERT_EQ(children[0], c1);
    ASSERT_EQ(children[1], c2);
    ASSERT_EQ(children[2], c3);

    relations_close(store);
    cleanup_dir(dir);
}

/* Test get_ancestors */
TEST(relations_get_ancestors) {
    const char* dir = "/tmp/test_relations_ancestors";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    relations_store_t* store = NULL;
    ASSERT_OK(relations_create(&store, dir, 100));

    /* Create: root -> mid -> leaf */
    node_id_t root, mid, leaf;
    ASSERT_OK(relations_alloc_node(store, &root));
    ASSERT_OK(relations_alloc_node(store, &mid));
    ASSERT_OK(relations_alloc_node(store, &leaf));

    ASSERT_OK(relations_set_parent(store, mid, root));
    ASSERT_OK(relations_set_parent(store, leaf, mid));

    node_id_t ancestors[10];
    size_t count = relations_get_ancestors(store, leaf, ancestors, 10);

    ASSERT_EQ(count, 2);
    ASSERT_EQ(ancestors[0], mid);
    ASSERT_EQ(ancestors[1], root);

    relations_close(store);
    cleanup_dir(dir);
}

/* Test count_descendants */
TEST(relations_count_descendants) {
    const char* dir = "/tmp/test_relations_descendants";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    relations_store_t* store = NULL;
    ASSERT_OK(relations_create(&store, dir, 100));

    /*
     * Tree structure:
     *     root
     *    /    \
     *   m1     m2
     *  / \      |
     * b1 b2    b3
     */
    node_id_t root, m1, m2, b1, b2, b3;
    ASSERT_OK(relations_alloc_node(store, &root));
    ASSERT_OK(relations_alloc_node(store, &m1));
    ASSERT_OK(relations_alloc_node(store, &m2));
    ASSERT_OK(relations_alloc_node(store, &b1));
    ASSERT_OK(relations_alloc_node(store, &b2));
    ASSERT_OK(relations_alloc_node(store, &b3));

    /* Set up tree */
    ASSERT_OK(relations_set_first_child(store, root, m1));
    ASSERT_OK(relations_set_next_sibling(store, m1, m2));
    ASSERT_OK(relations_set_first_child(store, m1, b1));
    ASSERT_OK(relations_set_next_sibling(store, b1, b2));
    ASSERT_OK(relations_set_first_child(store, m2, b3));

    ASSERT_EQ(relations_count_descendants(store, root), 5);
    ASSERT_EQ(relations_count_descendants(store, m1), 2);
    ASSERT_EQ(relations_count_descendants(store, m2), 1);
    ASSERT_EQ(relations_count_descendants(store, b1), 0);

    relations_close(store);
    cleanup_dir(dir);
}

/* Test persistence */
TEST(relations_persistence) {
    const char* dir = "/tmp/test_relations_persist";
    cleanup_dir(dir);
    mkdir(dir, 0755);

    /* Create and store */
    {
        relations_store_t* store = NULL;
        ASSERT_OK(relations_create(&store, dir, 100));

        node_id_t n1, n2;
        ASSERT_OK(relations_alloc_node(store, &n1));
        ASSERT_OK(relations_alloc_node(store, &n2));

        ASSERT_OK(relations_set_parent(store, n2, n1));
        ASSERT_OK(relations_set_level(store, n1, LEVEL_SESSION));
        ASSERT_OK(relations_set_level(store, n2, LEVEL_MESSAGE));

        ASSERT_OK(relations_sync(store));
        relations_close(store);
    }

    /* Reopen and verify */
    {
        relations_store_t* store = NULL;
        ASSERT_OK(relations_open(&store, dir));

        ASSERT_EQ(relations_count(store), 2);
        ASSERT_EQ(relations_get_parent(store, 1), 0);
        ASSERT_EQ(relations_get_level(store, 0), LEVEL_SESSION);
        ASSERT_EQ(relations_get_level(store, 1), LEVEL_MESSAGE);

        relations_close(store);
    }

    cleanup_dir(dir);
}

/* Test invalid arguments */
TEST(relations_invalid_args) {
    relations_store_t* store = NULL;

    ASSERT_EQ(relations_create(NULL, "/tmp/x", 100), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(relations_create(&store, NULL, 100), MEM_ERR_INVALID_ARG);
    ASSERT_EQ(relations_create(&store, "/tmp/x", 0), MEM_ERR_INVALID_ARG);

    node_id_t id;
    ASSERT_EQ(relations_alloc_node(NULL, &id), MEM_ERR_INVALID_ARG);

    ASSERT_EQ(relations_get_parent(NULL, 0), NODE_ID_INVALID);
    ASSERT_EQ(relations_count(NULL), 0);
}

TEST_MAIN()
