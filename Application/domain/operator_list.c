#include "domain/operator_list.h"
#include <string.h>
#include <stdio.h>

// Maximum limits
#define MAX_OPERATORS 100

// Internal storage
static operator_entry_t s_operators[MAX_OPERATORS];
static int s_operator_count = 0;

int operator_list_init(void)
{
    // Clear all stored data
    memset(s_operators, 0, sizeof(s_operators));
    s_operator_count = 0;
    
    printf("operator_list: initialized\n");
    return 0;
}

int operator_list_set(const operator_entry_t *operators, int operator_count)
{
    int i;
    
    // Validate inputs
    if (!operators && operator_count > 0) return -1;
    if (operator_count > MAX_OPERATORS) return -1;
    
    // Clear existing data
    memset(s_operators, 0, sizeof(s_operators));
    s_operator_count = 0;
    
    // Copy operators
    for (i = 0; i < operator_count && i < MAX_OPERATORS; i++)
    {
        s_operators[i] = operators[i];
        // Ensure null termination
        s_operators[i].name[sizeof(s_operators[i].name) - 1] = '\0';
        s_operators[i].pin[sizeof(s_operators[i].pin) - 1] = '\0';
    }
    s_operator_count = i;
    
    printf("operator_list: set %d operators\n", s_operator_count);
    return 0;
}

int operator_list_get_count(void)
{
    return s_operator_count;
}

int operator_list_get(int idx, operator_entry_t *operator)
{
    if (!operator || idx < 0 || idx >= s_operator_count)
        return -1;
    
    *operator = s_operators[idx];
    return 0;
}

bool operator_list_validate_pin(int operator_id, const char *pin,
                               operator_entry_t *operator)
{
    int i;
    
    // Validate inputs
    if (!pin)
        return false;
    
    // Find the operator
    for (i = 0; i < s_operator_count; i++)
    {
        if (s_operators[i].id == operator_id)
        {
            // Found the operator, check the PIN
            if (strcmp(s_operators[i].pin, pin) == 0)
            {
                // PIN is valid
                if (operator)
                    *operator = s_operators[i];
                return true;
            }
            else
            {
                // PIN is invalid
                return false;
            }
        }
    }
    
    // Operator not found
    return false;
}
