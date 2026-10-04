/* tools/pod_render_host.c -- `parena-pod-render`: render a multi-container Pod Deployment (+PVC,
 * Service, optional shared Ingress) from a line-oriented spec file, using stdlib/k8s/{k8s,pod,
 * gitops}.prn. The renderer for pods whose containers talk over unix sockets in a shared emptyDir
 * (docs: PRRJECT_FATBABY/docs/northstar/FATBABY_K8S_UDS_NORTHSTAR.md). Same GitOps flow as
 * parena-k8s-render: output is committed to the manifests repo and applied by parena-gitops.
 *
 * spec (one directive per line, `key=value` tokens, '#' comments, no spaces inside values):
 *   pod name=N namespace=NS image=IMG pvc=CLAIM pvc-gb=20 secret=SECRET var=/app/var run=/run/fatbaby [replicas=1]
 *   container name=C command=/app/bin/x [args=a,b,c] [port=8082] cpu=100 mem=128
 *   service ports=8082,9091           # ONE ClusterIP Service named after the pod, selecting it
 *   ingress name=edge namespace=NS rules=H@svc@8082,H2@svc2@9091   # the cluster's ONE Ingress (host@service@port); may stand alone, no pod needed
 *
 * usage: parena-pod-render SPEC
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pod_render_gen.c"

#define MAXKV 16
typedef struct { char *k[MAXKV]; char *v[MAXKV]; int n; } KV;

static void parse_kv(char *rest, KV *kv, int lineno) {
    kv->n = 0;
    for (char *tok = strtok(rest, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        char *eq = strchr(tok, '=');
        if (!eq || eq == tok || kv->n >= MAXKV) { fprintf(stderr, "spec:%d: bad token '%s'\n", lineno, tok); exit(2); }
        *eq = 0; kv->k[kv->n] = tok; kv->v[kv->n] = eq + 1; kv->n++;
    }
}
static const char *get(KV *kv, const char *key, const char *def, int lineno) {
    for (int i = 0; i < kv->n; i++) if (!strcmp(kv->k[i], key)) return kv->v[i];
    if (!def) { fprintf(stderr, "spec:%d: missing %s=\n", lineno, key); exit(2); }
    return def;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: parena-pod-render SPEC\n"); return 2; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 2; }
    Arena a; arena_init(&a);
    char line[4096];
    int lineno = 0, have_pod = 0, replicas = 1, pvc_gb = 0, ncont = 0;
    char name[128] = "", ns[128] = "", image[256] = "", pvc[128] = "", secret[128] = "", var[128] = "", run[128] = "", svc_ports[128] = "", ing_rules[1024] = "", ing_name[128] = "", ing_ns[128] = "";
    /* containers are rendered while reading, but the head must come first: buffer them. */
    char *containers = strdup("");
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;
        char *rest = p;
        while (*rest && *rest != ' ' && *rest != '\t' && *rest != '\n') rest++;
        char *kind = p;
        if (*rest) { *rest = 0; rest++; }
        KV kv;
        parse_kv(rest, &kv, lineno);
        if (!strcmp(kind, "pod")) {
            have_pod = 1;
            snprintf(name, sizeof name, "%s", get(&kv, "name", NULL, lineno));
            snprintf(ns, sizeof ns, "%s", get(&kv, "namespace", NULL, lineno));
            snprintf(image, sizeof image, "%s", get(&kv, "image", NULL, lineno));
            snprintf(pvc, sizeof pvc, "%s", get(&kv, "pvc", NULL, lineno));
            snprintf(secret, sizeof secret, "%s", get(&kv, "secret", NULL, lineno));
            snprintf(var, sizeof var, "%s", get(&kv, "var", NULL, lineno));
            snprintf(run, sizeof run, "%s", get(&kv, "run", NULL, lineno));
            replicas = atoi(get(&kv, "replicas", "1", lineno));
            pvc_gb = atoi(get(&kv, "pvc-gb", "20", lineno));
        } else if (!strcmp(kind, "container")) {
            if (!have_pod) { fprintf(stderr, "spec:%d: container before pod\n", lineno); return 2; }
            char *y = pod_container_yaml((char *)get(&kv, "name", NULL, lineno), image,
                (char *)get(&kv, "command", NULL, lineno), (char *)get(&kv, "args", "", lineno),
                atoi(get(&kv, "port", "0", lineno)), atoi(get(&kv, "cpu", "100", lineno)),
                atoi(get(&kv, "mem", "128", lineno)), secret, var, run, &a);
            containers = realloc(containers, strlen(containers) + strlen(y) + 1);
            strcat(containers, y);
            ncont++;
        } else if (!strcmp(kind, "service")) {
            snprintf(svc_ports, sizeof svc_ports, "%s", get(&kv, "ports", NULL, lineno));
        } else if (!strcmp(kind, "ingress")) {
            snprintf(ing_rules, sizeof ing_rules, "%s", get(&kv, "rules", NULL, lineno));
            snprintf(ing_name, sizeof ing_name, "%s", get(&kv, "name", "", lineno));
            snprintf(ing_ns, sizeof ing_ns, "%s", get(&kv, "namespace", "", lineno));
        } else { fprintf(stderr, "spec:%d: unknown directive '%s'\n", lineno, kind); return 2; }
    }
    fclose(f);
    if (!have_pod && ing_rules[0]) {   /* stand-alone shared Ingress */
        if (!ing_name[0] || !ing_ns[0]) { fprintf(stderr, "spec: stand-alone ingress needs name= and namespace=\n"); return 2; }
        fputs(pod_ingress_yaml(ing_name, ing_ns, ing_rules, &a), stdout);
        return 0;
    }
    if (!have_pod || ncont == 0) { fprintf(stderr, "spec: need a pod and at least one container\n"); return 2; }
    char *out = pvc_yaml(PVCSpec_new(pvc, ns, pvc_gb), &a);
    char *dep = pod_head_yaml(name, ns, replicas, &a);
    dep = concat(dep, containers, &a);
    dep = concat(dep, pod_tail_yaml(pvc, &a), &a);
    out = join_docs(out, dep, &a);
    if (svc_ports[0]) out = join_docs(out, pod_service_yaml(name, ns, svc_ports, &a), &a);
    if (ing_rules[0]) out = join_docs(out, pod_ingress_yaml(ing_name[0] ? ing_name : name, ing_ns[0] ? ing_ns : ns, ing_rules, &a), &a);
    fputs(out, stdout);
    return 0;
}
