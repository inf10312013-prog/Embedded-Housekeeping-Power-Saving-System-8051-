#include <reg52.h>

/*====================
  HARDWARE
====================*/
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

/*====================
  TYPE
====================*/
typedef unsigned char u8;
typedef unsigned int  u16;
typedef unsigned long u32;

/*====================
  GLOBAL
====================*/
u32 tick = 0;
u8 counter = 0;
u32 last_activity = 0;

bit seg_enable = 1;

/*====================
  STATE
====================*/
typedef enum {
    ACTIVE,
    POWERSAVE,
    FAULT
} STATE;

STATE state = ACTIVE;

/*====================
  7SEG
====================*/
u8 code seg_table[10] = {
0xC0,0xF9,0xA4,0xB0,0x99,
0x92,0x82,0xF8,0x80,0x90
};

u8 seg_buf[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

void SegShow(u8 n){ seg_buf[0] = seg_table[n]; }
void SegOff(){ seg_buf[0] = 0xFF; }

/*====================
  SEG REFRESH
====================*/
void SegRefresh()
{
    static u8 i=0;

    if(!seg_enable){ ENLED=1; return; }

    ENLED=0; ADDR3=1;

    if(i==0){ADDR2=0;ADDR1=0;ADDR0=0;i++;BUS=seg_buf[0];}
    else if(i==1){ADDR2=0;ADDR1=0;ADDR0=1;i++;BUS=seg_buf[1];}
    else if(i==2){ADDR2=0;ADDR1=1;ADDR0=0;i++;BUS=seg_buf[2];}
    else if(i==3){ADDR2=0;ADDR1=1;ADDR0=1;i++;BUS=seg_buf[3];}
    else if(i==4){ADDR2=1;ADDR1=0;ADDR0=0;i++;BUS=seg_buf[4];}
    else {ADDR2=1;ADDR1=0;ADDR0=1;i=0;BUS=seg_buf[5];}
}

/*====================
  TIMER
====================*/
void Timer0_ISR() interrupt 1
{
    TH0=0xFC; TL0=0x67;
    tick++;
    SegRefresh();
}

void TimerInit()
{
    TMOD=0x01;
    TH0=0xFC; TL0=0x67;
    ET0=1; EA=1; TR0=1;
}

/*====================
  KEY
====================*/
bit KeyPressed()
{
    P2=0xF7;
    return (KEY4==0);
}

/*====================
  I2C (簡化bit-bang)
====================*/
sbit SDA = P2^1;
sbit SCL = P2^0;

void I2CStart(){ SDA=1;SCL=1;SDA=0;SCL=0; }
void I2CStop(){ SDA=0;SCL=1;SDA=1; }

bit I2CWrite(u8 dat)
{
    u8 i;
    for(i=0;i<8;i++){
        SDA = (dat&0x80);
        SCL=1;SCL=0;
        dat<<=1;
    }
    SDA=1; SCL=1;
    i = SDA;
    SCL=0;
    return !i;
}

u8 I2CRead()
{
    u8 i,dat=0;
    SDA=1;
    for(i=0;i<8;i++){
        SCL=1;
        dat=(dat<<1)|SDA;
        SCL=0;
    }
    return dat;
}

u8 GetADC(u8 ch)
{
    u8 val;

    I2CStart();
    I2CWrite(0x90);
    I2CWrite(0x40|ch);
    I2CStart();
    I2CWrite(0x91);
    I2CRead();
    val = I2CRead();
    I2CStop();

    return val;
}

/*====================
  LCD（最簡版）
====================*/
void LcdCmd(u8 c)
{
    seg_enable=0; ENLED=1;
    LCD_RS=0; LCD_RW=0;
    BUS=c; LCD_E=1; LCD_E=0;
    seg_enable=1;
}

void LcdDat(u8 d)
{
    seg_enable=0; ENLED=1;
    LCD_RS=1; LCD_RW=0;
    BUS=d; LCD_E=1; LCD_E=0;
    seg_enable=1;
}

void LcdStr(u8 x,u8 y,char *s)
{
    LcdCmd(0x80 + (y?0x40:0)+x);
    while(*s) LcdDat(*s++);
}

void LcdInit()
{
    LcdCmd(0x38);
    LcdCmd(0x0C);
    LcdCmd(0x06);
    LcdCmd(0x01);
}

/*====================
  MAIN
====================*/
void main()
{
    u8 adc;

    TimerInit();
    LcdInit();

    SegShow(0);
    last_activity = tick;

    while(1)
    {
        /* KEY */
        if(KeyPressed())
        {
            counter=(counter+1)%10;
            SegShow(counter);
            last_activity = tick;

            if(state!=ACTIVE)
                state=ACTIVE;
        }

        /* TEMP (ADC) */
        adc = GetADC(0);

        if(adc > 180)   // 溫度過高
            state = FAULT;

        /* STATE */
        if(state==ACTIVE)
        {
            if(tick-last_activity>5000)
                state=POWERSAVE;

            LcdStr(0,0,"ACTIVE       ");
        }
        else if(state==POWERSAVE)
        {
            SegOff();
            LcdStr(0,0,"PWR SAVE     ");
        }
        else if(state==FAULT)
        {
            SegOff();
            LcdStr(0,0,"TEMP FAULT   ");
        }
    }
}