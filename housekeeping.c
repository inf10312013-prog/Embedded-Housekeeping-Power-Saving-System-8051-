#include <reg52.h>
#include <intrins.h>

#define BUS P0

sbit ADDR0 = P1^0;
sbit ADDR1 = P1^1;
sbit ADDR2 = P1^2;
sbit ADDR3 = P1^3;
sbit ENLED = P1^4;
sbit LCD_E = P1^5;

sbit LCD_RS = P1^0;
sbit LCD_RW = P1^1;

sbit KEY4 = P2^7;
sbit IO_18B20 = P3^2;

typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

typedef enum
{
    STATE_ACTIVE = 0,
    STATE_POWERSAVE,
    STATE_FAULT
} SystemState_t;

/*========================
  Global variables
========================*/
volatile u32 g_ms_tick = 0;
volatile bit g_seg_enable = 1;

SystemState_t g_state = STATE_ACTIVE;

u8  g_counter = 0;
u32 g_last_activity_ms = 0;

int g_temp_raw = 0;
int g_temp_c = 0;

#define TEMP_FAULT_TH  27

u8 code LedChar[10] = {
    0xC0, 0xF9, 0xA4, 0xB0, 0x99,
    0x92, 0x82, 0xF8, 0x80, 0x90
};

u8 LedBuff[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

/*========================
  Delay
========================*/
void delay_ms(u16 ms)
{
    u16 i, j;
    for(i = 0; i < ms; i++)
        for(j = 0; j < 120; j++);
}

/*========================
  7-seg control
========================*/
void Seg_ShowOneDigit(u8 num)
{
    LedBuff[0] = LedChar[num];
    LedBuff[1] = 0xFF;
    LedBuff[2] = 0xFF;
    LedBuff[3] = 0xFF;
    LedBuff[4] = 0xFF;
    LedBuff[5] = 0xFF;
}

void Seg_BlankAll(void)
{
    LedBuff[0] = 0xFF;
    LedBuff[1] = 0xFF;
    LedBuff[2] = 0xFF;
    LedBuff[3] = 0xFF;
    LedBuff[4] = 0xFF;
    LedBuff[5] = 0xFF;
}

void LedRefresh(void)
{
    static u8 i = 0;

    if(!g_seg_enable)
    {
        ENLED = 1;
        return;
    }

    ENLED = 0;
    ADDR3 = 1;

    if(i == 0)
    {
        ADDR2 = 0; ADDR1 = 0; ADDR0 = 0;
        BUS = LedBuff[0];
        i = 1;
    }
    else if(i == 1)
    {
        ADDR2 = 0; ADDR1 = 0; ADDR0 = 1;
        BUS = LedBuff[1];
        i = 2;
    }
    else if(i == 2)
    {
        ADDR2 = 0; ADDR1 = 1; ADDR0 = 0;
        BUS = LedBuff[2];
        i = 3;
    }
    else if(i == 3)
    {
        ADDR2 = 0; ADDR1 = 1; ADDR0 = 1;
        BUS = LedBuff[3];
        i = 4;
    }
    else if(i == 4)
    {
        ADDR2 = 1; ADDR1 = 0; ADDR0 = 0;
        BUS = LedBuff[4];
        i = 5;
    }
    else
    {
        ADDR2 = 1; ADDR1 = 0; ADDR0 = 1;
        BUS = LedBuff[5];
        i = 0;
    }
}

/*========================
  LCD bus arbitration
========================*/
void LcdBusTake(void)
{
    g_seg_enable = 0;
    ENLED = 1;
}

void LcdBusRelease(void)
{
    g_seg_enable = 1;
}

/*========================
  LCD1602
========================*/
void LcdWaitReady(void)
{
    u8 sta;

    LcdBusTake();

    BUS = 0xFF;
    LCD_RS = 0;
    LCD_RW = 1;
    do {
        LCD_E = 1;
        sta = BUS;
        LCD_E = 0;
    } while (sta & 0x80);

    LcdBusRelease();
}

void LcdWriteCmd(u8 cmd)
{
    LcdWaitReady();

    LcdBusTake();

    LCD_RS = 0;
    LCD_RW = 0;
    BUS = cmd;
    LCD_E = 1;
    LCD_E = 0;

    LcdBusRelease();
}

void LcdWriteDat(u8 dat)
{
    LcdWaitReady();

    LcdBusTake();

    LCD_RS = 1;
    LCD_RW = 0;
    BUS = dat;
    LCD_E = 1;
    LCD_E = 0;

    LcdBusRelease();
}

void LcdSetCursor(u8 x, u8 y)
{
    u8 addr;

    if(y == 0)
        addr = 0x00 + x;
    else
        addr = 0x40 + x;

    LcdWriteCmd(addr | 0x80);
}

void LcdShowStr(u8 x, u8 y, char *str)
{
    LcdSetCursor(x, y);
    while(*str != '\0')
    {
        LcdWriteDat(*str++);
    }
}

void InitLcd1602(void)
{
    delay_ms(20);
    LcdWriteCmd(0x38);
    LcdWriteCmd(0x0C);
    LcdWriteCmd(0x06);
    LcdWriteCmd(0x01);
    delay_ms(5);
}

/*========================
  Temperature display helper
========================*/
void ShowTempLine(int temp_c)
{
    char buf[16];

    buf[0] = 'T';
    buf[1] = ':';

    if(temp_c >= 100)
    {
        buf[2] = '0' + (temp_c / 100);
        buf[3] = '0' + ((temp_c / 10) % 10);
        buf[4] = '0' + (temp_c % 10);
        buf[5] = 'C';
        buf[6] = ' ';
        buf[7] = ' ';
        buf[8] = ' ';
        buf[9] = '\0';
    }
    else if(temp_c >= 10)
    {
        buf[2] = '0' + (temp_c / 10);
        buf[3] = '0' + (temp_c % 10);
        buf[4] = 'C';
        buf[5] = ' ';
        buf[6] = ' ';
        buf[7] = ' ';
        buf[8] = ' ';
        buf[9] = '\0';
    }
    else if(temp_c >= 0)
    {
        buf[2] = '0' + temp_c;
        buf[3] = 'C';
        buf[4] = ' ';
        buf[5] = ' ';
        buf[6] = ' ';
        buf[7] = ' ';
        buf[8] = ' ';
        buf[9] = '\0';
    }
    else
    {
        /* simple negative display */
        buf[2] = '-';
        buf[3] = '0' + (-temp_c);
        buf[4] = 'C';
        buf[5] = ' ';
        buf[6] = ' ';
        buf[7] = ' ';
        buf[8] = ' ';
        buf[9] = '\0';
    }

    LcdShowStr(0, 1, buf);
}

/*========================
  UI
========================*/
void UI_ShowActive(void)
{
    LcdWriteCmd(0x01);
    delay_ms(5);
    LcdShowStr(0, 0, "ACTIVE        ");
    ShowTempLine(g_temp_c);
    LcdSetCursor(12, 0);
    LcdWriteDat('0' + g_counter);
}

void UI_UpdateCounterOnly(void)
{
    LcdSetCursor(12, 0);
    LcdWriteDat('0' + g_counter);
}

void UI_UpdateTempOnly(void)
{
    ShowTempLine(g_temp_c);
}

void UI_ShowPowerSave(void)
{
    LcdWriteCmd(0x01);
    delay_ms(5);
    LcdShowStr(0, 0, "TIMEOUT > 5S  ");
    LcdShowStr(0, 1, "PWR SAVE      ");
}

void UI_ShowFault(void)
{
    LcdWriteCmd(0x01);
    delay_ms(5);
    LcdShowStr(0, 0, "TEMP FAULT    ");
    ShowTempLine(g_temp_c);
}

/*========================
  State transitions
========================*/
void EnterActive(void)
{
    g_state = STATE_ACTIVE;
    g_last_activity_ms = g_ms_tick;
    Seg_ShowOneDigit(g_counter);
    UI_ShowActive();
}

void EnterPowerSave(void)
{
    g_state = STATE_POWERSAVE;
    Seg_BlankAll();
    UI_ShowPowerSave();
}

void EnterFault(void)
{
    g_state = STATE_FAULT;
    Seg_BlankAll();
    UI_ShowFault();
}

/*========================
  Timer0
========================*/
void Timer0_Init(void)
{
    TMOD &= 0xF0;
    TMOD |= 0x01;

    TH0 = 0xFC;
    TL0 = 0x67;

    ET0 = 1;
    EA  = 1;
    TR0 = 1;
}

void Timer0_ISR(void) interrupt 1
{
    TH0 = 0xFC;
    TL0 = 0x67;
    g_ms_tick++;
    LedRefresh();
}

/*========================
  KEY4
========================*/
bit Key4_IsPressedRaw(void)
{
    P2 = 0xF7;
    return (KEY4 == 0) ? 1 : 0;
}

bit Key4_GetPressEvent(void)
{
    static bit stable_state = 1;
    static bit last_raw = 1;
    static u8 debounce_cnt = 0;
    bit raw_now;

    raw_now = Key4_IsPressedRaw() ? 0 : 1;

    if(raw_now == last_raw)
    {
        if(debounce_cnt < 3)
            debounce_cnt++;
    }
    else
    {
        debounce_cnt = 0;
        last_raw = raw_now;
    }

    if(debounce_cnt >= 3)
    {
        if(stable_state != raw_now)
        {
            stable_state = raw_now;
            if(stable_state == 0)
                return 1;
        }
    }

    return 0;
}

/*========================
  DS18B20
========================*/
void DelayX10us(u8 t)
{
    do {
        _nop_(); _nop_(); _nop_(); _nop_();
        _nop_(); _nop_(); _nop_(); _nop_();
    } while (--t);
}

bit Get18B20Ack(void)
{
    bit ack;

    EA = 0;
    IO_18B20 = 0;
    DelayX10us(50);
    IO_18B20 = 1;
    DelayX10us(6);
    ack = IO_18B20;
    while(!IO_18B20);
    EA = 1;

    return ack;
}

void Write18B20(u8 dat)
{
    u8 mask;

    EA = 0;
    for(mask = 0x01; mask != 0; mask <<= 1)
    {
        IO_18B20 = 0;
        _nop_();
        _nop_();
        if((mask & dat) == 0)
            IO_18B20 = 0;
        else
            IO_18B20 = 1;
        DelayX10us(6);
        IO_18B20 = 1;
    }
    EA = 1;
}

u8 Read18B20(void)
{
    u8 dat = 0;
    u8 mask;

    EA = 0;
    for(mask = 0x01; mask != 0; mask <<= 1)
    {
        IO_18B20 = 0;
        _nop_();
        _nop_();
        IO_18B20 = 1;
        _nop_();
        _nop_();

        if(!IO_18B20)
            dat &= ~mask;
        else
            dat |= mask;

        DelayX10us(6);
    }
    EA = 1;

    return dat;
}

bit Start18B20(void)
{
    bit ack;

    ack = Get18B20Ack();
    if(ack == 0)
    {
        Write18B20(0xCC);
        Write18B20(0x44);
    }
    return ~ack;
}

bit Get18B20Temp(int *temp)
{
    bit ack;
    u8 LSB, MSB;

    ack = Get18B20Ack();
    if(ack == 0)
    {
        Write18B20(0xCC);
        Write18B20(0xBE);
        LSB = Read18B20();
        MSB = Read18B20();
        *temp = ((int)MSB << 8) + LSB;
    }
    return ~ack;
}

/*========================
  Main task
========================*/
void Housekeeping_Task(void)
{
    static u32 last_key_scan_ms   = 0;
    static u32 last_temp_start_ms = 0;
    static u32 last_temp_read_ms  = 0;
    static u32 last_ui_temp_ms    = 0;
    bit key_event = 0;

    /* every 10ms scan key */
    if((g_ms_tick - last_key_scan_ms) >= 10)
    {
        last_key_scan_ms = g_ms_tick;
        key_event = Key4_GetPressEvent();
    }

    /* every 1000ms start a new DS18B20 conversion */
    if((g_ms_tick - last_temp_start_ms) >= 1000)
    {
        last_temp_start_ms = g_ms_tick;
        Start18B20();
    }

    /* read temperature after conversion time */
    if((g_ms_tick - last_temp_read_ms) >= 1200)
    {
        last_temp_read_ms = g_ms_tick;
        if(Get18B20Temp(&g_temp_raw))
        {
            g_temp_c = g_temp_raw / 16;
        }
    }

    switch(g_state)
    {
        case STATE_ACTIVE:

            if(g_temp_c >= TEMP_FAULT_TH)
            {
                EnterFault();
                break;
            }

            if(key_event)
            {
                g_counter++;
                if(g_counter >= 10)
                    g_counter = 0;

                g_last_activity_ms = g_ms_tick;
                Seg_ShowOneDigit(g_counter);
                UI_UpdateCounterOnly();
            }

            if((g_ms_tick - last_ui_temp_ms) >= 500)
            {
                last_ui_temp_ms = g_ms_tick;
                UI_UpdateTempOnly();
            }

            if((g_ms_tick - g_last_activity_ms) >= 5000)
            {
                EnterPowerSave();
            }
            break;

        case STATE_POWERSAVE:

            if(g_temp_c >= TEMP_FAULT_TH)
            {
                EnterFault();
                break;
            }

            if(key_event)
            {
                g_counter++;
                if(g_counter >= 10)
                    g_counter = 0;

                EnterActive();
            }
            break;

        case STATE_FAULT:

            /* only KEY4 can clear fault */
            if(key_event)
            {
                g_last_activity_ms = g_ms_tick;
                EnterActive();
            }
            break;

        default:
            EnterActive();
            break;
    }
}

/*========================
  main
========================*/
void main(void)
{
    ENLED = 1;
    Seg_BlankAll();

    InitLcd1602();
    Timer0_Init();

    g_counter = 0;
    g_temp_c = 0;

    Start18B20();   /* first conversion */
    EnterActive();

    while(1)
    {
        Housekeeping_Task();
    }
}