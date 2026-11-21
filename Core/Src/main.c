#include "main.h"
#include "gpio.h"
#include "tim.h"
#include "usart.h"
#include "OLED.h"

#include <stdio.h>

#define SERVO_MIN_PULSE_US       550U
#define SERVO_MAX_PULSE_US       2450U
#define SERVO_SEQUENCE_PERIOD_MS 1000U
#define KEY_DEBOUNCE_MS          20U
#define HEARTBEAT_PERIOD_MS      500U

static const uint8_t servo_sequence[] = {0U, 45U, 90U, 135U, 180U};

static volatile uint8_t key_inc_event;
static volatile uint8_t key_dec_event;

static uint8_t target_angle;
static uint8_t sequence_active = 1U;
static uint8_t sequence_index;
static uint8_t inc_key_checking;
static uint8_t dec_key_checking;
static uint32_t sequence_tick;
static uint32_t inc_key_tick;
static uint32_t dec_key_tick;
static uint32_t heartbeat_tick;

void SystemClock_Config(void);

static uint16_t Servo_AngleToCcr(uint8_t angle)
{
  uint32_t span;

  if (angle > 180U)
  {
    angle = 180U;
  }

  span = SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US;
  return (uint16_t)(SERVO_MIN_PULSE_US +
                    ((uint32_t)angle * span + 90U) / 180U);
}

static void OLED_ShowStatus(uint8_t angle, uint16_t ccr)
{
  OLED_ShowString(1U, 1U, "Target:");
  OLED_ShowNum(1U, 8U, angle, 3U);
  OLED_ShowString(1U, 11U, "deg");
  OLED_ShowString(1U, 14U, "   ");

  OLED_ShowString(2U, 1U, "CCR   :");
  OLED_ShowNum(2U, 8U, ccr, 4U);
  OLED_ShowString(2U, 12U, "     ");

  OLED_ShowString(3U, 1U, "Pulse :");
  OLED_ShowNum(3U, 8U, ccr / 1000U, 1U);
  OLED_ShowString(3U, 9U, ".");
  OLED_ShowNum(3U, 10U, ccr % 1000U, 3U);
  OLED_ShowString(3U, 13U, "ms");
  OLED_ShowString(3U, 15U, "  ");
  OLED_ShowString(4U, 1U, "                ");
}

static void UART_PrintStatus(uint8_t angle, uint16_t ccr)
{
  char message[64];
  int length = snprintf(
      message,
      sizeof(message),
      "Target=%3u  CCR=%4u  Pulse=%u.%03ums\r\n",
      (unsigned int)angle,
      (unsigned int)ccr,
      (unsigned int)(ccr / 1000U),
      (unsigned int)(ccr % 1000U));

  if ((length > 0) && (length < (int)sizeof(message)))
  {
    (void)HAL_UART_Transmit(
        &huart1,
        (uint8_t *)message,
        (uint16_t)length,
        100U);
  }
}

static void Servo_SetAngle(uint8_t angle)
{
  uint16_t ccr;

  if (angle > 180U)
  {
    angle = 180U;
  }

  target_angle = angle;
  ccr = Servo_AngleToCcr(target_angle);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, ccr);

  OLED_ShowStatus(target_angle, ccr);
  UART_PrintStatus(target_angle, ccr);
}

static void Servo_AdjustAngle(int8_t delta)
{
  int16_t next_angle = (int16_t)target_angle + delta;

  sequence_active = 0U;

  if (next_angle < 0)
  {
    next_angle = 0;
  }
  else if (next_angle > 180)
  {
    next_angle = 180;
  }

  if ((uint8_t)next_angle != target_angle)
  {
    Servo_SetAngle((uint8_t)next_angle);
  }
}

static void ProcessKeys(uint32_t now)
{
  if (key_inc_event != 0U)
  {
    key_inc_event = 0U;
    inc_key_checking = 1U;
    inc_key_tick = now;
  }
  if (key_dec_event != 0U)
  {
    key_dec_event = 0U;
    dec_key_checking = 1U;
    dec_key_tick = now;
  }

  if ((inc_key_checking != 0U) &&
      ((now - inc_key_tick) >= KEY_DEBOUNCE_MS))
  {
    inc_key_checking = 0U;
    if (HAL_GPIO_ReadPin(KEY_INC_GPIO_Port, KEY_INC_Pin) == GPIO_PIN_RESET)
    {
      Servo_AdjustAngle(15);
    }
  }

  if ((dec_key_checking != 0U) &&
      ((now - dec_key_tick) >= KEY_DEBOUNCE_MS))
  {
    dec_key_checking = 0U;
    if (HAL_GPIO_ReadPin(KEY_DEC_GPIO_Port, KEY_DEC_Pin) == GPIO_PIN_RESET)
    {
      Servo_AdjustAngle(-15);
    }
  }
}

static void ProcessSequence(uint32_t now)
{
  if ((sequence_active != 0U) &&
      ((now - sequence_tick) >= SERVO_SEQUENCE_PERIOD_MS))
  {
    sequence_tick = now;
    if (sequence_index < (sizeof(servo_sequence) - 1U))
    {
      sequence_index++;
      Servo_SetAngle(servo_sequence[sequence_index]);
    }
    else
    {
      sequence_active = 0U;
    }
  }
}

static void VerifyTimerClock(void)
{
  uint32_t ppre1 = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
  uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();

  if ((ppre1 & 0x4U) != 0U)
  {
    timer_clock *= 2U;
  }

  if (timer_clock != 72000000U)
  {
    Error_Handler();
  }
}

int main(void)
{
  uint32_t now;

  HAL_Init();
  SystemClock_Config();
  VerifyTimerClock();

  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();

  if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  OLED_Init();
  Servo_SetAngle(servo_sequence[0]);

  sequence_tick = HAL_GetTick();
  heartbeat_tick = sequence_tick;

  while (1)
  {
    now = HAL_GetTick();

    ProcessKeys(now);
    ProcessSequence(now);

    if ((now - heartbeat_tick) >= HEARTBEAT_PERIOD_MS)
    {
      heartbeat_tick = now;
      HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
    }
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef oscillator = {0};
  RCC_ClkInitTypeDef clocks = {0};

  oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  oscillator.HSEState = RCC_HSE_ON;
  oscillator.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  oscillator.HSIState = RCC_HSI_ON;
  oscillator.PLL.PLLState = RCC_PLL_ON;
  oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  oscillator.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
  {
    Error_Handler();
  }

  clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clocks.APB1CLKDivider = RCC_HCLK_DIV2;
  clocks.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == KEY_INC_Pin)
  {
    key_inc_event = 1U;
  }
  else if (GPIO_Pin == KEY_DEC_Pin)
  {
    key_dec_event = 1U;
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
