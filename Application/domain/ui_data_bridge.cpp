#include "domain/ui_data_bridge.h"
#include <gui/model/Model.hpp>
#include "FreeRTOS.h"
#include "semphr.h"

// Forward declaration of the function to get the Model instance
// In a real implementation, this would access the actual Model singleton
// For this example, we'll assume there's a way to get the Model instance
extern "C" {
    Model* getModelInstance();
}

static StaticSemaphore_t s_modelMutexBuffer;
static SemaphoreHandle_t s_modelMutex = NULL;

static SemaphoreHandle_t getModelMutex()
{
    if (s_modelMutex == NULL)
    {
        taskENTER_CRITICAL();
        if (s_modelMutex == NULL)
        {
            s_modelMutex = xSemaphoreCreateMutexStatic(&s_modelMutexBuffer);
        }
        taskEXIT_CRITICAL();
    }

    return s_modelMutex;
}

static void lockModel()
{
    SemaphoreHandle_t mutex = getModelMutex();
    if (mutex != NULL)
    {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
}

static void unlockModel()
{
    if (s_modelMutex != NULL)
    {
        xSemaphoreGive(s_modelMutex);
    }
}

void ui_bridge_set_operators(const operator_entry_t* list, int count)
{
    if (!list || count <= 0)
        return;
    
    lockModel();
    
    // Get the Model instance and update it
    Model* model = getModelInstance();
    if (model)
    {
        model->setOperators(list, count);
        // In a real implementation, we would also signal that operators have been updated
        // using event groups or a similar mechanism
    }

    unlockModel();
}

void ui_bridge_set_products(const product_entry_t* list, int count)
{
    if (!list || count <= 0)
        return;
    
    lockModel();
    
    // Get the Model instance and update it
    Model* model = getModelInstance();
    if (model)
    {
        model->setProducts(list, count);
        // Signal that products have been updated
    }

    unlockModel();
}

void ui_bridge_set_defect_types(int product_id, int category,
                               const defect_type_t* list, int count)
{
    if (!list || count <= 0)
        return;
    
    lockModel();
    
    // Get the Model instance and update it
    Model* model = getModelInstance();
    if (model)
    {
        model->setDefectTypes(product_id, category, list, count);
        // Signal that defect types have been updated
    }

    unlockModel();
}
