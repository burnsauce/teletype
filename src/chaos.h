#ifndef CHAOS_H
#define CHOAS_H
#include <stdint.h>


typedef enum {
  LOGISTIC, // logistic map
  CUBIC,    // cubic map
  HENON,    // henon map
  CELLULAR  // 1-d binary cellular automaton
} chaos_algo_t;


// keep value and parameter in both integer and float formats
// this way, can switch algos on the fly and re-initialize (dunno if this is possible anyway)
typedef struct {
  int16_t ix;      // state value in integer format 
  float fx;        // normalized floating point state value (as needed)
  int16_t ir;        // parameter value in integer format
  float fr;          // floating-point parm value (as needed)
  float fx0;          // state history (as needed)
  float fx1;          // state history (as needed)
  chaos_algo_t algo; // current algorithm
} chaos_state_t;

void chaos_set_val(int16_t);
int16_t chaos_get_val(void);
void chaos_set_r(int16_t);
int16_t chaos_get_r(void);

#endif
