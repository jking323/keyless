#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "ultra_wide.h"
#include "iostream"
#include "hardware/irq.h"
#include "pico/multicore.h"

// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi1
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

//GPIO output pins
#define OUT_PRIME 0 //Fuel pump prime signal
#define OUT_FUEL 1 //Fuel pump on signal
#define OUT_VATS 2 //VATS verify
#define OUT_POWER 3 //chassis power
#define OUT_BENDIX 4 //starter bendix exciter
#define OUT_START 5 //starter motor exciter
#define OUT_LOCK 6 //door lock relay
#define OUT_UNLOCK 7 //door unlock relay

//GPIO input pins
#define IN_RUN 8 //Engine Running?
#define IN_KILL 9 //Engine Kill
#define IN_START 10 //engine start button

volatile int start_button;
volatile int is_running;
volatile int kill_switch;
int key_value

void core1_interrupt_handler(){
	while (multicore_fifo_rvalid()){
		is_running = gpio_get(IN_RUN);
		kill_switch = gpio_get(IN_KILL);
		start_button = gpio_get(IN_START);
		key_value = ultra_wide_key();
		sleep_ms(1000);
	}
	multicore_fifo_clear_irq();
}

void core1_entry(){
	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_interrupt_handler(), true);
	irq_set_enabled(SIO_IRQ_PROC1, true);
	while (1){
		tight_loop_contents();
	}
}

bool start_button_enable;
int engine_start_switch(){
	while (is_running != 1) {
		int kill_switch_enable;
		if (is_running == 1) {
			kill_switch_enable = 1;
		}
		else {
			kill_switch_enable = 0;
		}
		if (kill_switch_enable == 0 && is_running == 0) {
			start_button_enable = true;
			return 1;
		}
		else {
			start_button_enable = false;
			return 0;
		}
	}
}

int start_engine() {
	if (engine_start_switch() == 1 && start_button==1) {
		while (start_button == 1) {
			gpio_pull_up(OUT_START);
			gpio_pull_up(OUT_BENDIX);
		}
	}
	else {
		return 0;
	}
}

void engine_kill(){
	int kill_engine;
	if (kill_switch == 1){
		kill_engine = 1;
	}
	else {
		kill_engine = 0;
	}
	if (kill_engine == 1){
		gpio_put(OUT_FUEL, false);
		gpio_put(OUT_POWER, false);
	}
}

bool security_check(){
	//key hash goes here

	/*
	 * if received_hash == key hash
	 * return true;
	 * else
	 * return false;
	 */
}

void main_car_logic(){

}

int main(){
	stdio_init_all();

	multicore_launch_core1(core1_entry);
	multicore_fifo_push_blocking(raw);

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
	spi_init(SPI_PORT, 1000*1000);
	gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
	gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
	gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
	gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

	// Chip select is active-low, so we'll initialise it to a driven-high state
	gpio_set_dir(PIN_CS, GPIO_OUT);
	gpio_put(PIN_CS, 1);


	main_car_logic();


}