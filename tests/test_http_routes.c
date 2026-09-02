/* tests/test_http_routes.c -- real end-to-end verification of stdlib/http/routes.prn (LO
 * FRAMEWORK_NORTHSTAR.md's own Rails-like "batteries included" plan, founder real-time: "ok lets
 * build SHITHUB using LO... make it like rails"). Confirms the real HTTP-method + route-table
 * layer built on top of http/router.prn's own already-verified path-pattern matching.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_http_routes_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real, hand-declared routes, Rails' own explicit get/post shape. */
    Vec router = vec_new(&arena);
    add_route_(&router, "GET", "/repos/:name", "repos#show", &arena);
    add_route_(&router, "GET", "/repos", "repos#index", &arena);
    assert(vec_len(&router) == 2);

    /* A GET to the more specific pattern matches its own declared route, not the other one --
     * real "first declared, first matched" order, not path specificity. */
    int idx = match_route(&router, "GET", "/repos/PARENA", &arena);
    assert(idx == 0);
    Route r0 = *(Route *)vec_get(&router, idx);
    assert(strcmp(route_handler_name(&r0), "repos#show") == 0);

    idx = match_route(&router, "GET", "/repos", &arena);
    assert(idx == 1);
    Route r1 = *(Route *)vec_get(&router, idx);
    assert(strcmp(route_handler_name(&r1), "repos#index") == 0);

    /* A real, honest "no match" case: right path, wrong method. */
    idx = match_route(&router, "POST", "/repos/PARENA", &arena);
    assert(idx == -1);

    /* resource-routes -- Rails' own `resources :repos` one-liner, all 5 real RESTful entries. */
    Vec resource_router = vec_new(&arena);
    resource_routes(&resource_router, "repos", &arena);
    assert(vec_len(&resource_router) == 5);

    struct { const char *method; const char *path; const char *want_handler; } cases[] = {
        {"GET", "/repos", "repos#index"},
        {"POST", "/repos", "repos#create"},
        {"GET", "/repos/1", "repos#show"},
        {"PUT", "/repos/1", "repos#update"},
        {"DELETE", "/repos/1", "repos#destroy"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int j = match_route(&resource_router, (char *)cases[i].method, (char *)cases[i].path, &arena);
        assert(j >= 0);
        Route rr = *(Route *)vec_get(&resource_router, j);
        assert(strcmp(route_handler_name(&rr), cases[i].want_handler) == 0);
    }

    printf("test_http_routes: all assertions passed\n");
    return 0;
}
