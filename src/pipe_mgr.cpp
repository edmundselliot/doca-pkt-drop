#include "pipe_mgr.hpp"

DOCA_LOG_REGISTER(PIPE_MGR);

PipeMgr::PipeMgr() {
    monitor_count.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
    fwd_drop.type = DOCA_FLOW_FWD_DROP;
}

PipeMgr::~PipeMgr() {}

doca_error_t PipeMgr::init(
    struct app_cfg_t *app_cfg,
    struct doca_flow_port *pf_port,
    struct doca_flow_port *vf_port,
    uint32_t pf_port_id,
    uint32_t vf_port_id)
{
    this->app_cfg = app_cfg;
    this->pf_port_id = pf_port_id;
    this->vf_port_id = vf_port_id;
    this->pf_port = pf_port;
    this->vf_port = vf_port;

    return create_pipes();
}

doca_error_t PipeMgr::create_pipes() {
    doca_error_t result = DOCA_SUCCESS;

    IF_SUCCESS(result, tx_root_drop_pipe_create());
    IF_SUCCESS(result, rx_root_pipe_create());

    if (result == DOCA_SUCCESS)
        DOCA_LOG_INFO("Created all static pipes on port %d", pf_port_id);
    else
        DOCA_LOG_ERR("Failed to create all static pipes on port %d, err: %s", pf_port_id, doca_error_get_descr(result));

    return result;
}

void PipeMgr::print_stats() {
    DOCA_LOG_INFO("=================================");
    struct doca_flow_resource_query stats;
    doca_error_t result;

    DOCA_LOG_INFO("ENTRIES:");
    for (auto entry : monitored_pipe_entries) {
        result = doca_flow_resource_query_entry(entry.second, &stats);
        if (result == DOCA_SUCCESS)
            DOCA_LOG_INFO("  %s hit: %lu packets", entry.first.c_str(), stats.counter.total_pkts);
        else
            DOCA_LOG_ERR("Failed to query entry %s: %s", entry.first.c_str(), doca_error_get_descr(result));
    }

    DOCA_LOG_INFO("PIPES:");
    for (auto pipe : monitored_pipe_misses) {
        result = doca_flow_resource_query_pipe_miss(pipe.second, &stats);
        if (result == DOCA_SUCCESS)
            DOCA_LOG_INFO("  %s miss: %lu pkts", pipe.first.c_str(), stats.counter.total_pkts);
        else
            DOCA_LOG_ERR("Failed to query pipe %s miss: %s", pipe.first.c_str(), doca_error_get_descr(result));
    }
}

doca_error_t PipeMgr::tx_root_drop_pipe_create() {
    assert(pf_port);
    doca_error_t result = DOCA_SUCCESS;
    int nr_entries = 8; // must be pow2

    doca_flow_fwd fwd_changeable;
    fwd_changeable.type = DOCA_FLOW_FWD_CHANGEABLE;

    doca_flow_match match_random_mask = {};
    match_random_mask.parser_meta.random = UINT16_MAX;

    struct doca_flow_pipe_cfg *pipe_cfg;
    IF_SUCCESS(result, doca_flow_pipe_cfg_create(&pipe_cfg, pf_port));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_name(pipe_cfg, "TX_ROOT_DROP"));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_domain(pipe_cfg, DOCA_FLOW_PIPE_DOMAIN_EGRESS));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_HASH));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_is_root(pipe_cfg, true));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, nr_entries));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_match(pipe_cfg, nullptr, &match_random_mask));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor_count));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_miss_counter(pipe_cfg, true));
    IF_SUCCESS(result, doca_flow_pipe_create(pipe_cfg, &fwd_changeable, nullptr, &tx_root_drop_pipe));
    if (pipe_cfg)
        doca_flow_pipe_cfg_destroy(pipe_cfg);
    // monitored_pipe_misses.push_back(std::make_pair("TX_ROOT_DROP_PIPE", tx_root_drop_pipe));

    assert(app_cfg->input_cfg->drop_cfg.pct_drop <= 100);
    uint16_t nr_drop_entries = nr_entries * app_cfg->input_cfg->drop_cfg.pct_drop / 100;

    DOCA_LOG_INFO("Creating entries to drop %d%% of packets!", app_cfg->input_cfg->drop_cfg.pct_drop);

    doca_flow_fwd fwd_wire = {};
    fwd_wire.type = DOCA_FLOW_FWD_PORT;
    fwd_wire.port_id = pf_port_id;

    entries_status status = {};
    for (uint16_t i = 0; i < nr_entries; i++) {
        struct doca_flow_pipe_entry *entry = {};
		IF_SUCCESS(result, doca_flow_pipe_hash_add_entry(
            0,
            tx_root_drop_pipe,
            i,
            nullptr,
            nullptr,
            i < nr_drop_entries ? &fwd_drop : &fwd_wire,
            DOCA_FLOW_NO_WAIT,
            &status,
            &entry));

        std::string name = "TX_ROOT_DROP_PIPE_ENTRY_" + std::to_string(i);
        if (i < nr_drop_entries)
            name += "_drop";
        else
            name += "_allow";
        monitored_pipe_entries.emplace_back(name, entry);
    }

    result = doca_flow_entries_process(pf_port, 0, DEFAULT_TIMEOUT_US, nr_entries);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to process entry: %s", doca_error_get_descr(result));
        return result;
    }

    if (status.nb_processed != nr_entries || status.failure) {
        DOCA_LOG_ERR("Failed to process entry; nb_processed = %d, failure = %d",
                 status.nb_processed,
                 status.failure);
        return DOCA_ERROR_BAD_STATE;
    }

    return result;
}

doca_error_t PipeMgr::rx_root_pipe_create() {
    assert(pf_port);
    assert(tx_root_drop_pipe);

    doca_error_t result = DOCA_SUCCESS;
    struct doca_flow_pipe_cfg *pipe_cfg;
    IF_SUCCESS(result, doca_flow_pipe_cfg_create(&pipe_cfg, pf_port));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_name(pipe_cfg, "RX_ROOT"));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_CONTROL));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_is_root(pipe_cfg, true));
    IF_SUCCESS(result, doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, 2));
    IF_SUCCESS(result, doca_flow_pipe_create(pipe_cfg, nullptr, nullptr, &rx_root_pipe));
    if (pipe_cfg)
        doca_flow_pipe_cfg_destroy(pipe_cfg);

    struct doca_flow_match match_from_vf = {};
    match_from_vf.parser_meta.port_meta = vf_port_id;

    struct doca_flow_match match_from_pf = {};
    match_from_pf.parser_meta.port_meta = pf_port_id;

    struct doca_flow_fwd fwd_tx = {};
    fwd_tx.type = DOCA_FLOW_FWD_PIPE;
    fwd_tx.next_pipe = tx_root_drop_pipe;

    struct doca_flow_fwd fwd_rx = {};
    fwd_rx.type = DOCA_FLOW_FWD_PORT;
    fwd_rx.port_id = vf_port_id;

    uint32_t rule_priority = 1;

    // 1. Forward traffic from the VF to the tx-root pipe
    IF_SUCCESS(result,
        doca_flow_pipe_control_add_entry(0,
                        rule_priority++,
                        rx_root_pipe,
                        &match_from_vf,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        &monitor_count,
                        &fwd_tx,
                        nullptr,
                        &rx_root_pipe_from_vf_entry));
    monitored_pipe_entries.push_back(std::make_pair("RX_ROOT_PIPE_FROM_VF", rx_root_pipe_from_vf_entry));

    // 2. Forward traffic from the wire to the rx-vlan pipe
    IF_SUCCESS(result,
        doca_flow_pipe_control_add_entry(0,
                        rule_priority++,
                        rx_root_pipe,
                        &match_from_pf,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        &monitor_count,
                        &fwd_rx,
                        nullptr,
                        &rx_root_pipe_from_pf_entry));
    monitored_pipe_entries.push_back(std::make_pair("RX_ROOT_PIPE_FROM_PF", rx_root_pipe_from_pf_entry));

    return result;
}
