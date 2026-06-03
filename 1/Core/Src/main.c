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
#include "adc.h"     //MQ135
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
#include "esp8266_onenet.h"
#include "common.h"
#include "mqttkit.h"
//#include "NanoEdgeAI.h"
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

RTC_TimeTypeDef sTime;
RTC_DateTypeDef sDate;
// MAX30102 ??
uint16_t HeartRate = 0;
float SpO2 = 0;
float max30102_data[2] = {0};
float fir_output[2] = {0};
uint8_t data_ready = 0;
char lcd_buf[32];

//MQ135
uint32_t mq135_adc_value = 0;      // ADC??? (0-4095)
float mq135_voltage = 0.0f;        // ???????
uint8_t mq135_alarm = 0;           // DO?????? (0=??,1=??)

// BH1750
BH1750_t bh1750;
float bh1750_lux = 0;
uint8_t bh1750_ok = 0;

// HC-SR501
uint8_t sr501_status = 0;
uint8_t sr501_detected = 0;

// DS1302 
DS1302_TIME ds1302_time;

// NanoEdge AI
//enum neai_state neai_state;
//bool use_pretrained = false;
//uint8_t similarity;
//float input_signal[NEAI_INPUT_SIGNAL_LENGTH * NEAI_INPUT_AXIS_NUMBER];

int16_t temperature,humidity;
static uint32_t last_onenet_upload = 0U;
static ESP8266_OneNET_Status_t last_onenet_status = ESP8266_ONENET_STATUS_OFFLINE;
static ESP8266_OneNET_Error_t last_onenet_error = ESP8266_ONENET_ERROR_NONE;
uint8_t Uart2_RxData = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void RTC_SetTime(uint8_t hh, uint8_t mm, uint8_t ss);
void RTC_GetNowTime(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint16_t last_hr = 0;
static float last_spo2 = 0;

int fputc(int ch, FILE *f)
{
	HAL_UART_Transmit(&huart1, (uint8_t*)&ch, 1, 100);
	return ch;
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
	MX_I2C1_Init();            // MAX30102
	//MX_USART2_UART_Init();
	MX_USART3_UART_Init();     // ESP-01S
	MX_I2C2_Init();            // AHT20 + BMP280
	MX_I2C3_Init();
	MX_USART1_UART_Init();
	MX_ADC1_Init();            // MQ135    ???ADC1 (PA0)
	/* USER CODE BEGIN 2 */
	HAL_Delay(100);
	HAL_ADC_Start(&hadc1);     // ??ADC??
	
	// LCD ???
	Lcd_Init();
	Lcd_Clear(BLACK);
	//ESP8266_OneNET_Init(&huart3);
	
	// AHT20 ???
	AHT20_Init();
	if (HAL_I2C_IsDeviceReady(&hi2c2, AHT20_ADDRESS, 3, 100) == HAL_OK)
		LCD_Show_String(0, 0, "AHT20 OK");
	else
		LCD_Show_String(0, 0, "AHT20 ERR");
	
	/* DS1302*/
	DS1302_Init();
	//{ DS1302_TIME t = {24, 6, 15, 6, 23, 18, 0}; DS1302_SetTime(&t); }
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
	//neai_state = neai_anomalydetection_init(use_pretrained);
	//if (neai_state == NEAI_OK) {
	//	LCD_Show_String(0, 64, "AI OK");
	//} else {
	//	LCD_Show_String(0, 64, "AI ERR");
	//}
	
	HAL_Delay(1000);
	Lcd_Clear(BLACK);
	
	//MAX30102 FIFO
	cache_counter = 0;
	
	float temp, humi;
	
	/* USER CODE END 2 */
	
	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1)
	{
		AHT20_Read(&temp, &humi);
		DS1302_ReadTime(&ds1302_time);
		
		//OneNET
		temperature = temp;
		humidity = humi;
		
		//MQ135
		//PA0 ADC
		if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
		{
			mq135_adc_value = HAL_ADC_GetValue(&hadc1);
			mq135_voltage = mq135_adc_value * 3.3f / 4096.0f;
		}
		//PB0
		mq135_alarm = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0);
		
		
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
		
		// CH340
		char uart_buf[96];
		sprintf(uart_buf, "TEMP:%.1f,HUMI:%.1f,MQ135:%.2fV,ALARM:%d,S:%d,E:%d\r\n",
            temp, humi, mq135_voltage, mq135_alarm == 0 ? 1 : 0,
            ESP8266_OneNET_GetStatus(), ESP8266_OneNET_GetLastError());
		HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 100);

        if ((ESP8266_OneNET_GetStatus() != last_onenet_status) ||
            (ESP8266_OneNET_GetLastError() != last_onenet_error))
        {
            last_onenet_status = ESP8266_OneNET_GetStatus();
            last_onenet_error = ESP8266_OneNET_GetLastError();
            snprintf(uart_buf, sizeof(uart_buf), "ONENET DBG S:%d,E:%d,R:%s\r\n",
                     last_onenet_status, last_onenet_error,
                     ESP8266_OneNET_GetLastResponse());
            HAL_UART_Transmit(&huart1, (uint8_t*)uart_buf, strlen(uart_buf), 1000);
        }
		
		//DS1302
		sprintf(lcd_buf, "TIME:%02d:%02d:%02d", ds1302_time.hour, ds1302_time.min, ds1302_time.sec);
		LCD_Show_String(0, 0, lcd_buf);
		
		//AHT20
		sprintf(lcd_buf, "TEMP:%.1f C", temp);
		LCD_Show_String(0, 16, lcd_buf);
		sprintf(lcd_buf, "HUMI:%.1f %%", humi);
		LCD_Show_String(0, 32, lcd_buf);
		
		// BH1750 
		if(bh1750_ok)
			sprintf(lcd_buf, "LUX:%.1f lx", bh1750_lux);
		else
			sprintf(lcd_buf, "LUX: --- lx");
		LCD_Show_String(0, 48, lcd_buf);
		
		//MAX30102
		if (HeartRate < 1u)
		{
			last_hr = 0u;
			sprintf(lcd_buf, "HR: ---bpm");
		}
		else if (HeartRate >= MAX30102_HR_VALID_LO && HeartRate <= MAX30102_HR_VALID_HI)
		{
			last_hr = HeartRate;
			sprintf(lcd_buf, "HR:%3d bpm", HeartRate);
		}
		else if (last_hr >= MAX30102_HR_VALID_LO && last_hr <= MAX30102_HR_VALID_HI)
			sprintf(lcd_buf, "HR:%3d~bpm", last_hr);
		else
			sprintf(lcd_buf, "HR: ---bpm");
		LCD_Show_String(0, 64, lcd_buf);
		
		if (SpO2 < 1.0f)
		{
			last_spo2 = 0.0f;
			sprintf(lcd_buf, "SPO2: ---%%");
		}
		else if (SpO2 >= 65.0f && SpO2 <= 100.0f)
		{
			last_spo2 = SpO2;
			sprintf(lcd_buf, "SPO2:%3.0f %%", SpO2);
		}
		else if (last_spo2 >= 65.0f && last_spo2 <= 100.0f)
			sprintf(lcd_buf, "SPO2:%3.0f~%%", last_spo2);
		else
			sprintf(lcd_buf, "SPO2: ---%%");
		LCD_Show_String(0, 80, lcd_buf);
		
		//MQ135
		sprintf(lcd_buf, "MQ135:%.2fV", mq135_voltage);
		LCD_Show_String(0, 96, lcd_buf);
		if (mq135_alarm == 0)
			LCD_Show_String(0, 112, "AIR:POOR");
		else
			LCD_Show_String(0, 112, "AIR:GOOD");
		
		// HC-SR501
		if(sr501_detected)
			sprintf(lcd_buf, "BODY: YES");
		else
			sprintf(lcd_buf, "BODY: NO ");
		LCD_Show_String(0, 128, lcd_buf);
		
		//OneNet
		//LCD_Show_String(0, 144, "S:");
		//LCD_Show_Num(16, 144, ESP8266_OneNET_GetStatus(), 1);
		//LCD_Show_String(32, 144, "E:");
		//LCD_Show_Num(48, 144, ESP8266_OneNET_GetLastError(), 2);
		
		
		ESP8266_OneNET_Task();

        if ((HAL_GetTick() - last_onenet_upload) >= ONENET_UPLOAD_INTERVAL_MS)
        {
            last_onenet_upload = HAL_GetTick();
            (void)ESP8266_OneNET_PostData((int32_t)HeartRate, (double)humidity, bh1750_lux, SpO2, (double)temperature);
        }
		
		// PC2 ????
		if(sr501_detected)
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);
		else
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);
		
		// PC12????,??PC10??PC11???(???????)
		static uint8_t alarm_state = 0;  // 0:??  1:??
		static uint8_t last_button_state = GPIO_PIN_RESET;  // ????????
		uint8_t button_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12);
		
		// ???????(??????)
		if(button_state == GPIO_PIN_SET && last_button_state == GPIO_PIN_RESET)
		{
			HAL_Delay(20);  // ????
			button_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12);
			if(button_state == GPIO_PIN_SET)
			{
				alarm_state = !alarm_state;  // ????
			}
		}
		last_button_state = button_state;
		
		// ???????????
		if(alarm_state)
		{
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_SET);  // PC10?
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, GPIO_PIN_SET);  // PC11????
		}
		else
		{
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET);  // PC10?
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, GPIO_PIN_RESET);  // PC11????
		}
		
		HAL_Delay(100);
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
