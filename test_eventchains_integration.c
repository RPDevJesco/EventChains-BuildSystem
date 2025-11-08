/**
 * ==============================================================================
 * EventChains Build System - Integration Test
 * ==============================================================================
 * 
 * Demonstrates building a C project using EventChains with middleware
 * 
 * Copyright (c) 2024 EventChains Project
 * Licensed under the MIT License
 * ==============================================================================
 */

#include "eventchains_build.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *source_dir = argc > 1 ? argv[1] : "./test_project";
    
    printf("|----------------------------------------------------------------|\n");
    printf("|     EventChains Build System - Integration Test               |\n");
    printf("|----------------------------------------------------------------|\n\n");
    
    printf("Testing EventChains-based compilation\n");
    printf("Source directory: %s\n\n", source_dir);
    
    /* Initialize EventChains library */
    event_chain_initialize();
    
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
    DependencyErrorCode err = dependency_graph_scan_directory(graph, source_dir, true);
    if (err != DEP_SUCCESS) {
        fprintf(stderr, "Failed to scan directory: %s\n",
                dependency_error_string(err));
        dependency_graph_destroy(graph);
        return 1;
    }
    
    printf("Found %zu source files\n\n", graph->file_count);
    
    /* Check for circular dependencies */
    char cycle_path[1024];
    if (dependency_graph_has_cycle(graph, cycle_path, sizeof(cycle_path))) {
        fprintf(stderr, "Circular dependency detected: %s\n", cycle_path);
        dependency_graph_destroy(graph);
        return 1;
    }
    
    /* Create build configuration */
    BuildConfig *config = build_config_create();
    if (!config) {
        fprintf(stderr, "Failed to create build configuration\n");
        dependency_graph_destroy(graph);
        return 1;
    }
    
    /* Configure build */
    build_config_set_output_dir(config, "build");
    build_config_set_output_binary(config, "test_program");
    config->verbose = true;
    config->debug = false;
    config->optimize = true;
    
    /* Add source directory as include path */
    build_config_add_include_path(config, source_dir);
    
    /* Build using EventChains */
    BuildStatistics stats;
    int result = eventchains_build_project(graph, config, &stats);
    
    /* Print detailed statistics */
    printf("\n");
    printf("|----------------------------------------------------------------|\n");
    printf("|                   Build Statistics Report                      |\n");
    printf("|----------------------------------------------------------------|\n");
    
    if (result == 0) {
        printf("|  Status:         SUCCESS                                       |\n");
    } else {
        printf("|  Status:         FAILED                                        |\n");
    }
    
    print_build_statistics(&stats);
    
    printf("\n");
    printf("EventChains Integration Test Complete\n");
    printf("Result: %s\n", result == 0 ? "PASSED" : "FAILED");
    
    /* Performance insights */
    if (result == 0 && stats.total_files > 0) {
        printf("\nPerformance Insights:\n");
        printf("  Cache Hit Rate: %.1f%% (%zu/%zu files)\n",
               (100.0 * stats.cached_files) / (stats.cached_files + stats.compiled_files),
               stats.cached_files,
               stats.cached_files + stats.compiled_files);
        
        if (stats.compiled_files > 0) {
            printf("  Avg Compile Time: %.3f seconds per file\n",
                   stats.compilation_time / stats.compiled_files);
        }
        
        printf("  EventChains Overhead: ~%.1f%%\n",
               ((stats.total_time - stats.compilation_time - stats.link_time) /
                stats.total_time) * 100.0);
    }
    
    /* Cleanup */
    build_config_destroy(config);
    dependency_graph_destroy(graph);
    event_chain_cleanup();
    
    return result;
}
