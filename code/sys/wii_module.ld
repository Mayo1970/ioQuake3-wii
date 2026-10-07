/* ld -r script for one native TA module: merges its .data/.bss and brackets them
   so wii_modules.c can restore .data and zero .bss on every load, like a fresh QVM. */
SECTIONS
{
  .data : { wiimod_data_start = .; *(.data .data.*) wiimod_data_end = .; }
  .bss  : { wiimod_bss_start = .; *(.bss .bss.* COMMON) wiimod_bss_end = .; }
}
