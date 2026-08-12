#ifndef ECAT_API_H
#define ECAT_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/****************=============================================================
 * OPAQUE HANDLES & TYPES
 * ==========================================================================*/

/**
 * @brief Opaque handle representing an EtherCAT Master instance.
 */
typedef struct ecat_master* ecat_master_t;

/**
 * @brief Opaque handle representing a Process Data Domain for data exchange.
 */
typedef struct ecat_domain* ecat_domain_t;

/**
 * @brief Opaque handle representing a configured EtherCAT Slave terminal.
 */
typedef struct ecat_slave* ecat_slave_t;

/**
 * @brief Opaque handle representing a registered Process Data Object (PDO).
 */
typedef struct ecat_pdo* ecat_pdo_t;

/**
 * @brief Opaque handle representing an Asynchronous Service Data Object (SDO) Request.
 */
typedef struct ecat_sdo_req* ecat_sdo_req_t;

/**
 * @brief Asynchronous CoE SDO Request Execution States.
 */
typedef enum {
    ECAT_SDO_IDLE = 0,    /**< SDO request is uninitialized or idle */
    ECAT_SDO_BUSY,        /**< SDO transfer is currently in progress on the bus */
    ECAT_SDO_SUCCESS,     /**< SDO transfer completed successfully */
    ECAT_SDO_ERROR        /**< SDO transfer failed or timed out */
} ecat_sdo_state_t;

/**
 * @brief EtherCAT Slave Application Layer (AL) States.
 */
typedef enum {
    ECAT_SLAVE_STATE_UNKNOWN = 0, /**< Slave state is unknown or invalid */
    ECAT_SLAVE_STATE_INIT    = 1, /**< Slave is in INIT state */
    ECAT_SLAVE_STATE_PREOP   = 2, /**< Slave is in PRE-OPERATIONAL state (SDO active, no PDO) */
    ECAT_SLAVE_STATE_BOOT    = 3, /**< Slave is in BOOTSTRAP mode (Firmware update) */
    ECAT_SLAVE_STATE_SAFEOP  = 4, /**< Slave is in SAFE-OPERATIONAL state (Inputs active, Outputs silent) */
    ECAT_SLAVE_STATE_OP      = 8  /**< Slave is in OPERATIONAL state (Inputs & Outputs fully active) */
} ecat_slave_state_t;

/**
 * @brief Master Bus Status Information.
 */
typedef struct {
    uint32_t slaves_responding; /**< Number of slaves responding on the physical network */
    bool     link_up;           /**< Physical Ethernet link state (true = Link UP) */
    uint32_t al_states;         /**< Combined Bitmask of Application Layer states */
} ecat_master_state_t;

/**
 * @brief Process Data Domain Working Counter State.
 */
typedef struct {
    uint32_t working_counter;   /**< Current Working Counter (WCC) value returned by frames */
    uint32_t wc_state;          /**< 0 = Zero, 1 = Incomplete, 2 = Complete match */
    bool     redundancy_active; /**< Cable redundancy status (true = Redundant loop active) */
} ecat_domain_state_t;


/****************=============================================================
 * 1. MASTER LIFECYCLE & CORE APIS
 * ==========================================================================*/

/**
 * @brief Retrieve the kernel driver magic version number.
 * @return Unsigned integer representing IgH EtherCAT version magic.
 */
unsigned int ecat_version_magic(void);

/**
 * @brief Request exclusive ownership of an EtherCAT Master instance.
 * @param master_index Index of the master (0 for primary NIC / /dev/EtherCAT0).
 * @return Valid handle ecat_master_t on success, or NULL on error.
 * @note Must be called before any slave or domain configuration.
 */
ecat_master_t ecat_req_master(uint32_t master_index);

/**
 * @brief Release EtherCAT master resources and safely free allocated memory.
 * @param master Master handle to release.
 * @note Call at program exit to return master control back to OS kernel.
 */
void ecat_rel_master(ecat_master_t master);

/**
 * @brief Reset master state machine to retry configuring slaves after a bus error.
 * @param master Master handle.
 * @return 0 on success, or negative error code on failure.
 */
int ecat_reset_master(ecat_master_t master);


/****************=============================================================
 * 2. DOMAIN MANAGEMENT APIS
 * ==========================================================================*/

/**
 * @brief Create a Process Data Exchange Domain.
 * @param master Master handle.
 * @return Handle ecat_domain_t on success, or NULL on error.
 * @note Domains bundle all cyclic I/O variables into single Ethernet frames.
 */
ecat_domain_t ecat_create_domain(ecat_master_t master);

/**
 * @brief Get the total byte size of domain process memory payload.
 * @param domain Domain handle.
 * @return Total payload size in bytes.
 */
size_t ecat_domain_size(ecat_domain_t domain);

/**
 * @brief Query working counter and domain status.
 * @param domain Domain handle.
 * @param state Pointer to ecat_domain_state_t structure to receive metrics.
 */
void ecat_domain_state(ecat_domain_t domain, ecat_domain_state_t *state);


/****************=============================================================
 * 3. MASTER REAL-TIME & SYNCHRONIZATION APIS
 * ==========================================================================*/

/**
 * @brief Lock configuration phase and activate Master into real-time operational mode.
 * @param master Master handle.
 * @return 0 on success, or negative error code on failure.
 * @note Call after configuring all slaves and registering all PDO entries.
 */
int ecat_activate(ecat_master_t master);

/**
 * @brief Deactivate master real-time mode and return master to configuration state.
 * @param master Master handle.
 */
void ecat_deactivate(ecat_master_t master);

/**
 * @brief Receive incoming Ethernet frames from NIC and process domain input data.
 * @param master Master handle.
 * @note MUST be called at the VERY START of every cyclic loop iteration.
 */
void ecat_recv(ecat_master_t master);

/**
 * @brief Queue updated domain output data and broadcast Ethernet frames out of NIC.
 * @param master Master handle.
 * @note MUST be called at the VERY END of every cyclic loop iteration.
 */
void ecat_send(ecat_master_t master);

/**
 * @brief Query current bus master health, responding slave count, and link state.
 * @param master Master handle.
 * @param state Pointer to ecat_master_state_t structure to receive state info.
 */
void ecat_get_state(ecat_master_t master, ecat_master_state_t *state);

/**
 * @brief Set current master application time in nanoseconds for Distributed Clocks (DC).
 * @param master Master handle.
 * @param time_ns System monotonic time in nanoseconds.
 */
void ecat_app_time(ecat_master_t master, uint64_t time_ns);

/**
 * @brief Synchronize master reference clock with slave hardware clocks (DC).
 * @param master Master handle.
 */
void ecat_sync_ref_clock(ecat_master_t master);

/**
 * @brief Synchronize all slave hardware clocks with reference clock (DC).
 * @param master Master handle.
 */
void ecat_sync_slave_clocks(ecat_master_t master);


/****************=============================================================
 * 4. DYNAMIC SLAVE & PDO CONFIGURATION APIS
 * ==========================================================================*/

/**
 * @brief Register and configure an EtherCAT Slave on the bus.
 * @param master Master handle.
 * @param alias Slave ring alias (0 if positional addressing is used).
 * @param position Physical bus position (0 = Coupler, 1 = Terminal 1, etc.).
 * @param vendor_id Vendor Identification number (e.g., Beckhoff = 0x00000002).
 * @param product_code Hardware Product Code (e.g., EL5101 = 0x13ed3052).
 * @return Handle ecat_slave_t on success, or NULL on error.
 */
ecat_slave_t ecat_slave_config(ecat_master_t master, uint16_t alias, uint16_t position, uint32_t vendor_id, uint32_t product_code);

/**
 * @brief Configure Distributed Clocks (DC) parameters for a slave terminal.
 * @param slave Slave handle.
 * @param assign_activate Sync Manager activation word (e.g. 0x0300).
 * @param sync0_cycle SYNC0 cycle time in nanoseconds.
 * @param sync0_shift SYNC0 shift time in nanoseconds.
 * @param sync1_cycle SYNC1 cycle time in nanoseconds.
 * @param sync1_shift SYNC1 shift time in nanoseconds.
 * @return 0 on success, or negative error code.
 */
int ecat_slave_config_dc(ecat_slave_t slave, uint16_t assign_activate, uint32_t sync0_cycle, int32_t sync0_shift, uint32_t sync1_cycle, int32_t sync1_shift);

/**
 * @brief Configure Watchdog timer for output channels on a slave.
 * @param slave Slave handle.
 * @param watchdog_divider Watchdog prescaler divider.
 * @param watchdog_intervals Watchdog interval count.
 * @return 0 on success, or negative error code.
 */
int ecat_slave_config_watchdog(ecat_slave_t slave, uint16_t watchdog_divider, uint16_t watchdog_intervals);

/**
 * @brief Register a PDO entry channel into the primary process data domain.
 * @param slave Slave handle.
 * @param index Object dictionary index (e.g., 0x6000 for Inputs, 0x7000 for Outputs).
 * @param subindex Object dictionary subindex (e.g., 0x01, 0x02).
 * @param bit_length Width of channel in bits (8, 16, 32, etc.).
 * @return Handle ecat_pdo_t on success, or NULL on registration failure.
 * @note This handle is passed directly to ecat_rd_* and ecat_wr_* inside loop.
 */
ecat_pdo_t ecat_pdo_reg(ecat_slave_t slave, uint16_t index, uint8_t subindex, uint8_t bit_length);

/**
 * @brief Query current Application Layer (AL) state of a slave (INIT, PREOP, SAFEOP, OP).
 * @param slave Slave handle.
 * @return ecat_slave_state_t enum value.
 */
ecat_slave_state_t ecat_slave_get_state(ecat_slave_t slave);


/****************=============================================================
 * 5. PROCESS DATA READ & WRITE ACCESSORS (`ecat_rd_` and `ecat_wr_`)
 * ==========================================================================*/

/** @brief Read 8-bit unsigned integer from PDO channel. */
uint8_t  ecat_rd_u8(ecat_pdo_t pdo);

/** @brief Read 16-bit unsigned integer from PDO channel (e.g. Encoder count). */
uint16_t ecat_rd_u16(ecat_pdo_t pdo);

/** @brief Read 32-bit unsigned integer from PDO channel. */
uint32_t ecat_rd_u32(ecat_pdo_t pdo);

/** @brief Read 8-bit signed integer from PDO channel. */
int8_t   ecat_rd_s8(ecat_pdo_t pdo);

/** @brief Read 16-bit signed integer from PDO channel. */
int16_t  ecat_rd_s16(ecat_pdo_t pdo);

/** @brief Read 32-bit signed integer from PDO channel (e.g. LVDT / Strain value). */
int32_t  ecat_rd_s32(ecat_pdo_t pdo);

/** @brief Read 1-bit boolean flag from PDO channel (Digital Input / Status Bit). */
bool     ecat_rd_bit(ecat_pdo_t pdo);

/** @brief Write 8-bit unsigned integer to PDO channel (Digital Output byte). */
void ecat_wr_u8(ecat_pdo_t pdo, uint8_t value);

/** @brief Write 16-bit unsigned integer to PDO channel (Analog Output). */
void ecat_wr_u16(ecat_pdo_t pdo, uint16_t value);

/** @brief Write 32-bit unsigned integer to PDO channel. */
void ecat_wr_u32(ecat_pdo_t pdo, uint32_t value);

/** @brief Write 8-bit signed integer to PDO channel. */
void ecat_wr_s8(ecat_pdo_t pdo, int8_t value);

/** @brief Write 16-bit signed integer to PDO channel. */
void ecat_wr_s16(ecat_pdo_t pdo, int16_t value);

/** @brief Write 32-bit signed integer to PDO channel (Set position value). */
void ecat_wr_s32(ecat_pdo_t pdo, int32_t value);

/** @brief Write 1-bit boolean state to PDO channel (Digital Output ON/OFF). */
void ecat_wr_bit(ecat_pdo_t pdo, bool value);


/****************=============================================================
 * 6. CoE SDO PARAMETER SERVICES
 * ==========================================================================*/

/**
 * @brief Create an asynchronous CoE SDO Request handle.
 * @param slave Slave handle.
 * @param index SDO dictionary object index.
 * @param subindex SDO dictionary object subindex.
 * @param max_size Maximum payload data size in bytes.
 * @return Handle ecat_sdo_req_t on success, or NULL.
 */
ecat_sdo_req_t ecat_sdo_create(ecat_slave_t slave, uint16_t index, uint8_t subindex, size_t max_size);

/**
 * @brief Queue an asynchronous SDO Write transfer.
 * @param sdo SDO request handle.
 * @param data Pointer to buffer containing value to write.
 * @param size Length of data buffer in bytes.
 * @return 0 on success, or negative error code.
 */
int ecat_sdo_wr_async(ecat_sdo_req_t sdo, const void *data, size_t size);

/**
 * @brief Queue an asynchronous SDO Read transfer.
 * @param sdo SDO request handle.
 * @return 0 on success, or negative error code.
 */
int ecat_sdo_rd_async(ecat_sdo_req_t sdo);

/**
 * @brief Query current transfer status of an asynchronous SDO request.
 * @param sdo SDO request handle.
 * @return ecat_sdo_state_t enum (BUSY, SUCCESS, ERROR).
 */
ecat_sdo_state_t ecat_sdo_get_state(ecat_sdo_req_t sdo);

/**
 * @brief Retrieve data payload returned by completed asynchronous SDO read.
 * @param sdo SDO request handle.
 * @param out_data Target buffer to receive read payload.
 * @param out_size Pointer to receive byte count read.
 * @return 0 on success, or negative error code.
 */
int ecat_sdo_get_data(ecat_sdo_req_t sdo, void *out_data, size_t *out_size);

/**
 * @brief Download (Write) SDO parameter to slave synchronously before master activation.
 * @param master Master handle.
 * @param slave_pos Bus position of slave.
 * @param index SDO dictionary object index.
 * @param subindex SDO dictionary object subindex.
 * @param data Pointer to buffer containing value to download.
 * @param size Length of data in bytes.
 * @param abort_code Pointer to receive 32-bit CoE abort code on failure.
 * @return 0 on success, or negative error code.
 */
int ecat_sdo_download(ecat_master_t master, uint16_t slave_pos, uint16_t index, uint8_t subindex, const void *data, size_t size, uint32_t *abort_code);

/**
 * @brief Upload (Read) SDO parameter from slave synchronously before master activation.
 * @param master Master handle.
 * @param slave_pos Bus position of slave.
 * @param index SDO dictionary object index.
 * @param subindex SDO dictionary object subindex.
 * @param target_buf Target buffer to receive data.
 * @param target_size Capacity of target buffer in bytes.
 * @param result_size Pointer to receive actual bytes uploaded.
 * @param abort_code Pointer to receive 32-bit CoE abort code on failure.
 * @return 0 on success, or negative error code.
 */
int ecat_sdo_upload(ecat_master_t master, uint16_t slave_pos, uint16_t index, uint8_t subindex, void *target_buf, size_t target_size, size_t *result_size, uint32_t *abort_code);

/**
 * @brief Perform a blocking write to an SDO parameter with a specified timeout.
 * @param slave Slave handle.
 * @param index SDO dictionary object index.
 * @param subindex SDO dictionary object subindex.
 * @param data Pointer to buffer containing value to write.
 * @param size Length of data buffer in bytes.
 * @param timeout_ms Timeout limit in milliseconds.
 * @return 0 on success, -1 on write error, -2 on timeout.
 */
int ecat_sdo_wr_blocking(ecat_slave_t slave, uint16_t index, uint8_t subindex, const void *data, size_t size, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* ECAT_API_H */
