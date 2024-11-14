#include <clocks.h>
#include <stm32l053xx.h>
#include <systick.h>
//#include <uart.h>
//#include <adxl345.h>
//#include <i2c_sw.h>
//#include <qmc5883l.h>
#include <ili9341.h>
#include <stdio.h>

#define TIME_60MS 60000
#define T_400 400

volatile uint16_t adc_array[240];//buffer to store the converted data of adc
volatile uint8_t adc_index = 0;// index of the buffer
volatile uint8_t messe_cnt = 240 ;// counter to measure the (time|index) of the first half of the discharge of the condensator
volatile uint8_t zoom = 3; 
volatile uint8_t draw_rc;


void Init_Pins()
{
    //configure the The ports in Register GPIOC
    RCC-> IOPENR |= RCC_IOPENR_GPIOCEN; //ENABLE GPIO OF REGISTER C


    //enable the pin c8
    GPIOC->MODER |= GPIO_MODER_MODE8_0; //SET THE PIN C8 as output mode
    GPIOC->MODER &= ~GPIO_MODER_MODE8_1;//SET THE PIN C8 as output mode

    //enable the pin c4
    GPIOC->MODER |= GPIO_MODER_MODE4_0; //SET THE PIN C4 as output mode
    GPIOC->MODER &= ~GPIO_MODER_MODE4_1;//SET THE PIN C4 as output mode

    //enable the pin c5
    GPIOC->MODER |= GPIO_MODER_MODE5_0; //SET THE PIN C5 as analog mode
    GPIOC->MODER |= GPIO_MODER_MODE5_1;//SET THE PIN C5 as analog mode

    //enable the pin c6
    GPIOC->MODER |= GPIO_MODER_MODE6_0; //SET THE PIN C6 as output mode
    GPIOC->MODER &= ~GPIO_MODER_MODE6_1;//SET THE PIN C6 as output mode

}

void timer(int n,uint16_t time)
{
    // timer[Hz]= CLK /(Psc+1)*(Arr+1) in our case CLK=16*(10**6) Hz | Time_Cnt=1/timer[Hz]
    switch (n)
    {
    case 2:
        RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
        TIM2->CR1 &= ~(TIM_CR1_CEN);
        TIM2->PSC |= 16 - 1 ; // in ms zu messen 
        TIM2 ->ARR = time ;
        TIM2 -> DIER |= TIM_DIER_UIE ;
        TIM2 -> CR1 |= TIM_CR1_CEN ;
        TIM2 -> EGR |= TIM_EGR_UG;

        NVIC_ClearPendingIRQ(TIM2_IRQn);
        NVIC_SetPriority(TIM2_IRQn, 3); 
        NVIC_EnableIRQ(TIM2_IRQn); 
        break;
    case 6:
        RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
        TIM6->CR1 &= ~(TIM_CR1_CEN);
        TIM6->PSC |= 16 - 1 ; // in ms zu messen 
        TIM6 ->ARR = time ;
        TIM6 -> DIER |= TIM_DIER_UIE ;
        TIM6 -> CR1 |= TIM_CR1_CEN ;
        TIM6 -> EGR |= TIM_EGR_UG;
        
        NVIC_ClearPendingIRQ(TIM6_IRQn);
        NVIC_SetPriority(TIM6_IRQn, 1); 
        NVIC_EnableIRQ(TIM6_IRQn);
        break;
    default:
        break;
    }
}

void Set_Buttons()
{
    //enable the buttons on Carlos
    RCC->IOPENR |= RCC_IOPENR_IOPBEN; //Enable the Register|the port B

    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN; //Enable the Peripheral clock register

    //set GPIOB in Input Modus For Pin B1/2
    GPIOB->MODER &= ~GPIO_MODER_MODE1_0;        // Enable PB1 as input
    GPIOB->MODER &= ~GPIO_MODER_MODE1_1;

    GPIOB->MODER &= ~GPIO_MODER_MODE2_0;        // Enable PB2 as ipnut
    GPIOB->MODER &= ~GPIO_MODER_MODE2_1;

    //Einschalten der EXTIs für both Buttons
    SYSCFG->EXTICR[0] |= SYSCFG_EXTICR1_EXTI1_PB;
    SYSCFG->EXTICR[0] |= SYSCFG_EXTICR1_EXTI2_PB;

    //Interrupt Handler SW1
    EXTI->IMR |= EXTI_IMR_IM1;
    EXTI->FTSR |= EXTI_FTSR_FT1;
    NVIC_SetPriority(EXTI0_1_IRQn, 2);
    NVIC_EnableIRQ(EXTI0_1_IRQn);

    //Interrupt Handler SW2
    EXTI->IMR |= EXTI_IMR_IM2;
    EXTI->FTSR |= EXTI_FTSR_FT2;
    NVIC_SetPriority(EXTI2_3_IRQn, 2);
    NVIC_EnableIRQ(EXTI2_3_IRQn);
}

void ADC1_ENABLE()
{
    /* Enable ADC*/
    RCC->APB2ENR |= RCC_APB2ENR_ADCEN;

    ADC1->ISR |= ADC_ISR_ADRDY;                 // Write ADRDY = 1 to clear it

    ADC1->CHSELR |= ADC_CHSELR_CHSEL15;         // Select adc_15 in (PC5) as only analog input

    ADC1->CR |= ADC_CR_ADEN;                    // Enable ADC

    while ((ADC1->ISR & ADC_ISR_ADRDY) == 0);

}

void draw_value(uint32_t measurement)
{
    char str[10];
    sprintf(str, "%d", (int) measurement);
    ili9341_str_print(str, ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
}

void draw_on_display()
{
    GPIOC-> ODR |= GPIO_ODR_OD6;
    uint8_t draw_i = adc_index;/*(adc_index-120)%240;*/
    uint32_t temp_avg = 0;
    uint16_t max_volt = 0;
    uint16_t min_volt = 4096;
    int16_t e_pos = 0;
    int16_t max_pos = 0;
    uint8_t trc_drawn = 0;
    uint8_t avg_drawn = 0;
    
    ili9341_rect_fill(0,62,240,75,ILI9341_COLOR_BLACK);
    ili9341_line_draw(120, 62, 120, 132, ILI9341_COLOR_GREEN); // (112, 3267 >> 6, 112 , 0) oder (draw_i, (adc_array[draw_i] >>6), draw_i , 0)

    for (uint8_t i = 0; i < 240 ; i++)
    {
        temp_avg = temp_avg + adc_array[draw_i];        // get average voltage

        if(max_volt < adc_array[draw_i])                // get max and min voltage
        {
            max_volt = adc_array[draw_i];
        }

        if(min_volt > adc_array[draw_i])
        {
            min_volt = adc_array[draw_i];
        }

        uint8_t y = (adc_array[draw_i] >>6); // gemesster Wert / 64
        ili9341_pixel_set(i,126-y,ILI9341_COLOR_WHITE);
        ili9341_pixel_set(i,126-y-1,ILI9341_COLOR_WHITE);

        if(((adc_array[(draw_i+239) % 240]) >= 4090) && ((adc_array[(draw_i+240) % 240]) < 4090) && (avg_drawn == 0))
        {
            ili9341_line_draw(i, 62, i, 132, ILI9341_COLOR_BLUE);
            max_pos = i;
            avg_drawn = 1;
        }

        if((avg_drawn == 1) && (adc_array[draw_i] <= 1506) && (trc_drawn == 0))  // 1/e * 4096 = ca. 1506
        {
            ili9341_line_draw(i, 62, i, 132, ILI9341_COLOR_ORANGE);
            e_pos = i;
            trc_drawn = 1;
        }

        if (draw_i < 239)
        {
            draw_i++;
        }
        else
        {
            draw_i=0;
        }
    }
    messe_cnt=240;

    int32_t average = 3300;
    average = average * (temp_avg / 240);
    average = average >> 12;

    int32_t peak2peak = 3300;
    peak2peak = peak2peak * (max_volt-min_volt);
    peak2peak = peak2peak >> 12;

    int16_t span = TIM2->ARR;

    int32_t rc = (TIM2->ARR) * (e_pos-max_pos);

    ili9341_text_pos_set(9,7);
    draw_value(average);
    ili9341_text_pos_set(9,8);
    draw_value(peak2peak);
    ili9341_text_pos_set(9,9);
    draw_value(rc);
    ili9341_text_pos_set(9,10);
    draw_value(rc);
    ili9341_text_pos_set(9,11);
    draw_value(span);
    ili9341_text_pos_set(9,12);
    ili9341_char_put(zoom + 48, ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);



    GPIOC-> ODR &= ~(GPIO_ODR_OD6);
}

void TIM2_IRQHandler(void)
{
    if(TIM2 -> SR & TIM_SR_UIF)
    {   
        ADC1->CR |= ADC_CR_ADSTART;         // Start the convertion

        //while (~(ADC1->ISR & ADC_ISR_EOC)); // Waits until it finishes the Convertion (End Of Conversion)
        while (ADC1->CR &  ADC_CR_ADSTART); // Waits until it finishes the Convertion (End Of Conversion)
        adc_array[adc_index] = ADC1->DR;    // Store The converted Values in the Buffer 
        //uint16_t mid = (adc_index + 120) % 240;
        
        if (messe_cnt > 0)
        {
            messe_cnt--;
        }else
        {
            if(adc_array[(adc_index + 120) % 240 - 1] > 2048  && adc_array[(adc_index + 120) % 240] <= 2048)
            {       
            draw_rc = 1;
            NVIC_DisableIRQ(TIM2_IRQn);
            }
        
        }

        if(adc_index < 239)
        {
            adc_index++;
        }
        else
        {
            adc_index=0;
        }
        TIM2 -> SR &= ~TIM_SR_UIF; //reset interrupt flag
    }    
}

void TIM6_IRQHandler(void)
{
    if(TIM6 -> SR & TIM_SR_UIF)
    {   
        GPIOC->ODR ^= GPIO_ODR_OD8;
        GPIOC->ODR ^= GPIO_ODR_OD4;
        
        TIM6-> SR &= ~TIM_SR_UIF;
    }    
}

void EXTI0_1_IRQHandler(void)
{
    systick_delay_ms(10);
    if(EXTI->PR & EXTI_PR_PIF1)
    {
        systick_delay_ms(100);
        if (TIM2->ARR > 100)
        {
            TIM2->ARR = (TIM2->ARR/2);
            zoom = zoom - 1;
        }
        messe_cnt = 240;
        EXTI->PR = EXTI_PR_PIF1;
    }
}

void EXTI2_3_IRQHandler(void)
{
    if(EXTI->PR & EXTI_PR_PIF2)
    {
        systick_delay_ms(10);
        if (TIM2->ARR < 1600)
        {
            TIM2->ARR = (TIM2->ARR*2);
            zoom = zoom + 1;
        }
        messe_cnt = 240;
        EXTI->PR = EXTI_PR_PIF2;
    }
}
void ili9341_str()
{
    ili9341_str_print("    Project 3 \n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("  Oscilloscope \n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("\n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("\n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("\n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("\n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("\n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("AVG  :         mV \n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("P-P  :         mV \n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("RC   :         us \n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("C    :         nF \n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("Span :         ms \n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
    ili9341_str_print("Zoom :            \n\r", ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK);
}


int main(void)
{
    clocks_init_pmi();
    ili9341_init(0);
    ili9341_str();
    Init_Pins();
    Set_Buttons();
    ADC1_ENABLE();
    timer(2,T_400);
    timer(6,TIME_60MS);
    
    while(1)
    {
        if (draw_rc)
        {
            draw_on_display();
            draw_rc=0;
            NVIC_EnableIRQ(TIM2_IRQn);
        }
        
    }
    return RC_SUCC;
}


