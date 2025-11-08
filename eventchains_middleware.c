/**
 * ==============================================================================
 * EventChains Build System - Middleware Implementation
 * ==============================================================================
 */

#include "eventchains_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ==============================================================================
 * Timing Middleware
 * ==============================================================================
 */

typedef struct TimingMiddlewareData {
    bool verbose;
} TimingMiddlewareData;

static void timing_middleware_execute(
    EventResult *result_ptr,
    ChainableEvent *event,
    EventContext *context,
    void (*next)(EventResult *, ChainableEvent *, EventContext *, void *),
    void *next_data,
    void *user_data
) {
    TimingMiddlewareData *data = (TimingMiddlewareData *)user_data;
    const char *event_name = chainable_event_get_name(event);

    if (data->verbose) {
        printf("  [TIMING] Starting: %s\n", event_name);
    }

    clock_t start = clock();

    /* Call next layer */
    next(result_ptr, event, context, next_data);

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    if (data->verbose) {
        printf("  [TIMING] Completed: %s (%.3f seconds)\n", event_name, elapsed);
    }

    /* Store timing in event data if it's a compile event */
    void *event_data = chainable_event_get_user_data(event);
    if (event_data) {
        /* Try to detect if this is CompileEventData */
        CompileEventData *compile_data = (CompileEventData *)event_data;
        /* This is a simple heuristic - in production, you'd want better type safety */
        if (compile_data->source != NULL) {
            compile_data->compile_time = elapsed;
        }
    }
}

EventMiddleware *create_timing_middleware(bool verbose) {
    TimingMiddlewareData *data = malloc(sizeof(TimingMiddlewareData));
    if (!data) return NULL;

    data->verbose = verbose;

    EventMiddleware *middleware = event_middleware_create(
        timing_middleware_execute,
        data,
        "TimingMiddleware"
    );

    if (!middleware) {
        free(data);
        return NULL;
    }

    return middleware;
}

/* ==============================================================================
 * Cache Middleware
 * ==============================================================================
 */

static void cache_middleware_execute(
    EventResult *result_ptr,
    ChainableEvent *event,
    EventContext *context,
    void (*next)(EventResult *, ChainableEvent *, EventContext *, void *),
    void *next_data,
    void *user_data
) {
    BuildCache *cache = (BuildCache *)user_data;

    /* Get event data */
    void *event_data = chainable_event_get_user_data(event);
    if (!event_data) {
        /* Not a compile event, just pass through */
        next(result_ptr, event, context, next_data);
        return;
    }

    /* Check if this is a compile event */
    CompileEventData *compile_data = (CompileEventData *)event_data;
    if (!compile_data->source) {
        /* Not a compile event, pass through */
        next(result_ptr, event, context, next_data);
        return;
    }

    /* Skip headers */
    if (compile_data->source->is_header) {
        event_result_success(result_ptr);
        compile_data->cache_hit = true;
        return;
    }

    /* Check cache using persistent metadata */
    bool skip_compilation = false;
    if (cache && !build_cache_needs_recompilation(cache, compile_data->source,
                                                   compile_data->object_path)) {
        /* Cache says source is unchanged, but we MUST verify .o file exists
         * Otherwise linking will fail! */
        bool object_exists = file_exists_cache(compile_data->object_path);

        if (object_exists) {
            /* Cache HIT and .o exists - can truly skip */
            skip_compilation = true;
            compile_data->cache_hit = true;
            compile_data->compile_time = 0.0;

            /* Still need to register the object file in context */
            char key[512];
            snprintf(key, sizeof(key), "object:%s", compile_data->source->path);
            event_context_set(context, key, strdup(compile_data->object_path));

            /* Set success result but DON'T return - let other middleware see it */
            event_result_success(result_ptr);
        } else {
            /* Cache says unchanged, but .o is missing - must recompile
             * This happens when build directory is deleted but sources unchanged.
             * The persistent cache survived (good!), but we need to regenerate .o files.
             */
            skip_compilation = false;
            compile_data->cache_hit = false;
        }
    }

    if (!skip_compilation) {
        /* Cache MISS or .o missing - proceed with compilation */
        compile_data->cache_hit = false;
        next(result_ptr, event, context, next_data);

        /* Update cache after successful compilation */
        if (cache && result_ptr->success) {
            /* Get dependency graph from context */
            DependencyGraph *graph = NULL;
            event_context_get(context, "dependency_graph", (void **)&graph);

            /* Update cache metadata */
            build_cache_update(
                cache,
                compile_data->source->path,
                compile_data->object_path,
                graph
            );
        }
    }
}

EventMiddleware *create_cache_middleware(void) {
    /* This version uses simple mtime-based caching (no user_data) */
    EventMiddleware *middleware = event_middleware_create(
        cache_middleware_execute,
        NULL,
        "CacheMiddleware"
    );

    return middleware;
}

EventMiddleware *create_persistent_cache_middleware(BuildCache *cache) {
    /* This version uses persistent content-hash caching */
    EventMiddleware *middleware = event_middleware_create(
        cache_middleware_execute,
        cache,
        "PersistentCacheMiddleware"
    );

    return middleware;
}

/* ==============================================================================
 * Logging Middleware
 * ==============================================================================
 */

typedef struct LoggingMiddlewareData {
    bool quiet;  /* Only log errors */
} LoggingMiddlewareData;

static void logging_middleware_execute(
    EventResult *result_ptr,
    ChainableEvent *event,
    EventContext *context,
    void (*next)(EventResult *, ChainableEvent *, EventContext *, void *),
    void *next_data,
    void *user_data
) {
    LoggingMiddlewareData *data = (LoggingMiddlewareData *)user_data;
    const char *event_name = chainable_event_get_name(event);

    /* Get event data to determine type */
    void *event_data = chainable_event_get_user_data(event);
    CompileEventData *compile_data = NULL;
    bool is_compile_event = false;

    if (event_data) {
        compile_data = (CompileEventData *)event_data;
        if (compile_data->source != NULL) {
            is_compile_event = true;
        }
    }

    /* Log start (if not quiet) */
    if (!data->quiet && is_compile_event) {
        printf("  [COMPILE] %s\n", compile_data->source->path);
    }

    /* Execute next layer */
    next(result_ptr, event, context, next_data);

    /* Log result */
    if (result_ptr->success) {
        if (is_compile_event && compile_data->cache_hit) {
            if (!data->quiet) {
                printf("  [CACHED]  %s\n", compile_data->source->path);
            }
        } else if (!data->quiet) {
            printf("  [SUCCESS] %s\n", event_name);
        }
    } else {
        /* Always log errors, even in quiet mode */
        printf("  [FAILED]  %s\n", event_name);
        if (result_ptr->error_message[0] != '\0') {
            printf("            %s\n", result_ptr->error_message);
        }
    }
}

EventMiddleware *create_logging_middleware(bool quiet) {
    LoggingMiddlewareData *data = malloc(sizeof(LoggingMiddlewareData));
    if (!data) return NULL;

    data->quiet = quiet;

    EventMiddleware *middleware = event_middleware_create(
        logging_middleware_execute,
        data,
        "LoggingMiddleware"
    );

    if (!middleware) {
        free(data);
        return NULL;
    }

    return middleware;
}

/* ==============================================================================
 * Statistics Middleware
 * ==============================================================================
 */

static void statistics_middleware_execute(
    EventResult *result_ptr,
    ChainableEvent *event,
    EventContext *context,
    void (*next)(EventResult *, ChainableEvent *, EventContext *, void *),
    void *next_data,
    void *user_data
) {
    BuildStatistics *stats = (BuildStatistics *)user_data;
    if (!stats) {
        next(result_ptr, event, context, next_data);
        return;
    }

    /* Get event data */
    void *event_data = chainable_event_get_user_data(event);
    CompileEventData *compile_data = NULL;
    bool is_compile_event = false;

    if (event_data) {
        compile_data = (CompileEventData *)event_data;
        if (compile_data->source != NULL) {
            is_compile_event = true;
        }
    }

    /* Execute next layer */
    clock_t start = clock();
    next(result_ptr, event, context, next_data);
    clock_t end = clock();

    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    /* Update statistics */
    if (is_compile_event) {
        if (result_ptr->success) {
            if (compile_data->cache_hit) {
                stats->cached_files++;
            } else {
                stats->compiled_files++;
                stats->compilation_time += elapsed;
            }
        } else {
            stats->failed_files++;
        }
    }
}

EventMiddleware *create_statistics_middleware(BuildStatistics *stats) {
    EventMiddleware *middleware = event_middleware_create(
        statistics_middleware_execute,
        stats,
        "StatisticsMiddleware"
    );

    return middleware;
}