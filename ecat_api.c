/****************=============================================================
 *
 * ERL Spectra EtherCAT Custom Library Source (ecat_api.c)
 * Company: ERL Spectra
 * API Prefix: ecat_
 *
 *=============================================================================*/

#include "ecat_api.h"
#include <ecrt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/****************=============================================================
 * INTERNAL STRUCTURE DEFINITIONS
 * ==========================================================================*/

struct ecat_pdo {
    uint16_t index;
    uint8_t  subindex;
    uint8_t  bit_length;
    unsigned int offset;
    unsigned int bit_position;
    struct ecat_slave *slave;
};

struct ecat_slave {
    uint16_t alias;
    uint16_t position;
    uint32_t vendor_id;
    uint32_t product_code;
    ec_slave_config_t *ec_sc;
    struct ecat_master *master;
    
    struct ecat_pdo **pdos;
    size_t pdo_count;
};

struct ecat_domain {
    ec_domain_t *ec_domain;
    uint8_t *domain_pd;
    size_t domain_size;
    struct ecat_master *master;
};

struct ecat_sdo_req {
    ec_sdo_request_t *ec_sdo;
    struct ecat_slave *slave;
};

struct ecat_master {
    uint32_t master_index;
    ec_master_t *ec_master;
    
    struct ecat_domain **domains;
    size_t domain_count;
    struct ecat_domain *primary_domain;

    struct ecat_slave **slaves;
    size_t slave_count;
};

/****************=============================================================
 * 1. MASTER LIFECYCLE APIS
 * ==========================================================================*/

unsigned int ecat_version_magic(void)
{
    return ecrt_version_magic();
}

ecat_master_t ecat_req_master(uint32_t master_index)
{
    ecat_master_t master = (ecat_master_t)calloc(1, sizeof(struct ecat_master));
    if (!master) {
        fprintf(stderr, "[ecat_Error] Memory allocation failed for Master.\n");
        return NULL;
    }

    master->master_index = master_index;
    master->ec_master = ecrt_request_master(master_index);
    if (!master->ec_master) {
        fprintf(stderr, "[ecat_Error] Failed to request EtherCAT Master index %u.\n", master_index);
        free(master);
        return NULL;
    }

    printf("[ecat_Success] Successfully requested EtherCAT Master index %u.\n", master_index);
    return master;
}

void ecat_rel_master(ecat_master_t master)
{
    if (!master) return;

    if (master->ec_master) {
        ecrt_release_master(master->ec_master);
        master->ec_master = NULL;
    }

    /* Free Domains */
    for (size_t i = 0; i < master->domain_count; i++) {
        if (master->domains[i]) {
            free(master->domains[i]);
        }
    }
    if (master->domains) free(master->domains);

    /* Free Slaves & PDOs */
    for (size_t i = 0; i < master->slave_count; i++) {
        if (master->slaves[i]) {
            for (size_t j = 0; j < master->slaves[i]->pdo_count; j++) {
                if (master->slaves[i]->pdos[j]) {
                    free(master->slaves[i]->pdos[j]);
                }
            }
            if (master->slaves[i]->pdos) free(master->slaves[i]->pdos);
            free(master->slaves[i]);
        }
    }
    if (master->slaves) free(master->slaves);

    printf("[ecat_Info] Released EtherCAT Master index %u.\n", master->master_index);
    free(master);
}

int ecat_reset_master(ecat_master_t master)
{
    if (!master || !master->ec_master) return -1;
    return ecrt_master_reset(master->ec_master);
}

/****************=============================================================
 * 2. DOMAIN MANAGEMENT APIS
 * ==========================================================================*/

ecat_domain_t ecat_create_domain(ecat_master_t master)
{
    if (!master || !master->ec_master) return NULL;

    ecat_domain_t domain = (ecat_domain_t)calloc(1, sizeof(struct ecat_domain));
    if (!domain) return NULL;

    domain->ec_domain = ecrt_master_create_domain(master->ec_master);
    if (!domain->ec_domain) {
        fprintf(stderr, "[ecat_Error] Failed to create EtherCAT domain.\n");
        free(domain);
        return NULL;
    }

    domain->master = master;

    /* Reallocate Master's Domain List */
    ecat_domain_t *new_domains = (ecat_domain_t *)realloc(master->domains, sizeof(ecat_domain_t) * (master->domain_count + 1));
    if (!new_domains) {
        free(domain);
        return NULL;
    }
    master->domains = new_domains;
    master->domains[master->domain_count++] = domain;

    if (!master->primary_domain) {
        master->primary_domain = domain;
    }

    printf("[ecat_Success] Successfully created EtherCAT domain.\n");
    return domain;
}

size_t ecat_domain_size(ecat_domain_t domain)
{
    if (!domain || !domain->ec_domain) return 0;
    domain->domain_size = ecrt_domain_size(domain->ec_domain);
    return domain->domain_size;
}

void ecat_domain_state(ecat_domain_t domain, ecat_domain_state_t *state)
{
    if (!domain || !domain->ec_domain || !state) return;
    ec_domain_state_t ec_state;
    ecrt_domain_state(domain->ec_domain, &ec_state);
    state->working_counter   = ec_state.working_counter;
    state->wc_state          = ec_state.wc_state;
    state->redundancy_active = ec_state.redundancy_active;
}

/****************=============================================================
 * 3. MASTER REAL-TIME & SYNCHRONIZATION APIS
 * ==========================================================================*/

int ecat_activate(ecat_master_t master)
{
    if (!master || !master->ec_master) return -1;

    printf("[ecat_Info] Activating EtherCAT Master index %u...\n", master->master_index);
    int ret = ecrt_master_activate(master->ec_master);
    if (ret < 0) {
        fprintf(stderr, "[ecat_Error] Failed to activate EtherCAT master (%d).\n", ret);
        return ret;
    }

    /* Retrieve Domain Process Memory Pointer */
    if (master->primary_domain && master->primary_domain->ec_domain) {
        master->primary_domain->domain_pd = ecrt_domain_data(master->primary_domain->ec_domain);
        if (!master->primary_domain->domain_pd) {
            fprintf(stderr, "[ecat_Error] Failed to retrieve domain process data memory pointer.\n");
            return -1;
        }
        master->primary_domain->domain_size = ecrt_domain_size(master->primary_domain->ec_domain);
    }

    printf("[ecat_Success] EtherCAT Master index %u activated successfully.\n", master->master_index);
    return 0;
}

void ecat_deactivate(ecat_master_t master)
{
    if (!master || !master->ec_master) return;
    ecrt_master_deactivate(master->ec_master);
}

void ecat_recv(ecat_master_t master)
{
    if (!master || !master->ec_master) return;

    ecrt_master_receive(master->ec_master);

    if (master->primary_domain && master->primary_domain->ec_domain) {
        ecrt_domain_process(master->primary_domain->ec_domain);
    }
}

void ecat_send(ecat_master_t master)
{
    if (!master || !master->ec_master) return;

    if (master->primary_domain && master->primary_domain->ec_domain) {
        ecrt_domain_queue(master->primary_domain->ec_domain);
    }

    ecrt_master_send(master->ec_master);
}

void ecat_get_state(ecat_master_t master, ecat_master_state_t *state)
{
    if (!master || !master->ec_master || !state) return;
    ec_master_state_t ec_state;
    ecrt_master_state(master->ec_master, &ec_state);
    state->slaves_responding = ec_state.slaves_responding;
    state->link_up           = ec_state.link_up;
    state->al_states         = ec_state.al_states;
}

void ecat_app_time(ecat_master_t master, uint64_t time_ns)
{
    if (!master || !master->ec_master) return;
    ecrt_master_application_time(master->ec_master, time_ns);
}

void ecat_sync_ref_clock(ecat_master_t master)
{
    if (!master || !master->ec_master) return;
    ecrt_master_sync_reference_clock(master->ec_master);
}

void ecat_sync_slave_clocks(ecat_master_t master)
{
    if (!master || !master->ec_master) return;
    ecrt_master_sync_slave_clocks(master->ec_master);
}

/****************=============================================================
 * 4. SLAVE & PDO CONFIGURATION APIS
 * ==========================================================================*/

ecat_slave_t ecat_slave_config(ecat_master_t master, uint16_t alias, uint16_t position, uint32_t vendor_id, uint32_t product_code)
{
    if (!master || !master->ec_master) return NULL;

    ecat_slave_t slave = (ecat_slave_t)calloc(1, sizeof(struct ecat_slave));
    if (!slave) return NULL;

    slave->alias = alias;
    slave->position = position;
    slave->vendor_id = vendor_id;
    slave->product_code = product_code;
    slave->master = master;

    slave->ec_sc = ecrt_master_slave_config(master->ec_master, alias, position, vendor_id, product_code);
    if (!slave->ec_sc) {
        fprintf(stderr, "[ecat_Error] Failed to configure slave at position %u (Vendor: 0x%08X, Product: 0x%08X).\n",
                position, vendor_id, product_code);
        free(slave);
        return NULL;
    }

    ecat_slave_t *new_slaves = (ecat_slave_t *)realloc(master->slaves, sizeof(ecat_slave_t) * (master->slave_count + 1));
    if (!new_slaves) {
        free(slave);
        return NULL;
    }
    master->slaves = new_slaves;
    master->slaves[master->slave_count++] = slave;

    printf("[ecat_Success] Successfully configured slave at position %u.\n", position);
    return slave;
}

int ecat_slave_config_dc(ecat_slave_t slave, uint16_t assign_activate, uint32_t sync0_cycle, int32_t sync0_shift, uint32_t sync1_cycle, int32_t sync1_shift)
{
    if (!slave || !slave->ec_sc) return -1;
    ecrt_slave_config_dc(slave->ec_sc, assign_activate, sync0_cycle, sync0_shift, sync1_cycle, sync1_shift);
    return 0;
}

int ecat_slave_config_watchdog(ecat_slave_t slave, uint16_t watchdog_divider, uint16_t watchdog_intervals)
{
    if (!slave || !slave->ec_sc) return -1;
    ecrt_slave_config_watchdog(slave->ec_sc, watchdog_divider, watchdog_intervals);
    return 0;
}

ecat_pdo_t ecat_pdo_reg(ecat_slave_t slave, uint16_t index, uint8_t subindex, uint8_t bit_length)
{
    if (!slave || !slave->ec_sc || !slave->master || !slave->master->primary_domain) {
        fprintf(stderr, "[ecat_Error] Cannot register PDO: Invalid slave or master context.\n");
        return NULL;
    }

    ecat_pdo_t pdo = (ecat_pdo_t)calloc(1, sizeof(struct ecat_pdo));
    if (!pdo) return NULL;

    pdo->index = index;
    pdo->subindex = subindex;
    pdo->bit_length = bit_length;
    pdo->slave = slave;

    /* IgH ecrt_slave_config_reg_pdo_entry takes 5 arguments and returns byte offset */
    int offset = ecrt_slave_config_reg_pdo_entry(
        slave->ec_sc,
        index,
        subindex,
        slave->master->primary_domain->ec_domain,
        &pdo->bit_position
    );

    if (offset < 0) {
        fprintf(stderr, "[ecat_Error] Failed to register PDO entry 0x%04X:%02X for slave at position %u.\n",
                index, subindex, slave->position);
        free(pdo);
        return NULL;
    }

    pdo->offset = (unsigned int)offset;

    ecat_pdo_t *new_pdos = (ecat_pdo_t *)realloc(slave->pdos, sizeof(ecat_pdo_t) * (slave->pdo_count + 1));
    if (!new_pdos) {
        free(pdo);
        return NULL;
    }
    slave->pdos = new_pdos;
    slave->pdos[slave->pdo_count++] = pdo;

    printf("[ecat_Success] Registered PDO 0x%04X:%02X for slave %u (Offset: %u bytes).\n",
           index, subindex, slave->position, pdo->offset);
    return pdo;
}

ecat_slave_state_t ecat_slave_get_state(ecat_slave_t slave)
{
    if (!slave || !slave->ec_sc) return ECAT_SLAVE_STATE_UNKNOWN;
    ec_slave_config_state_t state;
    ecrt_slave_config_state(slave->ec_sc, &state);
    return (ecat_slave_state_t)state.al_state;
}

/****************=============================================================
 * 5. PROCESS DATA READ & WRITE ACCESSORS (`ecat_rd_` and `ecat_wr_`)
 * ==========================================================================*/

uint8_t ecat_rd_u8(ecat_pdo_t pdo)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return 0;
    return EC_READ_U8(pdo->slave->master->primary_domain->domain_pd + pdo->offset);
}

uint16_t ecat_rd_u16(ecat_pdo_t pdo)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return 0;
    return EC_READ_U16(pdo->slave->master->primary_domain->domain_pd + pdo->offset);
}

uint32_t ecat_rd_u32(ecat_pdo_t pdo)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return 0;
    return EC_READ_U32(pdo->slave->master->primary_domain->domain_pd + pdo->offset);
}

int8_t ecat_rd_s8(ecat_pdo_t pdo)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return 0;
    return EC_READ_S8(pdo->slave->master->primary_domain->domain_pd + pdo->offset);
}

int16_t ecat_rd_s16(ecat_pdo_t pdo)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return 0;
    return EC_READ_S16(pdo->slave->master->primary_domain->domain_pd + pdo->offset);
}

int32_t ecat_rd_s32(ecat_pdo_t pdo)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return 0;
    return EC_READ_S32(pdo->slave->master->primary_domain->domain_pd + pdo->offset);
}

bool ecat_rd_bit(ecat_pdo_t pdo)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return false;
    return (bool)EC_READ_BIT(pdo->slave->master->primary_domain->domain_pd + pdo->offset, pdo->bit_position);
}

void ecat_wr_u8(ecat_pdo_t pdo, uint8_t value)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return;
    EC_WRITE_U8(pdo->slave->master->primary_domain->domain_pd + pdo->offset, value);
}

void ecat_wr_u16(ecat_pdo_t pdo, uint16_t value)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return;
    EC_WRITE_U16(pdo->slave->master->primary_domain->domain_pd + pdo->offset, value);
}

void ecat_wr_u32(ecat_pdo_t pdo, uint32_t value)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return;
    EC_WRITE_U32(pdo->slave->master->primary_domain->domain_pd + pdo->offset, value);
}

void ecat_wr_s8(ecat_pdo_t pdo, int8_t value)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return;
    EC_WRITE_S8(pdo->slave->master->primary_domain->domain_pd + pdo->offset, value);
}

void ecat_wr_s16(ecat_pdo_t pdo, int16_t value)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return;
    EC_WRITE_S16(pdo->slave->master->primary_domain->domain_pd + pdo->offset, value);
}

void ecat_wr_s32(ecat_pdo_t pdo, int32_t value)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return;
    EC_WRITE_S32(pdo->slave->master->primary_domain->domain_pd + pdo->offset, value);
}

void ecat_wr_bit(ecat_pdo_t pdo, bool value)
{
    if (!pdo || !pdo->slave || !pdo->slave->master || !pdo->slave->master->primary_domain || !pdo->slave->master->primary_domain->domain_pd) return;
    EC_WRITE_BIT(pdo->slave->master->primary_domain->domain_pd + pdo->offset, pdo->bit_position, value ? 1 : 0);
}

/****************=============================================================
 * 6. CoE SDO SERVICES
 * ==========================================================================*/

ecat_sdo_req_t ecat_sdo_create(ecat_slave_t slave, uint16_t index, uint8_t subindex, size_t max_size)
{
    if (!slave || !slave->ec_sc) return NULL;
    ecat_sdo_req_t sdo = (ecat_sdo_req_t)calloc(1, sizeof(struct ecat_sdo_req));
    if (!sdo) return NULL;

    sdo->ec_sdo = ecrt_slave_config_create_sdo_request(slave->ec_sc, index, subindex, max_size);
    if (!sdo->ec_sdo) {
        free(sdo);
        return NULL;
    }
    sdo->slave = slave;
    return sdo;
}

int ecat_sdo_wr_async(ecat_sdo_req_t sdo, const void *data, size_t size)
{
    if (!sdo || !sdo->ec_sdo || !data) return -1;
    uint8_t *sdo_buf = ecrt_sdo_request_data(sdo->ec_sdo);
    if (!sdo_buf) return -1;
    memcpy(sdo_buf, data, size);
    ecrt_sdo_request_write(sdo->ec_sdo);
    return 0;
}

int ecat_sdo_rd_async(ecat_sdo_req_t sdo)
{
    if (!sdo || !sdo->ec_sdo) return -1;
    ecrt_sdo_request_read(sdo->ec_sdo);
    return 0;
}

ecat_sdo_state_t ecat_sdo_get_state(ecat_sdo_req_t sdo)
{
    if (!sdo || !sdo->ec_sdo) return ECAT_SDO_ERROR;
    ec_request_state_t state = ecrt_sdo_request_state(sdo->ec_sdo);
    switch (state) {
        case EC_REQUEST_UNUSED:
        case EC_REQUEST_SUCCESS: return ECAT_SDO_SUCCESS;
        case EC_REQUEST_BUSY:    return ECAT_SDO_BUSY;
        case EC_REQUEST_ERROR:   
        default:                 return ECAT_SDO_ERROR;
    }
}

int ecat_sdo_get_data(ecat_sdo_req_t sdo, void *out_data, size_t *out_size)
{
    if (!sdo || !sdo->ec_sdo || !out_data) return -1;
    uint8_t *sdo_buf = ecrt_sdo_request_data(sdo->ec_sdo);
    size_t len = ecrt_sdo_request_data_size(sdo->ec_sdo);
    if (!sdo_buf) return -1;
    memcpy(out_data, sdo_buf, len);
    if (out_size) *out_size = len;
    return 0;
}

int ecat_sdo_download(ecat_master_t master, uint16_t slave_pos, uint16_t index, uint8_t subindex, const void *data, size_t size, uint32_t *abort_code)
{
    if (!master || !master->ec_master || !data) return -1;
    return ecrt_master_sdo_download(master->ec_master, slave_pos, index, subindex, (uint8_t *)data, size, abort_code);
}

int ecat_sdo_upload(ecat_master_t master, uint16_t slave_pos, uint16_t index, uint8_t subindex, void *target_buf, size_t target_size, size_t *result_size, uint32_t *abort_code)
{
    if (!master || !master->ec_master || !target_buf) return -1;
    return ecrt_master_sdo_upload(master->ec_master, slave_pos, index, subindex, (uint8_t *)target_buf, target_size, result_size, abort_code);
}

int ecat_sdo_wr_blocking(ecat_slave_t slave, uint16_t index, uint8_t subindex, const void *data, size_t size, uint32_t timeout_ms)
{
    if (!slave || !slave->master) return -1;
    ecat_sdo_req_t sdo = ecat_sdo_create(slave, index, subindex, size);
    if (!sdo) return -1;

    if (ecat_sdo_wr_async(sdo, data, size) < 0) {
        return -1;
    }

    uint32_t elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
        ecat_recv(slave->master);
        ecat_sdo_state_t state = ecat_sdo_get_state(sdo);
        ecat_send(slave->master);

        if (state == ECAT_SDO_SUCCESS) return 0;
        if (state == ECAT_SDO_ERROR) return -1;

        usleep(1000);
        elapsed_ms++;
    }
    return -2; // Timeout
}
