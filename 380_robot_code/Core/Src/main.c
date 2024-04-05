/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
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
#include <stdio.h>
#include "VL53Lx_api.h"
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
ADC_HandleTypeDef hadc2;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_ADC2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM2_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */
//static void MX_I2C1_Init(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define FWD 0
#define BWD 1

#define LEFT 0
#define RIGHT 1

#define MUX_ADDR 0x70 << 1
#define TCS_ADDR 0x29 << 1
#define ICM_ADDR 0x69 << 1
#define MAG_ADDR 0x0C << 1

#define EN_REG 0x80 | 0x00
#define INT_REG 0x80 | 0x01
#define GAIN_REG 0x80 | 0x0F

enum motors {
    LSM1,
    LSM2,
    RSM1,
    RSM2
};

enum {
	R,
	G,
	B
};

enum states {
	FOLLOW1,
	INTERMISSION,
	FOLLOW2,
};

//const uint16_t M_PINS[4] = {LS_M1_Pin, LS_M2_Pin, RS_M1_Pin, RS_M2_Pin};
volatile const uint32_t *M_CCR[4] = {&(TIM1->CCR1), &(TIM1->CCR2), &(TIM1->CCR3), &(TIM1->CCR4)};
const uint8_t M_DIR[2] = {LS_DIR_Pin, RS_DIR_Pin};
const double M_SCALE[4] = {1,1,1,1};

const uint8_t SENSOR_REGS[3] = {0x80 | 0x16, 0x80 | 0x18, 0x80 | 0x1A};
const uint8_t SENSORS[] = {2, 3, 4, 5}; // BL, FR, FL, BR
uint8_t NUM_SENSORS = sizeof(SENSORS)/sizeof(SENSORS[0]);

uint16_t rgb[] = {0, 0, 0};

void runMotors(uint8_t side, uint8_t dir, double duty) {

    char b [100];

    if (duty < 0) {
        dir = !dir;
        duty = abs(duty*1000)/1000.0;
    }

    duty = duty*0.8;
    if (duty > 0.8) duty = 0.8;

    double duty_adj = dir == FWD ? (1-duty) : duty;

//    sprintf(b, "duty %f fwd? %d \r\n", duty_adj, dir == FWD);
//    HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

    if (side == LEFT) {
        HAL_GPIO_WritePin(GPIOA, RS_DIR_Pin, dir == FWD ? GPIO_PIN_SET : GPIO_PIN_RESET);
        TIM1->CCR1 = duty_adj*TIM1->ARR;
        TIM1->CCR2 = duty_adj*TIM1->ARR;
    } else {
        HAL_GPIO_WritePin(GPIOA, LS_DIR_Pin, dir == FWD ? GPIO_PIN_SET : GPIO_PIN_RESET);
        TIM1->CCR3 = duty_adj*TIM1->ARR;
        TIM1->CCR4 = duty_adj*TIM1->ARR;
    }
}


void setBit(uint32_t bitMask, uint8_t value) {
	if (value) {
		GPIOA->ODR |= bitMask;
	} else {
		GPIOA->ODR &= ~bitMask;
	}
}

uint8_t selectMuxAddr(uint8_t sensor) {
  HAL_StatusTypeDef ret;
  char b [100];

  if (sensor > 7) {
 		sprintf(b, "sensor index %d out of bounds\r\n", sensor);
 		HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
  }

  uint8_t data[1] = {1 << sensor};

  ret = HAL_I2C_Master_Transmit(&hi2c1, MUX_ADDR, data, 1, HAL_MAX_DELAY);

  if ( ret != HAL_OK ) {
 		sprintf(b, "failed to connect to sensor %d - error code %d\r\n", sensor, ret);
 		HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
 		return 0;
 	} else {
// 		sprintf(b, "connected to sensor %d\r\n", sensor);
// 		HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
 		return 1;
 	}
}

uint16_t readSensor(uint8_t sensor) {
	if (!selectMuxAddr(sensor)) {
		return 0;
	}

  HAL_StatusTypeDef ret;
  uint8_t buf16[2];
  char out [100];
  uint16_t val;

	for (int i = 0; i < 3; i++) {
    ret = HAL_I2C_Mem_Read(&hi2c1, TCS_ADDR, SENSOR_REGS[i], I2C_MEMADD_SIZE_8BIT, buf16, 2, HAL_MAX_DELAY);

    if ( ret != HAL_OK ) {
   		sprintf(out, "sensor read %d failed with error code %d\r\n", sensor, ret);
   		HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
   	} else {
      val = buf16[1] << 8 | buf16[0];

//   		sprintf(b, "%d %d %d\r\n", buf16[1], buf16[0], val);
//   		HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

   		rgb[i] = val;
   	}
	}

	return getRGB(R);

}

uint16_t getRGB(uint8_t colour) {
	uint16_t r = rgb[0];
	uint16_t g = rgb[1];
	uint16_t b = rgb[2];

	uint16_t val2 = rgb[colour]*1000 / (r + g + b) * 3;

//	sprintf(out, "sensor %d  r %d g %d b %d scaled %d\r\n", sensor, r, g, b, val2);
//	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);

	return val2;
}

void initSensors() {
  HAL_StatusTypeDef ret;
  uint8_t int_time;
  uint8_t gain;
  uint8_t enable;
  char b [100];

  int_time = 0xFF;
  gain = 0x03;
  enable = 0x01;

	for (int i = 0; i < sizeof(SENSORS)/sizeof(SENSORS[0]); i++) {
		if (!selectMuxAddr(SENSORS[i])) {
				continue;
		}

		// Write integration time
	  ret = HAL_I2C_Mem_Write(&hi2c1, TCS_ADDR, INT_REG, I2C_MEMADD_SIZE_8BIT, &int_time, 1, HAL_MAX_DELAY);

	  if ( ret != HAL_OK ) {
			sprintf(b, "fail 1 %d\r\n", ret);
			HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
			return;
		}

	  ret = HAL_I2C_Mem_Write(&hi2c1, TCS_ADDR, GAIN_REG, I2C_MEMADD_SIZE_8BIT, &gain, 1, HAL_MAX_DELAY);

	  if ( ret != HAL_OK ) {
			sprintf(b, "fail 2 %d\r\n", ret);
			HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
			return;
		}

	  HAL_Delay(3);

	  ret = HAL_I2C_Mem_Write(&hi2c1, TCS_ADDR, EN_REG, I2C_MEMADD_SIZE_8BIT, &enable, 1, HAL_MAX_DELAY);

	  if ( ret != HAL_OK ) {
			sprintf(b, "fail 1 %d\r\n", ret);
			HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
			return;
		}

	  enable |= 0x02;

	  HAL_Delay(3);

	  ret = HAL_I2C_Mem_Write(&hi2c1, TCS_ADDR, EN_REG, I2C_MEMADD_SIZE_8BIT, &enable, 1, HAL_MAX_DELAY);

	  if ( ret != HAL_OK ) {
			sprintf(b, "fail 1 %d\r\n", ret);
			HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
			return;
		}

	  HAL_Delay(300);

	}
}

void initIMU() {
  HAL_Delay(500);

	HAL_StatusTypeDef ret;
	uint8_t buf8;
	char out [100];

	if (!selectMuxAddr(6)) return;

	// write bypass
	buf8 = 1 << 1;
  ret = HAL_I2C_Mem_Write(&hi2c1, ICM_ADDR, 0x0F, I2C_MEMADD_SIZE_8BIT, &buf8, 1, HAL_MAX_DELAY);

  if ( ret != HAL_OK ) {
  	sprintf(out, "fail %d\r\n", ret);
  	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  	return;
  } else {
  	sprintf(out, "success %d\r\n", buf8);
  	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  }

	// write sleep
	buf8 = 0x01;
  ret = HAL_I2C_Mem_Write(&hi2c1, ICM_ADDR, 0x06, I2C_MEMADD_SIZE_8BIT, &buf8, 1, HAL_MAX_DELAY);

  if ( ret != HAL_OK ) {
  	sprintf(out, "fail %d\r\n", ret);
  	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  	return;
  } else {
  	sprintf(out, "success %d\r\n", buf8);
  	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  }

  HAL_Delay(10);

  // enable magnetometer at 100 Hz
  buf8 = 0b01000;
  ret = HAL_I2C_Mem_Write(&hi2c1, MAG_ADDR, 0x31, I2C_MEMADD_SIZE_8BIT, &buf8, 1, HAL_MAX_DELAY);

  if ( ret != HAL_OK ) {
  	sprintf(out, "fail %d\r\n", ret);
  	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  	return;
  } else {
  	sprintf(out, "success %d\r\n", buf8);
  	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  }

}

void readIMURaw(int16_t* x, int16_t* y, int16_t* z) {
	if (!selectMuxAddr(6)) return;

	HAL_StatusTypeDef ret;
	uint8_t data[6];
	char out [100];
	uint8_t buf8;

	// read magnetometer
  ret = HAL_I2C_Mem_Read(&hi2c1, MAG_ADDR, 0x11, I2C_MEMADD_SIZE_8BIT, data, 6, HAL_MAX_DELAY);

  if ( ret != HAL_OK ) {
  	sprintf(out, "IMU read failed fail %d\r\n", ret);
  	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  } else {
//    	sprintf(out, "x %d %d y %d %d z %d %d\r\n", data[1], data[0], data[3], data[2], data[5], data[4]);
//    	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);

  	*x = data[1] << 8 | data[0];
  	*y = data[3] << 8 | data[2];
  	*z = data[5] << 8 | data[4];

  	sprintf(out, "x %d y %d z %d \r\n", *x, *y, *z);
  	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  }


  // have to read this register afterwards to clear data ready bit
  ret = HAL_I2C_Mem_Read(&hi2c1, MAG_ADDR, 0x18, I2C_MEMADD_SIZE_8BIT, &buf8, 1, HAL_MAX_DELAY);
  if ( ret != HAL_OK ) {
  	sprintf(out, "IMU read clear failed %d\r\n", ret);
  	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  } else {
//    	sprintf(out, "success %d %d\r\n", buf16[1], buf16[0]);
//    	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);
  }
}

double getAngle(double theta0) {
	int16_t x;
	int16_t y;
	int16_t z;
	char out [100];

	readIMURaw(&x, &y, &z);

	double theta = atan2(z+355, y+5);//atan2(z+350, y+60);
	theta = theta * 360 / (2 * M_PI);
	if (theta < 0) {
		theta += 360;
	}

	double output = theta - theta0;

	if (output < 0) {
		output += 360;
	}

//	sprintf(out, "angle %f\r\n", output);
//	HAL_UART_Transmit(&huart2, (uint8_t*)out, strlen(out), HAL_MAX_DELAY);

	return output;
}

void autoCalibrate(uint16_t* tape_val, uint16_t* wood_val) {
  char b [100];
	uint16_t tape = 0;
	uint16_t wood = 0;

	uint32_t wood_sum = 0;
	uint32_t tape_sum = 0;
	uint16_t avg_reading = 0;

	runMotors(LEFT, FWD, 0.4);
	runMotors(RIGHT, FWD, 0.4);

	uint16_t count = 1;
	uint16_t tape_count = 0;

	wood_sum += readSensor(SENSORS[0]);
	wood_sum += readSensor(SENSORS[3]);
	wood_sum /= 2;
	wood = wood_sum;

	sprintf(b, "wood %d\r\n", wood);
	HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

	while (1) {
		count++;
		avg_reading = 0;

		avg_reading += readSensor(SENSORS[0]);
		avg_reading += readSensor(SENSORS[3]);
		avg_reading /= 2;

		if (abs(avg_reading - wood) <= wood*0.3) {

			wood_sum += avg_reading;
			wood = wood_sum / count;

			sprintf(b, "wood %d\r\n", wood);
			HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

			if (tape_count > 0) {
				tape_count = 0;
				tape_sum = 0;
			}
		} else {
			sprintf(b, "tape? \r\n");
			HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

			tape_count++;
			if (tape_count > 10) {
				tape_sum += avg_reading;
			}
		}

		if (tape_count >= 30) {
			tape = tape_sum / (tape_count-10);

			sprintf(b, "found tape! %d\r\n", tape);
			HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
			break;
		}
	}



//	runMotors(LEFT, FWD, 0);
//	runMotors(RIGHT, FWD, 0);
}

void manCalibrate(uint16_t* tape_val, uint16_t* wood_val) {
	HAL_Delay(50);

	char b [100];

	uint16_t tape = 0;
	uint16_t wood = 0;

	uint8_t reads = 10;

	for (int i = 0; i < reads; i++) {
		tape += readSensor(SENSORS[1]);
		tape += readSensor(SENSORS[2]);
		wood += readSensor(SENSORS[0]);
		wood += readSensor(SENSORS[3]);
	}

	*tape_val = tape / (2*reads);
	*wood_val = wood / (2*reads);

	sprintf(b, "wood %d tape %d\r\n", *wood_val, *tape_val);
	HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
}

void midCalibrate(uint16_t* tape_val, uint16_t* wood_val) {
	HAL_Delay(50);

		char b [100];

		uint16_t tape = 0;
		uint16_t wood = (readSensor(SENSORS[0]) + readSensor(SENSORS[3]))/2;

		uint32_t wood_sum = wood;
		uint16_t avg_reading = 0;

		uint8_t reads = 10;

		uint16_t count = 1;
		uint16_t tape_count = 0;

		for (int i = 0; i < reads; i++) {
			tape += readSensor(SENSORS[1]);
			tape += readSensor(SENSORS[2]);
		}

		runMotors(LEFT, FWD, 0.4);
		runMotors(RIGHT, FWD, 0.4);


		while (1) {
				count++;
				avg_reading = 0;

				avg_reading += readSensor(SENSORS[0]);
				avg_reading += readSensor(SENSORS[3]);
				avg_reading /= 2;

				if (abs(avg_reading - wood) <= wood*0.3) {

					wood_sum += avg_reading;
					wood = wood_sum / count;

					sprintf(b, "wood %d\r\n", wood);
					HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

					if (tape_count > 0) {
						tape_count = 0;
					}
				} else {
					sprintf(b, "tape? %d \r\n", avg_reading);
					HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

					tape_count++;
				}

				if (tape_count >= 5) {
					sprintf(b, "found tape!\r\n");
					HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
					break;
				}
			}

		*tape_val = tape / (2*reads);
		*wood_val = wood;

		sprintf(b, "wood %d tape %d\r\n", *wood_val, *tape_val);
		HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
}

void rotateToAngle(double target, double theta0, uint8_t DIR) {

//	runMotors(RIGHT, FWD, 0);
//	runMotors(LEFT, FWD, 0);
//	HAL_Delay(500);

	double angle = getAngle(theta0);
	runMotors(!DIR, FWD, 0.8);
	runMotors(DIR, BWD, 0.8);

	while (angle < (target-10) || angle > (target+10)) {
		angle = getAngle(theta0);
		//HAL_Delay(10);
	}

	runMotors(RIGHT, FWD, 0);
	runMotors(LEFT, FWD, 0);

	HAL_Delay(100);
//
//	runMotors(RIGHT, FWD, 0.5);
//	runMotors(LEFT, FWD, 0.5);

}

void shoot() {
	HAL_Delay(2000);
}

void intakeAndStuff(double theta0) {
	runMotors(RIGHT, FWD, 0);
	runMotors(LEFT, FWD, 0);
	HAL_Delay(500);

	runMotors(RIGHT, BWD, 0.5);
	runMotors(LEFT, BWD, 0.5);
	HAL_Delay(300);
	runMotors(RIGHT, FWD, 0);
	runMotors(LEFT, FWD, 0);
	HAL_Delay(200);


	// DO NOT MAKE DUTY MORE THAN 0.8
	TIM3->CCR3 = 0.65*TIM3->ARR;

	if(getAngle(theta0) > 270 )
			rotateToAngle(270, theta0, LEFT);
	else
			rotateToAngle(270, theta0, RIGHT);

	runMotors(RIGHT, FWD, 0.25);
	runMotors(LEFT, FWD, 0.25);

	HAL_Delay(300);

	runMotors(RIGHT, FWD, 0);
	runMotors(LEFT, FWD, 0);

	HAL_Delay(300);

	rotateToAngle(225, theta0, LEFT);

	HAL_Delay(100);

	shoot();

	HAL_Delay(300);

	rotateToAngle(90, theta0, LEFT);

	runMotors(RIGHT, FWD, 0.6);
	runMotors(LEFT, FWD, 0.4);

}

const double MAX_DUTY = 0.65;
const double MIN_DUTY = 0;
const double AVG_DUTY = 0.45;
const double DUTY_RANGE = 0.45;

const double kp = 0.8;
const double kd = 1.2;




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
  MX_USART2_UART_Init();
  MX_TIM1_Init();
  MX_ADC2_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  //MX_I2C1_Init();
//  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
//  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
//  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);


  HAL_Delay(700);

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);

  // Make sure all motors are stopped
  TIM1->CCR1 = 0;
  TIM1->CCR2 = 0;
  TIM1->CCR3 = 0;
  TIM1->CCR4 = 0;

  TIM3->CCR3 = 0;

  HAL_GPIO_WritePin(GPIOA, LS_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, RS_DIR_Pin, GPIO_PIN_RESET);

  char b [100];

  sprintf(b, "hello world \r\n");
  HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

  initSensors();
  initIMU();

  runMotors(LEFT, BWD, 0.7);
  runMotors(RIGHT, FWD, 0.7);
  while (1) {
  	getAngle(0);
  }


  uint16_t tape_val;
  uint16_t wood_val;

  HAL_Delay(100);

  double theta0 = getAngle(0);

//  while(1) {
//  	runMotors(LEFT, FWD, 0.8);
//  	runMotors(RIGHT, BWD, 0.8);
//  	HAL_Delay(5000);
//  	runMotors(LEFT, BWD, 0.8);
//  	runMotors(RIGHT, FWD, 0.8);
//  	HAL_Delay(5000);
//  }

  midCalibrate(&tape_val, &wood_val);

  uint16_t TARGET = (tape_val+wood_val)/2;
  uint16_t READING_RANGE = TARGET - wood_val;

  runMotors(LEFT, FWD, 0.5);
  runMotors(RIGHT, FWD, 0.5);

  HAL_Delay(300);

  uint16_t turn_count_l = 0;
  uint16_t turn_count_r = 0;

  int16_t prev_r = TARGET;
  int16_t prev_l = TARGET;

  uint16_t state = FOLLOW1;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

  	uint16_t right = readSensor(SENSORS[1]);
  	uint16_t blue_r = getRGB(B);

  	uint16_t left = readSensor(SENSORS[2]);
  	uint16_t blue_l = getRGB(B);

  	if (state == FOLLOW1 && (blue_r > 1000 || blue_l > 1000)) {
  		state = INTERMISSION;
  		intakeAndStuff(theta0);
  		state = FOLLOW2;
  		continue;
  	}



//  	sprintf(b, "left sensor %d right sensor %d\r\n", right, left);
//  	HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

  	int16_t error_r = right - TARGET;
  	int16_t error_l = left - TARGET;

//  	sprintf(b, "left %d right %d ", error_l, error_r);
//  	HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

  	double duty_r;//MIN_DUTY + ratio/(tape/wood)*(MAX_DUTY-MIN_DUTY);//
  	double duty_l;//MIN_DUTY + (1/ratio)/(tape/wood)*(MAX_DUTY-MIN_DUTY); //

  	if (left < wood_val*1.1 && right < wood_val*1.1) {
    	prev_r = error_r;
    	prev_l = error_l;
  		continue;
  	}

  	if (left < wood_val*1.01 && right < 0.99*(tape_val-wood_val) + wood_val) {
  		turn_count_r ++;
  		turn_count_l = 0;
  	} else if (right < wood_val*1.01 && left < 0.99*(tape_val-wood_val) + wood_val) {
  		turn_count_r = 0;
  		turn_count_l ++;
  	} else {
  		turn_count_l = 0;
  		turn_count_r = 0;
  	}

  	if (turn_count_r > 3) {
  		duty_r = -0.6;
  		duty_l = 0.6;
  	} else if (turn_count_l > 3) {
  		duty_l = -0.6;
  		duty_r = 0.6;
  	} else {

  	  	double delta_r = (kp * error_r - kd * (error_r - prev_r)) / READING_RANGE * DUTY_RANGE;
  	  	double delta_l = (kp * error_l - kd * (error_l - prev_l)) / READING_RANGE * DUTY_RANGE;

  	  	duty_r = AVG_DUTY - delta_r;
  	  	duty_l = AVG_DUTY - delta_l;

  	    if (duty_l < MIN_DUTY) duty_l = MIN_DUTY;
  	    if (duty_l > MAX_DUTY) duty_l = MAX_DUTY;
  	    if (duty_r < MIN_DUTY) duty_r = MIN_DUTY;
  	    if (duty_r > MAX_DUTY) duty_r = MAX_DUTY;
  	}

  //	sprintf(b, "delta_r %f duty_r %f\r\n", delta_r, duty_r);
  //	HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);
//  	sprintf(b, "left duty %f right duty %f\r\n", duty_l*100, duty_r*100);
//  	HAL_UART_Transmit(&huart2, (uint8_t*)b, strlen(b), HAL_MAX_DELAY);

  	prev_r = error_r;
  	prev_l = error_l;

  	runMotors(RIGHT, FWD, duty_r);
  	runMotors(LEFT, FWD, duty_l);

  //	HAL_Delay(1000);


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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 9;
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
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc2.Init.Resolution = ADC_RESOLUTION_12B;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.GainCompensation = 0;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc2.Init.LowPowerAutoWait = DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.DMAContinuousRequests = DISABLE;
  hadc2.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc2.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_17;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x10808DD3;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

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
  htim1.Init.Prescaler = 72-1;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 99;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
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
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
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
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
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
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4.294967295E9;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_IC_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;
  if (HAL_TIM_IC_ConfigChannel(&htim3, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_IC_ConfigChannel(&htim3, &sConfigIC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LS_DIR_Pin|RS_DIR_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LS_DIR_Pin RS_DIR_Pin */
  GPIO_InitStruct.Pin = LS_DIR_Pin|RS_DIR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
//static void MX_I2C1_Init(void)
//{
//
//	hi2c1.Instance = I2C1;
//	hi2c1.Init.ClockSpeed = 100000;
//	hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
//	hi2c1.Init.OwnAddress1 = 0;
//	hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
//	hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
//	hi2c1.Init.OwnAddress2 = 0;
//	hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
//	hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
//	if (HAL_I2C_Init(&hi2c1) != HAL_OK)
//	{
//		Error_Handler();
//	}
//
//}

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
