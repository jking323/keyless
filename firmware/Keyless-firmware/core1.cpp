//
// Created by Jeremy King on 6/18/21.
//

#include "core1.h"
#include <stdio.h>
#include <vector>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "iostream"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include "keyless-firmware.h"
#include "pico/stdio.h"
#include "input.h"
#include "output.h"
using namespace std;
bool pin_data[3];
//input core1_obj;
extern input core1_obj;

void core1_interrupt_handler(){
	while (multicore_fifo_rvalid()){
		core1_obj.set_kill_status();
		core1_obj.set_run_status();
		core1_obj.set_start_status();
		sleep_ms(500);
	}
	multicore_fifo_clear_irq();
}

void core1_entry(){
	core1_obj.write_input_array(core1_obj.get_kill_status(), 0); //gets kill_switch status from class and adds it to vector
	core1_obj.write_input_array(core1_obj.get_run_status(), 1); //same thing but for run_status
	core1_obj.write_input_array(core1_obj.get_start_status(), 2); //again but with start button

	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_interrupt_handler);
	irq_set_enabled(SIO_IRQ_PROC1, true);
	while (1){
		tight_loop_contents();
	}
}

bool uwb_connected(){
	//dw_main();
	return true;
}
