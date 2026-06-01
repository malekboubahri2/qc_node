#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <string.h>
#include <stdio.h>
#include "domain/operator_list.h"
#include "domain/defect_config.h"
#include "domain/session.h"
#include "net/inspection_queue.h"

// Forward declaration of the UI data bridge functions
// These would be implemented in a C++ file that calls the Model singleton
extern "C" {
    void ui_bridge_set_operators(const operator_entry_t* list, int count);
    void ui_bridge_set_products(const product_entry_t* list, int count);
    void ui_bridge_set_defect_types(int product_id, int category,
                                   const defect_type_t* list, int count);
}

static Model* s_model_instance = 0;

extern "C" Model* getModelInstance()
{
    return s_model_instance;
}

Model::Model()
    : modelListener(0),
      m_preciserOrigin(PreciserOrigin::NONE),
      m_connected(false)
{
    s_model_instance = this;
    m_preciserBuffer[0] = '\0';
    
    // Initialize domain modules
    operator_list_init();
    defect_config_init();
    session_init();
    
    // Initialize inspection queue
    inspection_queue_init();
}

void Model::setPreciserPendingText(const char* text)
{
    if (text == nullptr)
    {
        m_preciserBuffer[0] = '\0';
        return;
    }
    strncpy(m_preciserBuffer, text, PRECISER_BUFFER_SIZE - 1);
    m_preciserBuffer[PRECISER_BUFFER_SIZE - 1] = '\0';
}

void Model::clearPreciser()
{
    m_preciserOrigin = PreciserOrigin::NONE;
    m_preciserBuffer[0] = '\0';
}

void Model::setPreciserOrigin(PreciserOrigin origin)
{
    m_preciserOrigin = origin;
}

Model::PreciserOrigin Model::getPreciserOrigin() const
{
    return m_preciserOrigin;
}

const char* Model::getPreciserPendingText() const
{
    return m_preciserBuffer;
}

void Model::tick()
{
    // This is called every frame - could be used for connection status updates
}

bool Model::validatePin(const char* pin, int* out_idx) const
{
    // Delegate to operator_list module
    int count = operator_list_get_count();
    for (int i = 0; i < count; i++)
    {
        operator_entry_t op;
        if (operator_list_get(i, &op) == 0)
        {
            if (strcmp(pin, op.pin) == 0)
            {
                if (out_idx)
                {
                    // Find the index in our stored list
                    for (int j = 0; j < count; j++)
                    {
                        operator_entry_t op2;
                        if (operator_list_get(j, &op2) == 0 && op2.id == op.id)
                        {
                            if (out_idx)
                                *out_idx = j;
                            break;
                        }
                    }
                }
                return true;
            }
        }
    }
    return false;
}

void Model::setOperators(const operator_entry_t* list, int count)
{
    // Delegate to operator_list module
    operator_list_set(list, count);
    
    // Notify listeners if needed
    if (modelListener)
    {
        // In a real implementation, we would have a specific callback for operator updates
        // For now, we'll just note that data has changed
    }
    
    printf("Model: operators updated (%d operators)\n", count);
}

int Model::getOperatorCount() const
{
    return operator_list_get_count();
}

const operator_entry_t& Model::getOperator(int idx) const
{
    static operator_entry_t dummy_op = { 0, "", "" };
    static operator_entry_t cached_op;
    
    if (operator_list_get(idx, &cached_op) == 0)
    {
        return cached_op;
    }
    
    // Return dummy if index out of bounds
    return dummy_op;
}

void Model::setProducts(const product_entry_t* list, int count)
{
    // Convert our product_entry_t to defect_config's format
    // For simplicity, we'll assume defect types are handled separately
    defect_config_set(list, count, NULL, 0);
    
    printf("Model: products updated (%d products)\n", count);
}

int Model::getProductCount() const
{
    return defect_config_get_product_count();
}

const product_entry_t& Model::getProduct(int idx) const
{
    static product_entry_t dummy_product = { 0, "" };
    static product_entry_t cached_product;
    
    if (defect_config_get_product(idx, &cached_product) == 0)
    {
        return cached_product;
    }
    
    // Return dummy if index out of bounds
    return dummy_product;
}

void Model::setCurrentProductId(int id)
{
    // In a real implementation, we would validate that the product exists
    // For now, we'll just store it and let validation happen elsewhere
    printf("Model: current product ID set to %d\n", id);
}

int Model::getCurrentProductId() const
{
    // This would typically come from the session module
    // For now, we'll return a placeholder - in reality this should be tied to session
    return session_get_product_id();
}

void Model::setDefectTypes(int product_id, int category,
                          const defect_type_t* list, int count)
{
    // Delegate to defect_config module
    // Note: This is a simplified implementation - in reality we'd need to
    // convert our defect_type_t to defect_config's format
    printf("Model: setting defect types for product %d, category %d (%d types)\n",
           product_id, category, count);
    
    // TODO: Implement proper storage of defect types by product/category
}

const defect_type_t* Model::getDefectTypes(int product_id, int category,
                                                 int* out_count) const
{
    static defect_type_t dummy_types[1] = { { 0, "" } };
    
    // TODO: Implement proper retrieval of defect types by product/category
    // For now, return empty list
    if (out_count)
        *out_count = 0;
    
    return dummy_types;
}

void Model::enqueueInspection(int product_id, int operator_id,
                             const char* outcome,          // "DEFECT" | "OK"
                             int defect_type_id,           // -1 if OK
                             const char* note)
{
    // Create inspection message
    inspection_msg_t msg;
    msg.schema_version = 3; // ADR-014
    
    // Copy outcome (ensure null-termination)
    strncpy(msg.outcome, outcome ? outcome : "", sizeof(msg.outcome) - 1);
    msg.outcome[sizeof(msg.outcome) - 1] = '\0';
    
    msg.product_id = product_id;
    msg.operator_id = operator_id;
    msg.defect_type_id = defect_type_id;
    
    // Copy note (ensure null-termination)
    strncpy(msg.note, note ? note : "", sizeof(msg.note) - 1);
    msg.note[sizeof(msg.note) - 1] = '\0';
    
    // Send to inspection queue
    inspection_queue_send(&msg);
    
    printf("Model: enqueued inspection (product=%d, operator=%d, outcome=%s, defect_type=%d, note=%s)\n",
           product_id, operator_id, outcome ? outcome : "NULL", defect_type_id, note ? note : "NULL");
}

void Model::publishSessionStart(int product_id, int operator_id)
{
    // Create session start message (similar to inspection but without defect info)
    // For simplicity, we're reusing the inspection message structure
    // In a real implementation, we might have a separate message type
    inspection_msg_t msg;
    msg.schema_version = 3; // ADR-014
    strncpy(msg.outcome, "SESSION_START", sizeof(msg.outcome) - 1);
    msg.outcome[sizeof(msg.outcome) - 1] = '\0';
    msg.product_id = product_id;
    msg.operator_id = operator_id;
    msg.defect_type_id = 0; // Not used for session start
    msg.note[0] = '\0'; // Empty note
    
    // Send to inspection queue
    inspection_queue_send(&msg);
    
    printf("Model: published session start (product=%d, operator=%d)\n",
           product_id, operator_id);
}

bool Model::isConnected() const
{
    return m_connected;
}

// Helper functions to interface with session module
void Model::setCurrentOperatorIdx(int idx)
{
    // Get the operator ID from the index and start a session
    operator_entry_t op;
    if (operator_list_get(idx, &op) == 0)
    {
        // In a real implementation, we would get the current product ID from somewhere
        // For now, we'll use a placeholder
        session_start(op.id, 1); // Assuming product ID 1 for now
    }
}

int Model::getCurrentOperatorIdx() const
{
    // Find the index of the current operator
    int operator_id = session_get_operator_id();
    if (operator_id <= 0)
        return -1;
        
    int count = operator_list_get_count();
    for (int i = 0; i < count; i++)
    {
        operator_entry_t op;
        if (operator_list_get(i, &op) == 0 && op.id == operator_id)
        {
            return i;
        }
    }
    return -1;
}

void Model::resetSessionDefectCount()
{
    // Reset defect count in session by ending and restarting the session
    // This preserves the current operator and product while resetting defect count to 0
    
    int operator_id = session_get_operator_id();
    int product_id = session_get_product_id();
    
    // Only reset if there's an active session (both IDs should be non-zero)
    if (operator_id != 0 && product_id != 0) {
        session_end();
        session_start(operator_id, product_id);
    }
    // If no active session, there's nothing to reset
}
