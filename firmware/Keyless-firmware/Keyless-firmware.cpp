#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "ultra_wide.h"
#include "iostream"
#include "engine_out.h"
#include "hardware/irq.h"

// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

//GPIO output pins
#define OUT_PRIME 1 //Fuel pump prime signal
#define OUT_FUEL 2 //Fuel pump on signal
#define OUT_VATS 5 //VATS verify
#define OUT_POWER 6 //chassis power
#define OUT_BENDIX 7 //starter bendix exciter
#define OUT_START 9 //starter motor exciter
#define OUT_LOCK 10 //door lock relay
#define OUT_UNLOCK 11 //door unlock relay

//GPIO input pins
#define IN_RUN 31 //Engine Running?
#define IN_KILL 32 //Engine Kill
#define IN_START 34 //engine start button


bool running;

void main_car_logic(){

}
int kill_switch;
bool start_button_enable;
int engine_running(){
	if (kill_switch == 1){
		start_button_enable = true;
		return 1;
	}
	else {
		start_button_enable = false;
		return 0;
	}
}

int start_engine(){
	int start_button = gpio_get(IN_START);
	if (start_button == 1){
		while()
		gpio_pull_up(OUT_BENDIX);
		gpio_pull_up(OUT_START);
	}
}

int main(){
	stdio_init_all();

	// SPI initialisation. This example will use SPI at 1MHz.
	spi_init(SPI_PORT, 1000*1000);
	gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
	gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
	gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
	gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

	// Chip select is active-low, so we'll initialise it to a driven-high state
	gpio_set_dir(PIN_CS, GPIO_OUT);
	gpio_put(PIN_CS, 1);

	wideband_main(SPI_PORT, PIN_MISO, PIN_CS, PIN_SCK, PIN_MOSI);
	irq_set_exclusive_handler(IN_KILL, PIO0_IRQ_0, engine_running());

	main_car_logic();


}