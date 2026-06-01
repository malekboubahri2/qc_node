#include "domain/config_parser.h"
#include "domain/operator_list.h"
#include "domain/defect_config.h"
#include "jsmn/jsmn.h"
#include <string.h>
#include <stdio.h>

/* Bounded token pool — config payloads are capped at ~4 KB upstream. */
#define CONFIG_PARSER_MAX_TOKENS 1024
static jsmntok_t s_tokens[CONFIG_PARSER_MAX_TOKENS];

/* ── token helpers ───────────────────────────────────────────────────────── */

static bool tok_streq(const char *js, const jsmntok_t *t, const char *s)
{
    if (t->type != JSMN_STRING) {
        return false;
    }
    int n = t->end - t->start;
    return (n == (int)strlen(s)) && (strncmp(js + t->start, s, (size_t)n) == 0);
}

static int tok_to_int(const char *js, const jsmntok_t *t)
{
    int i = t->start;
    int sign = 1;
    int val = 0;
    if (i < t->end && js[i] == '-') { sign = -1; i++; }
    for (; i < t->end; i++) {
        char c = js[i];
        if (c < '0' || c > '9') {
            break;
        }
        val = val * 10 + (c - '0');
    }
    return val * sign;
}

static bool tok_is_true(const char *js, const jsmntok_t *t)
{
    return (t->type == JSMN_PRIMITIVE) && (t->end - t->start == 4) &&
           (strncmp(js + t->start, "true", 4) == 0);
}

/* Copy a JSON string token into out, decoding the escapes the server emits.
 * Python json.dumps(ensure_ascii=True) renders accents as \uXXXX; we fold
 * code points <= 0xFF to a single Latin-1 byte, which the TouchGFX views cast
 * straight to UnicodeChar (so "é", "è", "à" render correctly). */
static void tok_copy_str(const char *js, const jsmntok_t *t,
                         char *out, size_t out_sz)
{
    size_t o = 0;
    int i = t->start;
    while (i < t->end && o + 1 < out_sz) {
        char c = js[i];
        if (c == '\\' && i + 1 < t->end) {
            char e = js[i + 1];
            switch (e) {
            case '"':  out[o++] = '"';  i += 2; break;
            case '\\': out[o++] = '\\'; i += 2; break;
            case '/':  out[o++] = '/';  i += 2; break;
            case 'b':  out[o++] = '\b'; i += 2; break;
            case 'f':  out[o++] = '\f'; i += 2; break;
            case 'n':  out[o++] = '\n'; i += 2; break;
            case 'r':  out[o++] = '\r'; i += 2; break;
            case 't':  out[o++] = '\t'; i += 2; break;
            case 'u': {
                if (i + 6 <= t->end) {
                    unsigned int cp = 0;
                    for (int k = 0; k < 4; k++) {
                        char h = js[i + 2 + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    }
                    out[o++] = (cp <= 0xFF) ? (char)cp : '?';
                    i += 6;
                } else {
                    i = t->end;
                }
                break;
            }
            default:
                out[o++] = e;
                i += 2;
                break;
            }
        } else {
            out[o++] = c;
            i++;
        }
    }
    out[o] = '\0';
}

/* Return the index just past the subtree rooted at token i. */
static int tok_skip(const jsmntok_t *t, int i)
{
    switch (t[i].type) {
    case JSMN_OBJECT: {
        int idx = i + 1;
        for (int j = 0; j < t[i].size; j++) {
            idx = tok_skip(t, idx); /* key */
            idx = tok_skip(t, idx); /* value */
        }
        return idx;
    }
    case JSMN_ARRAY: {
        int idx = i + 1;
        for (int j = 0; j < t[i].size; j++) {
            idx = tok_skip(t, idx);
        }
        return idx;
    }
    default:
        return i + 1;
    }
}

/* Find the value-token index for key within an object token; -1 if absent. */
static int obj_find(const char *js, const jsmntok_t *t, int obj_idx,
                    const char *key)
{
    if (t[obj_idx].type != JSMN_OBJECT) {
        return -1;
    }
    int idx = obj_idx + 1;
    for (int j = 0; j < t[obj_idx].size; j++) {
        int key_idx = idx;
        int val_idx = key_idx + 1;
        if (tok_streq(js, &t[key_idx], key)) {
            return val_idx;
        }
        idx = tok_skip(t, val_idx);
    }
    return -1;
}

/* Tokenize once into the shared pool. Returns token count or negative. */
static int parse_root(const char *json, size_t len)
{
    jsmn_parser p;
    jsmn_init(&p);
    int n = jsmn_parse(&p, json, len, s_tokens, CONFIG_PARSER_MAX_TOKENS);
    if (n < 0) {
        printf("config_parser: jsmn error %d\n", n);
        return -1;
    }
    if (n < 1 || s_tokens[0].type != JSMN_OBJECT) {
        printf("config_parser: root is not an object\n");
        return -1;
    }
    return n;
}

/* ── operators ───────────────────────────────────────────────────────────── */

int config_parser_apply_operators(const char *json, size_t len)
{
    static operator_entry_t ops[32];

    if (!json || len == 0) {
        return -1;
    }
    if (parse_root(json, len) < 0) {
        return -1;
    }

    int arr = obj_find(json, s_tokens, 0, "operators");
    if (arr < 0 || s_tokens[arr].type != JSMN_ARRAY) {
        printf("config_parser: no operators array\n");
        return -1;
    }

    int count = 0;
    int idx = arr + 1;
    for (int j = 0; j < s_tokens[arr].size; j++) {
        int obj = idx;
        if (s_tokens[obj].type == JSMN_OBJECT &&
            count < (int)(sizeof(ops) / sizeof(ops[0]))) {
            memset(&ops[count], 0, sizeof(ops[count]));

            int v_id   = obj_find(json, s_tokens, obj, "id");
            int v_name = obj_find(json, s_tokens, obj, "name");
            int v_pin  = obj_find(json, s_tokens, obj, "pin_hash");

            if (v_id >= 0) {
                ops[count].id = tok_to_int(json, &s_tokens[v_id]);
            }
            if (v_name >= 0) {
                tok_copy_str(json, &s_tokens[v_name], ops[count].name,
                             sizeof(ops[count].name));
            }
            if (v_pin >= 0) {
                tok_copy_str(json, &s_tokens[v_pin], ops[count].pin_hash,
                             sizeof(ops[count].pin_hash));
            }
            count++;
        }
        idx = tok_skip(s_tokens, obj);
    }

    operator_list_set(ops, count);
    printf("config_parser: applied %d operators\n", count);
    return 0;
}

/* ── products + defect types ─────────────────────────────────────────────── */

static void apply_category(const char *json, int cats_obj,
                           const char *key, int product_id, int category)
{
    static defect_type_t types[DEFECT_CONFIG_MAX_DEFECTS_PER_CATEGORY];

    int arr = obj_find(json, s_tokens, cats_obj, key);
    if (arr < 0 || s_tokens[arr].type != JSMN_ARRAY) {
        return;
    }

    int count = 0;
    int idx = arr + 1;
    for (int j = 0; j < s_tokens[arr].size; j++) {
        int obj = idx;
        if (s_tokens[obj].type == JSMN_OBJECT &&
            count < DEFECT_CONFIG_MAX_DEFECTS_PER_CATEGORY) {
            memset(&types[count], 0, sizeof(types[count]));

            int v_id    = obj_find(json, s_tokens, obj, "id");
            int v_label = obj_find(json, s_tokens, obj, "label");
            int v_other = obj_find(json, s_tokens, obj, "is_other_fallback");

            if (v_id >= 0) {
                types[count].id = tok_to_int(json, &s_tokens[v_id]);
            }
            if (v_label >= 0) {
                tok_copy_str(json, &s_tokens[v_label], types[count].label,
                             sizeof(types[count].label));
            }
            types[count].is_other =
                (v_other >= 0) && tok_is_true(json, &s_tokens[v_other]);
            count++;
        }
        idx = tok_skip(s_tokens, obj);
    }

    defect_config_set_defect_types(product_id, category, types, count);
}

int config_parser_apply_products(const char *json, size_t len)
{
    static product_entry_t prods[DEFECT_CONFIG_MAX_PRODUCTS];
    static int prod_tok[DEFECT_CONFIG_MAX_PRODUCTS];

    if (!json || len == 0) {
        return -1;
    }
    if (parse_root(json, len) < 0) {
        return -1;
    }

    int arr = obj_find(json, s_tokens, 0, "products");
    if (arr < 0 || s_tokens[arr].type != JSMN_ARRAY) {
        printf("config_parser: no products array\n");
        return -1;
    }

    int nprod = 0;
    int idx = arr + 1;
    for (int j = 0; j < s_tokens[arr].size; j++) {
        int obj = idx;
        if (s_tokens[obj].type == JSMN_OBJECT &&
            nprod < DEFECT_CONFIG_MAX_PRODUCTS) {
            memset(&prods[nprod], 0, sizeof(prods[nprod]));

            int v_id   = obj_find(json, s_tokens, obj, "id");
            int v_name = obj_find(json, s_tokens, obj, "name");

            if (v_id >= 0) {
                prods[nprod].id = tok_to_int(json, &s_tokens[v_id]);
            }
            if (v_name >= 0) {
                tok_copy_str(json, &s_tokens[v_name], prods[nprod].name,
                             sizeof(prods[nprod].name));
            }
            prod_tok[nprod] = obj;
            nprod++;
        }
        idx = tok_skip(s_tokens, obj);
    }

    defect_config_set_products(prods, nprod);

    for (int i = 0; i < nprod; i++) {
        int cats = obj_find(json, s_tokens, prod_tok[i], "categories");
        if (cats < 0 || s_tokens[cats].type != JSMN_OBJECT) {
            continue;
        }
        apply_category(json, cats, "PMP", prods[i].id, DEFECT_CONFIG_CATEGORY_PMP);
        apply_category(json, cats, "INJECTION", prods[i].id, DEFECT_CONFIG_CATEGORY_INJ);
    }

    printf("config_parser: applied %d products\n", nprod);
    return 0;
}
