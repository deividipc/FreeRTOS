/*
 * FreeRTOS Kernel V10.1.0
 * Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

/*
 * Creates all the demo application tasks, then starts the scheduler.  The WEB
 * documentation provides more details of the demo application tasks.
 * 
 * This demo is configured to execute on the ES449 prototyping board from
 * SoftBaugh. The ES449 has a built in LCD display and a single built in user
 * LED.  Therefore, in place of flashing an LED, the 'flash' and 'check' tasks
 * toggle '*' characters on the LCD.  The left most '*' represents LED 0, the
 * next LED 1, etc.
 *
 * Main. c also creates a task called 'Check'.  This only executes every three 
 * seconds but has the highest priority so is guaranteed to get processor time.  
 * Its main function is to check that all the other tasks are still operational.  
 * Each task that does not flash an LED maintains a unique count that is 
 * incremented each time the task successfully completes its function.  Should 
 * any error occur within such a task the count is permanently halted.  The 
 * 'check' task inspects the count of each task to ensure it has changed since
 * the last time the check task executed.  If all the count variables have 
 * changed all the tasks are still executing error free, and the check task
 * toggles an LED with a three second period.  Should any task contain an error 
 * at any time the LED toggle rate will increase to 500ms.
 *
 * Please read the documentation for the MSP430 port available on
 * http://www.FreeRTOS.org.
 */

/* Standard includes. */
#include <stdlib.h>
#include <signal.h>

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Demo application includes. */
#include "partest.h"
#include "flash.h"
#include "integer.h"
#include "comtest2.h"
#include "PollQ.h"

/* Constants required for hardware setup. */
#define mainALL_BITS_OUTPUT		( ( unsigned char ) 0xff )
#define mainMAX_FREQUENCY		( ( unsigned char ) 121 )

/* Constants that define the LED's used by the various tasks. [in this case
the '*' characters on the LCD represent LED's] */
#define mainCHECK_LED			( 4 )
#define mainCOM_TEST_LED		( 10 )

/* Demo task priorities. */
#define mainCHECK_TASK_PRIORITY			( tskIDLE_PRIORITY + 3 )
#define mainCOM_TEST_PRIORITY			( tskIDLE_PRIORITY + 2 )
#define mainQUEUE_POLL_PRIORITY			( tskIDLE_PRIORITY + 2 )
#define mainLED_TASK_PRIORITY			( tskIDLE_PRIORITY + 1 )

/* Baud rate used by the COM test tasks. */
#define mainCOM_TEST_BAUD_RATE			( ( unsigned long ) 19200 )

/* The frequency at which the 'Check' tasks executes.  See the comments at the 
top of the page.  When the system is operating error free the 'Check' task
toggles an LED every three seconds.  If an error is discovered in any task the
rate is increased to 500 milliseconds.  [in this case the '*' characters on the 
LCD represent LED's]*/
#define mainNO_ERROR_CHECK_DELAY		( ( TickType_t ) 3000 / portTICK_PERIOD_MS  )
#define mainERROR_CHECK_DELAY			( ( TickType_t ) 500 / portTICK_PERIOD_MS  )

/* 
 * The function that implements the Check task.  See the comments at the head
 * of the page for implementation details.
 */ 
static void vErrorChecks( void *pvParameters );

/*
 * Called by the Check task.  Returns pdPASS if all the other tasks are found
 * to be operating without error - otherwise returns pdFAIL.
 */
static short prvCheckOtherTasksAreStillRunning( void );

/* 
 * Perform the hardware setup required by the ES449 in order to run the demo
 * application.
 */
static void prvSetupHardware( void );

/* Used to detect the idle hook function stalling. */
static volatile unsigned long ulIdleLoops = 0UL;

/*-----------------------------------------------------------*/

/*
 * Start the demo application tasks - then start the real time scheduler.
 */
int main( void )
{
	/* Setup the hardware ready for the demo. */
	prvSetupHardware();
	vParTestInitialise();

	/* Start the standard demo application tasks. */
	vStartLEDFlashTasks( mainLED_TASK_PRIORITY );
	vStartIntegerMathTasks( tskIDLE_PRIORITY );
	vAltStartComTestTasks( mainCOM_TEST_PRIORITY, mainCOM_TEST_BAUD_RATE, mainCOM_TEST_LED - 1 );
	vStartPolledQueueTasks( mainQUEUE_POLL_PRIORITY );

	/* Start the 'Check' task which is defined in this file. */
	xTaskCreate( vErrorChecks, "Check", configMINIMAL_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY, NULL );	

	/* Start the scheduler. */
	vTaskStartScheduler();

	/* As the scheduler has been started the demo applications tasks will be
	executing and we should never get here! */
	return 0;
}
/*-----------------------------------------------------------*/

static void vErrorChecks( void *pvParameters )
{
static volatile unsigned long ulDummyVariable = 3UL;
TickType_t xDelayPeriod = mainNO_ERROR_CHECK_DELAY;

	/* Cycle for ever, delaying then checking all the other tasks are still
	operating without error. */
	for( ;; )
	{
		/* Wait until it is time to check again.  The time we wait here depends
		on whether an error has been detected or not.  When an error is 
		detected the time is shortened resulting in a faster LED flash rate. */
		vTaskDelay( xDelayPeriod );

		/* Perform a bit of 32bit maths to ensure the registers used by the 
		integer tasks get some exercise outside of the integer tasks 
		themselves. The result here is not important we are just deliberately
		changing registers used by other tasks to ensure that their context
		switch is operating as required. - see the demo application 
		documentation for more info. */
		ulDummyVariable *= 3UL;
		
		/* See if the other tasks are all ok. */
		if( prvCheckOtherTasksAreStillRunning() != pdPASS )
		{
			/* An error occurred in one of the tasks so shorten the delay 
			period - which has the effect of increasing the frequency of the
			LED toggle. */
			xDelayPeriod = mainERROR_CHECK_DELAY;
		}

		/* Flash! */
		vParTestToggleLED( mainCHECK_LED );
	}
}
/*-----------------------------------------------------------*/

static short prvCheckOtherTasksAreStillRunning( void )
{
static short sNoErrorFound = pdTRUE;
static unsigned long ulLastIdleLoops = 0UL;

	/* The demo tasks maintain a count that increments every cycle of the task
	provided that the task has never encountered an error.  This function 
	checks the counts maintained by the tasks to ensure they are still being
	incremented.  A count remaining at the same value between calls therefore
	indicates that an error has been detected.  Only tasks that do not flash
	an LED are checked. */

	if( xAreIntegerMathsTaskStillRunning() != pdTRUE )
	{
		sNoErrorFound = pdFALSE;
	}

	if( xAreComTestTasksStillRunning() != pdTRUE )
	{
		sNoErrorFound = pdFALSE;
	}
	
	if( xArePollingQueuesStillRunning() != pdTRUE )
	{
		sNoErrorFound = pdFALSE;
	}

	if( ulLastIdleLoops == ulIdleLoops )
	{
		sNoErrorFound = pdFALSE;
	}

	ulLastIdleLoops = ulIdleLoops;
	
	return sNoErrorFound;
}
/*-----------------------------------------------------------*/

static void prvSetupHardware( void )
{
	/* Stop the watchdog. */
	WDTCTL = WDTPW + WDTHOLD;

	/* Setup DCO+ for ( xtal * D * (N + 1) ) operation. */
	FLL_CTL0 |= DCOPLUS + XCAP18PF; 

	/* X2 DCO frequency, 8MHz nominal DCO */
	SCFI0 |= FN_4;                  

	/* (121+1) x 32768 x 2 = 7.99 Mhz */
	SCFQCTL = mainMAX_FREQUENCY;

	/* Setup the IO as per the SoftBaugh demo for the same target hardware. */
	P1SEL = 0x32;
	P2SEL = 0x00;
	P3SEL = 0x00;
	P4SEL = 0xFC;
	P5SEL = 0xFF;
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void );
void vApplicationIdleHook( void )
{
	/* Simple put the CPU into lowpower mode. */
	_BIS_SR( LPM3_bits );
	ulIdleLoops++;
}
/*-----------------------------------------------------------*/







