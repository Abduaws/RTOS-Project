#include <stdint.h>
#include <stdio.h>
#include "TM4C123GH6PM.h"
#include <FreeRTOS.h>
#include "task.h"
#include <time.h>
#include <stdbool.h>
#include <semphr.h>
#include <driverlib/gpio.c>
#include <driverlib/gpio.h>
#include <driverlib/sysctl.h>
#include <inc/hw_ints.h>
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
		
		for(int i = 4; i < 8; i++){
			uint32_t bit = Get_Bit(GPIOC->DATA, i);
			if(i == 4) {
				bit = Get_Bit(GPIOD->DATA, 2);
			}
			else if(i == 7) {
				bit = Get_Bit(GPIOD->DATA, 3);
			}
			if (bit == 0){
				moveWindow(PortC_Buttons[i-4]);
				isZero = false;
			}
		}
		if(isZero && !CarWindow.autoMode){
			stopWindow();
		}
		uint32_t bit = Get_Bit(GPIOB->DATA, 0);
		if (bit == 0){
			limitSwitchHandler(0);
		}
		
		bit = Get_Bit(GPIOB->DATA, 1);
		if (bit == 0){
			limitSwitchHandler(1);
		}
		
		bit = Get_Bit(GPIOB->DATA, 4);
		if (bit == 0) {
			CarWindow.isLocked = true;
		}
		else if (bit == 1) {
			CarWindow.isLocked = false;
		}
	}
}

void jamHandler(void *p){
	for(;;) {
		xSemaphoreTake(jamSemaphore, portMAX_DELAY);
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

	xSemaphoreGiveFromISR(jamSemaphore, &xHigherPriorityTaskWoken);

	portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
}



bool hasPermission(enum User user){
  if(CarWindow.isLocked && user == passenger)
    return false;
  return true;
}

void moveWindow(struct Button currBtn){
	if (! hasPermission(currBtn.user)) return;
	if (! CarWindow.autoMode){
		if (currBtn.dir == up){
			if (! CarWindow.isFullyClosed){
				Set_Bit(GPIOD->DATA, 0);
				Clear_Bit(GPIOD->DATA, 1);
			}
		}
		else if (currBtn.dir == down){
			if (! CarWindow.isFullyOpened){
				Clear_Bit(GPIOD->DATA, 0);
				Set_Bit(GPIOD->DATA, 1);
			}
		}
	}
	else{
		if (currBtn.dir == up){
			while(! CarWindow.isFullyClosed){
				Set_Bit(GPIOD->DATA, 0);
				Clear_Bit(GPIOD->DATA, 1);
			}
		}
		else if (currBtn.dir == down){
			while(! CarWindow.isFullyOpened){
				Clear_Bit(GPIOD->DATA, 0);
				Set_Bit(GPIOD->DATA, 1);
			}
		}
	}
}

void stopWindow(void){
	Clear_Bit(GPIOD->DATA, 0);
	Clear_Bit(GPIOD->DATA, 1);
}

void limitSwitchHandler(int limitSwitch){
  if (limitSwitch == 0){
		CarWindow.isFullyClosed = !CarWindow.isFullyClosed;
  }
  else if (limitSwitch == 1){
		CarWindow.isFullyOpened = !CarWindow.isFullyOpened;
  }
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
	
	//Red Led Setup
	GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1);
	
	//Manual/Auto Button Setup
	GPIOPinTypeGPIOInput(GPIO_PORTF_BASE , GPIO_PIN_0 );
	GPIOIntRegister(GPIO_PORTF_BASE, autoModeInterrupt);
	GPIOIntTypeSet(GPIO_PORTF_BASE, GPIO_PIN_0, GPIO_FALLING_EDGE);
	GPIOIntEnable(GPIO_PORTF_BASE, GPIO_INT_PIN_0 );
	Set_Bit(GPIOF->PUR, 0);
	
	//Jam Button Setup
	GPIOPinTypeGPIOInput(GPIO_PORTB_BASE , GPIO_PIN_5 );
	GPIOIntRegister(GPIO_PORTB_BASE, jamInterrupt);
	GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_5, GPIO_FALLING_EDGE);
	GPIOIntEnable(GPIO_PORTB_BASE, GPIO_INT_PIN_5 );
	Set_Bit(GPIOB->PUR, 5);
	
	//Motor Pins Setup
 	GPIOPinTypeGPIOOutput(GPIO_PORTD_BASE , GPIO_PIN_0 | GPIO_PIN_1 );
	
	//Limit Switch Pins Setup
  	GPIOPinTypeGPIOInput(GPIO_PORTB_BASE , GPIO_PIN_0 | GPIO_PIN_1 );
	GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1, GPIO_FALLING_EDGE);
	Set_Bit(GPIOB->PUR, 0);
	Set_Bit(GPIOB->PUR, 1);
	
	//On/Off Switch Pins Setup
  	GPIOPinTypeGPIOInput(GPIO_PORTB_BASE , GPIO_PIN_4 );
	GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_4, GPIO_FALLING_EDGE);
	Set_Bit(GPIOB->PUR, 4);
	
	//Up and Down Pins Setup
	GPIOPinTypeGPIOInput(GPIO_PORTC_BASE , GPIO_PIN_5 | GPIO_PIN_6 );
	GPIOIntTypeSet(GPIO_PORTC_BASE, GPIO_PIN_5 | GPIO_PIN_6 , GPIO_FALLING_EDGE);
	Set_Bit(GPIOC->PUR, 5);
	Set_Bit(GPIOC->PUR, 6);
	
	GPIOPinTypeGPIOInput(GPIO_PORTD_BASE , GPIO_PIN_2 | GPIO_PIN_3 );
	GPIOIntTypeSet(GPIO_PORTD_BASE, GPIO_PIN_2 | GPIO_PIN_3, GPIO_FALLING_EDGE);
	Set_Bit(GPIOD->PUR, 2);
	Set_Bit(GPIOD->PUR, 3);
	
	// Enable the Interrupt for PortF & PortB in NVIC
	__asm("CPSIE I");
	IntMasterEnable();

	NVIC_SetPriority(PortF_IRQn, 5);
	NVIC_SetPriority(PortB_IRQn, 5);
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
//	On/Off Switche
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