# STM32H7B3I-DK TBS

The default IDE is set to STM32CubeIDE, to change IDE open the STM32H7B3I-DK.ioc with STM32CubeMX and select from the supported IDEs (STM32CubeIDE, EWARM, and MDK-ARM).\n\nSupports flashing of the STM32H7B3I-DK board directly from TouchGFX Designer using GCC and STM32CubeProgrammer. Flashing the board requires STM32CubeProgrammer which can be downloaded from the ST webpage.

This TBS is configured for 272 x 480 pixels 24bpp screen resolution.

Performance testing can be done using the GPIO pins designated with the following signals: VSYNC_FREQ - Pin PH14 (D0), RENDER_TIME - Pin PH13 (D1), FRAME_RATE - Pin PI9 (D2), MCU_ACTIVE - Pin PH9 (D3).

