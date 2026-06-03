#include "AHT20.h"

//AHT20初始化
void AHT20_Init(void)
{
	//临时变量用于读取
	uint8_t ReadBuffer;

	//根据手册，上电后等待40ms
	HAL_Delay(40);

	//根据手册，通过发送0x71看状态字的校准使能位
	HAL_I2C_Master_Receive(&AHT20_I2C, AHT20_ADDRESS, &ReadBuffer, 1, AHT20_TIMEOUT);

	//根据手册，该位不为1则发送初始化指令
	if((ReadBuffer & 0x08) != 0x08)
	{
		//临时变量用于发送
		uint8_t SendBuffer[3] = {0xBE, 0x08, 0x00};

		//根据手册，发送初始化指令
		HAL_I2C_Master_Transmit(&AHT20_I2C, AHT20_ADDRESS, SendBuffer, 3, AHT20_TIMEOUT);
	}
}

//AHT20读取温湿度信息函数
void AHT20_Read(float *Temperature, float *Humidity)
{
	//触发测量指令
	uint8_t SendBuffer[3] = {0xAC, 0x33, 0x00};
	//临时变量用于读取
	uint8_t ReadBuffer[6];

	//发送测量指令
	HAL_I2C_Master_Transmit(&AHT20_I2C, AHT20_ADDRESS, SendBuffer, 3, AHT20_TIMEOUT);

	//根据手册，等待75ms
	HAL_Delay(75);

	//接收测量数据
	HAL_I2C_Master_Receive(&AHT20_I2C, AHT20_ADDRESS, ReadBuffer, 6, AHT20_TIMEOUT);

	//根据手册，检测忙状态Bit[7]为0后可以读取数据
	if((ReadBuffer[0] & 0x80) == 0x00)
	{
		//临时变量，存放数据
		uint32_t Data = 0;

		//湿度数据计算
		Data = ((uint32_t)ReadBuffer[3] >> 4) + ((uint32_t)ReadBuffer[2] << 4) + ((uint32_t)ReadBuffer[1] << 12);
		*Humidity = Data * 100.0f / (1<<20);

		//温度数据计算
		Data = ((uint32_t)ReadBuffer[5]) + ((uint32_t)ReadBuffer[4] << 8) + (((uint32_t)(ReadBuffer[3] & 0x0F)) << 16);
		*Temperature = Data * 200.0f / (1<<20) - 50;
	}
}
