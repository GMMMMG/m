/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "rtc.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "LCD_Driver.h"
#include "delay.h"
#include "stdio.h"
#include "AHT20.h"
#include "max30102.h"
#include "max30102_fir.h"
#include "BH1750.h"
#include "bsp_i2c.h"
#include "DS1302.h"
#include "string.h"
#include "NanoEdgeAI.h"
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

uint8_t white_led_state = 0;       // PC1白灯状态（0灭1亮），按钮和语音共同控制
uint8_t uart2_rx_data = 0;         // USART2接收缓冲区（ASR PRO语音指令）

BH1750_t bh1750;

RTC_TimeTypeDef sTime;
RTC_DateTypeDef sDate;

// MAX30102
uint16_t HeartRate = 0;
float SpO2 = 0;
float max30102_data[2] = {0};
float fir_output[2] = {0};
uint8_t data_ready = 0;
char lcd_buf[32];

//MQ135           
uint32_t mq135_adc_value = 0;      // ADC??? (0-4095)
float mq135_voltage = 0.0f;        // ???????
uint8_t mq135_alarm = 0;

// BH1750
float bh1750_lux = 0;
uint8_t bh1750_ok = 0;

// HC-SR501
uint8_t sr501_status = 0;
uint8_t sr501_detected = 0;

// DS1302 
DS1302_TIME ds1302_time;

// NanoEdge AI
enum neai_state neai_state;
bool use_pretrained = true;  // true = 使用预训练模型，无需学习阶段
uint8_t similarity;
float input_signal[NEAI_INPUT_SIGNAL_LENGTH * NEAI_INPUT_AXIS_NUMBER];
/* USER CODE END PV */

// 界面状态
typedef enum {
    SCREEN_MAIN = 0,
    SCREEN_ENV = 1,
    SCREEN_HUMAN = 2
} ScreenType;
static ScreenType current_screen = SCREEN_MAIN;
static ScreenType last_screen = SCREEN_MAIN;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void RTC_SetTime(uint8_t hh, uint8_t mm, uint8_t ss);
void RTC_GetNowTime(void);
void fill_buffer(float *input_signal);
void Key_Scan(void);
void Init_Main_Screen(void);
void Update_Main_Screen(void);
void Init_Env_Screen(void);
void Update_Env_Screen(void);
void Init_Human_Screen(void);
void Update_Human_Screen(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint16_t last_hr = 0;
static float last_spo2 = 0;

float temp, humi;

void fill_buffer(float *input_signal)
{
    // 使用您的传感器数据填充缓冲区
    // 根据 NanoEdgeAI.h 配置：3轴 x 1样本 = 3个 float 值
    input_signal[0] = temp;           // 轴1：温度
    input_signal[1] = humi;           // 轴2：湿度
    input_signal[2] = bh1750_lux;     // 轴3：光照强度
}

// 按键扫描函数（带消抖）
void Key_Scan(void) {
    static uint8_t k1_state = 1, k2_state = 1, k3_state = 1;
    static uint32_t k1_tick = 0, k2_tick = 0, k3_tick = 0;

    // K1 - PA12 - 主界面
    if(HAL_GPIO_ReadPin(K1_GPIO_Port, K1_Pin) == GPIO_PIN_RESET) {
        if(k1_state && (HAL_GetTick() - k1_tick > 50)) {
            k1_state = 0;
            k1_tick = HAL_GetTick();
            current_screen = SCREEN_MAIN;
        }
    } else {
        k1_state = 1;
    }

    // K2 - PA11 - 环境界面
    if(HAL_GPIO_ReadPin(K2_GPIO_Port, K2_Pin) == GPIO_PIN_RESET) {
        if(k2_state && (HAL_GetTick() - k2_tick > 50)) {
            k2_state = 0;
            k2_tick = HAL_GetTick();
            current_screen = SCREEN_ENV;
        }
    } else {
        k2_state = 1;
    }

    // K3 - PB12 - 人体检测界面
    if(HAL_GPIO_ReadPin(K3_GPIO_Port, K3_Pin) == GPIO_PIN_RESET) {
        if(k3_state && (HAL_GetTick() - k3_tick > 50)) {
            k3_state = 0;
            k3_tick = HAL_GetTick();
            current_screen = SCREEN_HUMAN;
        }
    } else {
        k3_state = 1;
    }
}

// 主界面
void Init_Main_Screen(void) {
    Lcd_Clear(BLACK);
    LCD_Show_String(16, 20, "SMART HEALTH");
    LCD_Show_String(0, 120, "K1-MAIN");
    LCD_Show_String(56, 120, "K2-ENV");
    LCD_Show_String(104, 120, "K3-HUMAN");
}

void Update_Main_Screen(void) {
    sprintf(lcd_buf, "%02d:%02d:%02d", ds1302_time.hour, ds1302_time.min, ds1302_time.sec);
    LCD_Show_String(24, 50, lcd_buf);
    sprintf(lcd_buf, "20%02d-%02d-%02d", ds1302_time.year, ds1302_time.mon, ds1302_time.day);
    LCD_Show_String(20, 70, lcd_buf);
}

// 环境界面
void Init_Env_Screen(void) {
    Lcd_Clear(BLACK);
    LCD_Show_String(0, 0, "ENVIRONMENT");
    LCD_Show_String(0, 140, "BACK: K1");
}

void Update_Env_Screen(void) {
    sprintf(lcd_buf, "TEMP: %.1f C", temp);
    LCD_Show_String(0, 20, lcd_buf);
    sprintf(lcd_buf, "HUMI: %.1f %%", humi);
    LCD_Show_String(0, 40, lcd_buf);
    if(bh1750_ok)
        sprintf(lcd_buf, "LUX: %.1f lx", bh1750_lux);
    else
        sprintf(lcd_buf, "LUX: --- lx");
    LCD_Show_String(0, 60, lcd_buf);
    sprintf(lcd_buf, "MQ135: %.2fV", mq135_voltage);
    LCD_Show_String(0, 80, lcd_buf);
    if(mq135_alarm == 0)
        LCD_Show_String(0, 100, "AIR: POOR");
    else
        LCD_Show_String(0, 100, "AIR: GOOD");
}

// 人体检测界面
void Init_Human_Screen(void) {
    Lcd_Clear(BLACK);
    LCD_Show_String(0, 0, "HUMAN DETECT");
    LCD_Show_String(0, 140, "BACK: K1");
}

void Update_Human_Screen(void) {
    if(HeartRate >= 40 && HeartRate <= 180)
        sprintf(lcd_buf, "HR: %3d bpm", HeartRate);
    else
        sprintf(lcd_buf, "HR: --- bpm");
    LCD_Show_String(0, 25, lcd_buf);
    if(SpO2 >= 70 && SpO2 <= 100)
        sprintf(lcd_buf, "SPO2: %3.0f %%", SpO2);
    else
        sprintf(lcd_buf, "SPO2: --- %%");
    LCD_Show_String(0, 50, lcd_buf);
    if(sr501_detected)
        LCD_Show_String(0, 75, "BODY: DETECTED");
    else
        LCD_Show_String(0, 75, "BODY: NO");
    sprintf(lcd_buf, "SIM: %3d %%", similarity);
    LCD_Show_String(0, 100, lcd_buf);
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
	MX_I2C2_Init();
	MX_I2C1_Init();
	MX_I2C3_Init();
	MX_USART1_UART_Init();
	MX_USART2_UART_Init();
	MX_ADC1_Init();
	
	HAL_Delay(100);
	
	HAL_ADC_Start(&hadc1);
	Lcd_Init();
	Lcd_Clear(BLACK);
	
	/* USER CODE BEGIN 2 */
	
	/* AHT20 */
	AHT20_Init();
	if (HAL_I2C_IsDeviceReady(&hi2c2, AHT20_ADDRESS, 3, 100) == HAL_OK)
		LCD_Show_String(0, 0, "AHT20 OK");
	else
		LCD_Show_String(0, 0, "AHT20 ERR");
	
	/* DS1302*/
	DS1302_Init();
	//{ DS1302_TIME t = {26, 6, 21, 7, 6, 13, 20}; DS1302_SetTime(&t); }
	
	
	// DS1302
	uint8_t test_sec = 0;
	DS1302_TIME test_time;
	DS1302_ReadTime(&test_time);
	test_sec = test_time.sec;
	if(test_sec <= 59)  // ????????????
		LCD_Show_String(0, 16, "DS1302 OK");
	else
		LCD_Show_String(0, 16, "DS1302 ERR");
	
	/* MAX30102 */
	max30102_init();
	max30102_fir_init();
	{
		uint8_t part_id = 0;
		max30102_i2c_read(0xFF, &part_id, 1);
		if (part_id == 0x15)
			LCD_Show_String(0, 32, "MAX30102 OK");
		else
			LCD_Show_String(0, 32, "MAX30102 ERR");
	}
	//BH1750
	if (BH1750_Init(&bh1750, &hi2c3, BH1750_ADDR_LOW) == HAL_OK)
	{
		LCD_Show_String(0, 48, "BH1750 OK");
		bh1750_ok = 1;
	}
	else
	{
		LCD_Show_String(0, 48, "BH1750 ERR");
		bh1750_ok = 0;
	}
	
	
	// NanoEdge AI
	neai_state = neai_anomalydetection_init(use_pretrained);
	if (neai_state == NEAI_OK) {
		LCD_Show_String(0, 64, "AI OK");
	} else {
		LCD_Show_String(0, 64, "AI ERR");
	}
	
	HAL_Delay(1000);
	Lcd_Clear(BLACK);
	
	//  MAX30102 
	cache_counter = 0;
	
	
	/* USER CODE END 2 */
	
	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1)
	{
		/* USER CODE END WHILE */
		AHT20_Read(&temp, &humi);
		DS1302_ReadTime(&ds1302_time);
		
		// MQ135
		if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
		{
			mq135_adc_value = HAL_ADC_GetValue(&hadc1);
			mq135_voltage = mq135_adc_value * 3.3f / 4096.0f;
		}
		mq135_alarm = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0); // PB0
		
		// MAX30102 
		data_ready = MAX30102_Get_DATA(&HeartRate, &SpO2, max30102_data, fir_output);
		
		// BH1750 
		if(bh1750_ok)
		{
			BH1750_ReadLux(&bh1750, &bh1750_lux);
		}
		
		// HC-SR501
		sr501_status = HAL_GPIO_ReadPin(SR501_OUT_GPIO_Port, SR501_OUT_Pin);
		sr501_detected = (sr501_status == GPIO_PIN_SET) ? 1 : 0;

		// 声音检测模块 - PC6低电平触发，检测到时PC5输出高电平
		if(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6) == GPIO_PIN_RESET) {
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_SET);   // PC5高电平
		} else {
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET); // PC5低电平
		}
		
		//CH340
		char uart_buf[64];
		sprintf(uart_buf, "%.1f,%.1f,%.1f\r\n", temp, humi, bh1750_lux);
		HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

		// 按键扫描
		Key_Scan();

		// 界面切换检测
		if(current_screen != last_screen) {
			last_screen = current_screen;
			switch(current_screen) {
				case SCREEN_MAIN:
					Init_Main_Screen();
					break;
				case SCREEN_ENV:
					Init_Env_Screen();
					break;
				case SCREEN_HUMAN:
					Init_Human_Screen();
					break;
			}
		}

		// 根据当前界面更新显示
		switch(current_screen) {
			case SCREEN_MAIN:
				Update_Main_Screen();
				break;
			case SCREEN_ENV:
				Update_Env_Screen();
				break;
			case SCREEN_HUMAN:
				Update_Human_Screen();
				break;
		}
		
		// PC2 人体检测
		if(sr501_detected)
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);
		else
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);
		
		// PA1按钮检测，控制PC10灯和PC11蜂鸣器（按一下切换状态）
		static uint8_t alarm_state = 0;  // 0:关闭  1:开启
		static uint8_t last_button_state = GPIO_PIN_RESET;  // 上一次的按键状态
		uint8_t button_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1);

		// 检测按键上升沿（从松开到按下）
		if(button_state == GPIO_PIN_SET && last_button_state == GPIO_PIN_RESET)
		{
			HAL_Delay(20);  // 简单消抖
			button_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1);
			if(button_state == GPIO_PIN_SET)
			{
				alarm_state = !alarm_state;  // 切换状态
			}
		}
		last_button_state = button_state;
		
		// 根据状态控制灯和蜂鸣器
		if(alarm_state)
		{
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_SET);  // PC10亮
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, GPIO_PIN_SET);  // PC11蜂鸣器响
		}
		else
		{
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET);  // PC10灭
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, GPIO_PIN_RESET);  // PC11蜂鸣器停
		}
		
		// PC0按钮检测，控制PC1白灯（按一下切换状态）
		static uint8_t last_pc0_state = GPIO_PIN_RESET;  // 上一次的按键状态
		uint8_t pc0_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0);
		
		// 检测按键上升沿（从松开到按下）
		if(pc0_state == GPIO_PIN_SET && last_pc0_state == GPIO_PIN_RESET)
		{
			HAL_Delay(20);  // 简单消抖
			pc0_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0);
			if(pc0_state == GPIO_PIN_SET)
			{
				white_led_state = !white_led_state;  // 切换状态
			}
		}
		last_pc0_state = pc0_state;
		
		// 根据状态控制PC1白灯
		if(white_led_state)
		{
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_SET);  // PC1亮
		}
		else
		{
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_RESET);  // PC1灭
		}
		
		// NanoEdge AI 直接检测（使用预训练模型）
		fill_buffer(input_signal);
		neai_anomalydetection_detect(input_signal, &similarity);

		HAL_Delay(100);
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
	
	/** Configure the main internal regulator output voltage
	 */
	HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);
	
	/** Configure LSE Drive Capability
	 */
	HAL_PWR_EnableBkUpAccess();
	__HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);
	
	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.LSEState = RCC_LSE_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV6;
	RCC_OscInitStruct.PLL.PLLN = 85;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
	RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
	RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
	
	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
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
