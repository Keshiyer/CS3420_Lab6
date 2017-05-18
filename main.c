#include <Board_Accelerometer.h>
#include <Board_Magnetometer.h>
#include <fsl_debug_console.h>
#include <board.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "utils.h"

/************************************* Structs ****************************************/

/*Define a structure for realtime to keep track of players current time*/
typedef unsigned long realtime_t;

typedef struct map_piece map_piece_t;
/* data structure of entire map */
struct map_piece {
	int gold;
	realtime_t duration;
	map_piece_t *exits[6]; //stores 
};

typedef enum { RED, GREEN } LEDcolor;

typedef enum {
	XPOS,
	XNEG,
	YPOS,
	YNEG,
	ZPOS,
	ZNEG,
} direction_t;

/********************************** Global variables ************************************/

ACCELEROMETER_STATE state; 
//MAGNETOMETER_STATE mstate; 
int is_blocked = 0; //if 1 then blocked, else unblocked. Global to keep track of blocking
realtime_t current_time; // The current time relative to process_start
realtime_t start_time; //starts counting once user is in correct direction
realtime_t base_duration; //base duration for moving
map_piece_t * current_piece; //current map the user is in
map_piece_t * init; //starting point of map
map_piece_t * finish; //finish line of maze
int total_gold; //total gold collected by user
int max_gold; //max gold the user can collect
LEDcolor led_color; //current color of LED

/******************************* Interrupt Handler **********************************/

void PIT0_IRQHandler(void) {
	current_time++;
	PIT_TFLG0 = 1;
}

/******************************* Helper functions **********************************/

map_piece_t *make_piece(int g, realtime_t d) {
	map_piece_t *temp = malloc(sizeof(map_piece_t));
	if (temp == NULL){
		return NULL;
	}
	temp->gold = g; 
	temp->duration = d;
	memset(temp->exits, NULL, 6 * sizeof(map_piece_t *));
	return temp;
}

void construct_map() {
	map_piece_t *arr[5];

	//building map pieces
	init = make_piece(0,base_duration);
	arr[0] = make_piece(-5,base_duration);
	arr[1] = make_piece(-7,base_duration);
	arr[2] = make_piece(-2,base_duration);
	arr[3] = make_piece(-1,base_duration);
	arr[4] = make_piece(-10,base_duration);
	finish = make_piece(100,base_duration);

	//linking map pieces to make a map
	init->exits[YPOS] = arr[0];
	init->exits[ZNEG] = finish;
	arr[0]->exits[XPOS] = arr[1];
	arr[0]->exits[XNEG] = arr[2];
	arr[1]->exits[ZPOS] = arr[3];
	arr[2]->exits[XPOS] = arr[0];
	arr[3]->exits[YNEG] = arr[4];
	arr[4]->exits[ZNEG] = finish;

	max_gold = 100;
}

/* return current direction of user */
direction_t extract_direction(ACCELEROMETER_STATE state) {
	int x = abs(state.x);
	int y = abs(state.y);
	int z = abs(state.z);
	if(x > y) {
		if(x > z) {
			return state.x >= 0 ? XPOS : XNEG;
		} else {
			return state.z >= 0 ? ZPOS : ZNEG;
		}
	} else {
		if(y > z) {
			return state.y >= 0 ? YPOS : YNEG;
		} else {
			return state.z >= 0 ? ZPOS : ZNEG;
		}
	}
}

/* check map and return 0 if correct direction, 1 if wrong direction */
int check_map(ACCELEROMETER_STATE state){
	direction_t dir = extract_direction(state);
	if(current_piece->exits[dir] != NULL) {
		if(start_time == 0) {
			start_time = current_time;
		} else {
			realtime_t diff = start_time < current_time ? current_time - start_time : current_time + (ULONG_MAX - start_time);
			//checking if we have passed the duration
			if(diff > current_piece->duration) {
				current_piece = current_piece->exits[dir];
				total_gold += current_piece->gold; 
				printf("You currently have %d gold with you.", total_gold);
				start_time = 0;
			}
		}
		return 0;
	} else {
		start_time = 0;
		return 1;
	}
}

void process_start () {
	NVIC_EnableIRQ(PIT0_IRQn); //enable PIT0 Interrupts
	SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK; //Enables the clock
	SIM->SCGC6 = SIM_SCGC6_PIT_MASK; // Enable clock to PIT module 

	PIT->CHANNEL[0].LDVAL = DEFAULT_SYSTEM_CLOCK/1000;
	PIT_MCR &= ~(1 << 1);
	PIT_TCTRL0 = 1;

	total_gold = 0;
	current_time = 0;

	//INTERRUPT SETTINGS
	PIT->CHANNEL[0].TCTRL = 0x2; //set TIE bit to 1, requests interr
	PIT->CHANNEL[0].TCTRL = 0x3; //enable timer and keep interr
}

int main() {
	hardware_init();
	Accelerometer_Initialize(); 
	//Magnetometer_Initialize();
	
	base_duration = 1000;
	construct_map();
	current_piece = init;
	process_start();
	
	while(1){
		Accelerometer_GetState(&state);
		//debug_printf("%5d %5d %5d\r\n", state.x, state.y, state.z);
		is_blocked = check_map(state);
		if(LEDcolor == RED) {
			LEDRed_Toggle();
		} else {
			LEDGreen_Toggle();
		}
		if(is_blocked) {
			LEDRed_On();
		} else {
			if(current_piece == finish) {
				LEDBlue_On();
				printf("Congrats you finished the game!\n");
				printf("You ended with %d gold\n", total_gold);
				printf("The max gold you can collect is: \n", max_gold);			
				break;
			} else {
				LEDGreen_On();
			}
		}
	}
}