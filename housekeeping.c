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

/* PMBus / Load pins */
sbit LOAD_DET  = P3^3;
sbit PMBUS_SCL = P3^4;
sbit PMBUS_SDA = P3^5;

typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

typedef enum
{
    STATE_ACTIVE = 0,
    STATE_POWERSAVE,
    STATE_FAULT
} SystemState_t;

/* PMBus command defines */
#define PMBUS_ADDR_7B           0x5A
#define PMBUS_CMD_CLEAR_FAULTS  0x03
#define PMBUS_CMD_STATUS_WORD   0x79

/*========================
  Global variables
========================*/
/*========================
  Global variables

  g_ms_tick:
    由 Timer0 每 1ms 遞增一次，作為整個系統的時間基準。
    Housekeeping 任務中的 key scan、溫度採樣、PMBus polling、
    UI 更新與 powersave timeout 都以此為基礎。

  g_seg_enable:
    因 LCD 與七段顯示器共用部分匯流排，
    LCD 存取期間暫時關閉 7-seg refresh，避免 bus contention。

  g_state:
    系統主狀態機，分為 ACTIVE / POWERSAVE / FAULT。

  g_counter:
    使用者透過 KEY4 觸發的示範計數值，
    同步顯示於 LCD 與七段顯示器。

  g_last_activity_ms:
    紀錄最近一次系統活動時間。
    活動來源包含按鍵事件與外部負載存在(load_present)。
    若超過 5 秒無活動，系統進入 POWERSAVE。

  g_temp_raw / g_temp_c:
    DS18B20 原始值與簡化後攝氏溫度值。

  g_pmbus_fault:
    由 PMBus STATUS_WORD 輪詢結果轉換而來的 fault flag。
    目前採簡化策略：只要 STATUS_WORD 非 0，即視為異常。
========================*/

volatile u32 g_ms_tick = 0;
volatile bit g_seg_enable = 1;

SystemState_t g_state = STATE_ACTIVE;

u8  g_counter = 0;
u32 g_last_activity_ms = 0;

int g_temp_raw = 0;
int g_temp_c = 0;

volatile bit g_pmbus_fault = 0;

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
    char buf[10];

    buf[0] = 'T';
    buf[1] = ':';

    if(temp_c >= 10)
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
    LcdShowStr(0, 0, "SYSTEM FAULT  ");
    ShowTempLine(g_temp_c);
}

/*========================
  State transitions
========================*/

/*========================
  State transitions

  EnterActive():
    進入正常運作狀態。
    - 重設 activity timeout 基準
    - 開啟單位數七段顯示
    - 更新 LCD 為 ACTIVE 畫面

  EnterPowerSave():
    進入低活動/節能狀態。
    - 關閉七段顯示內容
    - LCD 顯示 timeout / power save 訊息

  EnterFault():
    進入 fault latch 狀態。
    - 關閉七段顯示
    - LCD 顯示 fault 狀態
    - 後續需由使用者按 KEY4 執行 recovery
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

  Timer0 被設定為固定週期中斷來源，用來建立 1ms system tick。
  本專案沒有使用硬體 watchdog，
  但採用 timer-driven housekeeping 架構，
  透過固定時間基準週期性執行：
  - key scan
  - temperature conversion / readback
  - PMBus status polling
  - UI refresh
  - inactivity timeout supervision

  這種做法屬於 supervisor / watchdog-like 的韌體設計思維。
========================*/

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

  KEY4 為使用者輸入事件來源。
  這裡不是直接讀 raw pin 後立即採信，
  而是做簡單 debounce：
  - 每 10ms 掃描一次
  - 連續數次一致才更新 stable state
  - 僅在穩定按下瞬間回傳一次 press event

  這樣可避免機械按鍵 bouncing 造成誤觸發。
========================*/

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

  溫度感測在本專案中扮演本地端保護事件來源。
  流程分為兩步：
  1. 週期性送 Convert T 指令啟動新一輪溫度轉換
  2. 延遲足夠時間後再回讀 scratchpad 取得結果

  Housekeeping task 會將回讀結果轉成簡化的攝氏值 g_temp_c，
  若超過 TEMP_FAULT_TH，系統立即進入 FAULT 狀態。
========================*/

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

bit Get18B20Temp(int *temp_ptr)
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
        *temp_ptr = ((int)MSB << 8) + LSB;
    }
    return ~ack;
}

/*========================
  PMBus bit-bang basic

  這一區以軟體 bit-bang 方式在 P3.4 / P3.5 實作
  基本 PMBus/SMBus 風格 transaction。

  目前保留最小可展示版本：
  - Start / Stop
  - Write byte
  - Read byte
  - Read word
  - Send command

  本版專案中，PMBus 的角色不是完整數位電源控制器，
  而是作為外部 power device 狀態來源與 recovery command 通道。
========================*/

/*========================
  PMBus bit-bang basic
========================*/
void PMBus_Delay(void)
{
    _nop_(); _nop_(); _nop_(); _nop_();
    _nop_(); _nop_(); _nop_(); _nop_();
}

void PMBus_Start(void)
{
    PMBUS_SDA = 1;
    PMBUS_SCL = 1;
    PMBus_Delay();
    PMBUS_SDA = 0;
    PMBus_Delay();
    PMBUS_SCL = 0;
}

void PMBus_Stop(void)
{
    PMBUS_SDA = 0;
    PMBus_Delay();
    PMBUS_SCL = 1;
    PMBus_Delay();
    PMBUS_SDA = 1;
    PMBus_Delay();
}

bit PMBus_WriteByteRaw(u8 tx_byte)
{
    u8 i;
    bit ack_ok;

    for(i = 0; i < 8; i++)
    {
        PMBUS_SDA = (tx_byte & 0x80) ? 1 : 0;
        PMBus_Delay();
        PMBUS_SCL = 1;
        PMBus_Delay();
        PMBUS_SCL = 0;
        tx_byte <<= 1;
    }

    PMBUS_SDA = 1;
    PMBus_Delay();
    PMBUS_SCL = 1;
    PMBus_Delay();
    ack_ok = (PMBUS_SDA == 0) ? 1 : 0;
    PMBUS_SCL = 0;

    return ack_ok;
}

u8 PMBus_ReadByteRaw(bit ack_after_read)
{
    u8 i;
    u8 rx_byte = 0;

    PMBUS_SDA = 1;

    for(i = 0; i < 8; i++)
    {
        rx_byte <<= 1;
        PMBUS_SCL = 1;
        PMBus_Delay();

        if(PMBUS_SDA)
            rx_byte |= 0x01;

        PMBUS_SCL = 0;
        PMBus_Delay();
    }

    PMBUS_SDA = ack_after_read ? 0 : 1;
    PMBus_Delay();
    PMBUS_SCL = 1;
    PMBus_Delay();
    PMBUS_SCL = 0;
    PMBUS_SDA = 1;

    return rx_byte;
}

bit PMBus_SendCommand(u8 slave7, u8 cmd)
{
    bit ok;

    PMBus_Start();
    ok = PMBus_WriteByteRaw((slave7 << 1) | 0);
    if(!ok)
    {
        PMBus_Stop();
        return 0;
    }

    ok = PMBus_WriteByteRaw(cmd);
    PMBus_Stop();

    return ok;
}

bit PMBus_ReadWord(u8 slave7, u8 cmd, u16 *out_word)
{
    bit ok;
    u8 lo_byte, hi_byte;

    PMBus_Start();
    ok = PMBus_WriteByteRaw((slave7 << 1) | 0);
    if(!ok)
    {
        PMBus_Stop();
        return 0;
    }

    ok = PMBus_WriteByteRaw(cmd);
    if(!ok)
    {
        PMBus_Stop();
        return 0;
    }

    PMBus_Start();
    ok = PMBus_WriteByteRaw((slave7 << 1) | 1);
    if(!ok)
    {
        PMBus_Stop();
        return 0;
    }

    lo_byte = PMBus_ReadByteRaw(1);
    hi_byte = PMBus_ReadByteRaw(0);
    PMBus_Stop();

    *out_word = ((u16)hi_byte << 8) | lo_byte;
    return 1;
}

/*========================
  PMBus_Task()

  以固定週期輪詢外部 PMBus device 的 STATUS_WORD。
  目前採用 200ms polling interval，避免主迴圈中每次都做通訊。

  設計意圖：
  - 將外部 power device 狀態納入本地 housekeeping 決策
  - 讓 fault 不只來自溫度，也可能來自 PMBus status

  簡化策略：
  - 讀取成功且 STATUS_WORD != 0 -> 視為 PMBus fault
  - 讀取失敗 -> 暫時不拉高 fault flag

  後續若要擴充，可再加入：
  - STATUS_WORD bit decode
  - READ_VOUT / READ_IOUT telemetry
  - PEC / clock stretching
========================*/

void PMBus_Task(void)
{
    static u32 last_poll_ms = 0;
    u16 tmp_word;

    if((g_ms_tick - last_poll_ms) < 200)
        return;

    last_poll_ms = g_ms_tick;

    if(PMBus_ReadWord(PMBUS_ADDR_7B, PMBUS_CMD_STATUS_WORD, &tmp_word))
        g_pmbus_fault = (tmp_word != 0x0000) ? 1 : 0;
    else
        g_pmbus_fault = 0;
}

/*========================
  Housekeeping_Task()

  本專案的核心 supervisor task。
  這裡集中執行所有週期性背景工作，並根據狀態機做決策。

  主要職責：
  1. 每 10ms 掃描按鍵事件
  2. 每 1000ms 啟動一次溫度轉換
  3. 於足夠轉換時間後回讀溫度
  4. 讀取 LOAD_DET，判斷外部是否存在活動負載
  5. 呼叫 PMBus_Task() 取得外部 power device fault 狀態
  6. 根據 ACTIVE / POWERSAVE / FAULT 狀態執行對應行為

  ACTIVE:
    - 接受按鍵事件
    - 更新 activity timer
    - 溫度或 PMBus fault 進入 FAULT
    - 5 秒無活動進入 POWERSAVE

  POWERSAVE:
    - 保持低活動顯示模式
    - 偵測到 load 或按鍵即回 ACTIVE
    - 若保護條件異常仍會進入 FAULT

  FAULT:
    - 系統進入 latch 狀態
    - 由 KEY4 觸發 recovery
    - recovery 時送出 PMBus CLEAR_FAULTS command
========================*/

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
    bit load_present = 0;

    if((g_ms_tick - last_key_scan_ms) >= 10)
    {
        last_key_scan_ms = g_ms_tick;
        key_event = Key4_GetPressEvent();
    }

    if((g_ms_tick - last_temp_start_ms) >= 1000)
    {
        last_temp_start_ms = g_ms_tick;
        Start18B20();
    }

    if((g_ms_tick - last_temp_read_ms) >= 1200)
    {
        last_temp_read_ms = g_ms_tick;
        if(Get18B20Temp(&g_temp_raw))
            g_temp_c = g_temp_raw / 16;
    }

    load_present = (LOAD_DET == 0) ? 1 : 0;
    PMBus_Task();

    switch(g_state)
    {
        case STATE_ACTIVE:

            if((g_temp_c >= TEMP_FAULT_TH) || g_pmbus_fault)
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

            if(load_present)
                g_last_activity_ms = g_ms_tick;

            if((g_ms_tick - last_ui_temp_ms) >= 500)
            {
                last_ui_temp_ms = g_ms_tick;
                UI_UpdateTempOnly();
            }

            if((g_ms_tick - g_last_activity_ms) >= 5000)
                EnterPowerSave();
            break;

        case STATE_POWERSAVE:

            if((g_temp_c >= TEMP_FAULT_TH) || g_pmbus_fault)
            {
                EnterFault();
                break;
            }

            if(load_present || key_event)
            {
                if(key_event)
                {
                    g_counter++;
                    if(g_counter >= 10)
                        g_counter = 0;
                }
                EnterActive();
            }
            break;

        case STATE_FAULT:

            if(key_event)
            {
                PMBus_SendCommand(PMBUS_ADDR_7B, PMBUS_CMD_CLEAR_FAULTS);
                g_pmbus_fault = 0;
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
  main()

  初始化順序：
  1. 關閉/清空顯示
  2. 初始化 LCD
  3. 啟動 Timer0 形成 system tick
  4. 將 PMBus bus 釋放為 idle high
  5. 啟動第一次溫度轉換
  6. 進入 ACTIVE 初始狀態
  7. 在 while(1) 中持續執行 Housekeeping_Task()

  整體結構屬於典型 super loop + timer tick 的 8051 韌體設計。
========================*/

/*========================
  main
========================*/
void main(void)
{
    ENLED = 1;
    Seg_BlankAll();

    InitLcd1602();
    Timer0_Init();

    PMBUS_SDA = 1;
    PMBUS_SCL = 1;

    g_counter = 0;
    g_temp_c = 0;

    Start18B20();
    EnterActive();

    while(1)
    {
        Housekeeping_Task();
    }
}