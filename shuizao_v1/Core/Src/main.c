/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "pump.h"
#include "command.h"
#include "motor.h"
#include "string.h"
#include <stdio.h>
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
//extern bool flag_PG[PG_Z_Number];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */



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
  MX_DMA_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
    HAL_Delay(2000);
    //测试

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);

    motor_no();
    pump_set_no();

//    uint8_t message1[]={0xB8,0xB4,0xCE,0xBB,0xD6,0xD0};
//    sendMessage(message1,6);
//    HAL_Delay(100000);
//    motor_forward();
//    HAL_Delay(3000);
//    motor_no();


//    pump_set_no();
//    pump_set_no();
//    motor_forward();
//    HAL_Delay(2000);
//    motor_back();
//    HAL_Delay(2000);
//    motor_no();
//    HAL_Delay(10000);
//    UART_Command_Init();

//    while (1)
//    {
//        HAL_Delay(1000);
//        HAL_GPIO_TogglePin(LED1_GPIO_Port,LED1_Pin);
//        uint8_t temp[32]={};
//        sprintf(temp,"abccd");
//        HAL_UART_Transmit(&huart2,temp,4,1000);
//        PG_read_motor();
//        for(int i=0;i<4;i++) {
//            if (flag_PG_motor[i]) {
//                sprintf(temp, "%d=high",i);
//                HAL_UART_Transmit(&huart2, (uint8_t *) temp, 10, 1000);
//            }
//            else {
//                sprintf(temp, "%d=low",i);
//                HAL_UART_Transmit(&huart2, (uint8_t *) temp, 10, 1000);
//            }
//        }
//        PG_read_pump();
//        for(int i=0;i<2;i++) {
//            if (flag_PG_pump[i]) {
//                sprintf(temp, "aa%d=low",i);
//                HAL_UART_Transmit(&huart2, (uint8_t *) temp, 10, 1000);
//            }
//            else {
//                sprintf(temp, "aa%d=high",i);
//                HAL_UART_Transmit(&huart2, (uint8_t *) temp, 10, 1000);
//            }
//        }
//    }

//    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 50);
//    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 50);
//    HAL_Delay(100000);

//    motor_back();
//    HAL_Delay(5000);
//    motor_forward();
//    pump_set_out();
//    HAL_Delay(100000);
//    motor_back();
//    HAL_Delay(8000);
//    motor_no();
//    HAL_Delay(4000000);
//    motor_back();
//    HAL_Delay(10000);
//    motor_no();
//    HAL_Delay(4000);
//    motor_forward();
//    HAL_Delay(5000);
    PG_read_motor();
    PG_read_pump();
    HAL_Delay(100);
    UART_Command_Init();
    uint8_t message[]={0xB8,0xB4,0xCE,0xBB,0xD6,0xD0};
    sendMessage(message,6);
    HAL_Delay(1);
    sendMessage(message,6);
    //已到达位置
    if(flag_PG_motor[3])
    {

    }
    //未到达初始位置
    else
    {
        //中文：复位中
        uint8_t message[]={0xB8,0xB4,0xCE,0xBB,0xD6,0xD0};
        sendMessage(message,6);
        HAL_Delay(1);
        sendMessage(message,6);
        motor_back();
        uint32_t time_count=0;
        while (!flag_PG_motor[3])
        {
            PG_read_motor();
            if(flag_PG_motor[0]||flag_PG_motor[1]||flag_PG_motor[2])
                break;
            HAL_Delay(10);
            time_count+=10;
            if(time_count>50000)
                break;
        }
        motor_no();

        motor_forward();
        while (!flag_PG_motor[3])
        {
            PG_read_motor();
            HAL_Delay(10);
        }
        motor_no();
    }

    // 中文：复位完成
    HAL_Delay(100);
    uint8_t message1[]={0xB8,0xB4,0xCE,0xBB,0xCD,0xEA,0xB3,0xC9
    };
    sendMessage(message1,8);
    HAL_Delay(10);
    uint8_t message_1[]={0xB8,0xB4,0xCE,0xBB,0xCD,0xEA,0xB3,0xC9
    };
    sendMessage(message_1,8);
    HAL_Delay(10);

    //WriteToFlash((uint32_t*)&pump_delay_time_50[0], (uint32_t*)&pump_delay_time_100[0]);
    ReadFromFlash((uint32_t*)&pump_delay_time_50[0], (uint32_t*)&pump_delay_time_100[0]);

    /*******/
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
//      HAL_Delay(100000000);
    //假设收到命令，开始工作
//    if(flag_200||flag_100||flag_50) {
//        while (1) {
//            PG_read_pump();
//            // 说明此时Y轴并没有到达合适位置
//            if (!flag_PG_pump[1]) {
//
//                //屏幕显示，或者蜂鸣器叫之类的
//                page_warn();
//                flag_100 = false;
//                flag_200 = false;
//                flag_50 = false;
//                break;
//            } else {
//                break;
//            }
//            HAL_Delay(200);
//        }
//
//        // Z轴开始下降
//        if (flag_200) {
//            motor_back();
////            while ((!flag_PG_motor[2])&&flag_200) {
//            while ((!flag_PG_motor[1])&&flag_200) {
//                PG_read_motor();
//                HAL_Delay(10);
//            }
//            motor_no();
//        } else if (flag_100) {
//            motor_back();
////            while ((!flag_PG_motor[1])&&flag_100) {
//            while ((!flag_PG_motor[0])&&flag_100) {
//                PG_read_motor();
//                HAL_Delay(10);
//            }
//            motor_no();
//        }else if (flag_50) {
//            motor_back();
////            while ((!flag_PG_motor[1])&&flag_100) {
//            while ((!flag_PG_motor[2])&&flag_50) {
//                PG_read_motor();
//                HAL_Delay(10);
//            }
//            motor_no();
//        }
//
//        if (flag_200 || flag_100 || flag_50) {
//            //下降完成后，开始吸
//            pump_set_in();
//            //中文：抽取中
//            uint8_t message[] = {0xB3, 0xE9, 0xC8, 0xA1, 0xD6, 0xD0};
//            sendMessage(message, 6);
//            // 这里要等待一个标志位，等待人把打开漏斗阀门,收取沉淀液；
//            while ((!flag_xiqu)&&(flag_200 || flag_100|| flag_50)) {
//                HAL_Delay(10);
//
//            }
//            pump_set_no();
//            if(flag_200 || flag_100|| flag_50){
//                pump_set_no();
//                HAL_Delay(100);
//                // 中文：等待用户操作
//                uint8_t message[] = {0xB5, 0xC8, 0xB4, 0xFD, 0xD3, 0xC3, 0xBB, 0xA7, 0xB2, 0xD9, 0xD7, 0xF7
//                };
//                sendMessage(message, 12);
//            }
//            while ((!flag_shouqu)&&(flag_200 || flag_100|| flag_50)) {
//                HAL_Delay(10);
//            }
//            if(flag_200 || flag_100|| flag_50) {
//                //中文：清洗中
//                uint8_t message_q[] = {0xC7, 0xE5, 0xCF, 0xB4, 0xD6, 0xD0
//                };
//                sendMessage(message_q, 6);
//                // 接下来下降
//                motor_back();
//            }
//            while ((!flag_PG_motor[0])&&(flag_200 || flag_100|| flag_50)) {
//                PG_read_motor();
//                HAL_Delay(10);
//            }
//            if(flag_200 || flag_100|| flag_50) {
//                motor_no();
//                //接下来 开始喷水5s；
//                pump_set_out();
//
//                uint16_t time=0;
//                while (time<10000 && (flag_200 || flag_100|| flag_50))
//                {
//                    HAL_Delay(100);
//                    time+=100;
//                }
//                //接下来开始上升，同时喷水
//                if (flag_200 || flag_100|| flag_50)
//                    motor_forward();
//            }
//            //上升到200ml
////            while ((!flag_PG_motor[2]) &&(flag_200 || flag_100)){
//            while ((!flag_PG_motor[2]) &&(flag_200 || flag_100|| flag_50)){
//                PG_read_motor();
//                HAL_Delay(10);
//            }
//            if(flag_200 || flag_100|| flag_50) {
//                HAL_Delay(400);
//                pump_set_no();
//            }
//            //接下来上升到最高处即初始复位位置即可
//            while ((!flag_PG_motor[3])&&(flag_200 || flag_100|| flag_50)) {
//                PG_read_motor();
//                HAL_Delay(10);
//            }
//            if(flag_200 || flag_100|| flag_50) {
//                //清掉一些标志量，同时控制屏幕显示，或者蜂鸣器
//                HAL_UART_Transmit(&huart2, (uint8_t *) "end", 10, 1000);
//                // 中文：操作完成
//                uint8_t message_f[] = {0xB2, 0xD9, 0xD7, 0xF7, 0xCD, 0xEA, 0xB3, 0xC9
//                };
//                sendMessage(message_f, 8);
//            }
//        }
//        // 中文：操作完成
//        uint8_t message_f[] = {0xB2, 0xD9, 0xD7, 0xF7, 0xCD, 0xEA, 0xB3, 0xC9
//        };
//        sendMessage(message_f, 8);
//        motor_no();
//        pump_set_no();
//        flag_200 = false;
//        flag_100 = false;
//        flag_50 = false;
//        flag_shouqu = false;
//        flag_xiqu = false;
//    }
      if(flag_reset)
      {
          __set_FAULTMASK(1);
          HAL_NVIC_SystemReset();
      }
      if(flag_200||flag_100||flag_50) {
//          while (1) {
//              PG_read_pump();
//              // 说明此时Y轴并没有到达合适位置
//              if (!flag_PG_pump[1]) {
//
//                  //屏幕显示，或者蜂鸣器叫之类的
//                  page_warn();
//                  flag_100 = false;
//                  flag_200 = false;
//                  flag_50 = false;
//                  break;
//              } else {
//                  break;
//              }
//              HAL_Delay(200);
//          }

//          pump_test();
//
//          motor_back();
////            while ((!flag_PG_motor[2])&&flag_200) {
//              while ((!flag_PG_motor[2])&&flag_50) {
//                  PG_read_motor();
//                  HAL_Delay(10);
//              }

//            motor_no_no_delay();
//              pump_set_in();
//          HAL_Delay(10000);
//              pump_set_no();
//          HAL_Delay(1000);
//              pump_set_out_different_times();
//          HAL_Delay(6000);


          // Z轴开始下降
          if (flag_200) {
              motor_back();
//            while ((!flag_PG_motor[2])&&flag_200) {
              while ((!flag_PG_motor[1])&&flag_200) {
                  PG_read_motor();
                  HAL_Delay(1);
              }
              motor_no();
          } else if (flag_100) {
              motor_back();
//            while ((!flag_PG_motor[1])&&flag_100) {
              while ((!flag_PG_motor[1])&&flag_100) {
                  PG_read_motor();
                  HAL_Delay(1);
              }
              HAL_Delay(50);
              while ((flag_PG_motor[1])&&flag_100) {
                  PG_read_motor();
                  HAL_Delay(1);
              }
              motor_no();
              motor_forward();
              while ((!flag_PG_motor[1])&&flag_100) {
                  PG_read_motor();
                  HAL_Delay(1);
              }
              motor_no_no_delay();
          }else if (flag_50) {
              motor_back();
//            while ((!flag_PG_motor[1])&&flag_100) {
              while ((!flag_PG_motor[0])&&flag_50) {
                  PG_read_motor();
                  HAL_Delay(1);
              }
              motor_no_no_delay();
          }

          if (flag_200 || flag_100 || flag_50) {
              //下降完成后，开始吸
              pump_set_in();
              //中文：抽取中
              uint8_t message[] = {0xB3, 0xE9, 0xC8, 0xA1, 0xD6, 0xD0};
              sendMessage(message, 6);
              // 时间需要修改
              uint32_t delay_time=180000;
              uint32_t time_count=0;
              while (time_count<delay_time&&(flag_200 || flag_100|| flag_50))
              {
                  HAL_Delay(10);
                  time_count+=10;
              }

              if(flag_200 || flag_100|| flag_50) {
                  pump_set_no();
              }
              if(flag_200 || flag_100|| flag_50) {
                  //中文：清洗中
                  uint8_t message_q[] = {0xC7, 0xE5, 0xCF, 0xB4, 0xD6, 0xD0
                  };
                  sendMessage(message_q, 6);
                  // 接下来上升
                  motor_forward();
                  while ((!flag_PG_motor[2])&&(flag_200 || flag_100|| flag_50)) {
                      PG_read_motor();
                      HAL_Delay(10);
                  }
                  motor_no_no_delay();
                  if (flag_100)
                    pump_set_out_different_times_100ml();
                  if(flag_50)
                      pump_set_out_different_times_50ml();
                  uint16_t time=0;
                  while (time<5000 && (flag_200 || flag_100|| flag_50))
                  {
                      HAL_Delay(10);
                      time+=10;
                  }pump_set_no_no_delay();
                  motor_forward();
                  while ((!flag_PG_motor[3])&&(flag_200 || flag_100|| flag_50)) {
                      PG_read_motor();
                      HAL_Delay(10);
                  }
                  if(flag_200 || flag_100|| flag_50) {

                      // 中文：操作完成
                      uint8_t message_f[] = {0xB2, 0xD9, 0xD7, 0xF7, 0xCD, 0xEA, 0xB3, 0xC9
                      };
                      sendMessage(message_f, 8);
                  }
              }
          }
          // 中文：操作完成
          uint8_t message_f[] = {0xB2, 0xD9, 0xD7, 0xF7, 0xCD, 0xEA, 0xB3, 0xC9
          };
          sendMessage(message_f, 8);
          HAL_TIM_Base_Stop_IT(&htim4);
          motor_no();
          pump_set_no();
          flag_200 = false;
          flag_100 = false;
          flag_50 = false;
          flag_shouqu = false;
          flag_xiqu = false;
      }



    if(flag_mf&&(!flag_ms))
    {
        motor_forward();
        HAL_Delay(10);
        // 中文： 电机上升中
        uint8_t message_f[]={0xB5,0xE7,0xBB,0xFA,0xC9,0xCF,0xC9,0xFD,0xD6,0xD0
        };
        sendMessage(message_f,10);

        PG_read_motor();
        if(flag_PG_motor[3])
        {
            flag_mf=false;
            motor_no();
            //flag_ms=true;
            //中文：电机到达最顶部
            uint8_t message_f[]={0xB5,0xE7,0xBB,0xFA,0xB5,0xBD,0xB4,0xEF,0xD7,0xEE,0xB6,0xA5,0xB2,0xBF
            };
            sendMessage(message_f,14);

        }
    }
    else if(flag_mb&&(!flag_ms)) {
        motor_back();
        HAL_Delay(10);
//        sendMessage("Motor Descending");
        // 中文： 电机下降中
        uint8_t message_f[]={ 0xB5,0xE7,0xBB,0xFA,0xCF,0xC2,0xBD,0xB5,0xD6,0xD0
                               };
        sendMessage(message_f,10);
        PG_read_motor();
        if(flag_PG_motor[0])
        {
            flag_mb=false;
            motor_no();
            //flag_ms=true;

            // 电机到达最底部
            uint8_t message_f[]={  0xB5,0xE7,0xBB,0xFA,0xB5,0xBD,0xB4,0xEF,0xD7,0xEE,0xB5,0xD7,0xB2,0xBF
            };
            sendMessage(message_f,14);
        }
    }
    if(flag_ms)
    {
        // 中文：电机停止运动
        uint8_t message_f[]={0xB5,0xE7,0xBB,0xFA,0xCD,0xA3,0xD6,0xB9,0xD4,0xCB,0xB6,0xAF
        };
        sendMessage(message_f,12);
        motor_no();
        HAL_Delay(10);
        flag_ms=false;


    }

      if(flag_po&&(!flag_ps))
      {
          // 中文：清洗中
          uint8_t message_f[]={    0xC7,0xE5,0xCF,0xB4,0xD6,0xD0
          };
          sendMessage(message_f,6);
          pump_set_out();
          HAL_Delay(10);


      }
      else if(flag_pi&&(!flag_ps)) {
          pump_set_in();
          HAL_Delay(10);
          ///sendMessage("Extracting");
          // 中文：抽取中
          uint8_t message_f[]={      0xB3,0xE9,0xC8,0xA1,0xD6,0xD0
          };
          sendMessage(message_f,6);
      }
      if(flag_ps)
      {
          // 泵停止
          uint8_t message_f[]={        0xB1,0xC3,0xCD,0xA3,0xD6,0xB9
          };
          sendMessage(message_f,6);
          pump_set_no();
          HAL_Delay(10);
          flag_ps=false;
//          sendMessage("Pump stop");

      }

      if(flag_S100)
      {
//          sendMessage("Motor in motion");
            // 中文：电机运动中
          uint8_t message_f[]={0xB5,0xE7,0xBB,0xFA,0xD4,0xCB,0xB6,0xAF,0xD6,0xD0
          };
          sendMessage(message_f,10);
          // 先下降
          motor_back();
          while (!flag_ms)
          {
              HAL_Delay(10);
              PG_read_motor();
              //触底了 → 回退寻找 PG[1]
              if(flag_PG_motor[0])
              {
                  motor_no();
                  motor_forward();
                  HAL_Delay(1000);
                  while (1)
                  {
                      PG_read_motor();
                      if(flag_PG_motor[1])
                      {
                          motor_no();
                          break;
                      }
                      else if(flag_PG_motor[2])
                      {
                          //理论上不会发生的情况
                          break;
                      }
                      HAL_Delay(10);
                  }
                  break;
              }
              //正好找到了 PG[1]
              else if(flag_PG_motor[1])
              {
                  motor_no();
                  break;
              }
          }
          flag_S100=false;
//          sendMessage("Motor finish");
            // 电机运动完成
          uint8_t message[]={0xB5,0xE7,0xBB,0xFA,0xD4,0xCB,0xB6,0xAF,0xCD,0xEA,0xB3,0xC9
          };
          sendMessage(message,12);

      }
      else if(flag_S200)
      {
          //sendMessage("Motor in motion");
          //中文：电机运动中
          uint8_t message_f[]={0xB5,0xE7,0xBB,0xFA,0xD4,0xCB,0xB6,0xAF,0xD6,0xD0
          };
          sendMessage(message_f,10);
          // 先下降
          motor_back();
          while (!flag_ms)
          {
              HAL_Delay(10);
              PG_read_motor();
              //触底了
              if(flag_PG_motor[0])
              {
                  motor_no();
                  motor_forward();
                  HAL_Delay(1000);
                  while (1)
                  {
                      PG_read_motor();
                      if(flag_PG_motor[1])
                      {
                          motor_no();
                          break;
                      }
                      else if(flag_PG_motor[2])
                      {
                          //理论上不会发生的情况
                          break;
                      }
                      HAL_Delay(10);
                  }
                  break;
              }
                  //正好找到了
              else if(flag_PG_motor[1])
              {
                  motor_no();
                  break;
              }
          }
          flag_S200=false;
          //sendMessage("Motor finish");
          // 电机运动完成
          uint8_t message[]={0xB5,0xE7,0xBB,0xFA,0xD4,0xCB,0xB6,0xAF,0xCD,0xEA,0xB3,0xC9
          };
          sendMessage(message,12);
      }
      else if(flag_S50)
      {
          //sendMessage("Motor in motion");
          //中文：电机运动中
          uint8_t message_f[]={0xB5,0xE7,0xBB,0xFA,0xD4,0xCB,0xB6,0xAF,0xD6,0xD0
          };
          sendMessage(message_f,10);
          // 先下降
          motor_back();
          while (!flag_ms)
          {
              HAL_Delay(10);
              PG_read_motor();
              //触底了
              if(flag_PG_motor[0])
              {
                  motor_no();
                  break;
              }

          }
          flag_S50=false;
          //sendMessage("Motor finish");
          // 电机运动完成
          uint8_t message[]={0xB5,0xE7,0xBB,0xFA,0xD4,0xCB,0xB6,0xAF,0xCD,0xEA,0xB3,0xC9
          };
          sendMessage(message,12);
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
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
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
