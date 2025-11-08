/**
 * ==============================================================================
 * EventChains Build System - Dependency Resolution
 * ==============================================================================
 * 
 * Provides dependency analysis and topological sorting for C/C++ source files.
 * Integrates with EventChains to provide build order determination.
 * 
 * Copyright (c) 2024 EventChains Project
 * Licensed under the MIT License
 * ==============================================================================
 */

#ifndef DEPENDENCY_RESOLVER_H
#define DEPENDENCY_RESOLVER_H

#include <stdbool.h>
#include <stddef.h>

/* ==============================================================================
 * Configuration Constants
 * ==============================================================================
 */

#define MAX_SOURCE_FILES 1024
#define MAX_INCLUDES_PER_FILE 256
#define MAX_PATH_LENGTH 4096
#define MAX_INCLUDE_PATHS 64

/* ==============================================================================
 * Error Codes
 * ==============================================================================
 */

typedef enum {
    DEP_SUCCESS = 0,
    DEP_ERROR_NULL_POINTER,
    DEP_ERROR_FILE_NOT_FOUND,
    DEP_ERROR_PARSE_FAILED,
    DEP_ERROR_CIRCULAR_DEPENDENCY,
    DEP_ERROR_TOO_MANY_FILES,
    DEP_ERROR_TOO_MANY_INCLUDES,
    DEP_ERROR_OUT_OF_MEMORY,
    DEP_ERROR_INVALID_PATH,
    DEP_ERROR_TOPOLOGICAL_SORT_FAILED
} DependencyErrorCode;

/* ==============================================================================
 * Core Data Structures
 * ==============================================================================
 */

/**
 * SourceFile - Represents a single C/C++ source or header file
 */
typedef struct SourceFile {
    char path[MAX_PATH_LENGTH];              /* Full path to the file */
    char *includes[MAX_INCLUDES_PER_FILE];   /* Included files */
    size_t include_count;                     /* Number of includes */
    bool is_header;                           /* true if .h/.hpp file */
    bool visited;                             /* For graph traversal */
    bool in_stack;                            /* For cycle detection */
    int sort_order;                           /* Topological sort position */
} SourceFile;

/**
 * DependencyGraph - Dependency graph for all source files
 */
typedef struct DependencyGraph {
    SourceFile *files[MAX_SOURCE_FILES];     /* Array of source files */
    size_t file_count;                        /* Number of files */
    char *include_paths[MAX_INCLUDE_PATHS];  /* Search paths for headers */
    size_t include_path_count;                /* Number of include paths */
} DependencyGraph;

/**
 * BuildOrder - Topologically sorted build order
 */
typedef struct BuildOrder {
    SourceFile *ordered_files[MAX_SOURCE_FILES];  /* Files in build order */
    size_t file_count;                             /* Number of files */
} BuildOrder;

/* ==============================================================================
 * Public API
 * ==============================================================================
 */

/**
 * Create a new dependency graph
 * @return Pointer to new DependencyGraph, or NULL on error
 */
DependencyGraph *dependency_graph_create(void);

/**
 * Destroy a dependency graph and free all resources
 * @param graph  Pointer to DependencyGraph
 */
void dependency_graph_destroy(DependencyGraph *graph);

/**
 * Add an include search path to the graph
 * @param graph  Pointer to DependencyGraph
 * @param path   Include path to add
 * @return       DEP_SUCCESS or error code
 */
DependencyErrorCode dependency_graph_add_include_path(
    DependencyGraph *graph,
    const char *path
);

/**
 * Add a source file to the graph and parse its dependencies
 * @param graph      Pointer to DependencyGraph
 * @param file_path  Path to source file
 * @return           DEP_SUCCESS or error code
 */
DependencyErrorCode dependency_graph_add_file(
    DependencyGraph *graph,
    const char *file_path
);

/**
 * Scan a directory and add all C/C++ source files
 * @param graph      Pointer to DependencyGraph
 * @param directory  Directory to scan
 * @param recursive  Whether to scan subdirectories
 * @return           DEP_SUCCESS or error code
 */
DependencyErrorCode dependency_graph_scan_directory(
    DependencyGraph *graph,
    const char *directory,
    bool recursive
);

/**
 * Scan a directory and add all C/C++ source files with exclusions
 * @param graph           Pointer to DependencyGraph
 * @param directory       Directory to scan
 * @param recursive       Whether to scan subdirectories
 * @param exclude_dirs    Array of directory names to exclude
 * @param exclude_count   Number of directories to exclude
 * @return                DEP_SUCCESS or error code
 */
DependencyErrorCode dependency_graph_scan_directory_with_exclusions(
    DependencyGraph *graph,
    const char *directory,
    bool recursive,
    const char **exclude_dirs,
    size_t exclude_count
);

/**
 * Find a source file by path
 * @param graph  Pointer to DependencyGraph
 * @param path   File path to find
 * @return       Pointer to SourceFile, or NULL if not found
 */
SourceFile *dependency_graph_find_file(
    const DependencyGraph *graph,
    const char *path
);

/**
 * Perform topological sort to determine build order
 * @param graph  Pointer to DependencyGraph
 * @param order  Pointer to BuildOrder to populate
 * @return       DEP_SUCCESS or error code
 */
DependencyErrorCode dependency_graph_topological_sort(
    DependencyGraph *graph,
    BuildOrder *order
);

/**
 * Detect circular dependencies in the graph
 * @param graph       Pointer to DependencyGraph
 * @param cycle_path  Buffer to store cycle path (can be NULL)
 * @param path_size   Size of cycle_path buffer
 * @return            true if cycle detected, false otherwise
 */
bool dependency_graph_has_cycle(
    DependencyGraph *graph,
    char *cycle_path,
    size_t path_size
);

/**
 * Get all dependencies (transitive) for a source file
 * @param graph       Pointer to DependencyGraph
 * @param file        Source file to analyze
 * @param deps        Array to store dependencies
 * @param max_deps    Maximum number of dependencies
 * @param dep_count   Pointer to store actual dependency count
 * @return            DEP_SUCCESS or error code
 */
DependencyErrorCode dependency_graph_get_all_dependencies(
    DependencyGraph *graph,
    SourceFile *file,
    SourceFile **deps,
    size_t max_deps,
    size_t *dep_count
);

/**
 * Find the main entry point in the graph
 * @param graph  Pointer to DependencyGraph
 * @return       Pointer to SourceFile containing main(), or NULL
 */
SourceFile *dependency_graph_find_main(const DependencyGraph *graph);

/**
 * Identify library files (files with no main function)
 * @param graph       Pointer to DependencyGraph
 * @param lib_files   Array to store library files
 * @param max_files   Maximum number of files
 * @param file_count  Pointer to store actual file count
 * @return            DEP_SUCCESS or error code
 */
DependencyErrorCode dependency_graph_find_libraries(
    DependencyGraph *graph,
    SourceFile **lib_files,
    size_t max_files,
    size_t *file_count
);

/**
 * Print the dependency graph (for debugging)
 * @param graph  Pointer to DependencyGraph
 */
void dependency_graph_print(const DependencyGraph *graph);

/**
 * Print the build order (for debugging)
 * @param order  Pointer to BuildOrder
 */
void build_order_print(const BuildOrder *order);

/**
 * Get error string for a dependency error code
 * @param code  Error code
 * @return      Error description string
 */
const char *dependency_error_string(DependencyErrorCode code);

#endif /* DEPENDENCY_RESOLVER_H */