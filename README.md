**Waveshare pico-DALI2 adjusted for RP2040**

Control 3/4 wire RGB LED Strip from DALI/DALI2 commands
WS2812/WS2801/others

FreeRTOS based, dual task, Core 0 Comms, Core 1 LED Animation.

Will print all DALI messages, and respond to either short address group address or broadcast.

***USB CDC Menu***

    help                    Show the menu
    status                  Show current settings
    mode <normal|monitor>   Behaviour Mode         
    prom <0|1>              Set Promiscuous mode
    listen <0|1>            Set listen-only mode
    invert <0|1>            Invert RX
    addr <0-63>             Short Address
    group <mask>            Group mask
    defaults                Set Defaults

General DALI Commands

    OFF                     Turns Off
    RECALL_MAX_LEVEL        Fill
    GO_TO_SCENE0            Breathe
    GO_TO_SCENE1            Rainbow
    GO_TO_SCENE2            Strobe 
    GO_TO_SCENE3            Chase
    GO_TO_SCENE4            Sparkle
    GO_TO_SCENE5            ...

    SET_SCENE0              Set RED value
    SET_SCENE1              Set GREEN value
    SET_SCENE2              Set BLUE value

    QUERY_SCENE0_LEVEL      Get RED Value
    QUERY_SCENE1_LEVEL      Get GREEN Value
    QUERY_SCENE2_LEVEL      Get BLUE Value

**Monitor Mode**

Each LED in sequence responds to the DALI short-address of its position index. i,e, LED1 will light Green when Short Address 1 is ON, LED24 will be RED when Short Address 24 is OFF.
Groups 1-16 are LEDs 80-96  

LEDS will act like Lamps and respond to the following DALI commands:

    OFF
    RECALL_MAX_LEVEL
    RECALL_MIN_LEVEL
    UP
    DOWN
    STEP_UP
    STEP_DOWN
    STEP_DOWN_AND_OFF
    ON_AND_STEP_UP
    GO_TO_LAST_ACTIVE_LEVEL