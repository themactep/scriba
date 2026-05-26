#ifndef SPI_NOR_FLASH_TABLES_H
#define SPI_NOR_FLASH_TABLES_H

#include "spi_nor_flash.h"
#include "types.h"

extern struct chip_info chips_data[];
extern const int chips_data_count;

struct chip_info *chip_prob_binary_search(u8 mfr_id, u32 jedec,
                                          u32 jedec_strip);

#endif /* SPI_NOR_FLASH_TABLES_H */
