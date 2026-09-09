#pragma once

// =============================================================================
// Hardware revision selection
// =============================================================================
// HARDWARE_REV is set by the PlatformIO env (see platformio.ini):
//   -e hw_rev1  ->  HARDWARE_REV == 1   (original tester board)
//   -e hw_rev2  ->  HARDWARE_REV == 2   (new tester board)
//
// Building without picking an env (or with an unknown value) is a hard error,
// so a firmware binary always corresponds to exactly one hardware revision.
// =============================================================================
#if !defined(HARDWARE_REV)
#error "HARDWARE_REV is not set - build with 'pio run -e hw_rev1' or '-e hw_rev2'"
#endif

// Pin and ADC channel definitions (revision specific)
#if HARDWARE_REV == 1

#define cl_analog ADC1_CHANNEL_0
#define bl_analog ADC1_CHANNEL_3
#define piste_analog ADC1_CHANNEL_6
#define cr_analog ADC1_CHANNEL_7
#define br_analog ADC1_CHANNEL_4
#define ar_analog ADC1_CHANNEL_5

#define al_driver 33
#define bl_driver 21
#define cl_driver 23
#define ar_driver 25
#define br_driver 05
#define cr_driver 18
#define piste_driver 19

#elif HARDWARE_REV == 2

// TODO(hw_rev2): update the values below for the new board. This block is
// currently an exact copy of hw_rev1 - change only the pins/channels that
// actually differ on the new hardware.
#define cl_analog ADC1_CHANNEL_0
#define bl_analog ADC1_CHANNEL_3
#define piste_analog ADC1_CHANNEL_6
#define cr_analog ADC1_CHANNEL_7
#define br_analog ADC1_CHANNEL_4
#define ar_analog ADC1_CHANNEL_5

#define al_driver 14     // was 33
// GPIO12 is a boot strapping pin (MTDI / flash-voltage select). Safe here
// because it is only driven after boot and floats low at reset; a warm reset
// while this pin is driven high through a connected cord could wedge the
// bootloader. Burn the 3.3V flash-voltage eFuse if that is ever observed.
#define bl_driver 12     // was 21
#define cl_driver 13     // was 23
#define ar_driver 25     // unchanged
#define br_driver 26     // was 5
#define cr_driver 27     // was 18
#define piste_driver 14  // was 19 — now shared with al_driver

#else
#error "Unsupported HARDWARE_REV value (expected 1 or 2)"
#endif

// Printable hardware-revision string, e.g. for the OLED / serial banner.
#define HW_REV_STR2(x) #x
#define HW_REV_STR1(x) HW_REV_STR2(x)
#define HARDWARE_REV_STR HW_REV_STR1(HARDWARE_REV)

// I/O Direction and Value macros (shared across revisions)
#define IODirection_ar_br 231
#define IODirection_ar_cr 215
#define IODirection_ar_piste 183
#define IODirection_ar_bl 245
#define IODirection_ar_cl 243
#define IODirection_al_br 238
#define IODirection_al_cr 222
#define IODirection_al_piste 190
#define IODirection_al_bl 252
#define IODirection_al_cl 250
#define IODirection_br_cr 207
#define IODirection_br_bl 237
#define IODirection_br_cl 235
#define IODirection_br_piste 175
#define IODirection_bl_cl 249
#define IODirection_bl_piste 189
#define IODirection_bl_cr 221
#define IODirection_cr_piste 159
#define IODirection_cr_cl 219
#define IODirection_cl_piste 187
#define IODirection_cr_bl 221

#define IOValues_ar_br 8
#define IOValues_ar_cr 8
#define IOValues_ar_piste 8
#define IOValues_ar_bl 8
#define IOValues_ar_cl 8
#define IOValues_al_br 1
#define IOValues_al_cr 1
#define IOValues_al_piste 1
#define IOValues_al_bl 1
#define IOValues_al_cl 1
#define IOValues_br_cr 16
#define IOValues_br_bl 16
#define IOValues_br_cl 16
#define IOValues_br_piste 16
#define IOValues_bl_cl 2
#define IOValues_bl_piste 2
#define IOValues_bl_cr 2
#define IOValues_cr_piste 32
#define IOValues_cr_cl 32
#define IOValues_cl_piste 4
#define IOValues_cr_bl 32
