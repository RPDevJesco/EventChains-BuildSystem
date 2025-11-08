/**
 * ==============================================================================
 * ecbuild - EventChains Build System
 * ==============================================================================
 * 
 * Zero-configuration build tool for C/C++ projects
 * 
 * Usage: ecbuild [options] [source_directory]
 * 
 * Copyright (c) 2024 EventChains Project
 * Licensed under the MIT License
 * ==============================================================================
 */

#include "dependency_resolver.h"
#include "compile_events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==============================================================================
 * Version Information
 * ==============================================================================
 */

#define ECBUILD_VERSION_MAJOR 1
#define ECBUILD_VERSION_MINOR 0
#define ECBUILD_VERSION_PATCH 0
#define ECBUILD_VERSION_STRING "1.0.0"

/* ==============================================================================
 * Command Line Arguments
 * ==============================================================================
 */

typedef struct Arguments {
    char *source_dir;
    char *output_dir;
    char *output_binary;
    char **exclude_dirs;     /* Directories to exclude from scanning */
    size_t exclude_count;    /* Number of excluded directories */
    bool verbose;
    bool debug;
    bool no_optimize;
    bool clean;
    bool help;
    bool version;
    int parallel_jobs;
} Arguments;

static void print_usage(const char *program_name) {
    printf("ecbuild - EventChains Build System v%s\n\n", ECBUILD_VERSION_STRING);
    printf("Usage: %s [options] [source_directory]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --verbose           Verbose output (show all commands)\n");
    printf("  -V, --version           Show version information\n");
    printf("  -d, --debug             Debug build (-g)\n");
    printf("  -O0, --no-optimize      Disable optimization\n");
    printf("  -o, --output NAME       Output binary name (default: program)\n");
    printf("  -b, --build-dir DIR     Build directory (default: build)\n");
    printf("  -j, --jobs N            Number of parallel jobs (default: 1)\n");
    printf("  -c, --clean             Clean build directory before building\n");
    printf("  -e, --exclude DIRS      Exclude directories (comma-separated)\n");
    printf("                          Example: -e tests,examples,docs\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                      Build current directory\n", program_name);
    printf("  %s ./src                Build src directory\n", program_name);
    printf("  %s -v -o myapp ./src    Verbose build, output 'myapp'\n", program_name);
    printf("  %s -e tests,docs        Build, excluding tests and docs\n", program_name);
    printf("\n");
    printf("Default Exclusions:\n");
    printf("  The following directories are always excluded:\n");
    printf("  - build, builds (build outputs)\n");
    printf("  - .git, .svn (version control)\n");
    printf("  - .eventchains (cache metadata)\n");
    printf("  - node_modules, vendor (dependencies)\n");
    printf("\n");
    printf("Zero Configuration:\n");
    printf("  ecbuild automatically:\n");
    printf("  - Finds all .c/.cpp/.h files\n");
    printf("  - Determines dependencies from #include directives\n");
    printf("  - Calculates correct build order\n");
    printf("  - Detects main() entry point\n");
    printf("  - Compiles and links everything\n");
    printf("\n");
    printf("No Makefile, no CMakeLists.txt, no configuration needed!\n");
}

static void print_version(void) {
    printf("ecbuild v%s\n", ECBUILD_VERSION_STRING);
    printf("EventChains Build System\n");
    printf("Copyright (c) 2024 EventChains Project\n");
    printf("Licensed under the MIT License\n");
}

static bool parse_arguments(int argc, char **argv, Arguments *args) {
    /* Initialize defaults */
    memset(args, 0, sizeof(Arguments));
    args->source_dir = strdup(".");
    args->output_dir = strdup("build");
    args->output_binary = strdup("program");
    args->parallel_jobs = 1;
    args->exclude_dirs = NULL;
    args->exclude_count = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            args->help = true;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            args->version = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            args->verbose = true;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            args->debug = true;
        } else if (strcmp(argv[i], "-O0") == 0 || strcmp(argv[i], "--no-optimize") == 0) {
            args->no_optimize = true;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clean") == 0) {
            args->clean = true;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                free(args->output_binary);
                args->output_binary = strdup(argv[++i]);
            } else {
                fprintf(stderr, "Error: -o requires an argument\n");
                return false;
            }
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--build-dir") == 0) {
            if (i + 1 < argc) {
                free(args->output_dir);
                args->output_dir = strdup(argv[++i]);
            } else {
                fprintf(stderr, "Error: -b requires an argument\n");
                return false;
            }
        } else if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--jobs") == 0) {
            if (i + 1 < argc) {
                args->parallel_jobs = atoi(argv[++i]);
                if (args->parallel_jobs < 1) args->parallel_jobs = 1;
            } else {
                fprintf(stderr, "Error: -j requires an argument\n");
                return false;
            }
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--exclude") == 0) {
            if (i + 1 < argc) {
                /* Parse comma-separated list of directories */
                char *exclude_list = strdup(argv[++i]);
                char *token = strtok(exclude_list, ",");

                /* Count tokens first */
                char *temp_list = strdup(argv[i]);
                char *temp_token = strtok(temp_list, ",");
                size_t count = 0;
                while (temp_token) {
                    count++;
                    temp_token = strtok(NULL, ",");
                }
                free(temp_list);

                /* Allocate array */
                args->exclude_dirs = malloc(sizeof(char*) * count);
                args->exclude_count = 0;

                /* Parse again and store */
                free(exclude_list);
                exclude_list = strdup(argv[i]);
                token = strtok(exclude_list, ",");
                while (token) {
                    /* Trim whitespace */
                    while (*token == ' ' || *token == '\t') token++;
                    char *end = token + strlen(token) - 1;
                    while (end > token && (*end == ' ' || *end == '\t')) {
                        *end = '\0';
                        end--;
                    }

                    args->exclude_dirs[args->exclude_count++] = strdup(token);
                    token = strtok(NULL, ",");
                }
                free(exclude_list);
            } else {
                fprintf(stderr, "Error: -e requires an argument\n");
                return false;
            }
        } else if (argv[i][0] != '-') {
            /* Source directory */
            free(args->source_dir);
            args->source_dir = strdup(argv[i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
    }

    return true;
}

static void cleanup_arguments(Arguments *args) {
    if (!args) return;
    free(args->source_dir);
    free(args->output_dir);
    free(args->output_binary);

    /* Free exclude directories */
    if (args->exclude_dirs) {
        for (size_t i = 0; i < args->exclude_count; i++) {
            free(args->exclude_dirs[i]);
        }
        free(args->exclude_dirs);
    }
}

/* ==============================================================================
 * Main Entry Point
 * ==============================================================================
 */

int main(int argc, char **argv) {
    Arguments args;

    /* Parse command line arguments */
    if (!parse_arguments(argc, argv, &args)) {
        print_usage(argv[0]);
        return 1;
    }

    /* Handle --help */
    if (args.help) {
        print_usage(argv[0]);
        cleanup_arguments(&args);
        return 0;
    }

    /* Handle --version */
    if (args.version) {
        print_version();
        cleanup_arguments(&args);
        return 0;
    }

    printf("\n");
    printf("|----------------------------------------------------------------|\n");
    printf("|               ecbuild - EventChains Build System               |\n");
    printf("|----------------------------------------------------------------|\n\n");

    /* Create dependency graph */
    DependencyGraph *graph = dependency_graph_create();
    if (!graph) {
        fprintf(stderr, "Failed to create dependency graph\n");
        cleanup_arguments(&args);
        return 1;
    }

    /* Add source directory to include paths */
    dependency_graph_add_include_path(graph, args.source_dir);
    dependency_graph_add_include_path(graph, ".");

    /* Scan source directory */
    printf("Scanning: %s\n", args.source_dir);

    if (args.exclude_count > 0) {
        printf("Excluding: ");
        for (size_t i = 0; i < args.exclude_count; i++) {
            printf("%s%s", args.exclude_dirs[i],
                   i < args.exclude_count - 1 ? ", " : "");
        }
        printf("\n");
    }
    printf("\n");

    DependencyErrorCode err = dependency_graph_scan_directory_with_exclusions(
        graph, args.source_dir, true,
        (const char**)args.exclude_dirs, args.exclude_count
    );
    if (err != DEP_SUCCESS) {
        fprintf(stderr, "Failed to scan directory: %s\n", dependency_error_string(err));
        dependency_graph_destroy(graph);
        cleanup_arguments(&args);
        return 1;
    }
    
    if (graph->file_count == 0) {
        fprintf(stderr, "No source files found in %s\n", args.source_dir);
        dependency_graph_destroy(graph);
        cleanup_arguments(&args);
        return 1;
    }
    
    printf("Found %zu source files\n\n", graph->file_count);
    
    /* Check for circular dependencies */
    char cycle_path[1024];
    if (dependency_graph_has_cycle(graph, cycle_path, sizeof(cycle_path))) {
        fprintf(stderr, "Circular dependency detected: %s\n", cycle_path);
        dependency_graph_destroy(graph);
        cleanup_arguments(&args);
        return 1;
    }
    
    /* Create build configuration */
    BuildConfig *config = build_config_create();
    if (!config) {
        fprintf(stderr, "Failed to create build configuration\n");
        dependency_graph_destroy(graph);
        cleanup_arguments(&args);
        return 1;
    }
    
    /* Configure build */
    build_config_set_output_dir(config, args.output_dir);
    build_config_set_output_binary(config, args.output_binary);
    config->verbose = args.verbose;
    config->debug = args.debug;
    config->optimize = !args.no_optimize;
    config->parallel_jobs = args.parallel_jobs;
    
    /* Add source directory as include path */
    build_config_add_include_path(config, args.source_dir);
    
    /* Add debug flag if requested */
    if (args.debug) {
        build_config_add_cflag(config, "-g");
    }
    
    /* Clean if requested */
    if (args.clean) {
        /* Construct absolute build path */
        char absolute_build_dir[4096];
        if (args.output_dir[0] == '/' || (args.output_dir[0] && args.output_dir[1] == ':')) {
            /* Already absolute */
            snprintf(absolute_build_dir, sizeof(absolute_build_dir), "%s", args.output_dir);
        } else {
            /* Make relative to source dir */
            snprintf(absolute_build_dir, sizeof(absolute_build_dir), "%s/%s",
                    args.source_dir, args.output_dir);
        }

        printf("Cleaning build directory: %s\n\n", absolute_build_dir);
        char clean_cmd[512];
#ifdef _WIN32
        snprintf(clean_cmd, sizeof(clean_cmd), "rmdir /s /q \"%s\" 2>nul", absolute_build_dir);
#else
        snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf \"%s\"", absolute_build_dir);
#endif
        system(clean_cmd);
    }

    /* Build the project */
    int result = build_project(graph, config, args.source_dir);

    /* Cleanup */
    build_config_destroy(config);
    dependency_graph_destroy(graph);
    cleanup_arguments(&args);

    return result;
}