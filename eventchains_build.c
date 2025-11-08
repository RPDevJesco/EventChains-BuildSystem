/**
 * ==============================================================================
 * EventChains Build System - EventChains Integration Implementation
 * ==============================================================================
 */

#include "eventchains_build.h"
#include "cache_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <direct.h>
    #define mkdir(path, mode) _mkdir(path)
#else
    #include <sys/stat.h>
    #include <unistd.h>
#endif

/* ==============================================================================
 * Event Execution Functions
 * ==============================================================================
 */

EventResult compile_event_execute(EventContext *context, void *user_data) {
    EventResult result;
    CompileEventData *data = (CompileEventData *)user_data;

    if (!data || !data->source || !data->config) {
        event_result_failure(&result, "Invalid compile event data",
                           EC_ERROR_NULL_POINTER, ERROR_DETAIL_FULL);
        return result;
    }

    /* Skip headers - they don't need compilation */
    if (data->source->is_header) {
        event_result_success(&result);
        data->cache_hit = true;
        data->compile_time = 0.0;
        return result;
    }

    /* Compile the source file */
    CompileResult compile_result;
    bool success = compile_source_file(data->source, data->config, &compile_result);

    /* Store results */
    data->compile_time = compile_result.compile_time;
    data->cache_hit = false;

    if (compile_result.object_file) {
        strncpy(data->object_path, compile_result.object_file, MAX_PATH_LENGTH - 1);
        data->object_path[MAX_PATH_LENGTH - 1] = '\0';

        /* Store object file path in context */
        char key[512];
        snprintf(key, sizeof(key), "object:%s", data->source->path);
        event_context_set(context, key, strdup(data->object_path));
    }

    /* Convert to EventResult */
    if (success) {
        event_result_success(&result);
    } else {
        char error_msg[1024];
        snprintf(error_msg, sizeof(error_msg), "Compilation failed: %s",
                compile_result.error_output ? compile_result.error_output : "Unknown error");
        event_result_failure(&result, error_msg, EC_ERROR_EVENT_EXECUTION_FAILED,
                           ERROR_DETAIL_FULL);
    }

    compile_result_destroy(&compile_result);
    return result;
}

EventResult link_event_execute(EventContext *context, void *user_data) {
    EventResult result;
    LinkEventData *data = (LinkEventData *)user_data;

    if (!data || !data->config) {
        event_result_failure(&result, "Invalid link event data",
                           EC_ERROR_NULL_POINTER, ERROR_DETAIL_FULL);
        return result;
    }

    /* Collect object files from context */
    /* For now, use the object files already collected in LinkEventData */
    /* In a more sophisticated implementation, we'd scan the context for all "object:*" keys */

    if (data->object_count == 0) {
        event_result_failure(&result, "No object files to link",
                           EC_ERROR_INVALID_PARAMETER, ERROR_DETAIL_FULL);
        return result;
    }

    /* Link the executable */
    CompileResult link_result;
    bool success = link_executable(
        (const char **)data->object_files,
        data->object_count,
        data->config,
        &link_result
    );

    /* Store results */
    data->link_time = link_result.compile_time;

    if (link_result.object_file) {
        strncpy(data->binary_path, link_result.object_file, MAX_PATH_LENGTH - 1);
        data->binary_path[MAX_PATH_LENGTH - 1] = '\0';
    }

    /* Convert to EventResult */
    if (success) {
        event_result_success(&result);
    } else {
        char error_msg[1024];
        snprintf(error_msg, sizeof(error_msg), "Linking failed: %s",
                link_result.error_output ? link_result.error_output : "Unknown error");
        event_result_failure(&result, error_msg, EC_ERROR_EVENT_EXECUTION_FAILED,
                           ERROR_DETAIL_FULL);
    }

    compile_result_destroy(&link_result);
    return result;
}

/* ==============================================================================
 * Event Creation Functions
 * ==============================================================================
 */

ChainableEvent *create_compile_event(
    const SourceFile *source,
    const BuildConfig *config
) {
    if (!source || !config) return NULL;

    /* Allocate event data */
    CompileEventData *data = malloc(sizeof(CompileEventData));
    if (!data) return NULL;

    data->source = source;
    data->config = config;
    data->object_path[0] = '\0';
    data->cache_hit = false;
    data->compile_time = 0.0;

    /* Get object file path */
    get_object_file_path(source->path, config->output_dir,
                        data->object_path, MAX_PATH_LENGTH);

    /* Create event with descriptive name */
    char event_name[512];
    snprintf(event_name, sizeof(event_name), "Compile:%s", source->path);

    ChainableEvent *event = chainable_event_create(
        compile_event_execute,
        data,
        event_name
    );

    if (!event) {
        free(data);
        return NULL;
    }

    return event;
}

ChainableEvent *create_link_event(const BuildConfig *config) {
    if (!config) return NULL;

    /* Allocate event data */
    LinkEventData *data = malloc(sizeof(LinkEventData));
    if (!data) return NULL;

    data->config = config;
    data->object_files = NULL;
    data->object_count = 0;
    data->binary_path[0] = '\0';
    data->link_time = 0.0;

    /* Create event */
    ChainableEvent *event = chainable_event_create(
        link_event_execute,
        data,
        "Link:FinalBinary"
    );

    if (!event) {
        free(data);
        return NULL;
    }

    return event;
}

/* ==============================================================================
 * EventChain Construction
 * ==============================================================================
 */

EventChain *build_compilation_chain(
    DependencyGraph *graph,
    BuildConfig *config
) {
    if (!graph || !config) return NULL;

    /* Get build order from dependency graph */
    BuildOrder order;
    DependencyErrorCode err = dependency_graph_topological_sort(graph, &order);
    if (err != DEP_SUCCESS) {
        fprintf(stderr, "Failed to determine build order: %s\n",
                dependency_error_string(err));
        return NULL;
    }

    /* Create event chain */
    EventChain *chain = event_chain_create(FAULT_TOLERANCE_STRICT);
    if (!chain) {
        fprintf(stderr, "Failed to create event chain\n");
        return NULL;
    }

    /* Store build config in context */
    event_context_set(chain->context, CONTEXT_KEY_BUILD_CONFIG, (void *)config);

    /* Create compilation events for each source file */
    size_t compiled_count = 0;
    for (size_t i = 0; i < order.file_count; i++) {
        SourceFile *file = order.ordered_files[i];

        /* Skip headers - they don't compile */
        if (file->is_header) continue;

        ChainableEvent *event = create_compile_event(file, config);
        if (!event) {
            fprintf(stderr, "Failed to create compile event for %s\n", file->path);
            event_chain_destroy(chain);
            return NULL;
        }

        EventChainErrorCode add_err = event_chain_add_event(chain, event);
        if (add_err != EC_SUCCESS) {
            fprintf(stderr, "Failed to add event to chain: %s\n",
                    event_chain_error_string(add_err));
            chainable_event_destroy(event);
            event_chain_destroy(chain);
            return NULL;
        }

        compiled_count++;
    }

    if (compiled_count == 0) {
        fprintf(stderr, "No source files to compile\n");
        event_chain_destroy(chain);
        return NULL;
    }

    /* Note: Linking event will be added separately after we know we need it */
    /* This allows for library-only builds */

    return chain;
}

/* ==============================================================================
 * High-Level Build API
 * ==============================================================================
 */

int eventchains_build_project(
    DependencyGraph *graph,
    BuildConfig *config,
    BuildStatistics *stats
) {
    if (!graph || !config) return 1;

    /* Initialize statistics */
    if (stats) {
        memset(stats, 0, sizeof(BuildStatistics));
        stats->total_files = graph->file_count;
    }

    printf("\n");
    printf("|----------------------------------------------------------------|\n");
    printf("|        EventChains Build System - Building with Events        |\n");
    printf("|----------------------------------------------------------------|\n\n");

    /* Create output directory */
    printf("Creating output directory: %s\n", config->output_dir);
    mkdir(config->output_dir, 0755);

    /* Create/load persistent cache in PROJECT directory (not build directory!)
     * We need to extract the project directory from the output_dir path */
    printf("\nPhase 0: Cache Initialization\n");
    printf("----------------------------------------------------------------\n");

    /* Get project directory - go up one level from output_dir if it's a subdir */
    char project_dir[MAX_PATH_LENGTH];
    strncpy(project_dir, config->output_dir, MAX_PATH_LENGTH - 1);
    project_dir[MAX_PATH_LENGTH - 1] = '\0';

    /* Remove trailing slash if present */
    size_t len = strlen(project_dir);
    if (len > 0 && (project_dir[len-1] == '/' || project_dir[len-1] == '\\')) {
        project_dir[len-1] = '\0';
        len--;
    }

    /* Find last slash to get parent directory */
    char *last_slash = strrchr(project_dir, '/');
    if (!last_slash) last_slash = strrchr(project_dir, '\\');
    if (last_slash) {
        *last_slash = '\0'; /* Truncate to get parent directory */
    }

    BuildCache *cache = build_cache_create(project_dir);
    if (!cache) {
        printf("Warning: Failed to create cache, proceeding without caching\n\n");
    } else {
        printf("Cache directory: %s\n", cache->cache_dir);
        printf("Cache loaded: %zu entries\n\n", cache->entry_count);
    }

    /* Build the compilation chain */
    printf("Phase 1: Creating Event Chain\n");
    printf("----------------------------------------------------------------\n");

    EventChain *chain = build_compilation_chain(graph, config);
    if (!chain) {
        if (cache) build_cache_destroy(cache);
        return 1;
    }

    printf("Created chain with %zu compilation events\n\n", chain->event_count);

    /* Store dependency graph in context for middleware */
    event_context_set(chain->context, "dependency_graph", graph);

    /* Add middleware */
    printf("Phase 2: Attaching Middleware\n");
    printf("----------------------------------------------------------------\n");

    /* Statistics middleware (attached FIRST so it runs LAST) */
    if (stats) {
        EventMiddleware *stats_mw = create_statistics_middleware(stats);
        if (stats_mw) {
            event_chain_use_middleware(chain, stats_mw);
            printf("Attached: Statistics Middleware\n");
        }
    }

    /* Logging middleware */
    EventMiddleware *log_mw = create_logging_middleware(false);
    if (log_mw) {
        event_chain_use_middleware(chain, log_mw);
        printf("Attached: Logging Middleware\n");
    }

    /* Persistent cache middleware (attached LATE so it runs EARLY) */
    if (cache) {
        EventMiddleware *cache_mw = create_persistent_cache_middleware(cache);
        if (cache_mw) {
            event_chain_use_middleware(chain, cache_mw);
            printf("Attached: Persistent Cache Middleware\n");
        }
    } else {
        /* Fallback to simple mtime-based cache */
        EventMiddleware *cache_mw = create_cache_middleware();
        if (cache_mw) {
            event_chain_use_middleware(chain, cache_mw);
            printf("Attached: Cache Middleware\n");
        }
    }

    /* Timing middleware (if verbose) (attached LAST so it runs FIRST) */
    if (config->verbose) {
        EventMiddleware *timing_mw = create_timing_middleware(true);
        if (timing_mw) {
            event_chain_use_middleware(chain, timing_mw);
            printf("Attached: Timing Middleware\n");
        }
    }

    printf("\n");

    /* Execute the chain */
    printf("Phase 3: Executing Build Chain\n");
    printf("----------------------------------------------------------------\n");

    clock_t start_time = clock();

    ChainResult result;
    event_chain_execute(chain, &result);

    clock_t end_time = clock();

    if (stats) {
        stats->total_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    }

    printf("\n");

    /* Check results */
    if (!result.success) {
        printf("Build FAILED\n");
        if (result.failure_count > 0) {
            printf("\nFailures:\n");
            for (size_t i = 0; i < result.failure_count; i++) {
                FailureInfo *failure = &((FailureInfo *)result.failures)[i];
                printf("  - %s: %s\n", failure->event_name, failure->error_message);
            }
        }
        chain_result_destroy(&result);
        event_chain_destroy(chain);
        if (cache) {
            build_cache_save(cache);
            build_cache_destroy(cache);
        }
        return 1;
    }

    printf("All compilation events succeeded\n\n");

    /* Save cache after successful compilation */
    if (cache) {
        printf("Saving cache...\n");
        if (build_cache_save(cache)) {
            printf("Cache saved: %zu entries\n\n", cache->entry_count);
        } else {
            printf("Warning: Failed to save cache\n\n");
        }
    }

    /* Now collect object files for linking */
    printf("Phase 4: Linking\n");
    printf("----------------------------------------------------------------\n");

    /* Collect object files from events */
    const char *object_files[1024];
    size_t object_count = 0;

    for (size_t i = 0; i < chain->event_count; i++) {
        ChainableEvent *event = chain->events[i];
        CompileEventData *data = (CompileEventData *)chainable_event_get_user_data(event);

        if (data && data->object_path[0] != '\0' && !data->source->is_header) {
            object_files[object_count++] = data->object_path;
        }
    }

    /* Link executable */
    CompileResult link_result;
    bool link_success = link_executable(
        object_files,
        object_count,
        config,
        &link_result
    );

    if (!link_success) {
        fprintf(stderr, "Linking failed\n");
        if (link_result.error_output) {
            fprintf(stderr, "%s\n", link_result.error_output);
        }
        compile_result_destroy(&link_result);
        chain_result_destroy(&result);
        event_chain_destroy(chain);
        if (cache) build_cache_destroy(cache);
        return 1;
    }

    printf("Linked: %s\n\n", link_result.object_file);

    if (stats) {
        stats->link_time = link_result.compile_time;
    }

    /* Success! */
    printf("|----------------------------------------------------------------|\n");
    printf("|                      Build Complete!                           |\n");
    printf("|----------------------------------------------------------------|\n");

    if (stats) {
        print_build_statistics(stats);
    }

    /* Print cache statistics */
    if (cache) {
        build_cache_print_stats(cache);
    }

    compile_result_destroy(&link_result);
    chain_result_destroy(&result);
    event_chain_destroy(chain);

    if (cache) {
        build_cache_destroy(cache);
    }

    return 0;
}

void print_build_statistics(const BuildStatistics *stats) {
    if (!stats) return;

    printf("|  Total Files:    %3zu                                          |\n",
           stats->total_files);
    printf("|  Compiled:       %3zu files                                    |\n",
           stats->compiled_files);
    printf("|  Cached:         %3zu files                                    |\n",
           stats->cached_files);
    printf("|  Failed:         %3zu files                                    |\n",
           stats->failed_files);
    printf("|  Compile Time:   %.3f seconds                                 |\n",
           stats->compilation_time);
    printf("|  Link Time:      %.3f seconds                                 |\n",
           stats->link_time);
    printf("|  Total Time:     %.3f seconds                                 |\n",
           stats->total_time);
    printf("|----------------------------------------------------------------|\n");
}