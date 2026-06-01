#include "domain/operator_list.h"
#include "domain/pin_hash.h"
#include <string.h>
#include <stdio.h>

#define MAX_OPERATORS 32

static operator_entry_t s_operators[MAX_OPERATORS];
static int s_operator_count = 0;
static bool s_initialized = false;

int operator_list_init(void)
{
    if (s_initialized) {
        return 0; /* idempotent — preserve data populated by boot/MQTT */
    }
    memset(s_operators, 0, sizeof(s_operators));
    s_operator_count = 0;
    s_initialized = true;
    printf("operator_list: initialized\n");
    return 0;
}

int operator_list_set(const operator_entry_t *operators, int operator_count)
{
    if (!operators && operator_count > 0) {
        return -1;
    }
    if (operator_count < 0) {
        return -1;
    }

    memset(s_operators, 0, sizeof(s_operators));

    int n = 0;
    for (int i = 0; i < operator_count && n < MAX_OPERATORS; i++) {
        s_operators[n] = operators[i];
        s_operators[n].name[sizeof(s_operators[n].name) - 1] = '\0';
        s_operators[n].pin_hash[sizeof(s_operators[n].pin_hash) - 1] = '\0';
        n++;
    }
    s_operator_count = n;
    s_initialized = true;

    printf("operator_list: set %d operators\n", s_operator_count);
    return 0;
}

int operator_list_get_count(void)
{
    return s_operator_count;
}

int operator_list_get(int idx, operator_entry_t *op)
{
    if (!op || idx < 0 || idx >= s_operator_count) {
        return -1;
    }
    *op = s_operators[idx];
    return 0;
}

bool operator_list_check_pin(const char *pin, int *out_idx)
{
    if (!pin) {
        return false;
    }
    for (int i = 0; i < s_operator_count; i++) {
        if (pin_hash_verify_encoded(pin, s_operators[i].pin_hash)) {
            if (out_idx) {
                *out_idx = i;
            }
            return true;
        }
    }
    return false;
}
