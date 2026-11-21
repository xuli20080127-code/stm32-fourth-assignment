#include "tim.h"

TIM_HandleTypeDef htim2;

static void TIM_MspPostInit(TIM_HandleTypeDef *tim_handle);

void MX_TIM2_Init(void)
{
  TIM_ClockConfigTypeDef clock_config = {0};
  TIM_MasterConfigTypeDef master_config = {0};
  TIM_OC_InitTypeDef output_config = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 71U;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 19999U;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }

  clock_config.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &clock_config) != HAL_OK)
  {
    Error_Handler();
  }

  master_config.MasterOutputTrigger = TIM_TRGO_RESET;
  master_config.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &master_config) != HAL_OK)
  {
    Error_Handler();
  }

  output_config.OCMode = TIM_OCMODE_PWM1;
  output_config.Pulse = 550U;
  output_config.OCPolarity = TIM_OCPOLARITY_HIGH;
  output_config.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &output_config, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  TIM_MspPostInit(&htim2);
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *tim_pwmHandle)
{
  if (tim_pwmHandle->Instance == TIM2)
  {
    __HAL_RCC_TIM2_CLK_ENABLE();
  }
}

void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef *tim_pwmHandle)
{
  if (tim_pwmHandle->Instance == TIM2)
  {
    __HAL_RCC_TIM2_CLK_DISABLE();
  }
}

static void TIM_MspPostInit(TIM_HandleTypeDef *tim_handle)
{
  GPIO_InitTypeDef gpio_init = {0};

  if (tim_handle->Instance == TIM2)
  {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio_init.Pin = GPIO_PIN_0;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio_init);
  }
}
