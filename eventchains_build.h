/**
 * ==============================================================================
 * EventChains Build System - EventChains Integration
 * ==============================================================================
 * 
 * Provides EventChains-based compilation events and middleware for building
 * C/C++ projects. This integrates the dependency resolver and compilation
 * logic with the EventChains pattern.
 * 
 * Copyright (c) 2024 EventChains Project
 * Licensed under the MIT License
 * ==============================================================================
 */

#ifndef EVENTCHAINS_BUILD_H
#define EVENTCHAINS_BUILD_H

#include "include/eventchains.h"
#include "dependency_resolver.h"
#include "compile_events.h"
#include "cache_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration for cache */
typedef struct BuildCache BuildCache;

/* ==============================================================================
 * Event Data Structures
 * ==============================================================================
 */

/**
 * CompileEventData - User data for compilation events
 */
typedef struct CompileEventData {
    const SourceFile *source;          /* Source file to compile */
    const BuildConfig *config;         /* Build configuration */
    char object_path[MAX_PATH_LENGTH]; /* Path to output object file */
    bool cache_hit;                    /* Whether compilation was skipped (cached) */
    double compile_time;               /* Time taken to compile (seconds) */
} CompileEventData;

/**
 * LinkEventData - User data for linking event
 */
typedef struct LinkEventData {
    const BuildConfig *config;         /* Build configuration */
    char **object_files;               /* Array of object file paths */
    size_t object_count;               /* Number of object files */
    char binary_path[MAX_PATH_LENGTH]; /* Path to output binary */
    double link_time;                  /* Time taken to link (seconds) */
} LinkEventData;

/**
 * BuildStatistics - Overall build statistics
 */
typedef struct BuildStatistics {
    size_t total_files;                /* Total source files */
    size_t compiled_files;             /* Files actually compiled */
    size_t cached_files;               /* Files skipped (cache hit) */
    size_t failed_files;               /* Files that failed to compile */
    double total_time;                 /* Total build time (seconds) */
    double compilation_time;           /* Time spent compiling */
    double link_time;                  /* Time spent linking */
} BuildStatistics;

/* ==============================================================================
 * Event Creation Functions
 * ==============================================================================
 */

/**
 * Create a compilation event for a source file
 * @param source  Source file to compile
 * @param config  Build configuration
 * @return        ChainableEvent, or NULL on error
 */
ChainableEvent *create_compile_event(
    const SourceFile *source,
    const BuildConfig *config
);

/**
 * Create a linking event to produce final binary
 * @param config  Build configuration
 * @return        ChainableEvent, or NULL on error
 */
ChainableEvent *create_link_event(const BuildConfig *config);

/**
 * Event execution function for compilation
 * @param context    Event context
 * @param user_data  CompileEventData pointer
 * @return           EventResult indicating success/failure
 */
EventResult compile_event_execute(EventContext *context, void *user_data);

/**
 * Event execution function for linking
 * @param context    Event context
 * @param user_data  LinkEventData pointer
 * @return           EventResult indicating success/failure
 */
EventResult link_event_execute(EventContext *context, void *user_data);

/* ==============================================================================
 * EventChain Construction
 * ==============================================================================
 */

/**
 * Build a complete compilation EventChain from dependency graph
 *
 * This creates:
 * 1. One CompileEvent per non-header source file (in dependency order)
 * 2. One LinkEvent at the end to produce final binary
 *
 * Events are added in the correct order based on the dependency graph's
 * topological sort.
 *
 * @param graph  Dependency graph with all source files
 * @param config Build configuration
 * @return       EventChain ready for execution, or NULL on error
 */
EventChain *build_compilation_chain(
    DependencyGraph *graph,
    BuildConfig *config
);

/* ==============================================================================
 * Middleware Functions
 * ==============================================================================
 */

/**
 * Timing Middleware - Measures execution time for each event
 *
 * This middleware wraps each event and:
 * - Records start time before execution
 * - Records end time after execution
 * - Stores timing in CompileEventData or LinkEventData
 * - Prints timing information if verbose mode is enabled
 */
EventMiddleware *create_timing_middleware(bool verbose);

/**
 * Cache Middleware - Skips compilation for unchanged files
 *
 * This middleware:
 * - Checks if source file has changed since last compilation
 * - Checks if object file exists and is up-to-date
 * - Skips compilation if cache hit
 * - Marks event as cached in CompileEventData
 */
EventMiddleware *create_cache_middleware(void);

/**
 * Persistent Cache Middleware - Advanced caching with content hashing
 *
 * This middleware provides:
 * - Content-addressable caching (hash-based)
 * - Persistent cache across build directory deletions
 * - Transitive dependency tracking
 * - Fast incremental builds
 *
 * @param cache  Pointer to BuildCache (created with build_cache_create)
 */
EventMiddleware *create_persistent_cache_middleware(BuildCache *cache);

/**
 * Logging Middleware - Provides structured build logs
 *
 * This middleware:
 * - Logs event start (e.g., "Compiling main.c")
 * - Logs event completion with status
 * - Logs errors with full details
 * - Supports quiet mode (errors only)
 */
EventMiddleware *create_logging_middleware(bool quiet);

/**
 * Statistics Middleware - Collects build statistics
 *
 * This middleware:
 * - Tracks number of compiled vs cached files
 * - Tracks total time spent in each phase
 * - Stores statistics in BuildStatistics structure
 *
 * @param stats  Pointer to BuildStatistics to populate
 */
EventMiddleware *create_statistics_middleware(BuildStatistics *stats);

/* ==============================================================================
 * High-Level Build API
 * ==============================================================================
 */

/**
 * Build a project using EventChains
 *
 * This is the main entry point that:
 * 1. Creates the compilation chain from the dependency graph
 * 2. Attaches requested middleware (timing, caching, logging, stats)
 * 3. Executes the chain
 * 4. Collects and reports results
 *
 * @param graph    Dependency graph with all source files
 * @param config   Build configuration
 * @param stats    Pointer to store build statistics (can be NULL)
 * @return         0 on success, non-zero on error
 */
int eventchains_build_project(
    DependencyGraph *graph,
    BuildConfig *config,
    BuildStatistics *stats
);

/**
 * Print build statistics in a formatted report
 * @param stats  Build statistics to print
 */
void print_build_statistics(const BuildStatistics *stats);

/* ==============================================================================
 * Context Key Constants
 *
 * These are the keys used to store data in the EventContext during builds.
 * ==============================================================================
 */

/* Stores array of object file paths (char **) */
#define CONTEXT_KEY_OBJECT_FILES "build.object_files"

/* Stores count of object files (size_t *) */
#define CONTEXT_KEY_OBJECT_COUNT "build.object_count"

/* Stores build configuration (BuildConfig *) */
#define CONTEXT_KEY_BUILD_CONFIG "build.config"

/* Stores build statistics (BuildStatistics *) */
#define CONTEXT_KEY_BUILD_STATS "build.stats"

/* Stores current file being processed (const char *) */
#define CONTEXT_KEY_CURRENT_FILE "build.current_file"

#ifdef __cplusplus
}
#endif

#endif /* EVENTCHAINS_BUILD_H */