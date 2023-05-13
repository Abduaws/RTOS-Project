#include <stdint.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include "task.h"
#include <time.h>
#include <stdbool.h>
#include <semphr.h>
#include <driverlib/gpio.c>
#include <driverlib/gpio.h>
#include <driverlib/sysctl.h>
#include <inc/hw_ints.h>
#include "tm4c123gh6pm.h"
#include "buttons.h"

#define Set_Bit(reg, bit) {reg |= (1U << bit);}
#define Clear_Bit(reg, bit) {reg &= ~(1U << bit);}
#define Toggle_Bit(reg, bit) {reg ^= (1U << bit);}
#define Get_Bit(reg, bit) ((reg & (1U << bit)) >> bit);
#define PortF_IRQn ((IRQn_Type) 30 )
#define PortB_IRQn ((IRQn_Type) 1 )


struct Window {
	bool isFullyClosed;
	bool isFullyOpened;
	bool isLocked;
	bool autoMode;
};

static struct Window CarWindow;
static struct Button PortC_Buttons[4];
static SemaphoreHandle_t lockSemaphore;
static SemaphoreHandle_t jamSemaphore;
static SemaphoreHandle_t autoModeSemaphore;


void CheckButtons(void *p);

void init(void);
void initStructs(void);

void jamHandler(void *p);
void autoModeHandler(void *p);

void jamInterrupt(void);
void autoModeInterrupt(void);

bool hasPermission(enum User user);
void moveWindow(struct Button currBtn);
void limitSwitchHandler(int limitSwitch);
void stopWindow(void);
void delayMS(int ms);

void delayMS(int ms){
	int CTR = 0;
	while(CTR < ms*3100) CTR++;
}

int main(void){
	initStructs();
	init();
	
	lockSemaphore = xSemaphoreCreateBinary();
	jamSemaphore = xSemaphoreCreateBinary();
	autoModeSemaphore = xSemaphoreCreateBinary();
	
	xTaskCreate(CheckButtons, "CheckButtons", 100, NULL, 1, NULL);
	xTaskCreate(jamHandler, "jamHandler", 100, NULL, 2, NULL);
	xTaskCreate(autoModeHandler, "autoModeHandler", 100, NULL, 2, NULL);
	
	vTaskStartScheduler();
	return 0;
}

void CheckButtons(void *p){
	
	for( ; ; ){
		
		bool isZero = true;
		
		uint32_t bit = Get_Bit(GPIO_PORTB_DATA_R, 4);
		if (bit == 0) {
			CarWindow.isLocked = true;
		}
		else if (bit == 1) {
			CarWindow.isLocked = false;
		}
		
		for(int i = 4; i < 8; i++){
			bit = Get_Bit(GPIO_PORTC_DATA_R, i);
			if(i == 4) {
				bit = Get_Bit(GPIO_PORTD_DATA_R, 2);
			}
			else if(i == 7) {
				bit = Get_Bit(GPIO_PORTD_DATA_R, 3);
			}
			if (bit == 0){
				moveWindow(PortC_Buttons[i-4]);
				isZero = false;
			}
		}
		if(isZero && !CarWindow.autoMode){
			stopWindow();
		}
		
		bit = Get_Bit(GPIO_PORTB_DATA_R, 0);
		if (bit == 0){
			limitSwitchHandler(0);
			while(bit == 0){bit = Get_Bit(GPIO_PORTB_DATA_R, 0);}
		}
		
		bit = Get_Bit(GPIO_PORTB_DATA_R, 1);
		if (bit == 0){
			limitSwitchHandler(1);
			while(bit == 0){bit = Get_Bit(GPIO_PORTB_DATA_R, 1);}
		}
	}
}

void jamHandler(void *p){
	for(;;) {
		xSemaphoreTake(jamSemaphore, portMAX_DELAY);
		CarWindow.autoMode = false;
		Clear_Bit(GPIO_PORTD_DATA_R, 0);
		Set_Bit(GPIO_PORTD_DATA_R, 1);
		delayMS(500);
		Clear_Bit(GPIO_PORTD_DATA_R, 0);
		Clear_Bit(GPIO_PORTD_DATA_R, 1);
	}
}

void jamInterrupt(void) {
	
	GPIOIntClear(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
	
	portBASE_TYPE xHigherPriorityTaskWoken = ( ( BaseType_t ) 2 );
	
  xSemaphoreGiveFromISR(jamSemaphore, &xHigherPriorityTaskWoken);

	portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
}



void autoModeHandler(void *p){
	for(;;) {
		xSemaphoreTake(autoModeSemaphore, portMAX_DELAY);
	
		CarWindow.autoMode = ! CarWindow.autoMode;
	}
}

void autoModeInterrupt(void) {
	
	GPIOIntClear(GPIO_PORTF_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);

	portBASE_TYPE xHigherPriorityTaskWoken = ( ( BaseType_t ) 2 );

	xSemaphoreGiveFromISR(autoModeSemaphore, &xHigherPriorityTaskWoken);

	portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
}



bool hasPermission(enum User user){
  if(CarWindow.isLocked && user == passenger)
    return false;
  return true;
}

bool checkAutoUp( void ){
	uint32_t upDriverBit = Get_Bit(GPIO_PORTD_DATA_R, 2);
	uint32_t upPassengerBit = Get_Bit(GPIO_PORTC_DATA_R, 6);
	if ( upDriverBit == 0 || upPassengerBit == 0 ){
		return true;
	}
	return false;
}
bool checkAutoDown( void ){
	uint32_t downDriverBit = Get_Bit(GPIO_PORTC_DATA_R, 5);
	uint32_t downPassengerBit = Get_Bit(GPIO_PORTD_DATA_R, 3);
	if ( downDriverBit == 0 || downPassengerBit == 0 ){
		return true;
	}
	return false;
}
void moveWindow(struct Button currBtn){
	if (! hasPermission(currBtn.user)){
		Clear_Bit(GPIO_PORTD_DATA_R, 0);
		Clear_Bit(GPIO_PORTD_DATA_R, 1);
		return;
	}
	if (! CarWindow.autoMode){
		if (currBtn.dir == up){
			if (! CarWindow.isFullyClosed){
				Set_Bit(GPIO_PORTD_DATA_R, 0);
				Clear_Bit(GPIO_PORTD_DATA_R, 1);
			}
		}
		else if (currBtn.dir == down){
			if (! CarWindow.isFullyOpened){
				Clear_Bit(GPIO_PORTD_DATA_R, 0);
				Set_Bit(GPIO_PORTD_DATA_R, 1);
			}
		}
	}
	else{
		if (currBtn.dir == up){
			while(! CarWindow.isFullyClosed && CarWindow.autoMode){
				uint32_t bit = Get_Bit(GPIO_PORTB_DATA_R, 0);
				if (bit == 0){
					limitSwitchHandler(0);
					while(bit == 0){bit = Get_Bit(GPIO_PORTB_DATA_R, 0);}
					continue;
				}
				else if ( checkAutoDown() ){
					CarWindow.autoMode = false;
					continue;
				}
				else{
					Set_Bit(GPIO_PORTD_DATA_R, 0);
					Clear_Bit(GPIO_PORTD_DATA_R, 1);
				}
			}
		}
		else if (currBtn.dir == down){
			while(! CarWindow.isFullyOpened && CarWindow.autoMode){
				uint32_t bit = Get_Bit(GPIO_PORTB_DATA_R, 1);
				if (bit == 0){
					limitSwitchHandler(1);
					while(bit == 0){bit = Get_Bit(GPIO_PORTB_DATA_R, 1);}
					continue;
				}
				else if ( checkAutoUp() ){
					CarWindow.autoMode = false;
					continue;
				}
				else{
					Clear_Bit(GPIO_PORTD_DATA_R, 0);
					Set_Bit(GPIO_PORTD_DATA_R, 1);
				}
			}
		}
	}
}

void stopWindow(void){
	Clear_Bit(GPIO_PORTD_DATA_R, 0);
	Clear_Bit(GPIO_PORTD_DATA_R, 1);
}

void limitSwitchHandler(int limitSwitch){
  if (limitSwitch == 0){
		CarWindow.isFullyClosed = !CarWindow.isFullyClosed;
  }
  else if (limitSwitch == 1){
		CarWindow.isFullyOpened = !CarWindow.isFullyOpened;
  }
	CarWindow.autoMode = false;
}

void initStructs(void){
	CarWindow.isFullyClosed = false;
	CarWindow.isFullyOpened = false;
	CarWindow.isLocked = false;
	CarWindow.autoMode = false;
	
	static struct Button driverUpButton;
	static struct Button driverDownButton;
	static struct Button passengerUpButton;
	static struct Button passengerDownButton;
	
	driverUpButton.user = driver;
	driverUpButton.dir = up;
	
	driverDownButton.user = driver;
	driverDownButton.dir = down;
	
	passengerUpButton.user = passenger;
	passengerUpButton.dir = up;
	
	passengerDownButton.user = passenger;
	passengerDownButton.dir = down;
	
	PortC_Buttons[0] = driverUpButton;
	PortC_Buttons[1] = driverDownButton;
	PortC_Buttons[2] = passengerUpButton;
	PortC_Buttons[3] = passengerDownButton;
}

void init(void){

	//PORT F SETUP
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF));
	
	//PORT B SETUP
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB));
	
	//PORT C SETUP
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC));
	
	//PORT D SETUP
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD));
	
	//Manual/Auto Button Setup
	GPIOPinTypeGPIOInput(GPIO_PORTF_BASE , GPIO_PIN_4 );
	GPIOIntRegister(GPIO_PORTF_BASE, autoModeInterrupt);
	GPIOIntTypeSet(GPIO_PORTF_BASE, GPIO_PIN_4, GPIO_FALLING_EDGE);
	GPIOIntEnable(GPIO_PORTF_BASE, GPIO_INT_PIN_4 );
	Set_Bit(GPIO_PORTF_PUR_R, 4);
	
	//Jam Button Setup
	GPIOPinTypeGPIOInput(GPIO_PORTB_BASE , GPIO_PIN_5 );
	GPIOIntRegister(GPIO_PORTB_BASE, jamInterrupt);
	GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_5, GPIO_FALLING_EDGE);
	GPIOIntEnable(GPIO_PORTB_BASE, GPIO_INT_PIN_5 );
	Set_Bit(GPIO_PORTB_PUR_R, 5);
	
	//Motor Pins Setup
 	GPIOPinTypeGPIOOutput(GPIO_PORTD_BASE , GPIO_PIN_0 | GPIO_PIN_1 );
	
	//Limit Switch Pins Setup
  GPIOPinTypeGPIOInput(GPIO_PORTB_BASE , GPIO_PIN_0 | GPIO_PIN_1 );
	GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1, GPIO_FALLING_EDGE);
	Set_Bit(GPIO_PORTB_PUR_R, 0);
	Set_Bit(GPIO_PORTB_PUR_R, 1);
	
	//On/Off Switch Pins Setup
  GPIOPinTypeGPIOInput(GPIO_PORTB_BASE , GPIO_PIN_4 );
	GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_4, GPIO_FALLING_EDGE);
	Set_Bit(GPIO_PORTB_PUR_R, 4);
	
	//Up and Down Pins Setup
	GPIOPinTypeGPIOInput(GPIO_PORTC_BASE , GPIO_PIN_5 | GPIO_PIN_6 );
	GPIOIntTypeSet(GPIO_PORTC_BASE, GPIO_PIN_5 | GPIO_PIN_6 , GPIO_FALLING_EDGE);
	Set_Bit(GPIO_PORTC_PUR_R, 5);
	Set_Bit(GPIO_PORTC_PUR_R, 6);
	
	GPIOPinTypeGPIOInput(GPIO_PORTD_BASE , GPIO_PIN_2 | GPIO_PIN_3 );
	GPIOIntTypeSet(GPIO_PORTD_BASE, GPIO_PIN_2 | GPIO_PIN_3, GPIO_FALLING_EDGE);
	Set_Bit(GPIO_PORTD_PUR_R, 2);
	Set_Bit(GPIO_PORTD_PUR_R, 3);
	
	// Enable the Interrupt for PortF & PortB in NVIC
	__asm("CPSIE I");
	IntMasterEnable();
	IntEnable(INT_GPIOF);
	IntEnable(INT_GPIOB);
	IntPrioritySet(INT_GPIOF, 0xE0);
	IntPrioritySet(INT_GPIOB, 0xE0);
}


//////////////
//	Manual/Auto Button
//////////////
// F0

//////////////
//	Up & Down
//////////////
// C4 -> D2 (Now)
// C5
// C6
// C7 -> D3 (Now)

//////////////
//	Limit Switches
//////////////
// B0
// B1

//////////////
//	On/Off Switch
//////////////
// B4

//////////////
//	Jam Switch
//////////////
// B5

//////////////
//	Motor Pins
/////////////
// D0
// D1