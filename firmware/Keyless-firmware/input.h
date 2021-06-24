//
// Created by Jeremy King on 6/23/21.
//

#ifndef KEYLESS_FIRMWARE_INPUT_H
#define KEYLESS_FIRMWARE_INPUT_H
//GPIO input pins
#define IN_RUN 8 //Engine Running?
#define IN_KILL 9 //Engine Kill
#define IN_START 10 //engine start button
#include "hardware/gpio.h"
#include <vector>
#include <array>
using namespace std;

class input {
private:
	uint32_t start_button;
	uint32_t is_running;
	uint32_t kill_switch;
	uint32_t input_array[3] = {};
public:
	void set_run_status() {
		if (gpio_get(IN_RUN)){
			is_running = 1;
		}
		else {
			is_running = 0;
		}
	};
	void set_start_status(){
		if (gpio_get(IN_START)){
			start_button = 1;
		}
		else {
			start_button = 0;
		}
	};
	void set_kill_status(){
		if (gpio_get(IN_KILL)){
			kill_switch = 1;
		}
		else {
			kill_switch = 0;
		}
	};
	void write_input_array(uint32_t setter, int pos){
		input_array[pos] = setter;
	};
	uint32_t get_run_status(){
		return is_running;
	};
	uint32_t get_start_status(){
		return start_button;
	};
	uint32_t get_kill_status(){
		return kill_switch;
	};
	uint32_t get_input_array(){
		return *input_array;
	};
};


#endif //KEYLESS_FIRMWARE_INPUT_H
