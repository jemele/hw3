// Joshua Emele <jemele@acm.org>
// Tristan Monroe <twmonroe@eng.ucsd.edu>
#ifndef _irobot_h_
#define _irobot_h_

#include "search.h"
#include "uart.h"

typedef struct {
    int bumper;
    int wall;
} irobot_sensor_t;

// This may only be polled at an interval >= 15ms.
void irobot_read_sensor(uart_t *uart, irobot_sensor_t *s);

// Move in a straight line.
void irobot_drive_straight_rate(uart_t *uart, s16 rate);
int irobot_drive_straight_sense(uart_t *uart, s16 distance_mm);

// In place rotation.
void irobot_rotate_left(uart_t *uart);
void irobot_rotate_right(uart_t *uart);

// Please don't change these values. The direction_rotate function explicitly
// relies on the supplied encoding.
typedef enum {
    direction_left    = 0,
    direction_forward = 1,
    direction_right   = 2,
    direction_back    = 3,
} direction_t;

// Handy for debugging.
const char *direction_t_to_string(direction_t v);

// Calculate the direction based on the delta. This assumes we're starting
// forward.
int direction_from_delta(int dx, int dy);

// Calculate the number and type of rotation needed to achieve the next
// direction from the current direction.
void direction_rotation(int current, int next, char *rotation, int *count);

// Move along the path. This assumes the path is well defined, i.e., path->next
// is available.
void irobot_move(uart_t *uart, search_cell_t *path);

#endif
