#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "iostream"
#include "hardware/irq.h"
#include "core1.h"
#include "pico/multicore.h"
#include "input.h"
#include "output.h"
//#define CATCH_CONFIG_MAIN
#include "catch2.h"

// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi1
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19




//int poll_pin_array[3] = {start_button, is_running, kill_switch};
int primed;
int started;

bool start_button_enable;
bool kill_switch_enable;
input core1_obj;
output out_obj;
int start_engine() { //this function solely handles starting the engine
	if (core1_obj.get_run_status() == 0){ //add dwb_ky_connected_function once it's complete
		if (primed == 0){ //if primed is 0 then the fuel pump will prime for 3 seconds and set the flag primed
			out_obj.set_prime_status(true);
			sleep_ms(3000);
			out_obj.set_prime_status(false);
			primed = 1;
		}
		do { //if primed is 1 then the engine will begin the start sequence
			out_obj.set_bendix_status(true);
			out_obj.set_engine_start_status(true);
			out_obj.set_fuel_status(true);
			core1_obj.get_start_status();
		}
		while (core1_obj.get_start_status() == 1);
		//as long as the start button is held the engine will turn over, afterwards if will disengage the starter
		out_obj.set_bendix_status(false);
		out_obj.set_engine_start_status(false);
		if (core1_obj.get_run_status() == 1) {
			//if the is_running pin goes high then the function will return 2 indicating the engine has started
			started = 1;
			return 2;
		}
	}
	//if the status returns true then started will be set to 1 indicating the engine is already running and it will return 2
	else{
		if (core1_obj.get_run_status() == 0) {
			started = 1;
			return 2;
		}
		else{
			//if run_status returns 0 then the engine failed to start and the function will return 1 after setting started to 0
			started = 0;
			return 1;
		}
	}
	return 0;
}


bool engine_kill(){
	if (core1_obj.get_kill_status() == true){
		out_obj.set_fuel_status(false);
		out_obj.set_pwr_status(false);
		primed = 0;
		return true;
	}
	return false;
}

bool key_connected = true;
bool security_check(){
	//key hash goes here
	/*
	 * if received_hash == key hash
	 * return true;
	 * else
	 * return false;
	 */
	return true;
}

void main_car_logic() {
	while (true) {
		while (true) {
			if (key_connected) {
				if (core1_obj.get_start_status()) {
					start_engine();
				} else {
					sleep_ms(500);
				}
			} else {
				sleep_ms(1000);
			}
			if (engine_kill() == true) {
				started = 0;
				sleep_ms(1000);
				break;
			}
		}

	}
}

int main() {
	stdio_init_all();
	multicore_launch_core1(core1_entry);
	uint32_t get_pin_array = core1_obj.get_input_array();
	multicore_fifo_push_blocking(get_pin_array);

	//set GPIO signal directions
	gpio_set_dir(IN_START, GPIO_IN);
	gpio_set_dir(IN_KILL, GPIO_IN);
	gpio_set_dir(IN_RUN, GPIO_IN);
	gpio_set_dir(OUT_PRIME, GPIO_OUT);
	gpio_set_dir(OUT_FUEL, GPIO_OUT);
	gpio_set_dir(OUT_BENDIX, GPIO_OUT);
	gpio_set_dir(OUT_START, GPIO_OUT);
	gpio_set_dir(OUT_LOCK, GPIO_OUT);
	gpio_set_dir(OUT_UNLOCK, GPIO_OUT);

	// SPI initialisation. This example will use SPI at 1MHz.
	spi_init(SPI_PORT, 1000 * 1000);
	gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
	gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
	gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
	gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

	// Chip select is active-low, so we'll initialise it to a driven-high state
	gpio_set_dir(PIN_CS, GPIO_OUT);
	gpio_put(PIN_CS, 1);

	main_car_logic();
}