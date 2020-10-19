#define F_CPU 16000000UL

#include <stdint.h>
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>
#include <avr/interrupt.h>

//Including the library to work with the 7-segments display/
#include "tm1637.h"


//defining a structure TIME...
struct time
{
	int  min,sec;
};

//Setting the values for timer of all buttons in different jumper setting..
//each row corresponds to the duration of timer values for all the buttons if the jumper is set on the position...
int timers[5][3] = {
	{ 5, 10, 20},
	{ 10,20, 40},
	{ 15,30, 60},
	{ 20,40, 80},
	{ 25,50, 100}
};

void start_clock();
void stop_clock();
char read_timer_value(int);
void set_time_on_lcd(struct time);
void set_output(char);
void set_button_led(char,char);    //char index of the led (timer number), char low/high
char read_radar();
void pause_everything();
void resume_everything();
void start_timer2();
void stop_timer2();
void InitADC();
void update_high_time();
void set_buzzer(char);
int read_buttons();
uint16_t ReadADC(uint8_t);


char time_string[10];
//for counting millisecs and secs..
uint16_t ms_count = 0;
uint16_t secs_count = 0;



int status = 0;
char timer_done = 0;  //Has the timer completed?
char running = 0;     //is any timer running?
char waiting = 0;     //is running 45 secs wait timer? (waiting before starting the timer..)
char current_running_timer = 0;   //which timer is running?... timer1 -> 1, timer2 ->2, timer3 ->3
char temp[10];

//for the buzzer beep..we update the duty-cycle after each 1000ms
uint16_t T =1000;
uint16_t high_time = 0; //high-time for buzzer beep... (out of 1000ms)
uint16_t time_passed = 0; //How much time has been passed once the buzzer has started... (as we need to increase the dutycycle with time)

uint16_t remaining_time =45; //for how much time should it wait and beep the buzzer before starting any timer..
uint16_t total_wait_time = 45;

uint16_t adc_val = 0;
uint16_t ms = 0;
char string[7];
struct time current_time = {0,0};
struct time wait_time = {0,0};
	

int main(void)
{	
	
	//initiating ADC for A6 (PIR module)
	InitADC();
	
	DDRD = 0b00011010;
	PORTD |= 0b11000000;
	DDRB = 0b00110000;
	PORTB |= 0b00000000;
	DDRC |= 0b00111000;
	PORTC |= (0b00000111);
	PORTC &= 0b11000111;
	
	uart_init(9600);
	TM1637_init(1/*enable*/, 7/*brightness*/);
	
	
	//serial_writeln("Booting...");
	
		//setting 00:00 TO the display..
		TM1637_display_digit(0,0);
		TM1637_display_digit(1,0);
		TM1637_display_digit(2,0);
		TM1637_display_digit(3, 0);
		
	//enabling interrupts..	
	sei();
	
	_delay_ms(500);
	////serial_writeln("Started....");
	//starting timer interrupt... 1ms duration
	start_clock();
	
	while (1)
	{
	
		
		//if NOT running any timer and NOT waiting before starting the timer, then read the buttons..
	if(running == 0 && waiting == 0){
	status = read_buttons();
	
	
	if (status != 0){
		////serial_write("Button pressed: ");
		//serial_writeln(status);
		
		
		//finding how much minns timer is needed.
		int mins = read_timer_value(status);
		
		//setting which timer is running..
		current_running_timer = status;
		timer_done = 0;
		
		//starting waiting...
		waiting = 1;
		
		//high time is 100ms out of 1000ms (10% dutycycle)
		high_time = 100;
		time_passed = 0;
		
		//starting timer2 (for changing the timer period and state of buzzer after some set amount of time
		start_timer2();
		
		
		current_time.min = mins;
		current_time.sec = 0;
		
		wait_time.min = 0;
		wait_time.sec = total_wait_time;
		
		
		set_time_on_lcd(wait_time);
		start_clock();
			
	}
	}
		
		//just loop
	}
}

//-------------------------------------------------------------------------
//setting time on LCD.
void set_time_on_lcd(struct time this_time){
	
	itoa(this_time.min , time_string,10);
	strcat(time_string,":");
	
	char secs_temp[3];
	itoa(this_time.sec,secs_temp,10);
	strcat(time_string,secs_temp);
	
	//serial_writeln(time_string);
	TM1637_display_digit(0,this_time.min/10);
	TM1637_display_digit(1,this_time.min%10);
	TM1637_display_digit(2,this_time.sec/10);
	TM1637_display_digit(3, this_time.sec%10);
}


//-------------------------------------------------------------------------
int read_buttons(){
	
	
	if ( (PINC & (1<<0)) == 0){
		
		_delay_ms(250);
		return 1;
	}
	if ((PINC & (1<<1)) == 0){
		_delay_ms(250);
		return 2;
	}
	if ((PINC & (1<<2)) == 0){
		_delay_ms(250);
		return 3;
	}
	
	return 0;
}

//-------------------------------------------------------------------------
//TIMER Interrupt using TIMER1 for 1ms
void start_clock(){
	TCCR1A = 0x00;
	
	TIMSK1 = 0x01;
	TCNT1 = 63536;
	TCCR1B = 0b00000010;
}

//==============================
void stop_clock(){
	TCNT1 = 63536;
	TCCR1B = 0b00000000;
}

//=============================
ISR (TIMER1_OVF_vect){
	
	//1ms timer interrupt..
	//counting millisecs.
	ms_count++;
	
	if (running == 1){
		//if any TIMER is running,...
		
		//read radar status..
	char radar_val = read_radar();
		//read PIR status..
	uint16_t adc_val = ReadADC(6);
	
	//if RADAR is high or PIR is LOW..
	if(radar_val  == 1 || adc_val < 512){
		
		//PAUSE EVERYTHING..
		pause_everything();
		
		
		//serial_writeln("Checking...");
		
		//WAIT UNTIL both of them go untriggered.
		while(radar_val!=0 || adc_val < 512){
			//serial_writeln("stopped..");
			radar_val = read_radar();
			adc_val = ReadADC(6);
			_delay_ms(50);
		}
		
		//now the person has moved out...
		/// beep the buzzer for 22 secs before continuing the UV light operation..
		for (char x = 0; x < 22 ; x++){
			set_buzzer(1);
			_delay_ms(800);
			set_buzzer(0);
			_delay_ms(200);
		}
		
		//resuming everything and setting ON the uv lamp.
		resume_everything();
		set_output(1);
		
	}
	}
	
	//if ms = 1000, increment the Secs.
	if(ms_count == 1000){
		ms_count = 0;
		
		//if waiting??	
		if(waiting == 1){
			wait_time.sec --;
			
			
			if(wait_time.sec < 0){
				wait_time.sec =0;
				wait_time.min --;
					
				//if we are done WAITING..
				if(wait_time.min<0){
					
					
					wait_time.min = 0;
					wait_time.sec = 0;
					set_time_on_lcd(wait_time);
					
					_delay_ms(300);
					//WRITING THE timer time on the LCD..
					set_time_on_lcd(current_time);
					
					
					waiting = 0;
					
					//DONE WAiting, now RUNNING...
					running = 1;
					
					//SETTING the corresponding timer button LED ON
					set_button_led(current_running_timer,1);
					
					//stopping timer2 
					stop_timer2();
					
					//turning OFF the buzzer
					set_buzzer(0);
					
					//turning ON the UV lamp...
					set_output(1);
					
					return ;
					
				}
				else{
					set_time_on_lcd(wait_time);
				}
				
			}
			else{
				set_time_on_lcd(wait_time);
			}
		}
		
		
		//if already running...
		if (running == 1){
			
			//decrement the run time...
			current_time.sec --;
			
			if (current_time.sec<0){
				current_time.min --;
				current_time.sec = 59;
				
				if(current_time.min<0){
					
					//means timer is done....
					current_time.min = 0;
					current_time.sec = 0;
					
					set_time_on_lcd(current_time);
					
					//setting off status, settting UV lamp OFF.
					running = 0;
					set_output(0);
					set_button_led(current_running_timer,0);
					high_time=0;
					
					//REMAINING watiting time (before starting any timer = total_Wait_time
					remaining_time =total_wait_time;
					
				}
				else{
					set_time_on_lcd(current_time);
				}
				
				
			}
			else{
				set_time_on_lcd(current_time);
			}
		}
		
		
	}
	TCNT1 = 63536;
}

//=============================
char read_timer_value(int index){
	

	//reading which JUMPER is set...
	//index will be the index of button pressed...
	// 1 -> Timer1   ,  2 -> Timer2 ......
	
	//serial_writeln("Reading Jumper...");
	
	int temp;
	
	if (  !(PINB & (1 << PINB2))  ){
		temp = 0;
		//serial_writeln("Jumper 5");
		return timers[temp][index-1];
	}
	if (  !(PINB & (1 << PINB1))  ){
		temp = 1;
		//serial_writeln("Jumper 10");
		return timers[temp][index-1];
	}
	if (!(PINB & (1 << PINB0))){
		temp = 2;
		//serial_writeln("Jumper 15");
		return timers[temp][index-1];
	}
	if (!(PIND & (1 << PIND7))){
		temp = 3;
		//serial_writeln("Jumper 20");
		return timers[temp][index-1];
	}
	if (!(PIND & (1 << PIND6))){
		temp = 4;
		//serial_writeln("Jumper 25");
		return timers[temp][index-1];
	}
	
	
	//default case...
	//serial_writeln("Default case:");
	//serial_writeln("Jumper 5");
	return timers[0][index-1];
}


//=============================================
void set_output(char val){
	
	if (val == 0){
		PORTB &= !(1<<5);
		//serial_writeln("UV LAMP OFF..");
	}
	if (val == 1){
		//serial_writeln("UV LAMP ON..");
		PORTB |= (1<<5);
	}
}

//============================================
void set_button_led(char index,char state){
	
	switch(index){
		
		case 1:
			if(state == 1){
				PORTC |= 0b00001000;
				
			}
			else{
				PORTC &= 0b11110111;
			}
			break;
		
		case 2:
		   if(state == 1){
			   PORTC |= 0b00010000;
			   
		   }
		   else{
			   PORTC &= 0b11101111;
			   
		   }
		   break;
		 case 3: 
		  if(state == 1){
			  PORTC |= 0b00100000;
		  }
		  else{
			  PORTC &= 0b11011111;
			  
		  }
		  break;
		
	}
}






//===============================================================


void start_timer2(){
	TCCR2A = 0x00;
	TCCR2B = 0x04;
	TIMSK2 = 0x01;
	TCNT2 = 255-249;
}

//================================================================


void stop_timer2(){
	TCCR2A = 0x00;
	TCCR2B = 0x00;
	TIMSK2 = 0x01;
	TCNT2 = 255-249;
}

//-----------------------------------------------------------------

void set_buzzer(char st){
	
	if (st ==1){
		PORTB |= (1<<4);
		
	}
	if (st == 0){
		PORTB &= !(1<<4);
	}
}

//--------------------------------------------------interrupt for timer2
ISR (TIMER2_OVF_vect){
	
	ms++;
	time_passed++;
	
	//setting on the BUZZER and th button LED for the HIGH time value..
	if(time_passed == high_time){
		time_passed = 0;
		set_buzzer(0);
		
		if(waiting == 1){
			set_button_led(current_running_timer,0);
		}
		
	}
	
	//UNit second passed..
	if(ms == T){
		////serial_writeln("Second passed...");
		
		//setting on the BUZZER and LED button
		if(waiting == 1){
			set_button_led(current_running_timer,1);
		}
		set_buzzer(1);
		
		
		time_passed = 0;
		
		//decrementing remaining time secs..
		remaining_time--;
		
		//updating the duty cycle..
		update_high_time();
		
		ms = 0;
	}
	
	
	TCNT2 = 255-249;
}


//---------------------------------------------------------------------------------
void update_high_time(){
	
	//taking high time as a function of HOW MUCH time is remaining till the buzzer start....
	// time remaining = total_wait time => Duty cycle = 10% = 100ms
	// time remaining  < 3 secs... duty cycle = 100% = 1000ms
	high_time = 100 + (int)(900/(total_wait_time-3))*(total_wait_time - remaining_time);
	
	if(high_time > 1000){
		high_time = 1000;
		
		
	}
}

//-----------------------------------------------------------
char read_radar(){
	char radar_status = 0;
	if (PIND & (1<<5)){
		//serial_writeln("Radar stopped..");
		radar_status = 1;
		return radar_status;
	}
	else{
		
		radar_status = 0;
		return radar_status ;
	}
}
//----------------------------------------------------------

void pause_everything(){
	TCCR1B = 0b00000000;
	TCCR2B = 0b00000000;

set_output(0);
set_buzzer(1);

}
//-----------------------------------------------------------
void resume_everything(){
	TCCR1B = 0b00000010;
	TCCR2B = 0b00000100;
	set_output(1);
	set_buzzer(0);
	stop_timer2();
}
//------------------------------------------------------------
void InitADC()
{
	// Select Vref=AVcc
	ADMUX |= (1<<REFS0);
	//set prescaller to 128 and enable ADC
	ADCSRA |= (1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0)|(1<<ADEN);
}
//----------------------------------------------------------

uint16_t ReadADC(uint8_t ADCchannel)
{
	//select ADC channel with safety mask
	ADMUX = (ADMUX & 0xF0) | (ADCchannel & 0x0F);
	//single conversion mode
	ADCSRA |= (1<<ADSC);
	// wait until ADC conversion is complete
	while( ADCSRA & (1<<ADSC) );
	return ADC;
}