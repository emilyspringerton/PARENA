/* ast_sugar.h -- shared, header-only AST rewrites run before emission by the wasm (emit_llvm.c) and
 * TypeScript (emit_ts.c) backends. Static functions on purpose: both emitters are deliberately
 * independent translation units (see emit_ts.h), so this is included, not linked.
 *
 * Needs ast.h, arena.h, <stdio.h>, <string.h> already included by the includer. */
#ifndef PARENA_AST_SUGAR_H
#define PARENA_AST_SUGAR_H

#include "arena.h"
#include "ast.h"
#include <stdio.h>
#include <string.h>

/* --- AST sugar (2026-10-16, BRAWLPIT web) ----------------------------------------------------
   A ~2k-line game engine written as a single-expression-per-defn S-expression language is only
   tolerable with a little structure. These forms are pure AST rewrites run once over each defn
   body before emission; the emitter itself only ever sees the core forms it already understands.
     (cond t1 e1 t2 e2 ... [:else eN])    nested if            (no :else => statement form)
     (case x v1 e1 (v2 v3) e2 ... [dflt]) one evaluation of x, then nested if/=
     (dotimes [i n] body...)              counted loop, n evaluated once
     (unless c body...)                   (when (not c) body...)
     (inc! x) (dec! x) (add! x v) (sub! x v) (mul! x v)   compound assignment to a let/loop binding
     (+ a b c ...) likewise - * and or bit-and bit-or bit-xor    left-folded to binary form        */
static Node *mk_list(Arena *arena, int line) { return node_new_compound(arena, NODE_LIST, line); }
static Node *mk_sym(Arena *arena, const char *text, int line) {
    return node_new_atom(arena, NODE_SYMBOL, text, strlen(text), line);
}
static Node *mk_call2(Arena *arena, const char *head, Node *a, Node *b, int line) {
    Node *n = mk_list(arena, line);
    node_push_child(arena, n, mk_sym(arena, head, line));
    node_push_child(arena, n, a);
    node_push_child(arena, n, b);
    return n;
}
static int g_sugar_counter = 0;

static int is_else_test(Node *t) {
    return (t->type == NODE_KEYWORD && t->text && strcmp(t->text, ":else") == 0) ||
           (t->type == NODE_SYMBOL && (strcmp(t->text, "else") == 0 || strcmp(t->text, "true") == 0));
}

static int is_fold_op(const char *h) {
    return !strcmp(h, "+") || !strcmp(h, "-") || !strcmp(h, "*") || !strcmp(h, "and") || !strcmp(h, "or") ||
           !strcmp(h, "bit-and") || !strcmp(h, "bit-or") || !strcmp(h, "bit-xor");
}

static Node *ast_sugar(Arena *arena, Node *n, const char **out_error) {
    if (!n || (n->type != NODE_LIST && n->type != NODE_VEC && n->type != NODE_MAP)) return n;
    for (size_t i = 0; i < n->child_count; i++) {
        n->children[i] = ast_sugar(arena, n->children[i], out_error);
        if (*out_error) return n;
    }
    if (n->type != NODE_LIST || n->child_count == 0 || n->children[0]->type != NODE_SYMBOL) return n;
    const char *h = n->children[0]->text;
    int line = n->line;

    if (is_fold_op(h) && n->child_count > 3) {
        Node *acc = mk_call2(arena, h, n->children[1], n->children[2], line);
        for (size_t i = 3; i < n->child_count; i++) acc = mk_call2(arena, h, acc, n->children[i], line);
        return acc;
    }

    if (!strcmp(h, "unless") && n->child_count >= 2) {
        Node *w = mk_list(arena, line);
        node_push_child(arena, w, mk_sym(arena, "when", line));
        Node *neg = mk_list(arena, line);
        node_push_child(arena, neg, mk_sym(arena, "not", line));
        node_push_child(arena, neg, n->children[1]);
        node_push_child(arena, w, neg);
        for (size_t i = 2; i < n->child_count; i++) node_push_child(arena, w, n->children[i]);
        return w;
    }

    if ((!strcmp(h, "inc!") || !strcmp(h, "dec!")) && n->child_count == 2 && n->children[1]->type == NODE_SYMBOL) {
        Node *one = node_new_atom(arena, NODE_NUMBER, "1", 1, line);
        Node *set = mk_list(arena, line);
        node_push_child(arena, set, mk_sym(arena, "set!", line));
        node_push_child(arena, set, n->children[1]);
        node_push_child(arena, set, mk_call2(arena, !strcmp(h, "inc!") ? "+" : "-", n->children[1], one, line));
        return set;
    }
    if ((!strcmp(h, "add!") || !strcmp(h, "sub!") || !strcmp(h, "mul!")) && n->child_count == 3 &&
        n->children[1]->type == NODE_SYMBOL) {
        Node *set = mk_list(arena, line);
        node_push_child(arena, set, mk_sym(arena, "set!", line));
        node_push_child(arena, set, n->children[1]);
        node_push_child(arena, set, mk_call2(arena, !strcmp(h, "add!") ? "+" : (!strcmp(h, "sub!") ? "-" : "*"),
                                             n->children[1], n->children[2], line));
        return set;
    }

    if (!strcmp(h, "cond")) {
        size_t nforms = n->child_count - 1;
        Node *rest = NULL;
        size_t pairs = nforms / 2;
        if (nforms % 2 == 1) rest = n->children[n->child_count - 1];
        for (size_t k = pairs; k > 0; k--) {
            Node *test = n->children[1 + 2 * (k - 1)];
            Node *expr = n->children[2 + 2 * (k - 1)];
            if (is_else_test(test)) { rest = expr; continue; }
            Node *iff = mk_list(arena, line);
            node_push_child(arena, iff, mk_sym(arena, "if", line));
            node_push_child(arena, iff, test);
            node_push_child(arena, iff, expr);
            if (rest) node_push_child(arena, iff, rest);
            rest = iff;
        }
        return rest ? rest : mk_sym(arena, "unit", line);
    }

    if (!strcmp(h, "case") && n->child_count >= 2) {
        Node *subject = n->children[1];
        char tmpname[32];
        Node *tmp_ref = subject;
        int need_tmp = !(subject->type == NODE_SYMBOL || subject->type == NODE_NUMBER);
        if (need_tmp) {
            snprintf(tmpname, sizeof(tmpname), "__case%d", g_sugar_counter++);
            tmp_ref = mk_sym(arena, tmpname, line);
        }
        size_t nforms = n->child_count - 2;
        size_t pairs = nforms / 2;
        Node *rest = (nforms % 2 == 1) ? n->children[n->child_count - 1] : NULL;
        for (size_t k = pairs; k > 0; k--) {
            Node *vals = n->children[2 + 2 * (k - 1)];
            Node *expr = n->children[3 + 2 * (k - 1)];
            Node *test = NULL;
            if (vals->type == NODE_LIST) {
                for (size_t v = 0; v < vals->child_count; v++) {
                    Node *eq = mk_call2(arena, "=", tmp_ref, vals->children[v], line);
                    test = test ? mk_call2(arena, "or", test, eq, line) : eq;
                }
            } else {
                test = mk_call2(arena, "=", tmp_ref, vals, line);
            }
            if (!test) { *out_error = "emit_llvm: case clause with an empty value list"; return n; }
            Node *iff = mk_list(arena, line);
            node_push_child(arena, iff, mk_sym(arena, "if", line));
            node_push_child(arena, iff, test);
            node_push_child(arena, iff, expr);
            if (rest) node_push_child(arena, iff, rest);
            rest = iff;
        }
        if (!rest) rest = mk_sym(arena, "unit", line);
        if (!need_tmp) return rest;
        Node *let = mk_list(arena, line);
        node_push_child(arena, let, mk_sym(arena, "let", line));
        Node *bv = node_new_compound(arena, NODE_VEC, line);
        node_push_child(arena, bv, tmp_ref);
        node_push_child(arena, bv, subject);
        node_push_child(arena, let, bv);
        node_push_child(arena, let, rest);
        return let;
    }

    if (!strcmp(h, "dotimes") && n->child_count >= 3 && n->children[1]->type == NODE_VEC &&
        n->children[1]->child_count == 2 && n->children[1]->children[0]->type == NODE_SYMBOL) {
        Node *var = n->children[1]->children[0];
        Node *limit = n->children[1]->children[1];
        char tmpname[32];
        Node *limit_ref = limit;
        int need_tmp = !(limit->type == NODE_SYMBOL || limit->type == NODE_NUMBER);
        if (need_tmp) {
            snprintf(tmpname, sizeof(tmpname), "__dn%d", g_sugar_counter++);
            limit_ref = mk_sym(arena, tmpname, line);
        }
        Node *loop = mk_list(arena, line);
        node_push_child(arena, loop, mk_sym(arena, "loop", line));
        Node *lv = node_new_compound(arena, NODE_VEC, line);
        node_push_child(arena, lv, var);
        node_push_child(arena, lv, node_new_atom(arena, NODE_NUMBER, "0", 1, line));
        node_push_child(arena, loop, lv);
        Node *w = mk_list(arena, line);
        node_push_child(arena, w, mk_sym(arena, "when", line));
        node_push_child(arena, w, mk_call2(arena, "<", var, limit_ref, line));
        for (size_t i = 2; i < n->child_count; i++) node_push_child(arena, w, n->children[i]);
        Node *rec = mk_list(arena, line);
        node_push_child(arena, rec, mk_sym(arena, "recur", line));
        node_push_child(arena, rec, mk_call2(arena, "+", var, node_new_atom(arena, NODE_NUMBER, "1", 1, line), line));
        node_push_child(arena, w, rec);
        node_push_child(arena, loop, w);
        if (!need_tmp) return loop;
        Node *let = mk_list(arena, line);
        node_push_child(arena, let, mk_sym(arena, "let", line));
        Node *bv = node_new_compound(arena, NODE_VEC, line);
        node_push_child(arena, bv, limit_ref);
        node_push_child(arena, bv, limit);
        node_push_child(arena, let, bv);
        node_push_child(arena, let, loop);
        return let;
    }
    return n;
}


#endif /* PARENA_AST_SUGAR_H */
