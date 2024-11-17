## Firmware

### Prerequisites

1. [Miniconda](https://docs.anaconda.com/miniconda/), and an environment with Python 3.10
2. [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html)
3. [CMake](https://cmake.org/download/)
4. [Ninja](https://ninja-build.org/)

Now you can run a few commands to get more dependancies:

    cd <repo>/zephyr-workspace
    west update
    cd zephyr/scripts
    pip install -r requirements.txt
    cd <repo>/zephyr-workspace

### Building

There's no actual firmware or board yet - so in the mean time here's a generic
build for the included minimal sample.

    west build --pristine --board nucleo_l432kc firmware
