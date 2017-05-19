#include <Board_Accelerometer.h>
#include <Board_Magnetometer.h>
#include <fsl_debug_console.h>
#include <board.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "utils.h"

void line_divide();

/************************************* Structs ****************************************/

/*Define a structure for realtime to keep track of players current time*/
typedef unsigned long realtime_t;

/* data structure of entire map */
typedef struct map_piece map_piece_t;

struct map_piece {
	int gold;
	realtime_t duration;
	map_piece_t *exits[6]; //stores 
};

/* enum to determine which led is on */
typedef enum { RED, GREEN } LEDcolor;

/* enum for direction of user */
typedef enum {
	XPOS,
	XNEG,
	YPOS,
	YNEG,
	ZPOS,
	ZNEG,
} direction_t;

/******************************* Global variables ****************************/

ACCELEROMETER_STATE state; 
realtime_t current_time; // The current time relative to process_start
realtime_t start_time; //starts counting once user is in correct direction
realtime_t base_duration = 3000; //base duration for moving
map_piece_t * current_piece; //current map the user is in
map_piece_t * init; //starting point of map
map_piece_t * finish; //finish line of maze
int is_blocked = 0; //if 1 then user is blocked, else unblocked
int total_gold; //total gold collected by user
int total_pieces; //total number of pieces in the maze
int max_gold; //max gold the user can collect
LEDcolor led_color; //current color of LED
char path[1000];

/****************************** Interrupt Handler ****************************/

void PIT0_IRQHandler(void) {
	current_time++;
	PIT_TFLG0 = 1;
}

/*************************** Map creation functions **************************/

/* Initializes a single piece of the map */
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

/* function to make a set map */
void construct_set_map() {
	map_piece_t *arr[5];

	//building map pieces
	init = make_piece(0, base_duration);
	arr[0] = make_piece(-5, base_duration);
	arr[1] = make_piece(-7, base_duration);
	arr[2] = make_piece(-2, base_duration);
	arr[3] = make_piece(-1, base_duration);
	arr[4] = make_piece(-10, base_duration);
	finish = make_piece(100, base_duration);

	//linking map pieces to make a map
	init->exits[YPOS] = arr[0];
	init->exits[ZNEG] = finish;
	arr[0]->exits[XPOS] = arr[1];
	arr[0]->exits[XNEG] = arr[2];
	arr[1]->exits[ZPOS] = arr[3];
	arr[2]->exits[XPOS] = arr[0];
	arr[3]->exits[YNEG] = arr[4];
	arr[4]->exits[ZNEG] = finish;

	total_pieces = 7;
	max_gold = 100;
}

/* function to create a random map */
void construct_map_random(int num, int deviation, int fin_gold) {
	srand(num); // initialising random number generator
	
	int n = num-2;
	map_piece_t *arr[n];

	//building map pieces
	init = make_piece(0, base_duration);
	for(int i = 0; i < n; i++) {
		int gold = -(rand() % 10); 
		int dur = rand() % deviation;
		arr[i] = make_piece(gold, base_duration+dur);
	}
	finish = make_piece(fin_gold, base_duration);

	//linking map pieces to make a map
	int has_exit = 0; //0 if no exit, 1 if at least one exit
	int can_finish = 0; //0 if no piece can reach finish, 1 otherwise
	init->exits[ZNEG] = finish;
	init->exits[YPOS] = arr[0];
	for(int i = 0; i < n; i++) {
		for(int j = 0; j < 6; j++){
			int dec = rand() % 2; //if 1 then get an exit, if 0 then no exit
			if(dec) {
				int rand_piece = rand() % n; //determine exit at random
				if(rand_piece == i) {
					//jump to finish if we get the same piece
					arr[i]->exits[j] = finish; 
					can_finish = 1;
				} else {
					arr[i]->exits[j] = arr[rand_piece];
				}
				has_exit = 1;
			} else {
				arr[i]->exits[j] = NULL;
			} 
			//give an exit if no exit so far
			if(j == 5 && !has_exit) {
				int rand_piece = rand() % n; //determine exit at random
				if(rand_piece == i) {
					//jump to finish if we get the same piece
					arr[i]->exits[j] = finish; 
					can_finish = 1;
				} else {
					arr[i]->exits[j] = arr[rand_piece];
				}
			}
		}
	}
	if(!can_finish) {
		int p = rand() % n;
		int d = rand() % 6;
		arr[p]->exits[d] = finish;
	}
	total_pieces = num;
	max_gold = fin_gold;
}

/*************************** Map execution functions *************************/

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


/*Converts a direction to a string*/
char* direction_to_string(direction_t dir) {
	switch(dir) {
		case XPOS: return "Right ";
		case XNEG: return "Left ";
		case YPOS: return "Forward ";
		case YNEG: return "Backward ";
		case ZPOS: return "Up ";
		case ZNEG: return "Down ";
	}
}

/*Print a path*/
void print_path(char * next_path) {
	printf("%s ", strcat(path, next_path));
	printf(" \r\n");
}

/* check map and return 0 if correct direction, 1 if wrong direction */
int check_map(ACCELEROMETER_STATE state) {
	direction_t dir = extract_direction(state);
	if(current_piece->exits[dir] != NULL) {
		if(start_time == 0) {
			start_time = current_time;
		} else {
			//computing difference between current time and start time
			realtime_t diff = 
				start_time < current_time ? current_time - start_time : 
				current_time + (ULONG_MAX - start_time);
			//checking if we have passed the duration
			if(diff > current_piece->duration) {
				current_piece = current_piece->exits[dir];
				total_gold += current_piece->gold; 
				printf("You currently have %d gold with you.\r\n", total_gold);
				printf("Directions followed: \r\n");
				print_path(direction_to_string(dir));
				line_divide();
				start_time = 0;
			}
		}
		return 0;
	} else {
		start_time = 0;
		return 1;
	}
}

/*************************** Main running functions **************************/

/* line divide */
void line_divide() {
	printf("------------------------------------------\r\n");
}

/* function to initailise clocks and start the maze */
void start_maze () {
	NVIC_EnableIRQ(PIT0_IRQn); //enable PIT0 Interrupts
	SIM->SCGC6 |= SIM_SCGC6_PIT_MASK; // Enable clock to PIT module 

	PIT->CHANNEL[0].LDVAL = DEFAULT_SYSTEM_CLOCK/1000;
	PIT_MCR &= ~(1 << 1);

	total_gold = 0;
	current_time = 0;

	//INTERRUPT SETTINGS
	PIT->CHANNEL[0].TCTRL = 3; //start timer
}

/* function to get user input */
void intro() {
	printf("Welcome to the MAZE!\r\n");
	line_divide();
	char input;
	printf("Would you prefer a set map or a random map? \r\n");
	printf("For a set map type S and for a random map type R: ");
	scanf("%c", &input);
	printf("\r\n");
	line_divide();
	if(input == 'S') {
		printf("The set maze will now begin!\r\n");
		line_divide();
		construct_set_map();
	} else {
		int n, deviation, fin_gold;
		printf("Enter number of pieces of maze: ");
		scanf("%d", &n);
		printf("\r\n");
		printf("Enter max deviation of duration between pieces: ");
		scanf("%d", &deviation);
		printf("\r\n");
		printf("Enter amount of gold for final piece: ");
		scanf("%d", &fin_gold);
		printf("\r\n");
		line_divide();
		printf("The random maze will now begin!\r\n");
		line_divide();
		printf("Pieces, Deviation, Max Gold: %d %d %d \r\n", n, deviation, fin_gold);
		line_divide();
		construct_map_random(n,deviation,fin_gold);
	}
}

int main() {
	hardware_init();
	LED_Initialize();
	Accelerometer_Initialize(); 
	
	intro(); //get user data and make map
	current_piece = init;
	start_maze();
	
	while(1){
		Accelerometer_GetState(&state);
		is_blocked = check_map(state);
		//turn LED off before turning new one on later
		if(led_color == RED) {
			LEDRed_Toggle();
		} else {
			LEDGreen_Toggle();
		}
		//if not finished after a long time then assist
		/*if(abs(total_gold) > 10*total_pieces) {
			direction_t dir = find_optimal_route();
			line_divide();
			printf("HINT: move in direction \r\n");
			line_divide();
		} */
		
		if(is_blocked) {
			LEDRed_On();
		} else {
			if(current_piece == finish) {
				LEDBlue_On();
				printf("Congrats you finished the game!\r\n");
				printf("You ended with %d gold\r\n", total_gold);
				printf("The max gold you can collect is: %d \r\n", max_gold);	
				line_divide();		
				break;
			} else {
				LEDGreen_On();
			}
		}
	}
}
