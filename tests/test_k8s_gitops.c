/* tests/test_k8s_gitops.c -- stdlib/k8s/gitops.prn: single-Ingress emitter, doc joiner, and the
 * reconcile decision table. Hand-derived expectations. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "test_k8s_gitops_gen.c"

int main(void) {
    Arena a; arena_init(&a);
    char *ing = ingress_yaml("emily", "prod", "emily.okemily.com", "emily-agent", 8086, &a);
    assert(strstr(ing, "kind: Ingress"));
    assert(strstr(ing, "host: emily.okemily.com"));
    assert(strstr(ing, "name: emily-agent"));
    assert(strstr(ing, "number: 8086"));
    assert(strstr(ing, "kubernetes.io/ingress.class: gce"));
    assert(!strstr(ing, "LoadBalancer"));

    assert(strcmp(join_docs("a: 1\n", "b: 2\n", &a), "a: 1\n---\nb: 2\n") == 0);

    /* decision table: (changed, last_ok, attempt) -> action */
    assert(gitops_decide(false, true, 0) == 0);   /* synced */
    assert(gitops_decide(true, true, 0) == 1);    /* new revision */
    assert(gitops_decide(true, false, 0) == 1);   /* new revision after a failure: try the new one */
    assert(gitops_decide(false, false, 1) == 2);  /* same rev failed: backoff retry */
    assert(gitops_decide(false, false, 5) == 2);
    assert(gitops_decide(false, false, 6) == 3);  /* give up & alert */
    assert(gitops_decide(true, false, 6) == 1);   /* a NEW commit breaks out of HOLD */

    /* requeue: 30s poll, retry doubles 30,60,120,... capped at 300 */
    assert(gitops_requeue_seconds(0, 30, 0, 300) == 30);
    assert(gitops_requeue_seconds(2, 30, 1, 300) == 60);
    assert(gitops_requeue_seconds(2, 30, 3, 300) == 240);
    assert(gitops_requeue_seconds(2, 30, 4, 300) == 300);

    /* resources: requests == limits, so Autopilot bills a fixed, small amount */
    char *dep = deployment_yaml(Deployment_new("a", "ns", 2, Container_new("a", "img:1", 80, 250, 256)), &a);
    assert(strstr(dep, "requests:\n              cpu: 250m\n              memory: 256Mi"));
    assert(strstr(dep, "limits:\n              cpu: 250m\n              memory: 256Mi"));
    assert(strstr(dep, "replicas: 2"));
    assert(strcmp(namespace_yaml("emily", &a), "apiVersion: v1\nkind: Namespace\nmetadata:\n  name: emily\n") == 0);

    puts("test_k8s_gitops: OK");
    return 0;
}
