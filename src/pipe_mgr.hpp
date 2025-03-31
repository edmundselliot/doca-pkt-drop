#pragma once

#include <unistd.h>
#include <vector>
#include <set>

#include <rte_ether.h>
#include <rte_ethdev.h>

#include <doca_flow.h>
#include <doca_log.h>
#include <doca_dpdk.h>

#include "utils.h"
#include "main.hpp"


/*
    High-level pipe topology

             all packets
                  │
     from VF  ┌───▼───┐ from wire
     ┌────────┤rx root├────┐
 ┌───▼────┐   └───────┘    │
 │tx root │                │
 | (drop) |                |
 └───┬────┘                │
     │                     ▼
     ▼                fwd to VF
 fwd to wire
*/

class PipeMgr {
private:
    struct app_cfg_t *app_cfg;

    uint32_t pf_port_id;
    struct doca_flow_port *pf_port;
    uint32_t vf_port_id;
    struct doca_flow_port *vf_port;

    struct doca_flow_pipe *rx_root_pipe;
    struct doca_flow_pipe_entry *rx_root_pipe_from_vf_entry;
    struct doca_flow_pipe_entry *rx_root_pipe_from_pf_entry;
    struct doca_flow_pipe_entry *rx_root_pipe_unknown;

    struct doca_flow_pipe *tx_root_drop_pipe;
    std::vector<doca_flow_pipe_entry *> tx_root_pipe_drop_entries;
    std::vector<doca_flow_pipe_entry *> tx_root_pipe_allow_entries;

    struct doca_flow_monitor monitor_count = {};
    struct doca_flow_fwd fwd_drop = {};

    std::vector<std::pair<std::string, struct doca_flow_pipe_entry*>> monitored_pipe_entries = {};
    std::vector<std::pair<std::string, struct doca_flow_pipe*>> monitored_pipe_misses = {};

    doca_error_t create_pipes();
    doca_error_t rx_root_pipe_create();
    doca_error_t tx_root_drop_pipe_create();

public:
    PipeMgr();
    ~PipeMgr();

    doca_error_t init(
        app_cfg_t *app_cfg,
        doca_flow_port *pf_port,
        doca_flow_port *vf_port,
        uint32_t pf_port_id,
        uint32_t vf_port_id);

    void print_stats();
};
