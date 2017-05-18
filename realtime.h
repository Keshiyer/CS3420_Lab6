#ifndef __REALTIME_H__
#define __REALTIME_H__

typedef struct {
	unsigned int sec;
	unsigned int msec;
} realtime_t;

// The current time relative to process_start
extern realtime_t current_time;

#endif