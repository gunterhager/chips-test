#pragma once
// #version:5#
// machine generated, do not edit!
#include <stdint.h>
extern unsigned char dump_mz800_cgrom[2];
extern unsigned char dump_mz800_monitor[5];
extern unsigned char dump_mz800_dram2[24576];
typedef struct { const char* name; const uint8_t* ptr; int size; } dump_item;
#define DUMP_NUM_ITEMS (3)
extern dump_item dump_items[DUMP_NUM_ITEMS];
