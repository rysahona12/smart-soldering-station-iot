/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fonts.h"
#include "ssd1306.h"
#include <stdio.h>
#include <stdlib.h>
#include "adc.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"
#include "usart.h"
#include <string.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern UART_HandleTypeDef huart2;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// Accelerometer
int16_t accel_x, accel_y, accel_z;
int16_t last_x = 0, last_y = 0, last_z = 0;
uint32_t last_move_time = 0;
uint8_t rec_data[6];
const int16_t MOVEMENT_THRESHOLD = 2000; // Change this to make it more or less sensitive

// --- MQ-135 & Fan Variables ---
uint32_t adc_val = 0;
uint32_t smoke_clear_time = 0;
//const uint32_t SMOKE_THRESHOLD = 510; // CHANGE THIS: Lower value = more sensitive, Higher value = less sensitive
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
	// Servo
	static uint8_t is_docked = 1;
	uint32_t dock_time = 370;
	uint32_t undock_time = 450;



	// MAX6675 Variables
	uint16_t raw_temp = 0;
	float temp_c = 0.0;
	const float HOT_THRESHOLD = 40.0; // 50 degrees Celsius (Change this if needed)

	// MQ135
	uint32_t baseline_adc = 0;
	uint32_t dynamic_threshold = 0;
	const uint16_t FUME_SENSITIVITY_OFFSET = 22; // Triggers if smoke raises ADC by 30 above baseline
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  SSD1306_Init();

	// Wake up MPU6050
	uint8_t mpu_data = 0;
	HAL_I2C_Mem_Write(&hi2c1, 0xD0, 0x6B, 1, &mpu_data, 1, 100);

  // Driving Servo
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);

  // Auto-Calibrate MQ-135 Baseline
  // --- Auto-Calibrate MQ-135 Baseline ---
    SSD1306_Fill(0);
    SSD1306_GotoXY(15, 20);
    SSD1306_Puts("CALIBRATING...", &Font_7x10, 1);
    SSD1306_GotoXY(20, 38);
    SSD1306_Puts("Please Wait", &Font_7x10, 1);
    SSD1306_UpdateScreen();

    // 1. Pre-Heat Phase: Let the sensor heater warm up fully without recording bad data
    HAL_Delay(15000);

    // 2. Sampling Phase: Now that it's fully warm, take 20 clean, stable readings
    uint32_t adc_sum = 0;
    for (int i = 0; i < 30; i++) {
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 50) == HAL_OK) {
            adc_sum += HAL_ADC_GetValue(&hadc1);
        }
        HAL_ADC_Stop(&hadc1);
        HAL_Delay(100);
    }

    // Calculate clean-air baseline from warm readings only
    baseline_adc = adc_sum / 30;
    dynamic_threshold = baseline_adc + FUME_SENSITIVITY_OFFSET;

    // Initialize movement timer so buzzer doesn't trigger instantly on boot
        last_move_time = HAL_GetTick();

  /* USER CODE END 2 */

    /* Infinite loop */
      /* USER CODE BEGIN WHILE */
      while (1)
      {

          // ==========================================
          // 1. SENSOR READINGS & LOGIC
          // ==========================================
    	  GPIO_PinState dock_state = HAL_GPIO_ReadPin(FC51_DOC_GPIO_Port, FC51_DOC_Pin);
          // --- MQ-135 Gas Sensor ---
    	     uint32_t adc_sum = 0;
    	     for (int j = 0; j < 8; j++) {
    	         HAL_ADC_Start(&hadc1);
    	         if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
    	             adc_sum += HAL_ADC_GetValue(&hadc1);
    	         }
    	         HAL_ADC_Stop(&hadc1);
    	     }
    	     adc_val = adc_sum / 8; // Smooth, stable reading

          if (adc_val > dynamic_threshold) {
              smoke_clear_time = HAL_GetTick(); // Latch
          }
          uint8_t fumes_active = ((HAL_GetTick() - smoke_clear_time) < 3000);

          if (fumes_active && (dock_state == GPIO_PIN_SET)) {
              HAL_GPIO_WritePin(FAN_IN1_GPIO_Port, FAN_IN1_Pin, GPIO_PIN_SET);
              HAL_GPIO_WritePin(FAN_IN2_GPIO_Port, FAN_IN2_Pin, GPIO_PIN_RESET);
          } else {
              HAL_GPIO_WritePin(FAN_IN1_GPIO_Port, FAN_IN1_Pin, GPIO_PIN_RESET);
              HAL_GPIO_WritePin(FAN_IN2_GPIO_Port, FAN_IN2_Pin, GPIO_PIN_RESET);
          }

          // --- MPU-6050 Accelerometer ---
          HAL_I2C_Mem_Read(&hi2c1, 0xD0, 0x3B, 1, rec_data, 6, 100);
          accel_x = (int16_t)(rec_data[0] << 8 | rec_data[1]);
          accel_y = (int16_t)(rec_data[2] << 8 | rec_data[3]);
          accel_z = (int16_t)(rec_data[4] << 8 | rec_data[5]);

          if (abs(accel_x - last_x) > MOVEMENT_THRESHOLD ||
              abs(accel_y - last_y) > MOVEMENT_THRESHOLD ||
              abs(accel_z - last_z) > MOVEMENT_THRESHOLD) {
              last_move_time = HAL_GetTick();
          }
          last_x = accel_x; last_y = accel_y; last_z = accel_z;

          // --- Dynamic Baseline Update (Only when Docked) ---
          if (dock_state == GPIO_PIN_RESET) {
        	  // Smoothly track clean ambient air while the iron rests
        	  baseline_adc = (baseline_adc * 95 + adc_val * 5) / 100;
        	  dynamic_threshold = baseline_adc + FUME_SENSITIVITY_OFFSET;
          }

          // --- Buzzer Logic: Undocked & No movement for 45 seconds (45000 ms) ---
          if (dock_state == GPIO_PIN_SET && (HAL_GetTick() - last_move_time > 45000)) {
        	  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
          } else {
        	  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
          }


          // ==========================================
          // 2. OLED DISPLAY UPDATES
          // ==========================================

          SSD1306_Fill(0); // WIPE SCREEN ONCE

          // A. Draw Top Half (Dock / Move / Temp Status)
          if (dock_state == GPIO_PIN_RESET) {

              // Read Temp only when docked
              HAL_GPIO_WritePin(MAX6675_CS_GPIO_Port, MAX6675_CS_Pin, GPIO_PIN_RESET);
              HAL_SPI_Receive(&hspi1, (uint8_t *)&raw_temp, 2, 100);
              HAL_GPIO_WritePin(MAX6675_CS_GPIO_Port, MAX6675_CS_Pin, GPIO_PIN_SET);
              temp_c = (raw_temp >> 3) * 0.25;

              if (temp_c > HOT_THRESHOLD) {
                  SSD1306_GotoXY(40, 5);  SSD1306_Puts("Docked", &Font_7x10, 1);
                  SSD1306_GotoXY(15, 25); SSD1306_Puts("DO NOT TOUCH!", &Font_7x10, 1);
                  SSD1306_GotoXY(40, 45); SSD1306_Puts("HOT!!!", &Font_7x10, 1);
              } else {
                  SSD1306_GotoXY(40, 15); SSD1306_Puts("Docked", &Font_7x10, 1);
                  SSD1306_GotoXY(15, 35); SSD1306_Puts("Safe to Touch", &Font_7x10, 1);
              }

          } else {

              if ((HAL_GetTick() - last_move_time) < 5000) {
                  SSD1306_GotoXY(5, 25);  SSD1306_Puts("MOVING & UNDOCKED", &Font_7x10, 1);
              } else {
                  SSD1306_GotoXY(10, 25); SSD1306_Puts("IDLE & UNDOCKED", &Font_7x10, 1);
              }
          }

          // B. Draw Bottom Half (Fume Status)
          // Note: This only draws if the Temp warning isn't taking up the whole screen
          if (dock_state == GPIO_PIN_SET) {
              if (fumes_active) {
                  SSD1306_GotoXY(10, 45); SSD1306_Puts("FUMES: DETECTED", &Font_7x10, 1);
              } else {
                  SSD1306_GotoXY(10, 45); SSD1306_Puts("FUMES: CLEAR   ", &Font_7x10, 1);
              }
          }

          SSD1306_UpdateScreen(); // DRAW ALL TEXT AT ONCE
          HAL_Delay(200);


          // ==========================================
          // 3. SERVO MOTOR CONTROL
          // ==========================================
          GPIO_PinState current_dock_state = HAL_GPIO_ReadPin(FC51_DOC_GPIO_Port, FC51_DOC_Pin);

          if (current_dock_state == GPIO_PIN_RESET && !is_docked) {
              is_docked = 1;
              __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1200);
              HAL_Delay(dock_time);
              __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1500); // STOP

          } else if (current_dock_state == GPIO_PIN_SET && is_docked) {
              is_docked = 0;
              __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1800);
              HAL_Delay(undock_time);
              __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1500); // STOP
          }

          // ==========================================
          // 4. UART TELEMETRY TO ESP32 / WEMOS
          // ==========================================
          // Convert temperature to an integer (e.g., 25.5 -> 255) to bypass float limitations
                    int temp_scaled = (int)(temp_c * 10);

                    char tele_buf[128];
                    sprintf(tele_buf, "{\"t\":%d, \"f\":%d, \"d\":%d, \"m\":%d}\n",
                    temp_scaled,
                    fumes_active,
                    (dock_state == GPIO_PIN_RESET ? 1 : 0),
                    ((HAL_GetTick() - last_move_time) < 5000 ? 1 : 0));

                    // Transmit to ESP via USART2
                    HAL_UART_Transmit(&huart2, (uint8_t*)tele_buf, strlen(tele_buf), 50);

        /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
