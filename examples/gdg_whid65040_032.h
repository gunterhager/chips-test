#pragma once
//
//  gdg_whid65040_032.h
//
//  Header-only emulator of the GDG WHID 65040-032, a custom chip
//  found in the SHARP MZ-800 computer. It is used mainly as CRT controller.
//  Created by Gunter Hager on 03.07.18.
//

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    /// gdg_whid65040_032 state
    typedef struct {
        /// write format register
        uint8_t wf;
        /// read format register
        uint8_t rf;
        /// display mode register
        uint8_t dmd;
        /// display status register
        
        uint8_t status;
        /// scroll offset register 1
        uint8_t sof1;
        /// scroll offset register 2
        uint8_t sof2;
        /// scroll width register
        uint8_t sw;
        /// scroll start address register
        uint8_t ssa;
        /// scroll end address register
        uint8_t sea;
        
        /// border color register
        uint8_t bcol;
        
        /// superimpose bit
        uint8_t cksw;
    } gdg_whid65040_032_t;
    
/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <string.h>
#ifndef CHIPS_DEBUG
  #ifdef _DEBUG
    #define CHIPS_DEBUG
  #endif
#endif
#ifndef CHIPS_ASSERT
  #include <assert.h>
  #define CHIPS_ASSERT(c) assert(c)
#endif

#endif /* CHIPS_IMPL */
    
#ifdef __cplusplus
} /* extern "C" */
#endif
