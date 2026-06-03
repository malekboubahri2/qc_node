#ifndef MODEL_HPP
#define MODEL_HPP

#include <stddef.h>
#include "domain/defect_config.h"
#include "domain/operator_list.h"
#include "domain/session.h"
#include "net/inspection_queue.h"

class ModelListener;

/**
 * @brief Expanded Model class that holds dynamic configuration data
 *        received via MQTT and manages application state.
 */
class Model
{
public:
    /* Maximum PIN digits the login screen supports (4 circles in the Designer). */
    static const int PIN_MAX_LEN    = 4;

    Model();

    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    void tick();

    /* ----- Operator Management ----- */
    bool validatePin(const char* pin, int* out_idx) const;
    void setOperators(const operator_entry_t* list, int count);
    int  getOperatorCount() const;
    const operator_entry_t& getOperator(int idx) const;

    /* ----- Product Management ----- */
    void setProducts(const product_entry_t* list, int count);
    int  getProductCount() const;
    const product_entry_t& getProduct(int idx) const;
    void setCurrentProductId(int id);
    int  getCurrentProductId() const;

    /* ----- Defect Type Management ----- */
    void setDefectTypes(int product_id, int category,   // category: 0=PMP 1=INJ
                       const defect_type_t* list, int count);
    const defect_type_t* getDefectTypes(int product_id, int category,
                                       int* out_count) const;

    /* ----- Session Management ----- */
    void setCurrentOperatorIdx(int idx);
    int  getCurrentOperatorIdx() const;

    void incrementSessionDefectCount() { session_increment_defect_count(); }
    void resetSessionDefectCount();
    int  getSessionDefectCount() const { return session_get_defect_count(); }

    /* Parts inspected since the operator logged in (each published part counts
     * once). Reset on login; shown on the summary screen. */
    int  getSessionInspectedCount() const { return m_sessionInspected; }

    /* ----- Outgoing Inspection Management (per-part full inspection) -----
     * The PMP and INJ screens each commit their selected defect_type_ids for
     * the current part (empty = OK for that category). The summary screen then
     * publishes one full inspection. */
    void setCategoryDefects(int category,                // 0 = PMP, 1 = INJ
                            const int* defectTypeIds, int count,
                            const char* note);
    void publishInspection();   // sends the accumulated part inspection (if any)
    void clearInspection();     // discard the accumulated selections
    int  getInspectionDefectCount() const; // PMP + INJ defects in the current part
    void publishSessionStart(int product_id, int operator_id);

    /* ----- Preciser Management ----- */
    enum class PreciserOrigin {
        NONE,
        PMP_DEFECTS,
        INJ_DEFECTS
    };
    void setPreciserOrigin(PreciserOrigin origin);
    PreciserOrigin getPreciserOrigin() const;
    void setPreciserPendingText(const char* text);
    const char* getPreciserPendingText() const;
    void clearPreciser();

    /* ----- Connection Status ----- */
    bool isConnected() const; // Returns true if EVT_MQTT_CONNECTED bit is set

protected:
    ModelListener* modelListener;

private:
    static const size_t PRECISER_BUFFER_SIZE = 128;
    PreciserOrigin m_preciserOrigin;
    char m_preciserBuffer[PRECISER_BUFFER_SIZE];

    // Connection status tracking (would be set by event bits from app_events.h)
    bool m_connected;

    // Accumulated current-part inspection (filled by the PMP/INJ screens,
    // flushed by publishInspection() on the summary screen).
    int  m_pmpDefects[INSPECTION_MAX_DEFECTS];
    int  m_pmpCount;
    int  m_injDefects[INSPECTION_MAX_DEFECTS];
    int  m_injCount;
    char m_inspectionNote[128];
    bool m_inspectionPending;

    // Parts inspected since login (reset in setCurrentOperatorIdx).
    int  m_sessionInspected;
};

#endif // MODEL_HPP
