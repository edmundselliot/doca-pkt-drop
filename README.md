## Configuration
1. Follow the setup steps from the [setup script](./setup.sh)
1. Update the [config file](./app_cfg.yml)
1. Install external dependency `yaml-cpp`

## Build
```sh
meson build
ninja -C build
```

## Run the app
```sh
doca-pkt-drop app_cfg.yml
```

With the app running, all traffic from the VF will either be dropped, or sent to the wire. The percentage of dropped traffic is based on the input from the config file.


