#include "app.hpp"

DOCA_LOG_REGISTER(OFFLOAD_APP);

OffloadApp::OffloadApp(struct input_cfg_t *input_cfg) {
    this->app_cfg.input_cfg = input_cfg;

    this->pf_port_id = 0;
    this->vf_port_id = 1;

    this->app_cfg.dpdk_cfg.port_config.nb_ports = 2;
    this->app_cfg.dpdk_cfg.port_config.switch_mode = true;
    this->app_cfg.dpdk_cfg.port_config.isolated_mode = true;
    this->app_cfg.dpdk_cfg.port_config.nb_queues = 1;
}

OffloadApp::~OffloadApp() {
    DOCA_LOG_INFO("Destroying offload app");
}

doca_error_t OffloadApp::init() {
    DOCA_LOG_INFO("Initializing DOCA");

    doca_error_t result = init_doca_flow();
    IF_SUCCESS(result, init_dpdk());
    IF_SUCCESS(result, init_dev());
    // IF_SUCCESS(result, init_dpdk_queues_ports());

    IF_SUCCESS(result, start_port(pf_port_id, pf_dev, &pf_port));
    IF_SUCCESS(result, start_port(vf_port_id, nullptr, &vf_port));

    IF_SUCCESS(result, pipe_mgr.init(&app_cfg, pf_port, vf_port, pf_port_id, vf_port_id));

    return result;
}

doca_error_t OffloadApp::init_dpdk_queues_ports() {

    doca_error_t result = dpdk_queues_and_ports_init(&app_cfg.dpdk_cfg);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to update application ports and queues: %s", doca_error_get_descr(result));
        return result;
    }

    DOCA_LOG_INFO("DPDK ports and queues initialized");
    return result;
}

doca_error_t OffloadApp::init_dpdk() {
    const char *eal_args[] = {"", "-a", "00:00.0"};

    int n_eal_args = sizeof(eal_args) / sizeof(eal_args[0]);
    int rc = rte_eal_init(n_eal_args, (char **)eal_args);
    if (rc < 0) {
        DOCA_LOG_ERR("EAL initialization failed: %d", rc);
        for (int i = 0; i < n_eal_args; i++) {
            DOCA_LOG_ERR("EAL arg %d: %s", i, eal_args[i]);
        }
        return DOCA_ERROR_BAD_STATE;
    }

    // This can't be set until EAL init because it uses rte_lcore_count()
    // app_cfg.dpdk_cfg.port_config.nb_queues = rte_lcore_count();

    return DOCA_SUCCESS;
}

doca_error_t OffloadApp::init_dev(void)
{
    doca_error_t result = DOCA_SUCCESS;
    std::string dev_probe_str = std::string(
        "dv_flow_en=2,"     // hardware steering
        "dv_xmeta_en=4,"     // extended flow metadata support
        "fdb_def_rule_en=0," // disable default root flow table rule
        "vport_match=1,"
        "repr_matching_en=0,"
        "representor=pf0vf0");

    IF_SUCCESS(result, open_doca_device_with_pci(app_cfg.input_cfg->host_cfg.pf_pci.c_str(), nullptr, &pf_dev));
    IF_SUCCESS(result, doca_dpdk_port_probe(pf_dev, dev_probe_str.c_str()));
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to probe device %s: %s", app_cfg.input_cfg->host_cfg.pf_pci.c_str(), doca_error_get_descr(result));
        return result;
    }

    DOCA_LOG_INFO("Probed PF %s", app_cfg.input_cfg->host_cfg.pf_pci.c_str());
    return result;
}

doca_error_t OffloadApp::start_port(uint16_t port_id, doca_dev *port_dev, doca_flow_port **port)
{
    struct doca_flow_port_cfg *port_cfg;
    std::string port_id_str = std::to_string(port_id); // note that set_devargs() clones the string contents

    doca_error_t result = doca_flow_port_cfg_create(&port_cfg);
    IF_SUCCESS(result, doca_flow_port_cfg_set_devargs(port_cfg, port_id_str.c_str()));
    IF_SUCCESS(result, doca_flow_port_cfg_set_dev(port_cfg, port_dev));
    IF_SUCCESS(result, doca_flow_port_cfg_set_actions_mem_size(port_cfg, 1024));
    if (port_dev) {
        IF_SUCCESS(result, doca_flow_port_cfg_set_operation_state(port_cfg, DOCA_FLOW_PORT_OPERATION_STATE_ACTIVE));
    }
    IF_SUCCESS(result, doca_flow_port_start(port_cfg, port));
    if (result == DOCA_SUCCESS)
        DOCA_LOG_INFO("Started port_id %d", port_id);

    if (port_cfg)
        doca_flow_port_cfg_destroy(port_cfg);
    return result;
}

doca_error_t OffloadApp::init_doca_flow(void)
{
    doca_error_t result = DOCA_SUCCESS;

    uint16_t nb_queues = 1;
    uint16_t rss_queues[nb_queues];
    for (int i = 0; i < nb_queues; i++)
        rss_queues[i] = i;

    struct doca_flow_resource_rss_cfg rss_config = {};
    rss_config.nr_queues = nb_queues;
    rss_config.queues_array = rss_queues;

    struct doca_flow_cfg *flow_cfg;
    IF_SUCCESS(result, doca_flow_cfg_create(&flow_cfg));
    IF_SUCCESS(result, doca_flow_cfg_set_pipe_queues(flow_cfg, nb_queues));
    IF_SUCCESS(result, doca_flow_cfg_set_queue_depth(flow_cfg, 128));
    IF_SUCCESS(result, doca_flow_cfg_set_nr_counters(flow_cfg, 4096));
    IF_SUCCESS(result, doca_flow_cfg_set_mode_args(flow_cfg, "switch,hws,isolated,expert"));
    IF_SUCCESS(result, doca_flow_cfg_set_cb_entry_process(flow_cfg, OffloadApp::check_for_valid_entry));
    IF_SUCCESS(result, doca_flow_cfg_set_default_rss(flow_cfg, &rss_config));
    IF_SUCCESS(result, doca_flow_init(flow_cfg));

    if (flow_cfg)
        doca_flow_cfg_destroy(flow_cfg);
    return result;
}

void OffloadApp::check_for_valid_entry(doca_flow_pipe_entry *entry,
                         uint16_t pipe_queue,
                         enum doca_flow_entry_status status,
                         enum doca_flow_entry_op op,
                         void *user_ctx)
{
    (void)entry;
    (void)pipe_queue;

    auto *entry_status = (entries_status *)user_ctx;

    if (entry_status == nullptr)
        return;

    if (op != DOCA_FLOW_ENTRY_OP_ADD && op != DOCA_FLOW_ENTRY_OP_UPD)
        return;

    if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS)
        entry_status->failure = true; /* set failure to true if processing failed */

    entry_status->nb_processed++;
}

doca_error_t OffloadApp::run() {
    doca_error_t result = DOCA_SUCCESS;

    while(1) {
        pipe_mgr.print_stats();
        sleep(2);
    }

    return result;
}
