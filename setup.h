/* setup.h — menu API (safe to include from main.c) */
#ifndef SETUP_H
#define SETUP_H

#if defined(_MSC_VER)
#define SSCANF  sscanf_s
#else
#define SSCANF  sscanf
#endif

#include "disease.h"   /* Disease */
#include "config.h"    /* SimConfig, CureParams */

/*
  setup_menu:
    Draws a full-screen terminal menu to edit Disease + SimConfig (Cure) values.
    Returns:
      1 if user chooses [ START SIMULATION ]
      0 if user presses Esc (cancel)
    Behavior:
      - Same as the original, but with improved comments and named constants.
*/
int setup_menu(Disease* dz, SimConfig* cfg);

#endif /* SETUP_H */
