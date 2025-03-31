#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <fstream>

#include <yaml-cpp/yaml.h>

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

struct host_cfg_t {
    std::string hostname;
    std::string pf_pci;
    std::string vf_rep;
};

struct drop_cfg_t {
    uint8_t pct_drop; // out of 100
};

struct input_cfg_t {
    struct host_cfg_t host_cfg;
    struct drop_cfg_t drop_cfg;
};

struct app_cfg_t {
    struct input_cfg_t *input_cfg;
    struct application_dpdk_config dpdk_cfg;
};

doca_error_t parse_input_cfg(std::string filename, struct input_cfg_t *cfg);
