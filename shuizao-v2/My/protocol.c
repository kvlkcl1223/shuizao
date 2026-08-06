#include "protocol.h"
#include <stdlib.h>
#include <string.h>

/*
 * protocol.c
 * USART3 协议解析实现。
 * 中断层只收集字节，命令解析和入队在主循环中完成，避免中断里执行业务逻辑。
 */

#define PROTOCOL_UART                  (&huart3)
#define PROTOCOL_RX_DMA_SIZE           128U
#define PROTOCOL_FRAME_SIZE            96U
#define PROTOCOL_COMMAND_QUEUE_SIZE    6U

/* DMA 原始接收缓冲区，由 USART3 IDLE 中断判断本次有效长度。 */
static uint8_t rx_dma_buffer[PROTOCOL_RX_DMA_SIZE];

/* 中断与主循环之间的一帧数据交接区。业务解析只在主循环中执行。 */
static volatile uint8_t rx_pending;
static volatile uint16_t rx_pending_len;
static volatile uint8_t rx_pending_buffer[PROTOCOL_RX_DMA_SIZE];
static volatile uint8_t rx_overrun;

/* ASCII 帧缓存。帧格式为 #命令,参数;。 */
static char frame_buffer[PROTOCOL_FRAME_SIZE];
static uint8_t frame_len;
static bool frame_active;

/* 简单环形命令队列，避免在中断中直接运行应用逻辑。 */
static Protocol_Command command_queue[PROTOCOL_COMMAND_QUEUE_SIZE];
static uint8_t queue_head;
static uint8_t queue_tail;

static bool StringEquals(const char *left, const char *right)
{
    return strcmp(left, right) == 0;
}

static bool QueuePush(const Protocol_Command *command)
{
    uint8_t next_head = (uint8_t)((queue_head + 1U) % PROTOCOL_COMMAND_QUEUE_SIZE);

    /* 队列满时丢弃新命令。屏幕端一般不会连续高速发送控制帧。 */
    if (next_head == queue_tail) {
        return false;
    }

    command_queue[queue_head] = *command;
    queue_head = next_head;
    return true;
}

bool Protocol_PopCommand(Protocol_Command *command)
{
    if (command == 0 || queue_head == queue_tail) {
        return false;
    }

    *command = command_queue[queue_tail];
    queue_tail = (uint8_t)((queue_tail + 1U) % PROTOCOL_COMMAND_QUEUE_SIZE);
    return true;
}

static uint8_t ParsePercent(const char *text, uint8_t default_value)
{
    int value;

    if (text == 0) {
        return default_value;
    }

    /* 这里只做字符串到 uint8_t 的安全转换，范围夹紧交给泵控制层。 */
    value = atoi(text);
    if (value < 0) {
        return 0U;
    }
    if (value > 255) {
        return 255U;
    }

    return (uint8_t)value;
}

static void ParseManual(char **tokens, uint8_t token_count)
{
    Protocol_Command command;

    /* 手动命令格式：#MAN,Z,UP,50; 或 #MAN,PUMP,IN,60;。 */
    if (token_count < 3U) {
        return;
    }

    memset(&command, 0, sizeof(command));
    command.type = PROTOCOL_CMD_MANUAL;
    command.speed_percent = 0U;

    if (StringEquals(tokens[1], "Z")) {
        command.manual_target = PROTOCOL_MANUAL_TARGET_Z;

        if (StringEquals(tokens[2], "UP")) {
            command.manual_action = PROTOCOL_MANUAL_ACTION_UP;
        } else if (StringEquals(tokens[2], "DOWN")) {
            command.manual_action = PROTOCOL_MANUAL_ACTION_DOWN;
        } else if (StringEquals(tokens[2], "STOP")) {
            command.manual_action = PROTOCOL_MANUAL_ACTION_STOP;
        } else {
            return;
        }
    } else if (StringEquals(tokens[1], "PUMP")) {
        command.manual_target = PROTOCOL_MANUAL_TARGET_PUMP;

        if (StringEquals(tokens[2], "IN")) {
            command.manual_action = PROTOCOL_MANUAL_ACTION_IN;
        } else if (StringEquals(tokens[2], "OUT")) {
            command.manual_action = PROTOCOL_MANUAL_ACTION_OUT;
        } else if (StringEquals(tokens[2], "STOP")) {
            command.manual_action = PROTOCOL_MANUAL_ACTION_STOP;
        } else {
            return;
        }
    } else {
        return;
    }

    if (token_count >= 4U) {
        command.speed_percent = ParsePercent(tokens[3], 0U);
    }

    QueuePush(&command);
}

static void ParseFrame(char *frame)
{
    char *tokens[8];
    uint8_t token_count = 0;
    char *token;
    Protocol_Command command;

    /* strtok 会原地切分 frame_buffer，因此 frame 必须是可写缓冲区。 */
    token = strtok(frame, ",");
    while (token != 0 && token_count < (sizeof(tokens) / sizeof(tokens[0]))) {
        tokens[token_count++] = token;
        token = strtok(0, ",");
    }

    if (token_count == 0U) {
        return;
    }

    memset(&command, 0, sizeof(command));

    if (StringEquals(tokens[0], "START")) {
        /* #START,50,0; 或 #START,100,1; */
        if (token_count < 2U) {
            return;
        }
        command.type = PROTOCOL_CMD_START;
        command.volume_ml = (uint16_t)atoi(tokens[1]);
        command.keep10 = 0U;
        if (token_count >= 3U) {
            command.keep10 = ParsePercent(tokens[2], 0U) ? 1U : 0U;
        }
        QueuePush(&command);
    } else if (StringEquals(tokens[0], "STOP")) {
        command.type = PROTOCOL_CMD_STOP;
        QueuePush(&command);
    } else if (StringEquals(tokens[0], "ESTOP")) {
        command.type = PROTOCOL_CMD_ESTOP;
        QueuePush(&command);
    } else if (StringEquals(tokens[0], "HOME")) {
        command.type = PROTOCOL_CMD_HOME;
        QueuePush(&command);
    } else if (StringEquals(tokens[0], "OK")) {
        command.type = PROTOCOL_CMD_OK;
        QueuePush(&command);
    } else if (StringEquals(tokens[0], "SPD")) {
        /* #SPD,75; 设置全部蠕动泵速度百分比。 */
        if (token_count < 2U) {
            return;
        }
        command.type = PROTOCOL_CMD_SPEED_SET;
        command.speed_percent = ParsePercent(tokens[1], 0U);
        QueuePush(&command);
    } else if (StringEquals(tokens[0], "MAN")) {
        /* 手动控制命令继续交给 ParseManual 解析。 */
        ParseManual(tokens, token_count);
    } else if (StringEquals(tokens[0], "GET")) {
        if (token_count < 2U) {
            return;
        }
        if (StringEquals(tokens[1], "PG")) {
            command.type = PROTOCOL_CMD_GET_PG;
        } else if (StringEquals(tokens[1], "STATE")) {
            command.type = PROTOCOL_CMD_GET_STATE;
        } else {
            return;
        }
        QueuePush(&command);
    } else if (StringEquals(tokens[0], "RESET")) {
        command.type = PROTOCOL_CMD_RESET;
        QueuePush(&command);
    }
}

static void FeedByte(uint8_t byte)
{
    /* 找到帧头后开始收集，直到帧尾 ';' 再解析。 */
    if (byte == '#') {
        frame_active = true;
        frame_len = 0U;
        return;
    }

    if (!frame_active) {
        return;
    }

    if (byte == ';') {
        frame_buffer[frame_len] = '\0';
        ParseFrame(frame_buffer);
        frame_active = false;
        frame_len = 0U;
        return;
    }

    /* 帧过长时丢弃，防止缓冲区溢出。 */
    if (frame_len < (PROTOCOL_FRAME_SIZE - 1U)) {
        frame_buffer[frame_len++] = (char)byte;
    } else {
        frame_active = false;
        frame_len = 0U;
    }
}

void Protocol_Init(void)
{
    rx_pending = 0U;
    rx_pending_len = 0U;
    rx_overrun = 0U;
    frame_active = false;
    frame_len = 0U;
    queue_head = 0U;
    queue_tail = 0U;

    /* 启动 USART3 DMA 接收，并打开 IDLE 中断识别变长帧。 */
    __HAL_UART_ENABLE_IT(PROTOCOL_UART, UART_IT_IDLE);
    HAL_UART_Receive_DMA(PROTOCOL_UART, rx_dma_buffer, PROTOCOL_RX_DMA_SIZE);
}

void Protocol_UART_IRQHandler(UART_HandleTypeDef *huart)
{
    uint16_t len;
    uint16_t remaining;

    /* 只处理 USART3 的 IDLE 中断，USART2 保留给调试或备用。 */
    if (huart == 0 || huart->Instance != USART3) {
        return;
    }

    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE) != RESET) {
        __HAL_UART_CLEAR_IDLEFLAG(huart);

        /* 通过 DMA 剩余计数计算本次收到的字节数。 */
        remaining = __HAL_DMA_GET_COUNTER(huart->hdmarx);
        len = (uint16_t)(PROTOCOL_RX_DMA_SIZE - remaining);

        HAL_UART_DMAStop(huart);

        if (len > 0U) {
            /* 如果上一包还没被主循环处理，则记录溢出标志并丢弃新包。 */
            if (rx_pending == 0U) {
                if (len > PROTOCOL_RX_DMA_SIZE) {
                    len = PROTOCOL_RX_DMA_SIZE;
                }
                memcpy((void *)rx_pending_buffer, rx_dma_buffer, len);
                rx_pending_len = len;
                rx_pending = 1U;
            } else {
                rx_overrun = 1U;
            }
        }

        HAL_UART_Receive_DMA(huart, rx_dma_buffer, PROTOCOL_RX_DMA_SIZE);
    }
}

void Protocol_Process(void)
{
    uint8_t local_buffer[PROTOCOL_RX_DMA_SIZE];
    uint16_t len = 0U;

    if (rx_pending == 0U) {
        return;
    }

    /* 将中断缓冲复制到局部变量后再解析，缩短关中断时间。 */
    __disable_irq();
    if (rx_pending != 0U) {
        len = rx_pending_len;
        if (len > PROTOCOL_RX_DMA_SIZE) {
            len = PROTOCOL_RX_DMA_SIZE;
        }
        memcpy(local_buffer, (const void *)rx_pending_buffer, len);
        rx_pending = 0U;
        rx_pending_len = 0U;
    }
    __enable_irq();

    /* 逐字节喂给帧状态机，支持一包里包含多条 #...; 命令。 */
    for (uint16_t i = 0; i < len; i++) {
        FeedByte(local_buffer[i]);
    }

    (void)rx_overrun;
}
