/**
 * ==============================================================================
 * Dependency Resolver - Standalone Demo
 * ==============================================================================
 * 
 * Demonstrates the dependency resolution algorithm on a real C project
 * 
 * Copyright (c) 2024 EventChains Project
 * Licensed under the MIT License
 * ==============================================================================
 */

#include "dependency_resolver.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *source_dir = argc > 1 ? argv[1] : "./test_project";
    
    printf("|----------------------------------------------------------------|\n");
    printf("|         Dependency Resolution - Standalone Demo                |\n");
    printf("|----------------------------------------------------------------|\n\n");
    
    printf("Analyzing project: %s\n\n", source_dir);
    
    /* Create dependency graph */
    DependencyGraph *graph = dependency_graph_create();
    if (!graph) {
        fprintf(stderr, "Failed to create dependency graph\n");
        return 1;
    }
    
    /* Add include paths */
    dependency_graph_add_include_path(graph, source_dir);
    dependency_graph_add_include_path(graph, ".");
    
    /* Scan directory */
    printf("Phase 1: Discovering Source Files\n");
    printf("--------------------------------\n");
    
    DependencyErrorCode err = dependency_graph_scan_directory(graph, source_dir, true);
    if (err != DEP_SUCCESS) {
        fprintf(stderr, "Failed to scan directory: %s\n",
                dependency_error_string(err));
        dependency_graph_destroy(graph);
        return 1;
    }
    
    printf("Found %zu source files\n\n", graph->file_count);
    
    /* Print dependency graph */
    printf("Phase 2: Dependency Graph\n");
    printf("--------------------------------\n");
    dependency_graph_print(graph);
    
    /* Check for circular dependencies */
    printf("Phase 3: Circular Dependency Check\n");
    printf("--------------------------------\n");
    
    char cycle_path[1024];
    if (dependency_graph_has_cycle(graph, cycle_path, sizeof(cycle_path))) {
        printf("Circular dependency detected: %s\n\n", cycle_path);
        dependency_graph_destroy(graph);
        return 1;
    }
    
    printf("No circular dependencies detected\n\n");
    
    /* Find main entry point */
    printf("Phase 4: Entry Point Detection\n");
    printf("--------------------------------\n");
    
    SourceFile *main_file = dependency_graph_find_main(graph);
    if (!main_file) {
        printf("No main() function found\n\n");
    } else {
        printf("Found main() in: %s\n\n", main_file->path);
        
        /* Get all dependencies of main */
        SourceFile *deps[MAX_SOURCE_FILES];
        size_t dep_count;
        dependency_graph_get_all_dependencies(graph, main_file, deps,
                                             MAX_SOURCE_FILES, &dep_count);
        
        printf("  Main's dependencies (%zu):\n", dep_count);
        for (size_t i = 0; i < dep_count; i++) {
            printf("    - %s\n", deps[i]->path);
        }
        printf("\n");
    }
    
    /* Find library files */
    printf("Phase 5: Library Detection\n");
    printf("--------------------------------\n");
    
    SourceFile *lib_files[MAX_SOURCE_FILES];
    size_t lib_count;
    dependency_graph_find_libraries(graph, lib_files, MAX_SOURCE_FILES, &lib_count);
    
    printf("  Found %zu library file(s):\n", lib_count);
    for (size_t i = 0; i < lib_count; i++) {
        printf("    - %s\n", lib_files[i]->path);
    }
    printf("\n");
    
    /* Topological sort */
    printf("Phase 6: Build Order Determination\n");
    printf("--------------------------------\n");
    
    BuildOrder order;
    err = dependency_graph_topological_sort(graph, &order);
    if (err != DEP_SUCCESS) {
        fprintf(stderr, "Failed to determine build order: %s\n",
                dependency_error_string(err));
        dependency_graph_destroy(graph);
        return 1;
    }
    
    printf("Build order determined (%zu files)\n\n", order.file_count);
    build_order_print(&order);
    printf("\n");
    
    /* Summary */
    printf("|----------------------------------------------------------------|\n");
    printf("|                      Analysis Complete!                        |\n");
    printf("|----------------------------------------------------------------|\n");
    printf("|  Total Files:       %3zu                                       |\n", 
           graph->file_count);
    printf("|  Header Files:      %3zu                                       |\n",
           graph->file_count - lib_count - (main_file ? 1 : 0));
    printf("|  Library Files:     %3zu                                       |\n",
           lib_count);
    printf("|  Application Files: %3d                                        |\n",
           main_file ? 1 : 0);
    printf("|----------------------------------------------------------------|\n");
    
    /* Generate a simple build script */
    printf("\nGenerating build commands:\n");
    printf("--------------------------------\n");
    
    printf("# Compilation phase\n");
    for (size_t i = 0; i < order.file_count; i++) {
        if (!order.ordered_files[i]->is_header) {
            printf("gcc -c %s -o build/%s.o\n",
                   order.ordered_files[i]->path,
                   order.ordered_files[i]->path);
        }
    }
    
    printf("\n# Linking phase\n");
    printf("gcc build/*.o -o build/program\n");
    
    /* Cleanup */
    dependency_graph_destroy(graph);
    
    return 0;
}
