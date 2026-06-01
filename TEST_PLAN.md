# Host-Side Tests for Domain Modules

This document outlines the approach for implementing host-side tests for the domain modules in the QC Node firmware.

## Modules to Test

1. defect_config.c
2. operator_list.c
3. session.c
4. pin_hash.c
5. ui_data_bridge.c
6. inspection_queue.c
7. persistent_inspection_queue.c

## Testing Approach

### 1. Test Framework
- Use a standard unit testing framework (e.g., Unity, CMocka, or Google Test)
- Build tests for Linux/macOS host
- Use mock implementations for hardware-dependent functions

### 2. Mock Requirements
- Mock persistent storage functions (config_store_*)
- Mock OSPI functions (BSP_OSPI_NOR_*)
- Mock FreeRTOS functions where needed
- Mock printf for output capture

### 3. Test Cases for Each Module

#### defect_config.c
- Test initialization clears all data
- Test setting and retrieving products
- Test setting and retrieving defect types
- Test validation of defect types
- Test edge cases (empty arrays, maximum sizes, invalid indices)

#### operator_list.c
- Test initialization clears all data
- Test setting and retrieving operators
- Test PIN validation (valid and invalid PINs)
- Test edge cases (empty arrays, maximum sizes, invalid indices)

#### session.c
- Test session start/end functionality
- Test defect counting
- Test session state queries
- Test behavior when no session is active

#### pin_hash.c
- Test PIN hashing and verification
- Test that different PINs produce different hashes
- Test that same PIN with same salt produces same hash
- Test verification failure with wrong PIN

#### ui_data_bridge.c
- Test that operator list updates are passed to Model
- Test that product list updates are passed to Model
- Test that defect type updates are passed to Model

#### inspection_queue.c
- Test queue initialization
- Test sending and receiving messages
- Test queue full behavior
- Test queue empty behavior

#### persistent_inspection_queue.c
- Test queue initialization
- Test storing and retrieving messages
- Test queue full behavior
- Test queue empty behavior
- Test persistence across power cycle (simulated)
- Test CRC error detection and recovery

### 4. Continuous Integration
- Integrate tests into build system
- Run tests automatically on commit
- Require passing tests for merge approval

### 5. Coverage Goals
- Aim for >80% line coverage on all domain modules
- Test all error paths and edge cases
- Test concurrent access scenarios where applicable

## Implementation Steps

1. Set up test framework in the tests/ directory
2. Create mock implementations for required functions
3. Implement test cases for each module as outlined above
4. Integrate with build system
5. Set up CI/CD pipeline for automated testing

## Notes

- The pin_hash.c module would need actual cryptographic implementations for production
- For host testing, simplified hash functions can be used
- Persistent storage tests should simulate power cycles by re-initializing the queue
- Thread safety should be tested where applicable