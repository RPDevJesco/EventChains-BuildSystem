/**
 * ==============================================================================
 * Dependency Resolver Test Suite
 * ==============================================================================
 */

#include "dependency_resolver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("\n--- TEST: %s ---\n", name); \
    bool test_passed = true;

#define ASSERT(condition, message) \
    if (!(condition)) { \
        printf("FAILED: %s\n", message); \
        test_passed = false; \
    } else { \
        printf("%s\n", message); \
    }

#define TEST_END() \
    if (test_passed) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } else { \
        tests_failed++; \
        printf("FAILED\n"); \
    }

/* ==============================================================================
 * Test Helpers
 * ==============================================================================
 */

/**
 * Create a temporary test file
 */
static bool create_test_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "w");
    if (!fp) return false;
    
    fputs(content, fp);
    fclose(fp);
    return true;
}

/**
 * Remove a test file
 */
static void remove_test_file(const char *path) {
    remove(path);
}

/* ==============================================================================
 * Test Cases
 * ==============================================================================
 */

void test_graph_creation(void) {
    TEST("Graph Creation and Destruction");
    
    DependencyGraph *graph = dependency_graph_create();
    ASSERT(graph != NULL, "Graph created successfully");
    
    dependency_graph_destroy(graph);
    ASSERT(true, "Graph destroyed without crash");
    
    TEST_END();
}

void test_include_path_management(void) {
    TEST("Include Path Management");
    
    DependencyGraph *graph = dependency_graph_create();
    ASSERT(graph != NULL, "Graph created");
    
    DependencyErrorCode err = dependency_graph_add_include_path(graph, "/usr/include");
    ASSERT(err == DEP_SUCCESS, "Added first include path");
    
    err = dependency_graph_add_include_path(graph, "/usr/local/include");
    ASSERT(err == DEP_SUCCESS, "Added second include path");
    
    dependency_graph_destroy(graph);
    
    TEST_END();
}

void test_simple_dependency(void) {
    TEST("Simple Dependency Chain");
    
    /* Create test files */
    create_test_file("/tmp/test_header.h", 
        "// Simple header\n"
        "#ifndef TEST_HEADER_H\n"
        "#define TEST_HEADER_H\n"
        "void test_function(void);\n"
        "#endif\n"
    );
    
    create_test_file("/tmp/test_source.c",
        "#include \"/tmp/test_header.h\"\n"
        "void test_function(void) {\n"
        "    // Implementation\n"
        "}\n"
    );
    
    /* Build dependency graph */
    DependencyGraph *graph = dependency_graph_create();
    ASSERT(graph != NULL, "Graph created");
    
    DependencyErrorCode err = dependency_graph_add_file(graph, "/tmp/test_source.c");
    ASSERT(err == DEP_SUCCESS, "Added source file");
    
    ASSERT(graph->file_count == 2, "Both source and header added");
    
    /* Verify dependency */
    SourceFile *source = dependency_graph_find_file(graph, "/tmp/test_source.c");
    ASSERT(source != NULL, "Found source file");
    ASSERT(source->include_count == 1, "Source has one include");
    
    /* Topological sort */
    BuildOrder order;
    err = dependency_graph_topological_sort(graph, &order);
    ASSERT(err == DEP_SUCCESS, "Topological sort succeeded");
    ASSERT(order.file_count == 2, "Build order contains both files");
    
    /* Header should come before source */
    bool header_first = order.ordered_files[0]->is_header;
    ASSERT(header_first, "Header comes before source in build order");
    
    printf("\n  Build order:\n");
    for (size_t i = 0; i < order.file_count; i++) {
        printf("    %zu. %s\n", i + 1, order.ordered_files[i]->path);
    }
    
    /* Cleanup */
    dependency_graph_destroy(graph);
    remove_test_file("/tmp/test_header.h");
    remove_test_file("/tmp/test_source.c");
    
    TEST_END();
}

void test_multiple_dependencies(void) {
    TEST("Multiple Dependencies");
    
    /* Create test files */
    create_test_file("/tmp/utils.h",
        "#ifndef UTILS_H\n"
        "#define UTILS_H\n"
        "int add(int a, int b);\n"
        "#endif\n"
    );
    
    create_test_file("/tmp/math_ops.h",
        "#ifndef MATH_OPS_H\n"
        "#define MATH_OPS_H\n"
        "#include \"/tmp/utils.h\"\n"
        "int multiply(int a, int b);\n"
        "#endif\n"
    );
    
    create_test_file("/tmp/main.c",
        "#include \"/tmp/math_ops.h\"\n"
        "#include \"/tmp/utils.h\"\n"
        "int main(void) {\n"
        "    return add(1, multiply(2, 3));\n"
        "}\n"
    );
    
    /* Build dependency graph */
    DependencyGraph *graph = dependency_graph_create();
    DependencyErrorCode err = dependency_graph_add_file(graph, "/tmp/main.c");
    ASSERT(err == DEP_SUCCESS, "Added main.c");
    ASSERT(graph->file_count == 3, "All three files added");
    
    /* Topological sort */
    BuildOrder order;
    err = dependency_graph_topological_sort(graph, &order);
    ASSERT(err == DEP_SUCCESS, "Topological sort succeeded");
    
    printf("\n  Build order:\n");
    build_order_print(&order);
    
    /* Find main */
    SourceFile *main_file = dependency_graph_find_main(graph);
    ASSERT(main_file != NULL, "Found main() function");
    
    /* Cleanup */
    dependency_graph_destroy(graph);
    remove_test_file("/tmp/utils.h");
    remove_test_file("/tmp/math_ops.h");
    remove_test_file("/tmp/main.c");
    
    TEST_END();
}

void test_circular_dependency_detection(void) {
    TEST("Circular Dependency Detection");
    
    /* Create circular dependency */
    create_test_file("/tmp/circular_a.h",
        "#ifndef CIRCULAR_A_H\n"
        "#define CIRCULAR_A_H\n"
        "#include \"/tmp/circular_b.h\"\n"
        "void func_a(void);\n"
        "#endif\n"
    );
    
    create_test_file("/tmp/circular_b.h",
        "#ifndef CIRCULAR_B_H\n"
        "#define CIRCULAR_B_H\n"
        "#include \"/tmp/circular_a.h\"\n"
        "void func_b(void);\n"
        "#endif\n"
    );
    
    /* Build dependency graph */
    DependencyGraph *graph = dependency_graph_create();
    dependency_graph_add_file(graph, "/tmp/circular_a.h");
    
    /* Check for cycle */
    char cycle_path[1024];
    bool has_cycle = dependency_graph_has_cycle(graph, cycle_path, sizeof(cycle_path));
    ASSERT(has_cycle, "Circular dependency detected");
    
    printf("  Detected cycle: %s\n", cycle_path);
    
    /* Cleanup */
    dependency_graph_destroy(graph);
    remove_test_file("/tmp/circular_a.h");
    remove_test_file("/tmp/circular_b.h");
    
    TEST_END();
}

void test_transitive_dependencies(void) {
    TEST("Transitive Dependencies");
    
    /* Create dependency chain: main.c -> b.h -> a.h */
    create_test_file("/tmp/trans_a.h",
        "#ifndef TRANS_A_H\n"
        "#define TRANS_A_H\n"
        "void func_a(void);\n"
        "#endif\n"
    );
    
    create_test_file("/tmp/trans_b.h",
        "#ifndef TRANS_B_H\n"
        "#define TRANS_B_H\n"
        "#include \"/tmp/trans_a.h\"\n"
        "void func_b(void);\n"
        "#endif\n"
    );
    
    create_test_file("/tmp/trans_main.c",
        "#include \"/tmp/trans_b.h\"\n"
        "int main(void) {\n"
        "    func_b();\n"
        "    return 0;\n"
        "}\n"
    );
    
    /* Build dependency graph */
    DependencyGraph *graph = dependency_graph_create();
    dependency_graph_add_file(graph, "/tmp/trans_main.c");
    
    SourceFile *main_file = dependency_graph_find_file(graph, "/tmp/trans_main.c");
    ASSERT(main_file != NULL, "Found main file");
    
    /* Get all dependencies */
    SourceFile *deps[MAX_SOURCE_FILES];
    size_t dep_count;
    DependencyErrorCode err = dependency_graph_get_all_dependencies(
        graph, main_file, deps, MAX_SOURCE_FILES, &dep_count
    );
    
    ASSERT(err == DEP_SUCCESS, "Retrieved transitive dependencies");
    ASSERT(dep_count == 2, "Found both transitive dependencies");
    
    printf("  Transitive dependencies of main.c: %zu\n", dep_count);
    for (size_t i = 0; i < dep_count; i++) {
        printf("    - %s\n", deps[i]->path);
    }
    
    /* Cleanup */
    dependency_graph_destroy(graph);
    remove_test_file("/tmp/trans_a.h");
    remove_test_file("/tmp/trans_b.h");
    remove_test_file("/tmp/trans_main.c");
    
    TEST_END();
}

void test_library_detection(void) {
    TEST("Library File Detection");
    
    /* Create library and application files */
    create_test_file("/tmp/lib.c",
        "// Library file - no main\n"
        "int lib_function(int x) {\n"
        "    return x * 2;\n"
        "}\n"
    );
    
    create_test_file("/tmp/app.c",
        "// Application file - has main\n"
        "int main(void) {\n"
        "    return 0;\n"
        "}\n"
    );
    
    /* Build dependency graph */
    DependencyGraph *graph = dependency_graph_create();
    dependency_graph_add_file(graph, "/tmp/lib.c");
    dependency_graph_add_file(graph, "/tmp/app.c");
    
    /* Find main */
    SourceFile *main_file = dependency_graph_find_main(graph);
    ASSERT(main_file != NULL, "Found main() function");
    ASSERT(strcmp(main_file->path, "/tmp/app.c") == 0, "main() in correct file");
    
    /* Find libraries */
    SourceFile *lib_files[MAX_SOURCE_FILES];
    size_t lib_count;
    DependencyErrorCode err = dependency_graph_find_libraries(
        graph, lib_files, MAX_SOURCE_FILES, &lib_count
    );
    
    ASSERT(err == DEP_SUCCESS, "Found library files");
    ASSERT(lib_count == 1, "Detected one library file");
    ASSERT(strcmp(lib_files[0]->path, "/tmp/lib.c") == 0, "Correct library file");
    
    printf("  Found %zu library file(s):\n", lib_count);
    for (size_t i = 0; i < lib_count; i++) {
        printf("    - %s\n", lib_files[i]->path);
    }
    
    /* Cleanup */
    dependency_graph_destroy(graph);
    remove_test_file("/tmp/lib.c");
    remove_test_file("/tmp/app.c");
    
    TEST_END();
}

/* ==============================================================================
 * Main Test Runner
 * ==============================================================================
 */

int main(void) {
    printf("|----------------------------------------------------------------|\n");
    printf("|         EventChains Build System - Dependency Resolver         |\n");
    printf("|                        Test Suite                              |\n");
    printf("|----------------------------------------------------------------|\n");
    
    /* Run all tests */
    test_graph_creation();
    test_include_path_management();
    test_simple_dependency();
    test_multiple_dependencies();
    test_circular_dependency_detection();
    test_transitive_dependencies();
    test_library_detection();
    
    /* Print summary */
    printf("\n");
    printf("|----------------------------------------------------------------|\n");
    printf("|                         Test Summary                           |\n");
    printf("|----------------------------------------------------------------|\n");
    printf("|  Total Tests:  %3d                                             |\n", 
           tests_passed + tests_failed);
    printf("|  Passed:       %3d                                             |\n",
           tests_passed);
    printf("|  Failed:       %3d                                             |\n",
           tests_failed);
    printf("-----------------------------------------------------------------|\n");
    
    return tests_failed == 0 ? 0 : 1;
}
