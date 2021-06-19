//
// Created by Jeremy King on 6/18/21.
//

#include "core1.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
//#include "iostream"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include "keyless-firmware.h"
#include "pico/stdio.h"
#include "uwb_connect.h"
/*
//GPIO input pins
#define IN_RUN 8 //Engine Running?
#define IN_KILL 9 //Engine Kill
#define IN_START 10 //engine start button
*/

int start_button;
int is_running;
int kill_switch;

void core1_interrupt_handler(){
	while (multicore_fifo_rvalid()){
		is_running = gpio_get(IN_RUN);
		kill_switch = gpio_get(IN_KILL);
		start_button = gpio_get(IN_START);
		sleep_ms(1000);
	}
	multicore_fifo_clear_irq();
}

void core1_entry(){
	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_interrupt_handler);
	irq_set_enabled(SIO_IRQ_PROC1, true);
	while (1){
		tight_loop_contents();
	}
}

bool uwb_connected(){
	dw_main();
}
