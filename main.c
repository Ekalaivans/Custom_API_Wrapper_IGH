/****************=============================================================
 *
 * Real-Hardware Diagnostic Test using Concise `ecat_` APIs
 * Company: ERL Spectra
 *
 *=============================================================================*/

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "ecat_api.h"

#define VENDOR_BECKHOFF 0x00000002

#define PRODUCT_EK1100  0x044c2c52
#define PRODUCT_EL5101  0x13ed3052
#define PRODUCT_EL5072  0x13d03052
#define PRODUCT_EL3356  0x0d1c3052

static bool running = true;

void signal_handler(int sig) {
    (void)sig;
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("===============================================================\n");
    printf(" ERL Spectra EtherCAT Library - Hardware Diagnostic Test\n");
    printf(" Library Version Magic: 0x%08X\n", ecat_version_magic());
    printf("===============================================================\n\n");

    /* TEST 1: Request Master 0 */
    printf("[TEST 1] Requesting Master 0...\n");
    ecat_master_t master = ecat_req_master(0);
    if (!master) {
        fprintf(stderr, "FAILED: Could not acquire Master 0.\n");
        return -1;
    }
    printf(" -> PASSED: Master 0 acquired.\n\n");

    /* TEST 2: Create Domain */
    printf("[TEST 2] Creating Process Data Domain...\n");
    ecat_domain_t domain = ecat_create_domain(master);
    if (!domain) {
        fprintf(stderr, "FAILED: Could not create Domain.\n");
        ecat_rel_master(master);
        return -1;
    }
    printf(" -> PASSED: Domain created.\n\n");

    /* TEST 3: Configure Slaves */
    printf("[TEST 3] Configuring Connected Slaves...\n");
    ecat_slave_t ek1100 = ecat_slave_config(master, 0, 0, VENDOR_BECKHOFF, PRODUCT_EK1100);
    ecat_slave_t el5101 = ecat_slave_config(master, 0, 1, VENDOR_BECKHOFF, PRODUCT_EL5101);
    ecat_slave_t el5072 = ecat_slave_config(master, 0, 2, VENDOR_BECKHOFF, PRODUCT_EL5072);
    ecat_slave_t el3356 = ecat_slave_config(master, 0, 3, VENDOR_BECKHOFF, PRODUCT_EL3356);

    if (!ek1100 || !el5101 || !el5072 || !el3356) {
        fprintf(stderr, "FAILED: One or more slave configurations failed.\n");
        ecat_rel_master(master);
        return -1;
    }
    printf(" -> PASSED: All Slaves Configured.\n\n");

    /* TEST 4: Register PDO Entries (`ecat_pdo_reg`) */
    printf("[TEST 4] Registering PDO Channels using ecat_pdo_reg...\n");
    
    // EL5101 Encoder Counter (0x6000:02, 16-bit)
    ecat_pdo_t pdo_encoder = ecat_pdo_reg(el5101, 0x6000, 0x02, 16);
    
    // EL5072 LVDT Position Value (0x6001:01, 32-bit)
    ecat_pdo_t pdo_lvdt = ecat_pdo_reg(el5072, 0x6001, 0x01, 32);
    
    // EL3356 Strain/Resistor Value (0x6000:11, 32-bit)
    ecat_pdo_t pdo_strain = ecat_pdo_reg(el3356, 0x6000, 0x11, 32);

    if (!pdo_encoder || !pdo_lvdt || !pdo_strain) {
        fprintf(stderr, "FAILED: PDO registration failed.\n");
        ecat_rel_master(master);
        return -1;
    }
    printf(" -> PASSED: PDO channels registered successfully.\n\n");

    /* TEST 5: Activate Master (`ecat_activate`) */
    printf("[TEST 5] Activating Master...\n");
    if (ecat_activate(master) < 0) {
        fprintf(stderr, "FAILED: Master activation failed.\n");
        ecat_rel_master(master);
        return -1;
    }
    printf(" -> PASSED: Master Activated! Domain Size: %zu bytes\n\n", ecat_domain_size(domain));

    /* TEST 6: Real-Time Cyclic Read Loop (`ecat_recv`, `ecat_rd_`, `ecat_send`) */
    printf("[TEST 6] Live Cyclic Sensor Data Read (Press Ctrl+C to Stop)\n");
    printf("---------------------------------------------------------------\n");

    uint32_t cycle = 0;
    while (running) {
        ecat_recv(master);

        uint16_t enc_val    = ecat_rd_u16(pdo_encoder);
        int32_t  lvdt_val   = ecat_rd_s32(pdo_lvdt);
        int32_t  strain_val = ecat_rd_s32(pdo_strain);

        if (cycle % 500 == 0) { // Print every 500ms
            ecat_master_state_t m_state;
            ecat_get_state(master, &m_state);

            printf("[Cycle %5u] Encoder: %5u | LVDT: %10d | Strain: %10d | Bus Slaves: %u\n",
                   cycle, enc_val, lvdt_val, strain_val, m_state.slaves_responding);
        }

        ecat_send(master);
        usleep(1000); // 1 ms cycle
        cycle++;
    }

    printf("\n[CLEANUP] Releasing master resources...\n");
    ecat_rel_master(master);
    printf("Diagnostic test completed successfully.\n");
    return 0;
}
