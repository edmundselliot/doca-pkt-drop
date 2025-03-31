#pragma once

#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_arp.h>

#include <doca_dev.h>
#include <doca_flow.h>
#include <doca_log.h>
#include <doca_dpdk.h>

#include "dpdk_utils.h"
#include "utils.h"
#include "common.h"

#include "main.hpp"
#include "pipe_mgr.hpp"

class OffloadApp {
private:
    struct app_cfg_t app_cfg;

    struct doca_dev *pf_dev;
    struct doca_flow_port *pf_port;
    struct doca_flow_port *vf_port;

    uint32_t pf_port_id;
    uint32_t vf_port_id;

    PipeMgr pipe_mgr = PipeMgr();

    doca_error_t init_doca_flow();
    doca_error_t init_dpdk();
    doca_error_t init_dev();
    doca_error_t init_dpdk_queues_ports();
    doca_error_t start_port(uint16_t port_id, doca_dev *port_dev, doca_flow_port **port);

    static void check_for_valid_entry(
        doca_flow_pipe_entry *entry,
        uint16_t pipe_queue,
        enum doca_flow_entry_status status,
        enum doca_flow_entry_op op,
        void *user_ctx);

public:
    OffloadApp(struct input_cfg_t *input_cfg);
    ~OffloadApp();

    doca_error_t init();
    doca_error_t run();
};
