//
// Created by Jeremy King on 6/23/21.
//

#ifndef KEYLESS_FIRMWARE_OUTPUT_H
#define KEYLESS_FIRMWARE_OUTPUT_H
//GPIO output pins
#define OUT_PRIME 0 //Fuel pump prime signal
#define OUT_FUEL 1 //Fuel pump on signal
#define OUT_VATS 2 //VATS verify
#define OUT_POWER 3 //chassis power
#define OUT_BENDIX 4 //starter bendix exciter
#define OUT_START 5 //starter motor exciter
#define OUT_LOCK 6 //door lock relay
#define OUT_UNLOCK 7 //door unlock relay


class output {
private:
	bool prime;
	bool fuel;
	bool vats;
	bool pwr;
	bool bendix;
	bool engine_start;
	bool door_lock;
	bool door_unlock;
public:
	void set_prime_status(bool status){
		prime = status;
		gpio_put(OUT_PRIME, status);
	};
	void set_fuel_status(bool status){
		fuel = status;
		gpio_put(OUT_FUEL, status);
	};
	void set_vats_status(bool status){
		vats = status;
		gpio_put(OUT_VATS, status);
	};
	void set_pwr_status(bool status){
		pwr = status;
		gpio_put(OUT_POWER, status);
	};
	void set_bendix_status(bool status){
		bendix = status;
		gpio_put(OUT_BENDIX, status);
	};
	void set_engine_start_status(bool status){
		engine_start = status;
		gpio_put(OUT_START, status);
	};
	void set_lock_status(bool status){
		door_lock = status;
		gpio_put(OUT_LOCK, status);
	};
	void set_unlock_status(bool status){
		door_unlock = status;
		gpio_put(OUT_UNLOCK, status);
	};
	bool get_prime_status(){
		return prime;
	};
	bool get_fuel_status(){
		return fuel;
	};
	bool get_vats_status(){
		return vats;
	};
	bool get_pwr_status(){
		return pwr;
	};
	bool get_bendix_status(){
		return bendix;
	};
	bool get_start_status(){
		return engine_start;
	};
	bool get_lock_status(){
		return door_lock;
	};
	bool get_unlock_status(){
		return door_unlock;
	};
};


#endif //KEYLESS_FIRMWARE_OUTPUT_H
