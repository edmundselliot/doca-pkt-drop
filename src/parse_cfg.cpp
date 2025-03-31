#include "main.hpp"

DOCA_LOG_REGISTER(PARSE_CFG);

doca_error_t parse_input_cfg(std::string filename, struct input_cfg_t *cfg) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        DOCA_LOG_ERR("Failed to open file %s", filename.c_str());
        return DOCA_ERROR_IO_FAILED;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string yaml_content = buffer.str();

    YAML::Node root = YAML::Load(yaml_content);

    const auto& dev_node = root["device_cfg"][0];
    cfg->host_cfg.pf_pci = dev_node["pci"].as<std::string>();
    cfg->host_cfg.vf_rep = dev_node["vf"].as<std::string>();

    const auto& drop_node = root["drop_cfg"][0];
    cfg->drop_cfg.pct_drop = drop_node["pct_drop"].as<uint8_t>();

    return DOCA_SUCCESS;
}
