**Waveshare pico-DALI2 adjusted for RP2040**

Control 3/4 wire RGB LED Strip from DALI/DALI2 commands
WS2812/WS2801/others

FreeRTOS based, dual task, Core 0 Comms, Core 1 LED Animation.

Will print all DALI messages, and respond to either short address group address or broadcast.

OFF
RECALL_MAX_LEVEL - Fill
GO_TO_SCENE0-15 - Breathe, Rainbow, Strobe, Chase, Sparkle, more coming

SET_SCENE0              Set RED value
SET_SCENE1              Set GREEN value
SET_SCENE2              Set BLUE value

QUERY_SCENE0_LEVEL      Get RED Value
QUERY_SCENE1_LEVEL      Get GREEN Value
QUERY_SCENE2_LEVEL      Get BLUE Value