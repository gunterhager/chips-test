#pragma once
//
//  gdg_whid65040_032.h
//
//  Header-only emulator of the GDG WHID 65040-032, a custom chip
//  found in the SHARP MZ-800 computer. It is used mainly as CRT controller.
//  The GDG acts as memory controller, too. We don't emulate that here.
//
//  Created by Gunter Hager on 03.07.18.
//

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    /// GDG WHID 65040-032 state
    typedef struct {
        /// Write format register
        uint8_t wf;
        /// Read format register
        uint8_t rf;
        
        /// Display mode register
        uint8_t dmd;
        /// Display status register
        uint8_t status;
        
        /// Scroll offset register 1
        uint8_t sof1;
        /// Scroll offset register 2
        uint8_t sof2;
        /// Scroll width register
        uint8_t sw;
        /// Scroll start address register
        uint8_t ssa;
        /// Scroll end address register
        uint8_t sea;
        
        /// Border color register
        uint8_t bcol;
        
        /// Superimpose bit
        uint8_t cksw;
    } gdg_whid65040_032_t;
    
    /*
     GDG WHID 65040-032 pins
     
     AD ... CPU address bus (16 bit)
     DT ... CPU data bus (8 bit)
     MREQ
     RD
     WR
     IORQ
     M1
     NPL ... NTSC/PAL selection, low for PAL
     MOD7 ... MZ-700/800 mode selection, low for MZ-700 mode
     MNRT ... Manual reset
     WTGD ... Wait signal to CPU
     
     */
    
/* control pins shared directly with Z80 CPU */
#define  GDG_M1    (1ULL<<24)       /* machine cycle 1 */
#define  GDG_IORQ  (1ULL<<26)       /* input/output request */
#define  GDG_RD    (1ULL<<27)       /* read */
#define  GDG_WR    (1ULL<<28)       /* write */
#define  GDG_INT   (1ULL<<30)       /* interrupt request */
#define  GDG_RESET (1ULL<<31)       /* put GDG into reset state (same as Z80 reset) */

    
    /* extract 8-bit data bus from 64-bit pins */
#define GDG_GET_DATA(p) ((uint8_t)(p>>16))
    /* merge 8-bit data bus value into 64-bit pins */
#define GDG_SET_DATA(p,d) {p=((p&~0xFF0000)|((d&0xFF)<<16));}
    
    /* initialize a new GDG WHID 65040-032 instance */
    extern void gdg_whid65040_032_init(gdg_whid65040_032_t* gdg);
    /* reset an existing GDG WHID 65040-032 instance */
    extern void gdg_whid65040_032_reset(gdg_whid65040_032_t* gdg);
    /* perform an IORQ machine cycle */
    extern uint64_t gdg_whid65040_032_iorq(gdg_whid65040_032_t* gdg, uint64_t pins);

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

    /**
     gdg_whid65040_032_init
     
     Call this once to initialize a new GDG WHID 65040-032 instance, this will
     clear the gdg_whid65040_032_t struct and go into a reset state.
     */
    void gdg_whid65040_032_init(gdg_whid65040_032_t* gdg) {
        CHIPS_ASSERT(gdg);
        gdg_whid65040_032_reset(gdg);
    }
    
    /**
     gdg_whid65040_032_reset
     
     Puts the GDG WHID 65040-032 into the reset state.
     */
    void gdg_whid65040_032_reset(gdg_whid65040_032_t* gdg) {
        CHIPS_ASSERT(gdg);
        memset(gdg, 0, sizeof(*gdg));
    }

    /**
     gdg_whid65040_032_iorq
     
     Perform an IORQ machine cycle
     */
    uint64_t gdg_whid65040_032_iorq(gdg_whid65040_032_t* gdg, uint64_t pins) {
        uint64_t outpins = pins;
        if ((pins & (GDG_IORQ | GDG_M1)) == GDG_IORQ) {
            uint16_t address = Z80_GET_ADDR(pins);
            
            // Read display status register
            if ((address == 0x00ce) && (pins & GDG_RD)) {
                Z80_SET_DATA(outpins, gdg->status);
            }
            
            // Border color register
            else if ((address == 0x06cf) && (pins & GDG_WR)) {
                gdg->bcol = Z80_GET_DATA(pins);
            }
            
            // DEBUG
            else {
                CHIPS_ASSERT(1);
            }

        }
        
        return outpins;
    }

#endif /* CHIPS_IMPL */
    
#ifdef __cplusplus
} /* extern "C" */
#endif
