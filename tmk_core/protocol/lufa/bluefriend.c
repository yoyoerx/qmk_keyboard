/*
Bluefruit LE UART Friend Protocol for TMK firmware
Author: Benjamin Gould, 2013
        Jack Humbert, 2015
        Kevin Wepasnick, 2016
Based on code Copyright 2011 Jun Wako <wakojun@gmail.com>
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include <string.h>
#include <util/delay.h>
#include "report.h"
#include "../serial.h"
#include "bluefriend.h"

#define BLUEFRIEND_KRO_MAX 5
#define BLUEFRIEND_RETRY_MAX 1
#define PREPEND_LENGTH 19
#define BLUEFRIEND_WAIT 15
#define CMD_LEN_MAX 40

#define HIGHBITMASK 0b11110000
#define LOWBITMASK 0b00001111

const uint8_t bluefriend_at_prepend[PREPEND_LENGTH] = {'A','T','+','B','L','E','K','E','Y','B','O','A','R','D','C','O','D','E','='};


void bluefriend_factoryreset(void);
void bluefriend_enablehidmode(void);
void bluefriend_disablehidmode(void);
void bluefriend_name_set(void);

inline void bluefriend_clear_buffer(void);
void bluefriend_reset(void);
inline void bluefriend_ready(void);
inline void bluefriend_mode_switch(void);

void bluefriend_set_cmd_mode(void);
void bluefriend_send_cmd(const uint8_t* cmd);

void bluefriend_keyboard_send(report_keyboard_t *report);

inline void bluefriend_clear_buffer(void){
	while(serial_recv()!=0);
}

inline void bluefriend_mode_switch(void){
	serial_send('+');
	serial_send('+');
	serial_send('+');
	serial_send('\n');
}

inline void bluefriend_ready(void){
	serial_send('A');
	serial_send('T');
	serial_send('\n');
}

void bluefriend_reset(void){
	serial_send('A');
	serial_send('T');
	serial_send('Z');
	serial_send('\n');
}

void bluefriend_factoryreset(void){
	const uint8_t cmd[] = "AT+FACTORYRESET\n";
	bluefriend_send_cmd(cmd);
	bluefriend_reset();
}

void bluefriend_enablehidmode(void){
	const uint8_t cmd[] = "AT+BLEHIDEN=1\n";
	bluefriend_send_cmd(cmd);
	bluefriend_reset();
}

void bluefriend_disablehidmode(void){
	const uint8_t cmd[] = "AT+BLEHIDEN=0\n";
	bluefriend_send_cmd(cmd);
	bluefriend_reset();
}

void bluefriend_name_set(void){
	const uint8_t cmd[] = "AT+GAPDEVNAME=Planck Keyboard\n";
	bluefriend_send_cmd(cmd);
	bluefriend_reset();
}

void bluefriend_set_cmd_mode(void){
	uint8_t bluefriend_sucess = 0;
	uint8_t retry = 0;
	uint8_t uart_in = 0;
	do{
		//ready to talk?
		bluefriend_ready();

		_delay_ms(BLUEFRIEND_WAIT);
		
		//read the response, should be "OK", anything else is a failure.
		uart_in = serial_recv();
		if (uart_in == 'O') { // O as in OK
			bluefriend_sucess = 1;
			bluefriend_clear_buffer();
			break;
		} else { // E as in ERROR or anything else
			bluefriend_sucess = 0;
			bluefriend_mode_switch(); //make sure we are in cmd mode
			_delay_ms(BLUEFRIEND_WAIT);
		}

		bluefriend_clear_buffer(); //read out the rest of the UART ring buffer
		retry++;
	} while((!bluefriend_sucess) && (retry < BLUEFRIEND_RETRY_MAX));
}

void bluefriend_send_cmd(const uint8_t* cmd){
	uint8_t bluefriend_sucess = 0;
	uint8_t retry = 0;
	uint8_t uart_in = 0;
	uint8_t cmd_length = strnlen((const char*) cmd, CMD_LEN_MAX);
	
	bluefriend_set_cmd_mode();

	do{
		for(uint8_t i = 0; i<cmd_length;i++){ 
			serial_send(cmd[i]);
		}

		//wait for the Friend to process and respond.
		_delay_ms(BLUEFRIEND_WAIT);

		//read the response, should be "OK", anything else is a failure.
		uart_in = serial_recv();
		if (uart_in == 'O') { // O as in OK
			bluefriend_sucess = 1;
			bluefriend_clear_buffer();
			break;
		} else { // E as in ERROR or anything else
			bluefriend_sucess = 0;
		}

		bluefriend_clear_buffer(); //read out the rest of the UART ring buffer
		retry++;
	} while((!bluefriend_sucess) && (retry < BLUEFRIEND_RETRY_MAX));
}

void bluefriend_send_txt(const char* txt){
	uint8_t txt_length = strnlen(txt, CMD_LEN_MAX);
	for(uint8_t i = 0; i<txt_length;i++){ 
		serial_send(txt[i]);
	}
}

void bluefriend_keyboard_send(report_keyboard_t *report)
{
	uint8_t highbyte = 0;
	uint8_t lowbyte = 0;
	uint8_t bluefriend_sucess = 0;
	uint8_t retry = 0;
	uint8_t uart_in = 0;
	do{
		//send the raw HID code to the BLE UART Friend as an AT command
		// command is formated as AT+BLEKEYBOARDCODE=XX-00-YY-ZZ
		// where XX is the modifier code ie "02" for shift
		// 00 must be 00
		// YY is the keycode of the pressed key
		// ZZ is the keycode of the next pressed key
		// this can continue up to 6 keycodes
		for(uint8_t i = 0; i<PREPEND_LENGTH;i++){ //sending prepend message of "AT+BLEKEYBOARDCODE="
			serial_send(bluefriend_at_prepend[i]);
		}
		for (uint8_t i = 0; i < BLUEFRIEND_KRO_MAX; i++) {
			//convert the raw hex keycodes into ascii
			highbyte = (HIGHBITMASK & report->raw[i])>>4;
			if(highbyte <=9) {
				highbyte += 0x30; //0x0 turns into 0x30 which is "0"
			} else {
				highbyte += 0x37; //0xA turns into 0x41 which is ascii "A"
			}
			lowbyte = LOWBITMASK & report->raw[i];
			if(lowbyte <=9) {
				lowbyte += 0x30; //0x0 turns into 0x30 which is "0"
			} else {
				lowbyte += 0x37; //0xA turns into 0x41 which is ascii "A"
			}
			//send the high and low bytes of the keycode
			serial_send(highbyte);
			serial_send(lowbyte);
			if (i < (BLUEFRIEND_KRO_MAX - 1)) serial_send('-'); //add a hyphen unless it is the last code.
		}
		serial_send('\n'); //signals the FRIEND that it should process the AT command

		//wait for the Friend to process and respond.
		_delay_ms(BLUEFRIEND_WAIT);

		//read the response, should be "OK", anything else is a failure.
		uart_in = serial_recv();
		if (uart_in == 'O') { // O as in OK
			bluefriend_sucess = 1;
			bluefriend_clear_buffer();
			break;
		} else { // E as in ERROR or anything else
			bluefriend_sucess = 0;
		}

		bluefriend_clear_buffer(); //read out the rest of the UART ring buffer
		retry++;
	} while((!bluefriend_sucess) && (retry < BLUEFRIEND_RETRY_MAX)); //try to send until Friend says "OK" or we reach our limit.
}
 