#include <reg52.h>

/*==================================================
  KST-51 board mapping
==================================================*/
#define BUS P0

sbit ADDR0 = P1^0;
sbit ADDR1 = P1^1;
sbit ADDR2 = P1^2;
sbit ADDR3 = P1^3;
sbit ENLED = P1^4;
sbit LCD_E = P1^5;

/* LCD1602 on this board:
   RS -> P1.0
   RW -> P1.1
   E  -> P1.5
   DB -> P0
*/
sbit LCD_RS = P1^0;
sbit LCD_RW = P1^1;

/* one key only */
sbit KEY4 = P2^7;

/*==================================================
  basic types
==================================================*/
typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

/*==================================================
  system state
==================================================*/
typedef enum
{
    STATE_ACTIVE = 0,
    STATE_POWERSAVE
} SystemState_t;

/*==================================================
  global variables
==================================================*/
volatile u32 g_ms_tick = 0;
volatile bit g_seg_enable = 1;   /* 1: allow 7seg refresh, 0: pause refresh */

SystemState_t g_state = STATE_ACTIVE;

u8  g_counter = 0;               /* 0~9 */
u32 g_last_activity_ms = 0;

/* 7-seg code, based on your provided example */
u8 code LedChar[10] = {
    0xC0, 0xF9, 0xA4, 0xB0, 0x99,
    0x92, 0x82, 0xF8, 0x80, 0x90
};

/* 6-digit display buffer; we only use ONE digit */
u8 LedBuff[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/*==================================================
  delay
==================================================*/
void delay_ms(u16 ms)
{
    u16 i, j;
    for(i = 0; i < ms; i++)
        for(j = 0; j < 120; j++);
}

/*==================================================
  7-seg helper
  Only ONE digit is used.
  We put data on LedBuff[0], other digits are blank.
==================================================*/
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

/*==================================================
  7-seg refresh
  Must be called periodically (here: every 1ms in timer ISR)
==================================================*/
void LedRefresh(void)
{
    static u8 i = 0;

    if(!g_seg_enable)
    {
        ENLED = 1;      /* disable LED path while LCD uses the bus */
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

/*==================================================
  LCD bus lock/unlock
  LCD and 7-seg share P0. Pause seg refresh during LCD access.
==================================================*/
void LcdBusTake(void)
{
    g_seg_enable = 0;
    ENLED = 1;     /* disable LED display path */
}

void LcdBusRelease(void)
{
    g_seg_enable = 1;
}

/*==================================================
  LCD1602 functions
==================================================*/
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

void LcdClearLine(u8 y)
{
    u8 i;
    LcdSetCursor(0, y);
    for(i = 0; i < 16; i++)
    {
        LcdWriteDat(' ');
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

/*==================================================
  UI functions
==================================================*/
void UI_ShowActive(void)
{
    LcdWriteCmd(0x01);
    delay_ms(5);

    LcdShowStr(0, 0, "ACTIVE        ");
    LcdShowStr(0, 1, "KEY4:+1       ");

    LcdSetCursor(12, 0);
    LcdWriteDat('0' + g_counter);
}

void UI_UpdateCounterOnly(void)
{
    LcdSetCursor(12, 0);
    LcdWriteDat('0' + g_counter);
}

void UI_ShowPowerSave(void)
{
    LcdWriteCmd(0x01);
    delay_ms(5);

    LcdShowStr(0, 0, "TIMEOUT > 5S  ");
    LcdShowStr(0, 1, "PWR SAVE      ");
}

/*==================================================
  state transitions
==================================================*/
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

/*==================================================
  Timer0: 1ms tick
==================================================*/
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

/*==================================================
  KEY4 scan
  From your sample:
    P2 = 0xF7;
    read P2.7
==================================================*/
bit Key4_IsPressedRaw(void)
{
    P2 = 0xF7;
    return (KEY4 == 0) ? 1 : 0;
}

/* return 1 only once per press */
bit Key4_GetPressEvent(void)
{
    static bit stable_state = 1;      /* 1=released, 0=pressed */
    static bit last_raw = 1;
    static u8 debounce_cnt = 0;

    bit raw_now;

    raw_now = Key4_IsPressedRaw() ? 0 : 1;  /* convert to released/pressed style:
                                               raw pressed -> 0, released -> 1 */

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
            {
                return 1;   /* press event */
            }
        }
    }

    return 0;
}

/*==================================================
  Housekeeping main task
==================================================*/
void Housekeeping_Task(void)
{
    static u32 last_key_scan_ms = 0;

    bit key_event = 0;

    /* scan key every 10ms */
    if((g_ms_tick - last_key_scan_ms) >= 10)
    {
        last_key_scan_ms = g_ms_tick;
        key_event = Key4_GetPressEvent();
    }

    switch(g_state)
    {
        case STATE_ACTIVE:
            if(key_event)
            {
                g_counter++;
                if(g_counter >= 10)
                    g_counter = 0;

                g_last_activity_ms = g_ms_tick;
                Seg_ShowOneDigit(g_counter);
                UI_UpdateCounterOnly();
            }

            if((g_ms_tick - g_last_activity_ms) >= 5000)
            {
                EnterPowerSave();
            }
            break;

        case STATE_POWERSAVE:
            if(key_event)
            {
                g_counter++;
                if(g_counter >= 10)
                    g_counter = 0;

                EnterActive();
            }
            break;

        default:
            EnterActive();
            break;
    }
}

/*==================================================
  main
==================================================*/
void main(void)
{
    ENLED = 1;
    Seg_BlankAll();

    InitLcd1602();
    Timer0_Init();

    g_counter = 0;
    EnterActive();

    while(1)
    {
        Housekeeping_Task();
    }
}