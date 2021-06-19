//
// Created by Jeremy King on 6/9/21.
//

#ifndef KEYLESS_FIRMWARE_KEYLESS_FIRMWARE_H
#define KEYLESS_FIRMWARE_KEYLESS_FIRMWARE_H
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
#define IN_START 10


#endif //KEYLESS_FIRMWARE_KEYLESS_FIRMWARE_H
