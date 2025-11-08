/**
 * ==============================================================================
 * EventChains Build System - Compilation Events
 * ==============================================================================
 * 
 * Provides compilation event structures that integrate with EventChains
 * to actually build C/C++ projects.
 * 
 * Copyright (c) 2024 EventChains Project
 * Licensed under the MIT License
 * ==============================================================================
 */

#ifndef COMPILE_EVENTS_H
#define COMPILE_EVENTS_H

#include "dependency_resolver.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * Configuration Constants
 * ==============================================================================
 */

#define MAX_COMPILER_FLAGS 64
#define MAX_INCLUDE_PATHS 64
#define MAX_LIBRARY_PATHS 64
#define MAX_LIBRARIES 64
#define MAX_COMMAND_LENGTH 8192

/* ==============================================================================
 * Compiler Types
 * ==============================================================================
 */

typedef enum {
    COMPILER_AUTO,      /* Auto-detect */
    COMPILER_GCC,       /* GCC */
    COMPILER_CLANG,     /* Clang */
    COMPILER_MSVC       /* Microsoft Visual C++ */
} CompilerType;

/* ==============================================================================
 * Build Configuration
 * ==============================================================================
 */

typedef struct BuildConfig {
    /* Compiler settings */
    CompilerType compiler;
    char *compiler_path;                        /* Path to compiler executable */
    
    /* Flags */
    char *cflags[MAX_COMPILER_FLAGS];          /* Compiler flags (-Wall, -O2, etc.) */
    size_t cflag_count;
    
    char *ldflags[MAX_COMPILER_FLAGS];         /* Linker flags */
    size_t ldflag_count;
    
    /* Paths */
    char *include_paths[MAX_INCLUDE_PATHS];    /* Include directories (-I) */
    size_t include_path_count;
    
    char *library_paths[MAX_LIBRARY_PATHS];    /* Library directories (-L) */
    size_t library_path_count;
    
    char *libraries[MAX_LIBRARIES];            /* Libraries to link (-l) */
    size_t library_count;
    
    /* Output settings */
    char *output_dir;                          /* Directory for .o files */
    char *output_binary;                       /* Final executable name */
    
    /* Build options */
    bool verbose;                              /* Print commands */
    bool debug;                                /* Debug build (-g) */
    bool optimize;                             /* Optimization (-O2) */
    int parallel_jobs;                         /* Number of parallel jobs */
} BuildConfig;

/* ==============================================================================
 * Compilation Result
 * ==============================================================================
 */

typedef struct CompileResult {
    bool success;
    char *object_file;                         /* Path to generated .o file */
    char *error_output;                        /* Compiler error output */
    int exit_code;                             /* Compiler exit code */
    double compile_time;                       /* Time taken in seconds */
} CompileResult;

/* ==============================================================================
 * Build Configuration API
 * ==============================================================================
 */

/**
 * Create a new build configuration with defaults
 * @return Pointer to BuildConfig, or NULL on error
 */
BuildConfig *build_config_create(void);

/**
 * Destroy a build configuration
 * @param config  Pointer to BuildConfig
 */
void build_config_destroy(BuildConfig *config);

/**
 * Add a compiler flag
 * @param config  Pointer to BuildConfig
 * @param flag    Flag to add (e.g., "-Wall", "-O2")
 * @return        true on success, false on error
 */
bool build_config_add_cflag(BuildConfig *config, const char *flag);

/**
 * Add a linker flag
 * @param config  Pointer to BuildConfig
 * @param flag    Flag to add (e.g., "-lm", "-lpthread")
 * @return        true on success, false on error
 */
bool build_config_add_ldflag(BuildConfig *config, const char *flag);

/**
 * Add an include path
 * @param config  Pointer to BuildConfig
 * @param path    Include directory path
 * @return        true on success, false on error
 */
bool build_config_add_include_path(BuildConfig *config, const char *path);

/**
 * Add a library
 * @param config  Pointer to BuildConfig
 * @param library Library name (without -l prefix)
 * @return        true on success, false on error
 */
bool build_config_add_library(BuildConfig *config, const char *library);

/**
 * Set output directory for object files
 * @param config  Pointer to BuildConfig
 * @param path    Output directory path
 * @return        true on success, false on error
 */
bool build_config_set_output_dir(BuildConfig *config, const char *path);

/**
 * Set output binary name
 * @param config  Pointer to BuildConfig
 * @param name    Binary name
 * @return        true on success, false on error
 */
bool build_config_set_output_binary(BuildConfig *config, const char *name);

/**
 * Auto-detect compiler
 * @param config  Pointer to BuildConfig
 * @return        true if compiler found, false otherwise
 */
bool build_config_auto_detect_compiler(BuildConfig *config);

/* ==============================================================================
 * Compilation API
 * ==============================================================================
 */

/**
 * Compile a single source file
 * @param source  Source file to compile
 * @param config  Build configuration
 * @param result  Pointer to store compilation result
 * @return        true on success, false on error
 */
bool compile_source_file(
    const SourceFile *source,
    const BuildConfig *config,
    CompileResult *result
);

/**
 * Link object files into executable
 * @param object_files  Array of object file paths
 * @param object_count  Number of object files
 * @param config        Build configuration
 * @param result        Pointer to store link result
 * @return              true on success, false on error
 */
bool link_executable(
    const char **object_files,
    size_t object_count,
    const BuildConfig *config,
    CompileResult *result
);

/**
 * Build a complete project
 * @param graph       Dependency graph
 * @param config      Build configuration
 * @param source_dir  Source directory (build directory will be created relative to this)
 * @return            0 on success, non-zero on error
 */
int build_project(
    DependencyGraph *graph,
    BuildConfig *config,
    const char *source_dir
);

/**
 * Destroy a compilation result
 * @param result  Pointer to CompileResult
 */
void compile_result_destroy(CompileResult *result);

/* ==============================================================================
 * Utility Functions
 * ==============================================================================
 */

/**
 * Get object file path for a source file
 * @param source_path  Source file path
 * @param output_dir   Output directory
 * @param dest         Destination buffer
 * @param dest_size    Size of destination buffer
 * @return             true on success, false on error
 */
bool get_object_file_path(
    const char *source_path,
    const char *output_dir,
    char *dest,
    size_t dest_size
);

/**
 * Execute a command and capture output
 * @param command      Command to execute
 * @param output       Buffer for output (can be NULL)
 * @param output_size  Size of output buffer
 * @param exit_code    Pointer to store exit code
 * @return             true on success, false on error
 */
bool execute_command(
    const char *command,
    char *output,
    size_t output_size,
    int *exit_code
);

/**
 * Check if a file needs recompilation (modification time check)
 * @param source_file  Source file path
 * @param object_file  Object file path
 * @return             true if recompilation needed, false otherwise
 */
bool needs_recompilation(
    const char *source_file,
    const char *object_file
);

#ifdef __cplusplus
}
#endif

#endif /* COMPILE_EVENTS_H */