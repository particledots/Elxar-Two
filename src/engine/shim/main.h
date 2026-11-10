/* ELXAR-Two shim for the firmware's main.h. The sound engine only reads four "is a cable plugged in" switches
 * (mixer.c). The plugin pretends every output jack is plugged, so each voice keeps its routing. */
#ifndef ELXAR_SHIM_MAIN_H
#define ELXAR_SHIM_MAIN_H
#include "stm32f7xx_hal.h"
#include "Soundengine/config.h"
typedef struct { uint32_t IDR; } ElxarGpio;
extern ElxarGpio elxar_jacks;
#define switch_l1_Pin 1u
#define switch_r1_Pin 1u
#define switch_l2_Pin 1u
#define switch_r2_Pin 1u
#define switch_l1_GPIO_Port (&elxar_jacks)
#define switch_r1_GPIO_Port (&elxar_jacks)
#define switch_l2_GPIO_Port (&elxar_jacks)
#define switch_r2_GPIO_Port (&elxar_jacks)
#endif
