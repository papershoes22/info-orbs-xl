#pragma once
#include <stdint.h>
#include <stddef.h>
#define FLASH_SECTOR_SIZE 4096u
#define PICO_FLASH_SIZE_BYTES (16u * 1024u * 1024u)
extern uint8_t fake_flash_sector[FLASH_SECTOR_SIZE];
#define XIP_BASE ((uintptr_t)fake_flash_sector - (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE))
void flash_range_erase(uint32_t off, size_t n);
void flash_range_program(uint32_t off, const uint8_t *d, size_t n);
