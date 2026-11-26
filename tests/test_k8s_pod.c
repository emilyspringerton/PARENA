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
    /* Autopilot caps a pod at 10Gi ephemeral and defaults each container to 1Gi: must be explicit */
    assert(strstr(c, "memory: 128Mi\n              ephemeral-storage: 256Mi\n            limits:"));

    char *n = pod_container_yaml("newssite", "img:1", "/app/bin/newssite", "", 8082, 250, 256, "s", "/app/var", "/run/fatbaby", &a);
    assert(strstr(n, "containerPort: 8082") && strstr(n, "tcpSocket:\n              port: 8082"));
    assert(!strstr(n, "args:"));

    char *t = pod_tail_yaml("fatbaby-var", &a);
    assert(strstr(t, "claimName: fatbaby-var") && strstr(t, "emptyDir: {}"));
    assert(!strstr(t, "hostPath"));                 /* Autopilot forbids hostPath */

    char *svc = pod_service_yaml("fatbaby-core", "fatbaby", "8082,9091", "", &a);
    assert(strstr(svc, "kind: Service") && strstr(svc, "type: ClusterIP") && !strstr(svc, "LoadBalancer"));
    assert(strstr(svc, "- name: p8082\n      port: 8082\n      targetPort: 8082\n"));
    assert(strstr(svc, "- name: p9091\n      port: 9091\n      targetPort: 9091\n"));
    assert(strstr(svc, "selector:\n    app: fatbaby-core"));

    char *tl = pod_tail_yaml("", &a);
    assert(!strstr(tl, "persistentVolumeClaim") && strstr(tl, "name: var\n          emptyDir: {}"));

    char *u = pod_udp_service_yaml("redgarden-stable", "emily", "8778,8300", "34.1.2.3", &a);
    assert(strstr(u, "name: redgarden-stable-udp") && strstr(u, "type: LoadBalancer"));
    assert(strstr(u, "loadBalancerIP: 34.1.2.3") && strstr(u, "externalTrafficPolicy: Local"));
    assert(strstr(u, "- name: u8778\n      protocol: UDP\n      port: 8778\n      targetPort: 8778\n"));
    assert(strstr(u, "- name: u8300\n      protocol: UDP\n      port: 8300\n      targetPort: 8300\n"));
    assert(strstr(u, "selector:\n    app: redgarden-stable"));
    assert(!strstr(pod_udp_service_yaml("x", "n", "1", "", &a), "loadBalancerIP"));
    { char *t = pod_tcp_service_yaml("m", "emily", "2323,2222", "34.9.9.9", &a); assert(strstr(t, "name: m-tcp") && strstr(t, "protocol: TCP") && strstr(t, "name: t2323") && !strstr(t, "UDP")); }

    char *bs = pod_service_yaml("w", "n", "8081", "ws-timeout", &a);
    assert(strstr(bs, "cloud.google.com/backend-config: '{\"default\": \"ws-timeout\"}'") && strstr(bs, "type: ClusterIP"));
    assert(!strstr(pod_service_yaml("w", "n", "8081", "", &a), "annotations"));
    char *bc = pod_backendconfig_yaml("ws-timeout", "emily", "3600", &a);
    assert(strstr(bc, "kind: BackendConfig") && strstr(bc, "timeoutSec: 3600"));
    char *ci = pod_ingress_yaml("edge", "emily", "a.io@s@80", "edge-certs", "edge-ip", &a);
    assert(strstr(ci, "networking.gke.io/certmap: edge-certs") && strstr(ci, "global-static-ip-name: edge-ip") && strstr(ci, "spec:\n  rules:"));
    assert(!strstr(pod_ingress_yaml("edge", "emily", "a.io@s@80", "", "", &a), "certmap"));
    char *gw = pod_gateway_yaml("edge-gw", "emily", "edge-certs", "gw-ip", "ws.x.io@relay@8081", "3600", &a);
    assert(strstr(gw, "kind: Gateway") && strstr(gw, "networking.gke.io/certmap: edge-certs"));
    assert(strstr(gw, "gatewayClassName: gke-l7-global-external-managed") && strstr(gw, "type: NamedAddress\n      value: gw-ip"));
    assert(strstr(gw, "kind: HTTPRoute") && strstr(gw, "- ws.x.io") && strstr(gw, "name: relay\n          port: 8081"));
    assert(strstr(gw, "kind: GCPBackendPolicy") && strstr(gw, "timeoutSec: 3600") && strstr(gw, "kind: Service\n    name: relay"));

    char *ing = pod_ingress_yaml("fatbaby-core", "fatbaby", "fatbaby.io@fatbaby-core@8082,api.fatbaby.io@fatbaby-core@9091,golden.okemily.com@collections-server@8087", "", "", &a);
    assert(strstr(ing, "kind: Ingress") && strstr(ing, "kubernetes.io/ingress.class: gce"));
    assert(strstr(ing, "host: fatbaby.io") && strstr(ing, "number: 8082"));
    assert(strstr(ing, "host: api.fatbaby.io") && strstr(ing, "number: 9091"));
    assert(strstr(ing, "host: golden.okemily.com") && strstr(ing, "name: collections-server") && strstr(ing, "number: 8087"));
    {   /* exactly ONE Ingress document => one external LB */
        int n = 0; for (const char *q = ing; (q = strstr(q, "kind: Ingress")); q++) n++;
        assert(n == 1);
    }

    /* ---- Vultr provider (GCP shut off 2026-11-26) ---- */
    {
        char *hp = pod_hostports_container_yaml("8778,8300", "2323", &a);
        assert(strstr(hp, "- name: hostports\n          image: registry.k8s.io/pause:3.10"));
        assert(strstr(hp, "containerPort: 8778\n              hostPort: 8778\n              protocol: UDP"));
        assert(strstr(hp, "containerPort: 8300\n              hostPort: 8300\n              protocol: UDP"));
        assert(strstr(hp, "containerPort: 2323\n              hostPort: 2323\n              protocol: TCP"));
        assert(!strstr(hp, "readinessProbe"));
        char *u = pod_hostports_container_yaml("6969", "", &a);
        assert(strstr(u, "protocol: UDP") && !strstr(u, "protocol: TCP"));
        assert(strcmp(pod_node_selector_yaml("game", &a), "      nodeSelector:\n        emily/pool: game\n") == 0);
        char *vg = pod_gateway_vultr_yaml("edge-gw", "emily", "eg", "edge-tls", "ws.x.io@relay@8081,g.x.io@coll@8087", "3600", &a);
        assert(strstr(vg, "kind: Gateway") && strstr(vg, "gatewayClassName: eg"));
        assert(strstr(vg, "certificateRefs:\n          - kind: Secret\n            name: edge-tls"));
        assert(strstr(vg, "kind: BackendTrafficPolicy") && strstr(vg, "requestTimeout: 3600s"));
        assert(strstr(vg, "kind: Gateway\n      name: edge-gw"));
        assert(strstr(vg, "- ws.x.io") && strstr(vg, "name: relay\n          port: 8081"));
        assert(strstr(vg, "- g.x.io") && strstr(vg, "name: coll\n          port: 8087"));
        assert(!strstr(vg, "gke") && !strstr(vg, "GCP") && !strstr(vg, "certmap"));
        int n = 0; for (const char *q = vg; (q = strstr(q, "\nkind: HTTPRoute")); q++) n++;
        assert(n == 2);
    }

    puts("test_k8s_pod: OK");
    return 0;
}
