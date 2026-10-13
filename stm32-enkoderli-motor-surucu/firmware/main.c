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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lcd.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PWM_MAX        3420     /* %95 - bootstrap icin ust sinir */
#define DARBE_PER_TUR  12000UL  /* cikis mili, tek kanal yukselen kenar */
#define YON_BEKLEME_MS 500      /* yon degisiminde frenli bekleme */
#define KONTROL_MS     50       /* olcum + kontrol periyodu */
#define REF_RAMPA      4.0f     /* rpm / 50 ms  ->  80 rpm/s */
#define KFF            (3599 / 80.0f)   /* ileri besleme: 80 rpm ~ %95 */
#define KP             15.0f    /* duty sayaci / rpm */
#define KI             60.0f    /* duty sayaci / (rpm*s) */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim4;

/* USER CODE BEGIN PV */
volatile int32_t hedef_rpm = 0;
volatile uint8_t motor_acik = 0;
volatile int8_t  yon = 1;
int32_t  uygulanan_duty  = 0;
int8_t   uygulanan_yon   = 1;
uint16_t onceki_sayac    = 0;
uint32_t olculen_rpm_x10 = 0;
float ref_rpm  = 0.0f;
float integral = 0.0f;
float gosterilen_rpm = 0.0f;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM1_Init(void);
/* USER CODE BEGIN PFP */
void Butonlari_Oku(void);
void Motor_Sur(int8_t y, int32_t duty);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void Butonlari_Oku(void)
{
  static const uint16_t pin[4] = {GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15};
  static uint8_t onceki[4] = {1, 1, 1, 1};

  for (int i = 0; i < 4; i++) {
    uint8_t simdi = HAL_GPIO_ReadPin(GPIOB, pin[i]);
    if (onceki[i] == 1 && simdi == 0) {          // sadece basıldığı an
      switch (i) {
        case 0: if (hedef_rpm < 70) hedef_rpm += 5; break;  // SW1 Artır
        case 1: if (hedef_rpm > 0)  hedef_rpm -= 5; break;  // SW2 Azalt
        case 2: motor_acik = !motor_acik;            break;  // SW3 Başlat/Durdur
        case 3: yon = -yon;                          break;  // SW4 Yön
      }
    }
    onceki[i] = simdi;
  }
}
void Motor_Sur(int8_t y, int32_t duty)
{
  if (duty < 0)       duty = 0;
  if (duty > PWM_MAX) duty = PWM_MAX;

  if (y > 0) {
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty); /* Motor+ PWM */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);    /* Motor- alt MOSFET acik */
  } else {
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, duty);
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

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
  MX_TIM4_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */
  lcd_init();
  lcd_gotoxy(0, 0); lcd_print("Bitirme Projesi");
  lcd_gotoxy(0, 1); lcd_print("Motor Surucu");
  HAL_Delay(1000);
  lcd_clear();
  HAL_TIM_Base_Start(&htim4);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  onceki_sayac = __HAL_TIM_GET_COUNTER(&htim4);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t son_tarama = 0, son_olcum = 0, son_lcd = 0;
  char satir[33];
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	    uint32_t simdi = HAL_GetTick();

	    /* ---- 10 ms: butonlar ---- */
	    if (simdi - son_tarama >= 10) {
	      son_tarama += 10;
	      Butonlari_Oku();
	    }

	    /* ---- 50 ms: olcum + kontrol ---- */
	    if (simdi - son_olcum >= KONTROL_MS) {
	      son_olcum += KONTROL_MS;

	      /* 1) RPM olcumu */
	      uint16_t sayac = __HAL_TIM_GET_COUNTER(&htim4);
	      uint16_t fark  = sayac - onceki_sayac;
	      onceki_sayac   = sayac;
	      olculen_rpm_x10 = (uint32_t)fark * 12000UL / DARBE_PER_TUR;
	      float olculen = olculen_rpm_x10 / 10.0f;
	      gosterilen_rpm += 0.1f * (olculen - gosterilen_rpm);   /* ekran icin ~0.5 s yumusatma */

	      /* 2) hedef: motor kapaliysa ya da yon degisiyorsa 0 */
	      static uint16_t yon_bekleme = 0;
	      float hedef = motor_acik ? (float)hedef_rpm : 0.0f;
	      if (uygulanan_yon != yon) {
	        hedef = 0.0f;
	        if (ref_rpm == 0.0f && olculen_rpm_x10 == 0) {      /* durdu mu? */
	          yon_bekleme += KONTROL_MS;
	          if (yon_bekleme >= YON_BEKLEME_MS) {               /* 500 ms fren */
	            uygulanan_yon = yon;
	            yon_bekleme = 0;
	          }
	        }
	      } else {
	        yon_bekleme = 0;
	      }

	      /* 3) referans rampasi */
	      if (ref_rpm < hedef) {
	        ref_rpm += REF_RAMPA;
	        if (ref_rpm > hedef) ref_rpm = hedef;
	      } else if (ref_rpm > hedef) {
	        ref_rpm -= REF_RAMPA;
	        if (ref_rpm < hedef) ref_rpm = hedef;
	      }

	      /* 4) PI + ileri besleme */
	      if (ref_rpm <= 0.0f) {
	        integral = 0.0f;
	        uygulanan_duty = 0;                      /* iki alt MOSFET acik: fren */
	      } else {
	        float hata = ref_rpm - olculen;
	        float yeni_integral = integral + KI * hata * (KONTROL_MS / 1000.0f);
	        float cikis = KFF * ref_rpm + KP * hata + yeni_integral;

	        /* anti-windup: doyumdaysa integrali sadece doyumdan cikaran yonde guncelle */
	        if ((cikis < PWM_MAX || hata < 0.0f) && (cikis > 0.0f || hata > 0.0f)) {
	          integral = yeni_integral;
	        }

	        cikis = KFF * ref_rpm + KP * hata + integral;
	        if (cikis < 0.0f)    cikis = 0.0f;
	        if (cikis > PWM_MAX) cikis = PWM_MAX;
	        uygulanan_duty = (int32_t)cikis;
	      }

	      Motor_Sur(uygulanan_yon, uygulanan_duty);
	    }

	    /* ---- 200 ms: LCD ---- */
	    if (simdi - son_lcd >= 200) {
	      son_lcd = simdi;
	      int g = (int)(gosterilen_rpm * 10.0f + 0.5f);
	      snprintf(satir, sizeof satir, "H:%2d R:%3d.%d rpm",
	               (int)hedef_rpm, g / 10, g % 10);
	      lcd_gotoxy(0, 0); lcd_print(satir);

	      snprintf(satir, sizeof satir, "%-5s %-5s %3d%%",
	               motor_acik ? "CALIS" : "DUR",
	               (yon > 0) ? "ILERI" : "GERI",
	               (int)(uygulanan_duty * 100 / 3599));
	      lcd_gotoxy(0, 1); lcd_print(satir);
	    }
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
}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 3599;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sSlaveConfig.SlaveMode = TIM_SLAVEMODE_EXTERNAL1;
  sSlaveConfig.InputTrigger = TIM_TS_TI2FP2;
  sSlaveConfig.TriggerPolarity = TIM_TRIGGERPOLARITY_RISING;
  sSlaveConfig.TriggerFilter = 8;
  if (HAL_TIM_SlaveConfigSynchro(&htim4, &sSlaveConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 PA2 PA3
                           PA4 PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB12 PB13 PB14 PB15 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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