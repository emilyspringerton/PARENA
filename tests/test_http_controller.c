/* tests/test_http_controller.c -- real end-to-end verification of stdlib/http/controller.prn +
 * examples/shithub_controller_demo.prn (LO FRAMEWORK_NORTHSTAR.md's own Rails-like "batteries
 * included" plan, Controllers pillar, S225-04). Confirms the full real pipeline: a Router built
 * with resource-routes, a real Request, dispatch finding the matched route and calling the right
 * demo action, and the real Response it returns -- not just that each piece compiles alone.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_http_controller_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    Vec router = vec_new(&arena);
    resource_routes(&router, "repos", &arena);
    assert(vec_len(&router) == 5);

    /* GET /repos -> repos#index -> demo-repos-index -> a real 200 JSON response. */
    Request idx_req = {"GET", "/repos", ""};
    Response idx_resp = dispatch(&router, &idx_req, &arena);
    assert(idx_resp.status == 200);
    assert(strcmp(idx_resp.content_type, "application/json") == 0);
    assert(strcmp(idx_resp.body, "[]") == 0);

    /* GET /repos/PARENA -> repos#show -> demo-repos-show, real route-param extraction via the
     * already-real, already-tested http/router/extract-param -- proves Controllers reach route
     * params correctly with no generic params collection on Request itself. */
    Request show_req = {"GET", "/repos/PARENA", ""};
    Response show_resp = dispatch(&router, &show_req, &arena);
    assert(show_resp.status == 200);
    assert(strcmp(show_resp.content_type, "application/json") == 0);
    assert(strcmp(show_resp.body, "{\"id\":\"PARENA\"}") == 0);

    /* POST /repos -> repos#create -> a real route this demo doesn't implement -> a real 404,
     * not a crash or a silently wrong response. */
    Request create_req = {"POST", "/repos", "{}"};
    Response create_resp = dispatch(&router, &create_req, &arena);
    assert(create_resp.status == 404);

    /* A path matching no real route at all -> the same real 404 path. */
    Request unknown_req = {"GET", "/nonexistent", ""};
    Response unknown_resp = dispatch(&router, &unknown_req, &arena);
    assert(unknown_resp.status == 404);
    assert(strcmp(unknown_resp.body, "Not Found") == 0);

    printf("test_http_controller: all assertions passed\n");
    return 0;
}
