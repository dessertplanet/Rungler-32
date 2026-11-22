/* Flash build: the application region 0x3F0000-0x3F5FB9.
   Never place anything at 0x3F5FBA-0x3F5FFF or at 0x3F6000-0x3F7FFF: that area belongs to the bootloader
   and is not part of a firmware update.
   RAM: 0x0040-0x056F holds the bootloader's flash routines after boot. Firmware may use it as scratch
   through the .bootram section (uninitialised); Mother32::EnterUpdateMode() restores the routines
   before it needs them. Stack in M1 above it, data and RAM-resident code in L0. */
MEMORY
{
PAGE 0:
    FLASH_APP : origin = 0x3F0000, length = 0x005FBA
    RAML0     : origin = 0x008000, length = 0x001000
PAGE 1:
    RAMBOOT   : origin = 0x000040, length = 0x000530
    RAMM1     : origin = 0x000570, length = 0x000290
}
SECTIONS
{
    .text        : > FLASH_APP, PAGE = 0
    .cinit       : > FLASH_APP, PAGE = 0
    .pinit       : > FLASH_APP, PAGE = 0
    .switch      : > FLASH_APP, PAGE = 0
    .econst      : > FLASH_APP, PAGE = 0
    .TI.ramfunc  : LOAD = FLASH_APP, RUN = RAML0, PAGE = 0,
                   LOAD_START(_RamfuncsLoadStart), LOAD_SIZE(_RamfuncsLoadSize),
                   RUN_START(_RamfuncsRunStart)
    .ebss        : > RAML0, PAGE = 0
    .esysmem     : > RAML0, PAGE = 0
    .stack       : > RAMM1, PAGE = 1
    .bootram     : > RAMBOOT, PAGE = 1
    .reset       : > FLASH_APP, PAGE = 0, TYPE = DSECT
}
