#!/bin/bash

# Commands to upload the firmware via SWD using a Raspberry Pi Debug Probe connected to a Raspberry Pi.

# Copy the .elf file to the remote machine.
sshpass -p 'nn22picoprobe' scp ./build/SrcBoardFirmware.elf pico@pico-probe.local:~/nn22bin/

# Upload the firmware to the RP2040
sshpass -p 'nn22picoprobe' ssh pico@pico-probe.local \
    'sudo openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" \
    -c "program /home/pico/nn22bin/SrcBoardFirmware.elf verify reset exit"'
