# MAX32655 Reactor Project (FreeRTOS Boilerplate)

This is the embedded firmware for the project, targeting the
**MAX32655FTHR** running **FreeRTOS** using the **Analog Devices MSDK**.

## Prerequisites

You need the MSDK locally.

Recommended:

```bash
mkdir ~/dev
cd ~/dev
git clone https://github.com/analogdevicesinc/msdk.git
export MAXIM_PATH=~/dev/msdk
```

## Development

We recommend opening this /firmware directory in VSCode and using CodeFusionStudio for development.

## Description

A basic getting started application for FreeRTOS. 


## Software

### Project Usage

Universal instructions on building, flashing, and debugging this project can be found in the **[MSDK User Guide](https://analogdevicesinc.github.io/msdk/USERGUIDE/)**.

### Project-Specific Build Notes

* This project comes pre-configured for the MAX32655EVKIT.  See [Board Support Packages](https://analogdevicesinc.github.io/msdk/USERGUIDE/#board-support-packages) in the MSDK User Guide for instructions on changing the target board.

## Required Connections
If using the MAX32655EVKIT (EvKit\_V1):
-   Connect a USB cable between the PC and the CN1 (USB/PWR) connector.
-   Connect pins JP4(RX_SEL) and JP5(TX_SEL) to RX0 and TX0 header. Also connect JP6 and JP7 for CTS/RTS signals. 
-   Open an terminal application on the PC and connect to the EV kit's console UART at 115200, 8-N-1. Enable CTS/RTS on PC terminal.
-   Close jumper JP2 (LED0 EN).
-   Close jumper JP3 (LED1 EN).

If using the MAX32655FTHR (FTHR\_Apps\_P1):
-   Connect a USB cable between the PC and the J4 (USB/PWR) connector.
-   Open an terminal application on the PC and connect to the board's console UART at 115200, 8-N-1. Enable CTS/RTS on PC terminal.

## Expected Output

```
-=- MAX32655 FreeRTOS (V10.5.1) Demo -=-
SystemCoreClock = 100000000
Starting scheduler.
Uptime is 0x00000000 (0 seconds), tickless-idle is disabled

Enter 'help' to view a list of available commands.
cmd>
```