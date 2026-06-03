#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <string.h>
#include <stdio.h>
#include "domain/operator_list.h"
#include "domain/defect_config.h"
#include "domain/session.h"
#include "net/inspection_queue.h"
#include "time/time_source.h"

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
      m_connected(false),
      m_pmpCount(0),
      m_injCount(0),
      m_inspectionPending(false),
      m_sessionInspected(0)
{
    s_model_instance = this;
    m_preciserBuffer[0] = '\0';
    m_inspectionNote[0] = '\0';

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
    // Delegate to operator_list, which re-hashes the entered PIN (SHA-256)
    // and compares against the server-provided pin_hash.
    return operator_list_check_pin(pin, out_idx);
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
    // Defect types arrive separately via setDefectTypes(); setting the product
    // list clears any previously stored defect types in defect_config.
    defect_config_set_products(list, count);

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
    defect_config_set_defect_types(product_id, category, list, count);
}

const defect_type_t* Model::getDefectTypes(int product_id, int category,
                                                 int* out_count) const
{
    const defect_type_t* types = nullptr;
    int count = 0;
    if (defect_config_get_defect_types(product_id, category, &types, &count) == 0)
    {
        if (out_count)
            *out_count = count;
        return types;
    }

    if (out_count)
        *out_count = 0;
    return nullptr;
}

void Model::setCategoryDefects(int category, const int* defectTypeIds, int count,
                               const char* note)
{
    int* dest = (category == 1) ? m_injDefects : m_pmpDefects;
    int  n = 0;
    for (int i = 0; i < count && n < INSPECTION_MAX_DEFECTS; ++i)
    {
        if (defectTypeIds[i] >= 0)
            dest[n++] = defectTypeIds[i];
    }
    if (category == 1)
        m_injCount = n;
    else
        m_pmpCount = n;

    if (note != nullptr && note[0] != '\0')
    {
        strncpy(m_inspectionNote, note, sizeof(m_inspectionNote) - 1);
        m_inspectionNote[sizeof(m_inspectionNote) - 1] = '\0';
    }

    m_inspectionPending = true;

    printf("Model: committed %s selection (%d defects)\n",
           (category == 1) ? "INJ" : "PMP", n);
}

void Model::publishInspection()
{
    if (!m_inspectionPending)
        return;

    inspection_msg_t msg;
    msg.schema_version = 4; // per-part full inspection
    msg.product_id = getCurrentProductId();
    msg.operator_id = getOperator(getCurrentOperatorIdx()).id;

    msg.pmp_count = (m_pmpCount > INSPECTION_MAX_DEFECTS) ? INSPECTION_MAX_DEFECTS : m_pmpCount;
    for (int i = 0; i < msg.pmp_count; ++i)
        msg.pmp_defects[i] = m_pmpDefects[i];

    msg.inj_count = (m_injCount > INSPECTION_MAX_DEFECTS) ? INSPECTION_MAX_DEFECTS : m_injCount;
    for (int i = 0; i < msg.inj_count; ++i)
        msg.inj_defects[i] = m_injDefects[i];

    strncpy(msg.note, m_inspectionNote, sizeof(msg.note) - 1);
    msg.note[sizeof(msg.note) - 1] = '\0';

    /* Stamp when the part was inspected, so an offline-queued part keeps its
     * real time. 0 when the clock isn't synced yet -> server uses receipt time. */
    msg.logged_at_utc = time_source_now_utc();

    inspection_queue_send(&msg);
    ++m_sessionInspected;   /* one more part inspected this session */

    printf("Model: published inspection (product=%d, operator=%d, pmp=%d, inj=%d, session_parts=%d)\n",
           msg.product_id, msg.operator_id, msg.pmp_count, msg.inj_count, m_sessionInspected);

    clearInspection();
}

void Model::clearInspection()
{
    m_pmpCount = 0;
    m_injCount = 0;
    m_inspectionNote[0] = '\0';
    m_inspectionPending = false;
}

int Model::getInspectionDefectCount() const
{
    return m_pmpCount + m_injCount;
}

void Model::publishSessionStart(int product_id, int operator_id)
{
    /* The session topic is not wired up yet; the per-part inspection carries
     * operator_id + product_id, which is sufficient for the PoC. */
    (void)product_id;
    (void)operator_id;
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
        m_sessionInspected = 0;  // new session: reset the parts-inspected counter
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
