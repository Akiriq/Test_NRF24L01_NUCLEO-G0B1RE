/*
 * test_screen.c
 *
 *  Created on: Mar 1, 2025
 *      Author: RGL
 */

#include "test_screen.h"
#include "ssd1306.h"
#include "fonts.h"
#include "stdio.h"

extern I2C_HandleTypeDef hi2c1;
extern void Error_Handler(void);

void runTestScreen(void)
{
	  if (ssd1306_Init(&hi2c1) != 0) {
	      Error_Handler();
	    }
	    HAL_Delay(1000);

	    ssd1306_Fill(Black);
	    ssd1306_UpdateScreen(&hi2c1);

	    HAL_Delay(1000);

	    // Write data to local screenbuffer
	    ssd1306_SetCursor(0, 0);
	    ssd1306_WriteString("ssd1306", Font_11x18, White);

	    ssd1306_SetCursor(0, 36);
	    ssd1306_WriteString("4ilo", Font_16x26, White);

	    // Draw rectangle on screen
	    for (uint8_t i=0; i<10; i++) {
	        for (uint8_t j=0; j<60; j++) {
	            ssd1306_DrawPixel(100+i, 0+j, White);
	        }
	    }

	    // Copy all data from local screenbuffer to the screen
	    ssd1306_UpdateScreen(&hi2c1);

	    HAL_Delay(1000);
	    char *msg1 ="123456790";
	    ssd1306_Fill(Black);
	    ssd1306_SetCursor(0, 0);
	    ssd1306_WriteString(msg1, Font_11x18, White);
	    ssd1306_UpdateScreen(&hi2c1);
	    HAL_Delay(1000);
	    char *msg2 ="abcdefghij";
	    ssd1306_Fill(Black);
	    ssd1306_SetCursor(0, 0);
	    ssd1306_WriteString(msg2, Font_11x18, White);
	    ssd1306_UpdateScreen(&hi2c1);
	    HAL_Delay(1000);
	    char *msg3 ="<<<<<<<<<<";
	    ssd1306_Fill(Black);
	    ssd1306_SetCursor(0, 0);
	    ssd1306_WriteString(msg3, Font_11x18, White);
	    ssd1306_UpdateScreen(&hi2c1);
	    char *msg4 =">>>>>>>>>>";
	    HAL_Delay(1000);
	    ssd1306_Fill(Black);
	    ssd1306_SetCursor(0, 0);
	    ssd1306_WriteString(msg4, Font_11x18, White);
	    ssd1306_UpdateScreen(&hi2c1);

	    uint8_t l = 0;
	    uint8_t dir = 1;
	    char *msg5 =">";
	    char *msg6 ="<";

	    uint32_t tick_save = 0;
	    uint32_t dt = 0;
	    char buf[128] = {0};
	    while(1)
	    {
	    	tick_save = HAL_GetTick();
	    	ssd1306_Fill(Black);

	    	sprintf(buf, "delay: %lu", dt);
	    	ssd1306_SetCursor(0, 40);
	    	ssd1306_WriteString(buf, Font_11x18, White);
	    	if(dir)
	    	{
		    	ssd1306_SetCursor(0, 0);
	    		ssd1306_WriteString(msg5, Font_11x18, White);
	    		l++;
	    	}
	    	else
	    	{
		    	ssd1306_SetCursor(0, 0);
	    		ssd1306_WriteString(msg6, Font_11x18, White);
	    		l--;
	    	}

		    for (uint8_t i=0; i<l; i++)
		    {
		        for (uint8_t j=0; j<20; j++)
		        {
		            ssd1306_DrawPixel(0+i, 20+j, White);
		        }
		    }
		    ssd1306_UpdateScreen(&hi2c1);
		    if(l==128) dir = 0;
		    if(l==0)   dir = 1;
		    dt = HAL_GetTick() - tick_save;
	    }


}

