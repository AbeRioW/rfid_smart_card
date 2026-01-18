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
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "RC522.h"
#include "oled.h"
#include "string.h"
#include "stdlib.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* 系统状态定义 */
#define STATE_WELCOME    0
#define STATE_HOME       1
#define STATE_SETTING    2
#define STATE_PIN_SET    3
#define STATE_ADD_CARD   4
#define STATE_DELETE_CARD 5

/* 卡片相关定义 */
#define MAX_CARDS        10
#define CARD_ID_LENGTH   4

/* PIN码相关定义 */
#define PIN_LENGTH       4
#define FLASH_PIN_ADDR   0x0800F000  // FLASH存储PIN码的地址
#define FLASH_CARDS_ADDR 0x0800F100  // FLASH存储卡片的地址

/* 错误计数定义 */
#define MAX_ERROR_COUNT  3

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* 系统状态变量 */
uint8_t system_state = STATE_WELCOME;

/* 卡片相关变量 */
uint8_t card_id[CARD_ID_LENGTH];
uint8_t saved_cards[MAX_CARDS][CARD_ID_LENGTH];
uint8_t card_count = 0;

/* PIN码相关变量 */
uint8_t pin_code[PIN_LENGTH] = {0, 0, 0, 0};
uint8_t pin_input[PIN_LENGTH] = {0, 0, 0, 0};
uint8_t pin_pos = 0;

/* 设置界面相关变量 */
uint8_t setting_option = 0;

/* 错误计数 */
uint8_t error_count = 0;

/* UART接收相关变量 */
uint8_t uart_rx_buffer[4];
uint8_t uart_rx_count = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* 状态处理函数 */
void welcome_state(void);
void home_state(void);
void setting_state(void);
void pin_set_state(void);
void add_card_state(void);
void delete_card_state(void);

/* FLASH操作函数 */
void flash_write(uint32_t addr, uint8_t *data, uint16_t size);
void flash_read(uint32_t addr, uint8_t *data, uint16_t size);

/* 卡片操作函数 */
uint8_t check_card_id(uint8_t *id);
void save_card_id(uint8_t *id);
void delete_card_id(uint8_t *id);

/* 继电器和蜂鸣器控制函数 */
void relay_on(void);
void relay_off(void);
void beep_on(void);
void beep_off(void);

/* 按键处理函数 */
void key1_handler(void);
void key2_handler(void);
void key3_handler(void);

/* UART处理函数 */
void uart2_data_handler(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* FLASH操作函数 */
void flash_write(uint32_t addr, uint8_t *data, uint16_t size)
{
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError = 0;
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = addr;
    eraseInit.NbPages = 1;
    HAL_FLASHEx_Erase(&eraseInit, &pageError);
    
    for (uint16_t i = 0; i < size; i += 2)
    {
        uint16_t halfword_data;
        if (i + 1 < size)
        {
            halfword_data = (data[i + 1] << 8) | data[i];
        }
        else
        {
            halfword_data = data[i];
        }
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, halfword_data);
    }
    
    HAL_FLASH_Lock();
}

void flash_read(uint32_t addr, uint8_t *data, uint16_t size)
{
    for (uint16_t i = 0; i < size; i++)
    {
        data[i] = *(__IO uint8_t *)(addr + i);
    }
}

/* 卡片操作函数 */
uint8_t check_card_id(uint8_t *id)
{
    for (uint8_t i = 0; i < card_count; i++)
    {
        if (memcmp(saved_cards[i], id, CARD_ID_LENGTH) == 0)
        {
            return 1;
        }
    }
    return 0;
}

void save_card_id(uint8_t *id)
{
    if (card_count < MAX_CARDS)
    {
        memcpy(saved_cards[card_count], id, CARD_ID_LENGTH);
        card_count++;
        flash_write(FLASH_CARDS_ADDR, (uint8_t *)&card_count, 1);
        flash_write(FLASH_CARDS_ADDR + 1, (uint8_t *)saved_cards, card_count * CARD_ID_LENGTH);
    }
}

void delete_card_id(uint8_t *id)
{
    uint8_t found = 0;
    for (uint8_t i = 0; i < card_count; i++)
    {
        if (found)
        {
            memcpy(saved_cards[i - 1], saved_cards[i], CARD_ID_LENGTH);
        }
        else if (memcmp(saved_cards[i], id, CARD_ID_LENGTH) == 0)
        {
            found = 1;
        }
    }
    if (found && card_count > 0)
    {
        card_count--;
        flash_write(FLASH_CARDS_ADDR, (uint8_t *)&card_count, 1);
        flash_write(FLASH_CARDS_ADDR + 1, (uint8_t *)saved_cards, card_count * CARD_ID_LENGTH);
    }
}

/* 继电器和蜂鸣器控制函数 */
void relay_on(void)
{
    HAL_GPIO_WritePin(LAY_GPIO_Port, LAY_Pin, GPIO_PIN_RESET);
}

void relay_off(void)
{
    HAL_GPIO_WritePin(LAY_GPIO_Port, LAY_Pin, GPIO_PIN_SET);
}

void beep_on(void)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
}

void beep_off(void)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
}

/* 按键处理函数 */
void key1_handler(void)
{
    switch (system_state)
    {
        case STATE_HOME:
            system_state = STATE_SETTING;
            setting_option = 0;
            break;
        case STATE_SETTING:
            system_state = STATE_HOME;
            break;
        case STATE_PIN_SET:
            if (pin_pos == 3)
            {
                memcpy(pin_code, pin_input, PIN_LENGTH);
                flash_write(FLASH_PIN_ADDR, pin_code, PIN_LENGTH);
                system_state = STATE_SETTING;
            }
            break;
        case STATE_ADD_CARD:
        case STATE_DELETE_CARD:
            system_state = STATE_SETTING;
            break;
    }
}

void key2_handler(void)
{
    switch (system_state)
    {
        case STATE_SETTING:
            setting_option = (setting_option + 1) % 3;
            break;
        case STATE_PIN_SET:
            pin_input[pin_pos] = (pin_input[pin_pos] + 1) % 10;
            break;
    }
}

void key3_handler(void)
{
    switch (system_state)
    {
        case STATE_SETTING:
            if (setting_option == 0)
            {
                system_state = STATE_PIN_SET;
                pin_pos = 0;
                pin_input[0] = rand() % 10;
                for (uint8_t i = 1; i < PIN_LENGTH; i++)
                {
                    pin_input[i] = 0;
                }
            }
            else if (setting_option == 1)
            {
                system_state = STATE_ADD_CARD;
            }
            else if (setting_option == 2)
            {
                system_state = STATE_DELETE_CARD;
            }
            break;
        case STATE_PIN_SET:
            pin_pos = (pin_pos + 1) % PIN_LENGTH;
            break;
    }
}

/* UART处理函数 */
void uart2_data_handler(void)
{
    if (uart_rx_count == 4)
    {
        if (memcmp(uart_rx_buffer, pin_code, 4) == 0)
        {
            relay_on();
            HAL_Delay(2000);
            relay_off();
            error_count = 0;
        }
        else
        {
            error_count++;
            if (error_count >= MAX_ERROR_COUNT)
            {
                beep_on();
                HAL_Delay(3000);
                beep_off();
                error_count = 0;
            }
        }
        uart_rx_count = 0;
    }
}

/* USER CODE END 0 */

/* 状态处理函数 */
void welcome_state(void)
{
    OLED_ShowString(0, 2, "Welcome Bcak home", 16, 0);
    OLED_Refresh();
    HAL_Delay(2000);
    system_state = STATE_HOME;
}

void home_state(void)
{
    OLED_Clear();
    OLED_ShowString(0, 2, "Waiting to open", 16, 0);
    OLED_Refresh();
    
    uint8_t card_type;
    if (PCD_Request(PICC_REQIDL, &card_type) == MFRC_OK)
    {
        if (PCD_Anticoll(card_id) == MFRC_OK)
        {
            if (check_card_id(card_id))
            {
                relay_on();
                HAL_Delay(2000);
                relay_off();
                error_count = 0;
            }
            else
            {
                error_count++;
                if (error_count >= MAX_ERROR_COUNT)
                {
                    beep_on();
                    HAL_Delay(3000);
                    beep_off();
                    error_count = 0;
                }
            }
        }
    }
    HAL_Delay(500);
}

void setting_state(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "Setting", 16, 0);
    
    if (setting_option == 0)
    {
        OLED_ShowString(0, 2, "-> 1.pin set", 16, 0);
        OLED_ShowString(0, 4, "  2.add card", 16, 0);
        OLED_ShowString(0, 6, "  3.delete card", 16, 0);
    }
    else if (setting_option == 1)
    {
        OLED_ShowString(0, 2, "  1.pin set", 16, 0);
        OLED_ShowString(0, 4, "-> 2.add card", 16, 0);
        OLED_ShowString(0, 6, "  3.delete card", 16, 0);
    }
    else if (setting_option == 2)
    {
        OLED_ShowString(0, 2, "  1.pin set", 16, 0);
        OLED_ShowString(0, 4, "  2.add card", 16, 0);
        OLED_ShowString(0, 6, "-> 3.delete card", 16, 0);
    }
    
    OLED_Refresh();
    HAL_Delay(200);
}

void pin_set_state(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "PIN SET", 16, 0);
    
    char pin_str[10] = "PIN: ";
    for (uint8_t i = 0; i < PIN_LENGTH; i++)
    {
        if (i == pin_pos)
        {
            pin_str[5 + i] = pin_input[i] + '0';
        }
        else
        {
            pin_str[5 + i] = '_';
        }
    }
    pin_str[9] = '\0';
    OLED_ShowString(0, 3, (uint8_t *)pin_str, 16, 0);
    
    OLED_Refresh();
    HAL_Delay(200);
}

void add_card_state(void)
{
    OLED_Clear();
    OLED_ShowString(0, 2, "card close", 16, 0);
    OLED_Refresh();
    
    uint8_t card_type;
    if (PCD_Request(PICC_REQIDL, &card_type) == MFRC_OK)
    {
        if (PCD_Anticoll(card_id) == MFRC_OK)
        {
            save_card_id(card_id);
            OLED_Clear();
            OLED_ShowString(0, 2, "Card added", 16, 0);
            OLED_Refresh();
            HAL_Delay(1000);
            system_state = STATE_SETTING;
        }
    }
    HAL_Delay(500);
}

void delete_card_state(void)
{
    OLED_Clear();
    OLED_ShowString(0, 2, "card close", 16, 0);
    OLED_Refresh();
    
    uint8_t card_type;
    if (PCD_Request(PICC_REQIDL, &card_type) == MFRC_OK)
    {
        if (PCD_Anticoll(card_id) == MFRC_OK)
        {
            delete_card_id(card_id);
            OLED_Clear();
            OLED_ShowString(0, 2, "Card deleted", 16, 0);
            OLED_Refresh();
            HAL_Delay(1000);
            system_state = STATE_SETTING;
        }
    }
    HAL_Delay(500);
}

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
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  
  /* 初始化OLED */
  OLED_Init();
  OLED_Clear();
  
  /* 初始化RC522 */
  PCD_Init();
  
  /* 从FLASH读取数据 */
  flash_read(FLASH_PIN_ADDR, pin_code, PIN_LENGTH);
  flash_read(FLASH_CARDS_ADDR, &card_count, 1);
  if (card_count > 0 && card_count <= MAX_CARDS)
  {
      flash_read(FLASH_CARDS_ADDR + 1, (uint8_t *)saved_cards, card_count * CARD_ID_LENGTH);
  }
  else
  {
      card_count = 0;
  }
  
  /* 启动USART2中断接收 */
  HAL_UART_Receive_IT(&huart2, &uart_rx_buffer[0], 1);
  
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    switch (system_state)
    {
        case STATE_WELCOME:
            welcome_state();
            break;
        case STATE_HOME:
            home_state();
            break;
        case STATE_SETTING:
            setting_state();
            break;
        case STATE_PIN_SET:
            pin_set_state();
            break;
        case STATE_ADD_CARD:
            add_card_state();
            break;
        case STATE_DELETE_CARD:
            delete_card_state();
            break;
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

#ifdef  USE_FULL_ASSERT
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
