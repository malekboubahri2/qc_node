#include "domain/session.h"
#include <string.h>
#include <stdio.h>

// Internal session state
static bool s_session_active = false;
static int s_operator_id = 0;
static int s_product_id = 0;
static int s_defect_count = 0;

int session_init(void)
{
    // Clear session state
    s_session_active = false;
    s_operator_id = 0;
    s_product_id = 0;
    s_defect_count = 0;
    
    printf("session: initialized\n");
    return 0;
}

int session_start(int operator_id, int product_id)
{
    // In a real implementation, we would validate the operator_id and product_id
    // against the operator_list and defect_config modules
    
    // For now, we'll just accept any non-zero IDs
    if (operator_id <= 0 || product_id <= 0)
        return -1;
    
    // End any existing session first
    session_end();
    
    // Start new session
    s_session_active = true;
    s_operator_id = operator_id;
    s_product_id = product_id;
    s_defect_count = 0;
    
    printf("session: started (operator=%d, product=%d)\n", operator_id, product_id);
    return 0;
}

int session_end(void)
{
    if (!s_session_active)
        return -1;  // No session to end
    
    printf("session: ended (operator=%d, product=%d, defects=%d)\n",
           s_operator_id, s_product_id, s_defect_count);
    
    s_session_active = false;
    s_operator_id = 0;
    s_product_id = 0;
    s_defect_count = 0;
    
    return 0;
}

bool session_is_active(void)
{
    return s_session_active;
}

int session_get_operator_id(void)
{
    return s_session_active ? s_operator_id : 0;
}

int session_get_product_id(void)
{
    return s_session_active ? s_product_id : 0;
}

int session_increment_defect_count(void)
{
    if (!s_session_active)
        return -1;
    
    s_defect_count++;
    return 0;
}

int session_get_defect_count(void)
{
    return s_session_active ? s_defect_count : 0;
}

int session_get_info(int *operator_id, int *product_id, int *defect_count)
{
    if (!s_session_active)
        return -1;
    
    if (operator_id)
        *operator_id = s_operator_id;
    if (product_id)
        *product_id = s_product_id;
    if (defect_count)
        *defect_count = s_defect_count;
    
    return 0;
}
