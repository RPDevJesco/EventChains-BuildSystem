/**
 * ==============================================================================
 * EventChains Build System - Compilation Events Implementation
 * ==============================================================================
 */

#include "compile_events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define popen _popen
    #define pclose _pclose
    #define mkdir(path, mode) _mkdir(path)
#else
    #include <unistd.h>
    #include <sys/wait.h>
#endif

/* ==============================================================================
 * Build Configuration Implementation
 * ==============================================================================
 */

BuildConfig *build_config_create(void) {
    BuildConfig *config = (BuildConfig *)calloc(1, sizeof(BuildConfig));
    if (!config) return NULL;
    
    config->compiler = COMPILER_AUTO;
    config->compiler_path = NULL;
    config->cflag_count = 0;
    config->ldflag_count = 0;
    config->include_path_count = 0;
    config->library_path_count = 0;
    config->library_count = 0;
    config->output_dir = strdup("build");
    config->output_binary = strdup("program");
    config->verbose = false;
    config->debug = false;
    config->optimize = true;
    config->parallel_jobs = 1;
    
    /* Add default flags */
    build_config_add_cflag(config, "-Wall");
    if (config->optimize) {
        build_config_add_cflag(config, "-O2");
    }
    
    return config;
}

void build_config_destroy(BuildConfig *config) {
    if (!config) return;
    
    free(config->compiler_path);
    free(config->output_dir);
    free(config->output_binary);
    
    for (size_t i = 0; i < config->cflag_count; i++) {
        free(config->cflags[i]);
    }
    for (size_t i = 0; i < config->ldflag_count; i++) {
        free(config->ldflags[i]);
    }
    for (size_t i = 0; i < config->include_path_count; i++) {
        free(config->include_paths[i]);
    }
    for (size_t i = 0; i < config->library_path_count; i++) {
        free(config->library_paths[i]);
    }
    for (size_t i = 0; i < config->library_count; i++) {
        free(config->libraries[i]);
    }
    
    free(config);
}

bool build_config_add_cflag(BuildConfig *config, const char *flag) {
    if (!config || !flag || config->cflag_count >= MAX_COMPILER_FLAGS) {
        return false;
    }
    
    config->cflags[config->cflag_count] = strdup(flag);
    if (!config->cflags[config->cflag_count]) {
        return false;
    }
    
    config->cflag_count++;
    return true;
}

bool build_config_add_ldflag(BuildConfig *config, const char *flag) {
    if (!config || !flag || config->ldflag_count >= MAX_COMPILER_FLAGS) {
        return false;
    }
    
    config->ldflags[config->ldflag_count] = strdup(flag);
    if (!config->ldflags[config->ldflag_count]) {
        return false;
    }
    
    config->ldflag_count++;
    return true;
}

bool build_config_add_include_path(BuildConfig *config, const char *path) {
    if (!config || !path || config->include_path_count >= MAX_INCLUDE_PATHS) {
        return false;
    }
    
    config->include_paths[config->include_path_count] = strdup(path);
    if (!config->include_paths[config->include_path_count]) {
        return false;
    }
    
    config->include_path_count++;
    return true;
}

bool build_config_add_library(BuildConfig *config, const char *library) {
    if (!config || !library || config->library_count >= MAX_LIBRARIES) {
        return false;
    }
    
    config->libraries[config->library_count] = strdup(library);
    if (!config->libraries[config->library_count]) {
        return false;
    }
    
    config->library_count++;
    return true;
}

bool build_config_set_output_dir(BuildConfig *config, const char *path) {
    if (!config || !path) return false;
    
    free(config->output_dir);
    config->output_dir = strdup(path);
    return config->output_dir != NULL;
}

bool build_config_set_output_binary(BuildConfig *config, const char *name) {
    if (!config || !name) return false;
    
    free(config->output_binary);
    config->output_binary = strdup(name);
    return config->output_binary != NULL;
}

bool build_config_auto_detect_compiler(BuildConfig *config) {
    if (!config) return false;
    
    /* Try to find a compiler */
    const char *compilers[] = {"gcc", "clang", "cl"};
    const CompilerType types[] = {COMPILER_GCC, COMPILER_CLANG, COMPILER_MSVC};
    
    for (size_t i = 0; i < 3; i++) {
        char test_cmd[256];
        
#ifdef _WIN32
        snprintf(test_cmd, sizeof(test_cmd), "where %s > nul 2>&1", compilers[i]);
#else
        snprintf(test_cmd, sizeof(test_cmd), "which %s > /dev/null 2>&1", compilers[i]);
#endif
        
        if (system(test_cmd) == 0) {
            config->compiler = types[i];
            config->compiler_path = strdup(compilers[i]);
            return true;
        }
    }
    
    return false;
}

/* ==============================================================================
 * Utility Functions
 * ==============================================================================
 */

/**
 * Create an absolute build directory path relative to the source directory
 */
static bool create_build_dir_path(
    const char *source_dir,
    const char *build_dir_name,
    char *dest,
    size_t dest_size
) {
    if (!source_dir || !build_dir_name || !dest || dest_size == 0) {
        return false;
    }
    
    /* If build_dir_name is already absolute, use it as-is */
    if (build_dir_name[0] == '/' || 
        (build_dir_name[0] && build_dir_name[1] == ':')) {  /* Windows absolute path */
        snprintf(dest, dest_size, "%s", build_dir_name);
        return true;
    }
    
    /* Make path relative to source directory */
    snprintf(dest, dest_size, "%s/%s", source_dir, build_dir_name);
    return true;
}

bool get_object_file_path(
    const char *source_path,
    const char *output_dir,
    char *dest,
    size_t dest_size
) {
    if (!source_path || !output_dir || !dest || dest_size == 0) {
        return false;
    }
    
    /* Get filename from path */
    const char *filename = strrchr(source_path, '/');
    if (!filename) {
        filename = strrchr(source_path, '\\');
    }
    filename = filename ? filename + 1 : source_path;
    
    /* Replace extension with .o */
    char obj_name[512];
    strncpy(obj_name, filename, sizeof(obj_name) - 1);
    obj_name[sizeof(obj_name) - 1] = '\0';
    
    char *ext = strrchr(obj_name, '.');
    if (ext) {
        strcpy(ext, ".o");
    } else {
        strcat(obj_name, ".o");
    }
    
    /* Build full path */
    snprintf(dest, dest_size, "%s/%s", output_dir, obj_name);
    
    return true;
}

bool execute_command(
    const char *command,
    char *output,
    size_t output_size,
    int *exit_code
) {
    if (!command) return false;
    
    if (output && output_size > 0) {
        /* Capture output */
        FILE *fp = popen(command, "r");
        if (!fp) {
            if (exit_code) *exit_code = -1;
            return false;
        }
        
        size_t total_read = 0;
        while (fgets(output + total_read, output_size - total_read, fp)) {
            total_read = strlen(output);
            if (total_read >= output_size - 1) break;
        }
        
        int status = pclose(fp);
        if (exit_code) {
#ifdef _WIN32
            *exit_code = status;
#else
            *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
        }
        
        return status == 0;
    } else {
        /* Just execute */
        int status = system(command);
        if (exit_code) {
#ifdef _WIN32
            *exit_code = status;
#else
            *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
        }
        
        return status == 0;
    }
}

bool needs_recompilation(
    const char *source_file,
    const char *object_file
) {
    if (!source_file || !object_file) return true;
    
    struct stat source_stat, object_stat;
    
    /* If object file doesn't exist, need to compile */
    if (stat(object_file, &object_stat) != 0) {
        return true;
    }
    
    /* If source file doesn't exist, error (but return true to trigger error) */
    if (stat(source_file, &source_stat) != 0) {
        return true;
    }
    
    /* Recompile if source is newer than object */
    return source_stat.st_mtime > object_stat.st_mtime;
}

/* ==============================================================================
 * Compilation Implementation
 * ==============================================================================
 */

bool compile_source_file(
    const SourceFile *source,
    const BuildConfig *config,
    CompileResult *result
) {
    if (!source || !config || !result) return false;
    
    /* Initialize result */
    memset(result, 0, sizeof(CompileResult));
    result->success = false;
    
    /* Skip headers */
    if (source->is_header) {
        result->success = true;
        return true;
    }
    
    /* Get object file path */
    char object_path[MAX_PATH_LENGTH];
    if (!get_object_file_path(source->path, config->output_dir, 
                              object_path, sizeof(object_path))) {
        return false;
    }
    
    result->object_file = strdup(object_path);
    
    /* Check if recompilation needed */
    if (!needs_recompilation(source->path, object_path)) {
        if (config->verbose) {
            printf("  [CACHED] %s\n", source->path);
        }
        result->success = true;
        return true;
    }
    
    /* Build compile command */
    char command[MAX_COMMAND_LENGTH];
    int pos = 0;
    
    /* Compiler */
    const char *compiler = config->compiler_path ? config->compiler_path : "gcc";
    pos += snprintf(command + pos, sizeof(command) - pos, "%s -c %s -o %s",
                   compiler, source->path, object_path);
    
    /* Add include paths */
    for (size_t i = 0; i < config->include_path_count; i++) {
        pos += snprintf(command + pos, sizeof(command) - pos, " -I%s",
                       config->include_paths[i]);
    }
    
    /* Add compiler flags */
    for (size_t i = 0; i < config->cflag_count; i++) {
        pos += snprintf(command + pos, sizeof(command) - pos, " %s",
                       config->cflags[i]);
    }
    
    if (config->verbose) {
        printf("  [COMPILE] %s\n", source->path);
        printf("            %s\n", command);
    }
    
    /* Execute compilation */
    clock_t start = clock();
    
    char error_output[4096] = {0};
    int exit_code = 0;
    bool success = execute_command(command, error_output, sizeof(error_output), &exit_code);
    
    clock_t end = clock();
    result->compile_time = (double)(end - start) / CLOCKS_PER_SEC;
    result->exit_code = exit_code;
    
    if (strlen(error_output) > 0) {
        result->error_output = strdup(error_output);
    }
    
    result->success = success;
    
    if (!success && config->verbose) {
        printf("  [FAILED] Compilation failed with exit code %d\n", exit_code);
        if (result->error_output) {
            printf("%s\n", result->error_output);
        }
    }
    
    return success;
}

bool link_executable(
    const char **object_files,
    size_t object_count,
    const BuildConfig *config,
    CompileResult *result
) {
    if (!object_files || object_count == 0 || !config || !result) {
        return false;
    }
    
    /* Initialize result */
    memset(result, 0, sizeof(CompileResult));
    result->success = false;
    
    /* Build link command */
    char command[MAX_COMMAND_LENGTH];
    int pos = 0;
    
    /* Compiler/linker */
    const char *compiler = config->compiler_path ? config->compiler_path : "gcc";
    
    /* Output binary path */
    char binary_path[MAX_PATH_LENGTH];
    snprintf(binary_path, sizeof(binary_path), "%s/%s",
             config->output_dir, config->output_binary);
    
#ifdef _WIN32
    strcat(binary_path, ".exe");
#endif
    
    pos += snprintf(command + pos, sizeof(command) - pos, "%s", compiler);
    
    /* Add all object files */
    for (size_t i = 0; i < object_count; i++) {
        pos += snprintf(command + pos, sizeof(command) - pos, " %s", object_files[i]);
    }
    
    /* Output binary */
    pos += snprintf(command + pos, sizeof(command) - pos, " -o %s", binary_path);
    
    /* Add library paths */
    for (size_t i = 0; i < config->library_path_count; i++) {
        pos += snprintf(command + pos, sizeof(command) - pos, " -L%s",
                       config->library_paths[i]);
    }
    
    /* Add libraries */
    for (size_t i = 0; i < config->library_count; i++) {
        pos += snprintf(command + pos, sizeof(command) - pos, " -l%s",
                       config->libraries[i]);
    }
    
    /* Add linker flags */
    for (size_t i = 0; i < config->ldflag_count; i++) {
        pos += snprintf(command + pos, sizeof(command) - pos, " %s",
                       config->ldflags[i]);
    }
    
    if (config->verbose) {
        printf("  [LINK] %s\n", binary_path);
        printf("         %s\n", command);
    }
    
    /* Execute linking */
    clock_t start = clock();
    
    char error_output[4096] = {0};
    int exit_code = 0;
    bool success = execute_command(command, error_output, sizeof(error_output), &exit_code);
    
    clock_t end = clock();
    result->compile_time = (double)(end - start) / CLOCKS_PER_SEC;
    result->exit_code = exit_code;
    
    if (strlen(error_output) > 0) {
        result->error_output = strdup(error_output);
    }
    
    result->success = success;
    result->object_file = strdup(binary_path);
    
    if (!success && config->verbose) {
        printf("  [FAILED] Linking failed with exit code %d\n", exit_code);
        if (result->error_output) {
            printf("%s\n", result->error_output);
        }
    }
    
    return success;
}

void compile_result_destroy(CompileResult *result) {
    if (!result) return;
    
    free(result->object_file);
    free(result->error_output);
    
    memset(result, 0, sizeof(CompileResult));
}

/* ==============================================================================
 * Complete Project Build
 * ==============================================================================
 */

int build_project(
    DependencyGraph *graph,
    BuildConfig *config,
    const char *source_dir
) {
    if (!graph || !config || !source_dir) return 1;
    
    printf("\n");
    printf("|----------------------------------------------------------------|\n");
    printf("|            EventChains Build System - Building Project        |\n");
    printf("|----------------------------------------------------------------|\n\n");
    
    /* Auto-detect compiler if needed */
    if (config->compiler == COMPILER_AUTO || !config->compiler_path) {
        printf("Phase 1: Compiler Detection\n");
        printf("----------------------------------------------------------------\n");
        if (!build_config_auto_detect_compiler(config)) {
            fprintf(stderr, "No compiler found (tried gcc, clang, cl)\n");
            return 1;
        }
        printf("Found compiler: %s\n\n", config->compiler_path);
    }
    
    /* Create absolute build directory path relative to source directory */
    char absolute_build_dir[MAX_PATH_LENGTH];
    if (!create_build_dir_path(source_dir, config->output_dir, 
                                absolute_build_dir, sizeof(absolute_build_dir))) {
        fprintf(stderr, "Failed to create build directory path\n");
        return 1;
    }
    
    /* Update config to use absolute path */
    free(config->output_dir);
    config->output_dir = strdup(absolute_build_dir);
    
    printf("  Build directory: %s\n", config->output_dir);
    
    /* Create output directory */
    mkdir(config->output_dir, 0755);
    
    /* Get build order */
    BuildOrder order;
    DependencyErrorCode err = dependency_graph_topological_sort(graph, &order);
    if (err != DEP_SUCCESS) {
        fprintf(stderr, "Failed to determine build order\n");
        return 1;
    }
    
    printf("\nPhase 2: Compilation\n");
    printf("----------------------------------------------------------------\n");
    
    /* Compile each source file */
    const char *object_files[1024];
    size_t object_count = 0;
    size_t compiled_count = 0;
    size_t cached_count = 0;
    size_t failed_count = 0;
    
    for (size_t i = 0; i < order.file_count; i++) {
        if (order.ordered_files[i]->is_header) continue;
        
        CompileResult result;
        if (compile_source_file(order.ordered_files[i], config, &result)) {
            if (result.object_file) {
                object_files[object_count++] = strdup(result.object_file);
                if (needs_recompilation(order.ordered_files[i]->path, result.object_file)) {
                    compiled_count++;
                } else {
                    cached_count++;
                }
            }
            compile_result_destroy(&result);
        } else {
            failed_count++;
            fprintf(stderr, "Failed to compile: %s\n", order.ordered_files[i]->path);
            compile_result_destroy(&result);
            
            /* Clean up object files */
            for (size_t j = 0; j < object_count; j++) {
                free((void *)object_files[j]);
            }
            return 1;
        }
    }
    
    printf("Compiled: %zu files\n", compiled_count);
    if (cached_count > 0) {
        printf("Cached: %zu files\n", cached_count);
    }
    printf("\n");
    
    /* Link */
    printf("Phase 3: Linking\n");
    printf("----------------------------------------------------------------\n");
    
    CompileResult link_result;
    if (!link_executable(object_files, object_count, config, &link_result)) {
        fprintf(stderr, "Linking failed\n");
        compile_result_destroy(&link_result);
        
        /* Clean up */
        for (size_t i = 0; i < object_count; i++) {
            free((void *)object_files[i]);
        }
        return 1;
    }
    
    printf("Linked: %s\n\n", link_result.object_file);
    
    /* Success! */
    printf("|----------------------------------------------------------------|\n");
    printf("|                      Build Complete!                           |\n");
    printf("|----------------------------------------------------------------|\n");
    printf("|  Compiled:  %3zu files                                         |\n", compiled_count);
    printf("|  Cached:    %3zu files                                         |\n", cached_count);
    printf("|  Output:    %-45s|\n", link_result.object_file);
    printf("|----------------------------------------------------------------|\n");
    
    compile_result_destroy(&link_result);
    
    /* Clean up */
    for (size_t i = 0; i < object_count; i++) {
        free((void *)object_files[i]);
    }
    
    return 0;
}