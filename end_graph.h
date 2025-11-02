#ifndef END_GRAPH_H
#define END_GRAPH_H

#include "turnpoints.h"   /* TurnTracker, TPNode, TurnPoint */

/*
  End Graph Module
  ----------------
  Renders a stacked bar “timeline” of key turning points:
    - grey  = dead
    - red   = infected
    - blue  = alive (and after cure activates, infected merges into blue)
  Bars are evenly spaced left-to-right. Height is normalized per point.

  Hitmap mode records which bar screen column maps back to which TP node,
  so the end screen can place a selector and show details.
*/

/* ----- Layout knobs (single source of truth) ----- */
/* Bar height in terminal rows (total rows stacked = this). */
#define ENDGRAPH_BAR_H        20

/* One bar is ENDGRAPH_COL_W chars wide, separated by ENDGRAPH_GAP chars. */
#define ENDGRAPH_COL_W         1
#define ENDGRAPH_GAP           2

/* Graph placement (1-based terminal coordinates). */
#define ENDGRAPH_LEFT_X        6   /* left margin (column of first bar) */
#define ENDGRAPH_TOP_Y         9   /* top row of the graph area */
#define ENDGRAPH_BASE_Y   (ENDGRAPH_TOP_Y + ENDGRAPH_BAR_H - 1)

/* Console width we assume for layout math if you don't track it elsewhere. */
#define ENDGRAPH_SCREEN_COLS  120

/* Safety cap for hitmap storage (protects from pathological lists). */
#define ENDGRAPH_MAX_BARS     512

/* Hit-map from drawn bars to data nodes (for selection) */
typedef struct {
    int count;                              /* number of bars actually drawn */
    const TPNode* node[ENDGRAPH_MAX_BARS];  /* list node per drawn bar      */
    int col_x[ENDGRAPH_MAX_BARS];           /* screen column of each bar    */
} EndGraphHitmap;

/* Legacy draw (no selection) */
void endgraph_show(const TurnTracker* tp);

/* Draw + fill hitmap for interactive selection */
void endgraph_show_with_hitmap(const TurnTracker* tp, EndGraphHitmap* hm);

#endif /* END_GRAPH_H */
