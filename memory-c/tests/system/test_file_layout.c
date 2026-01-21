/*
 * SVC_MEM_TEST_0002 - Verify file layout
 *
 * Test specification:
 * - Store data across all hierarchy levels
 * - Verify all required files exist in expected locations
 * - Verify mmap'd files are readable without service running
 * - Verify LMDB database is valid and queryable
 */

#include "../test_framework.h"
#include "../../src/storage/wal.h"
#include "../../src/storage/embeddings.h"
#include "../../src/storage/relations.h"
#include "../../include/types.h"
#include "../../include/error.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#define TEST_DIR "/tmp/test_file_layout"

static void cleanup_dir(const char* dir) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

static void setup_dirs(void) {
    cleanup_dir(TEST_DIR);
    mkdir(TEST_DIR, 0755);

    char path[256];
    snprintf(path, sizeof(path), "%s/embeddings", TEST_DIR);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/relations", TEST_DIR);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/wal", TEST_DIR);
    mkdir(path, 0755);
}

static bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static size_t file_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (size_t)st.st_size;
}

/*
 * TEST: Verify embedding files exist for all levels
 */
TEST(embedding_files_exist) {
    setup_dirs();

    char emb_path[256];
    snprintf(emb_path, sizeof(emb_path), "%s/embeddings", TEST_DIR);

    /* Create embeddings store */
    embeddings_store_t* emb = NULL;
    ASSERT_OK(embeddings_create(&emb, emb_path, 100));

    /* Store embeddings at each level */
    for (int level = 0; level < LEVEL_COUNT; level++) {
        uint32_t idx;
        ASSERT_OK(embeddings_alloc(emb, (hierarchy_level_t)level, &idx));

        float values[EMBEDDING_DIM];
        for (int i = 0; i < EMBEDDING_DIM; i++) {
            values[i] = (float)level + (float)i * 0.01f;
        }
        ASSERT_OK(embeddings_set(emb, (hierarchy_level_t)level, idx, values));
    }

    ASSERT_OK(embeddings_sync(emb));
    embeddings_close(emb);

    /* Verify files exist */
    char path[512];
    for (int level = 0; level < LEVEL_COUNT; level++) {
        snprintf(path, sizeof(path), "%s/level_%d.bin", emb_path, level);
        ASSERT_MSG(file_exists(path), "embedding file should exist");
        ASSERT_GT(file_size(path), 0);
    }

    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify relation files exist
 */
TEST(relation_files_exist) {
    setup_dirs();

    char rel_path[256];
    snprintf(rel_path, sizeof(rel_path), "%s/relations", TEST_DIR);

    /* Create relations store */
    relations_store_t* rel = NULL;
    ASSERT_OK(relations_create(&rel, rel_path, 100));

    /* Create some nodes */
    for (int i = 0; i < 10; i++) {
        node_id_t id;
        ASSERT_OK(relations_alloc_node(rel, &id));
    }

    ASSERT_OK(relations_sync(rel));
    relations_close(rel);

    /* Verify files exist */
    char path[512];

    snprintf(path, sizeof(path), "%s/parent.bin", rel_path);
    ASSERT_MSG(file_exists(path), "parent.bin should exist");

    snprintf(path, sizeof(path), "%s/first_child.bin", rel_path);
    ASSERT_MSG(file_exists(path), "first_child.bin should exist");

    snprintf(path, sizeof(path), "%s/next_sibling.bin", rel_path);
    ASSERT_MSG(file_exists(path), "next_sibling.bin should exist");

    snprintf(path, sizeof(path), "%s/level.bin", rel_path);
    ASSERT_MSG(file_exists(path), "level.bin should exist");

    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify WAL file exists and has correct format
 */
TEST(wal_file_format) {
    setup_dirs();

    char wal_path[256];
    snprintf(wal_path, sizeof(wal_path), "%s/wal/operations.log", TEST_DIR);

    /* Create WAL and write entries */
    wal_t* wal = NULL;
    ASSERT_OK(wal_create(&wal, wal_path, 1024 * 1024));

    wal_node_data_t data = { .node_id = 42, .level = LEVEL_MESSAGE };
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &data, sizeof(data)));

    ASSERT_OK(wal_sync(wal));
    wal_close(wal);

    /* Verify WAL file exists */
    ASSERT_MSG(file_exists(wal_path), "WAL file should exist");
    ASSERT_GT(file_size(wal_path), 0);

    /* Read WAL file directly and verify magic number */
    int fd = open(wal_path, O_RDONLY);
    ASSERT_GE(fd, 0);

    uint32_t magic;
    ssize_t n = read(fd, &magic, sizeof(magic));
    close(fd);

    ASSERT_EQ(n, sizeof(magic));
    ASSERT_EQ(magic, 0x57414C30);  /* "WAL0" */

    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify mmap'd files are readable without service
 */
TEST(mmap_files_readable_offline) {
    setup_dirs();

    char emb_path[256];
    snprintf(emb_path, sizeof(emb_path), "%s/embeddings", TEST_DIR);

    /* Create and populate store */
    {
        embeddings_store_t* emb = NULL;
        ASSERT_OK(embeddings_create(&emb, emb_path, 100));

        uint32_t idx;
        ASSERT_OK(embeddings_alloc(emb, LEVEL_STATEMENT, &idx));

        float values[EMBEDDING_DIM];
        for (int i = 0; i < EMBEDDING_DIM; i++) {
            values[i] = (float)i * 0.123f;
        }
        ASSERT_OK(embeddings_set(emb, LEVEL_STATEMENT, idx, values));

        ASSERT_OK(embeddings_sync(emb));
        embeddings_close(emb);
    }

    /* Read file directly via mmap (without using our API) */
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/level_0.bin", emb_path);

    int fd = open(file_path, O_RDONLY);
    ASSERT_GE(fd, 0);

    struct stat st;
    ASSERT_EQ(fstat(fd, &st), 0);

    void* mapped = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    ASSERT_NE(mapped, MAP_FAILED);

    /* Verify header magic */
    uint32_t* header = (uint32_t*)mapped;
    ASSERT_EQ(header[0], 0x454D4230);  /* "EMB0" magic */

    /* Verify embedding dimension in header */
    ASSERT_EQ(header[2], EMBEDDING_DIM);  /* dim field */

    /* Verify embedding count */
    ASSERT_EQ(header[3], 1);  /* count field */

    /* Read embedding data (after 32-byte header) */
    float* emb_data = (float*)((char*)mapped + 32);  /* Header is 32 bytes */
    ASSERT_FLOAT_EQ(emb_data[0], 0.0f, 0.001f);  /* First value */
    ASSERT_FLOAT_EQ(emb_data[1], 0.123f, 0.001f);  /* Second value */

    munmap(mapped, st.st_size);
    close(fd);

    cleanup_dir(TEST_DIR);
}

/*
 * TEST: Verify file layout matches spec
 * Expected structure:
 *   $DATA_DIR/
 *   ├── embeddings/
 *   │   ├── level_0.bin
 *   │   ├── level_1.bin
 *   │   ├── level_2.bin
 *   │   └── level_3.bin
 *   ├── relations/
 *   │   ├── parent.bin
 *   │   ├── first_child.bin
 *   │   ├── next_sibling.bin
 *   │   └── level.bin
 *   └── wal/
 *       └── operations.log
 */
TEST(complete_file_layout) {
    setup_dirs();

    /* Create all stores */
    embeddings_store_t* emb = NULL;
    relations_store_t* rel = NULL;
    wal_t* wal = NULL;

    char emb_path[256], rel_path[256], wal_path[256];
    snprintf(emb_path, sizeof(emb_path), "%s/embeddings", TEST_DIR);
    snprintf(rel_path, sizeof(rel_path), "%s/relations", TEST_DIR);
    snprintf(wal_path, sizeof(wal_path), "%s/wal/operations.log", TEST_DIR);

    ASSERT_OK(embeddings_create(&emb, emb_path, 100));
    ASSERT_OK(relations_create(&rel, rel_path, 100));
    ASSERT_OK(wal_create(&wal, wal_path, 1024 * 1024));

    /* Populate with data */
    for (int level = 0; level < LEVEL_COUNT; level++) {
        uint32_t idx;
        ASSERT_OK(embeddings_alloc(emb, (hierarchy_level_t)level, &idx));
    }

    node_id_t id;
    ASSERT_OK(relations_alloc_node(rel, &id));

    wal_node_data_t data = { .node_id = 1 };
    ASSERT_OK(wal_append(wal, WAL_OP_NODE_INSERT, &data, sizeof(data)));

    /* Sync and close */
    ASSERT_OK(embeddings_sync(emb));
    ASSERT_OK(relations_sync(rel));
    ASSERT_OK(wal_sync(wal));

    embeddings_close(emb);
    relations_close(rel);
    wal_close(wal);

    /* Verify complete file structure */
    char path[512];

    /* Embeddings directory */
    for (int i = 0; i < LEVEL_COUNT; i++) {
        snprintf(path, sizeof(path), "%s/level_%d.bin", emb_path, i);
        ASSERT_MSG(file_exists(path), "embedding level file should exist");
    }

    /* Relations directory */
    const char* rel_files[] = {"parent.bin", "first_child.bin", "next_sibling.bin", "level.bin"};
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "%s/%s", rel_path, rel_files[i]);
        ASSERT_MSG(file_exists(path), "relation file should exist");
    }

    /* WAL directory */
    ASSERT_MSG(file_exists(wal_path), "WAL file should exist");

    cleanup_dir(TEST_DIR);
}

TEST_MAIN()
