/* tests/test_k8s_pod.c -- stdlib/k8s/pod.prn fragments. Hand-derived expectations. */
#include "parena_runtime.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "test_k8s_pod_gen.c"

int main(void) {
    Arena a; arena_init(&a);

    assert(strcmp(args_yaml("", "  ", &a), "") == 0);
    assert(strcmp(args_yaml("-addr,unix:///run/fatbaby/s.sock", "    ", &a),
                  "    - \"-addr\"\n    - \"unix:///run/fatbaby/s.sock\"\n") == 0);

    char *head = pod_head_yaml("fatbaby-core", "fatbaby", 1, &a);
    assert(strstr(head, "kind: Deployment"));
    assert(strstr(head, "type: Recreate"));          /* RWO PVC: no overlapping rolling update */
    assert(strstr(head, "fsGroup: 65532"));
    assert(strstr(head, "app: fatbaby-core"));
    assert(strstr(head, "replicas: 1"));
    assert(head[strlen(head) - 1] == '\n' && strstr(head, "containers:\n"));

    char *c = pod_container_yaml("signalapi", "img:1", "/app/bin/signalapi", "-addr,unix:///run/fatbaby/signalapi.sock",
                                 0, 100, 128, "fatbaby-env", "/app/var", "/run/fatbaby", &a);
    assert(strstr(c, "- name: signalapi"));
    assert(strstr(c, "- /app/bin/signalapi"));
    assert(strstr(c, "- \"unix:///run/fatbaby/signalapi.sock\""));
    assert(!strstr(c, "ports:") && !strstr(c, "readinessProbe"));   /* socket-only: no TCP probe */
    assert(strstr(c, "value: /run/fatbaby/notify"));
    assert(strstr(c, "name: fatbaby-env") && strstr(c, "optional: true"));
    assert(strstr(c, "mountPath: /app/var") && strstr(c, "mountPath: /run/fatbaby"));
    assert(strstr(c, "requests:\n              cpu: 100m\n              memory: 128Mi"));
    assert(strstr(c, "limits:\n              cpu: 100m\n              memory: 128Mi"));

    char *n = pod_container_yaml("newssite", "img:1", "/app/bin/newssite", "", 8082, 250, 256, "s", "/app/var", "/run/fatbaby", &a);
    assert(strstr(n, "containerPort: 8082") && strstr(n, "tcpSocket:\n              port: 8082"));
    assert(!strstr(n, "args:"));

    char *t = pod_tail_yaml("fatbaby-var", &a);
    assert(strstr(t, "claimName: fatbaby-var") && strstr(t, "emptyDir: {}"));
    assert(!strstr(t, "hostPath"));                 /* Autopilot forbids hostPath */

    char *svc = pod_service_yaml("fatbaby-core", "fatbaby", "8082,9091", &a);
    assert(strstr(svc, "kind: Service") && strstr(svc, "type: ClusterIP") && !strstr(svc, "LoadBalancer"));
    assert(strstr(svc, "- name: p8082\n      port: 8082\n      targetPort: 8082\n"));
    assert(strstr(svc, "- name: p9091\n      port: 9091\n      targetPort: 9091\n"));
    assert(strstr(svc, "selector:\n    app: fatbaby-core"));

    char *ing = pod_ingress_yaml("fatbaby-core", "fatbaby", "fatbaby.io@fatbaby-core@8082,api.fatbaby.io@fatbaby-core@9091,golden.okemily.com@collections-server@8087", &a);
    assert(strstr(ing, "kind: Ingress") && strstr(ing, "kubernetes.io/ingress.class: gce"));
    assert(strstr(ing, "host: fatbaby.io") && strstr(ing, "number: 8082"));
    assert(strstr(ing, "host: api.fatbaby.io") && strstr(ing, "number: 9091"));
    assert(strstr(ing, "host: golden.okemily.com") && strstr(ing, "name: collections-server") && strstr(ing, "number: 8087"));
    {   /* exactly ONE Ingress document => one external LB */
        int n = 0; for (const char *q = ing; (q = strstr(q, "kind: Ingress")); q++) n++;
        assert(n == 1);
    }

    puts("test_k8s_pod: OK");
    return 0;
}
