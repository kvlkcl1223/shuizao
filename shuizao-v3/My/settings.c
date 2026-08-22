#include "settings.h"
#include "stm32f1xx_hal.h"
#include <string.h>

/*
 * settings.c
 * 片内 Flash 参数保存实现。
 * STM32F103RC Flash 页大小为 2KB，本模块使用最后一页保存少量运行参数。
 */

#define SETTINGS_MAGIC          0x535A5633UL /* "SZV3" */
#define SETTINGS_VERSION        4U
#define SETTINGS_VERSION_V3     3U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t reserved;
    uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT];
    uint32_t zvirt_ms[APP_ZVIRT_COUNT];
    uint32_t checksum;
} Settings_RecordV3;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t reserved;
    uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT];
    uint32_t zvirt_ms[APP_ZVIRT_COUNT];
    uint32_t pump_speed_percent;
    uint32_t checksum;
} Settings_Record;

static uint32_t Settings_ChecksumWords(const uint32_t *words, uint32_t word_count)
{
    uint32_t checksum = 0xA5A55A5AUL;

    /* 校验范围不包含最后的 checksum 字段。 */
    for (uint32_t i = 0U; i < word_count; i++) {
        checksum ^= words[i] + 0x9E3779B9UL + (checksum << 6) + (checksum >> 2);
    }

    return checksum;
}

static uint32_t Settings_Checksum(const Settings_Record *record)
{
    return Settings_ChecksumWords((const uint32_t *)record,
                                  (uint32_t)(sizeof(Settings_Record) / sizeof(uint32_t) - 1U));
}

static uint32_t Settings_ChecksumV3(const Settings_RecordV3 *record)
{
    return Settings_ChecksumWords((const uint32_t *)record,
                                  (uint32_t)(sizeof(Settings_RecordV3) / sizeof(uint32_t) - 1U));
}

static bool Settings_IsTimeValid(uint32_t value)
{
#if APP_TIME_MIN_MS > 0U
    return value >= APP_TIME_MIN_MS && value <= APP_TIME_MAX_MS;
#else
    return value <= APP_TIME_MAX_MS;
#endif
}

static bool Settings_IsZVirtTimeValid(uint32_t value)
{
    return value >= APP_ZVIRT_TIME_MIN_MS && value <= APP_ZVIRT_TIME_MAX_MS;
}

static bool Settings_IsPumpSpeedValid(uint32_t value)
{
    return value >= APP_MIN_PUMP_SPEED_PERCENT && value <= APP_MAX_PUMP_SPEED_PERCENT;
}

static bool Settings_RecordIsValid(const Settings_Record *record)
{
    if (record == 0) {
        return false;
    }

    if (record->magic != SETTINGS_MAGIC ||
        record->version != SETTINGS_VERSION ||
        record->length != sizeof(Settings_Record)) {
        return false;
    }

    for (uint8_t profile = 0U; profile < APP_SPRAY_PROFILE_COUNT; profile++) {
        for (uint8_t pump = 0U; pump < APP_PUMP_COUNT; pump++) {
            if (!Settings_IsTimeValid(record->spray_ms[profile][pump])) {
                return false;
            }
        }
    }

    for (uint8_t i = 0U; i < APP_ZVIRT_COUNT; i++) {
        if (!Settings_IsZVirtTimeValid(record->zvirt_ms[i])) {
            return false;
        }
    }

    if (!Settings_IsPumpSpeedValid(record->pump_speed_percent)) {
        return false;
    }

    return record->checksum == Settings_Checksum(record);
}

static bool Settings_RecordV3IsValid(const Settings_RecordV3 *record)
{
    if (record == 0) {
        return false;
    }

    if (record->magic != SETTINGS_MAGIC ||
        record->version != SETTINGS_VERSION_V3 ||
        record->length != sizeof(Settings_RecordV3)) {
        return false;
    }

    for (uint8_t profile = 0U; profile < APP_SPRAY_PROFILE_COUNT; profile++) {
        for (uint8_t pump = 0U; pump < APP_PUMP_COUNT; pump++) {
            if (!Settings_IsTimeValid(record->spray_ms[profile][pump])) {
                return false;
            }
        }
    }

    for (uint8_t i = 0U; i < APP_ZVIRT_COUNT; i++) {
        if (!Settings_IsZVirtTimeValid(record->zvirt_ms[i])) {
            return false;
        }
    }

    return record->checksum == Settings_ChecksumV3(record);
}

bool Settings_LoadAll(uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT],
                      uint32_t zvirt_ms[APP_ZVIRT_COUNT],
                      uint8_t *pump_speed_percent)
{
    const Settings_Record *record = (const Settings_Record *)APP_SETTINGS_FLASH_ADDR;
    const Settings_RecordV3 *record_v3 = (const Settings_RecordV3 *)APP_SETTINGS_FLASH_ADDR;

    if (spray_ms == 0 || zvirt_ms == 0 || pump_speed_percent == 0) {
        return false;
    }

    if (Settings_RecordIsValid(record)) {
        for (uint8_t profile = 0U; profile < APP_SPRAY_PROFILE_COUNT; profile++) {
            for (uint8_t pump = 0U; pump < APP_PUMP_COUNT; pump++) {
                spray_ms[profile][pump] = record->spray_ms[profile][pump];
            }
        }

        for (uint8_t i = 0U; i < APP_ZVIRT_COUNT; i++) {
            zvirt_ms[i] = record->zvirt_ms[i];
        }

        *pump_speed_percent = (uint8_t)record->pump_speed_percent;
        return true;
    }

    if (Settings_RecordV3IsValid(record_v3)) {
        for (uint8_t profile = 0U; profile < APP_SPRAY_PROFILE_COUNT; profile++) {
            for (uint8_t pump = 0U; pump < APP_PUMP_COUNT; pump++) {
                spray_ms[profile][pump] = record_v3->spray_ms[profile][pump];
            }
        }

        for (uint8_t i = 0U; i < APP_ZVIRT_COUNT; i++) {
            zvirt_ms[i] = record_v3->zvirt_ms[i];
        }

        *pump_speed_percent = APP_DEFAULT_PUMP_SPEED_PERCENT;
        return true;
    }

    return false;
}

bool Settings_LoadSprayMs(uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT])
{
    uint32_t zvirt_ms[APP_ZVIRT_COUNT];
    uint8_t pump_speed_percent;

    return Settings_LoadAll(spray_ms, zvirt_ms, &pump_speed_percent);
}

static bool Settings_ProgramHalfWord(uint32_t address, uint16_t value)
{
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, value) != HAL_OK) {
        return false;
    }

    return (*(volatile uint16_t *)address) == value;
}

bool Settings_SaveAll(const uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT],
                      const uint32_t zvirt_ms[APP_ZVIRT_COUNT],
                      uint8_t pump_speed_percent)
{
    Settings_Record record;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0U;
    uint32_t address = APP_SETTINGS_FLASH_ADDR;
    const uint16_t *half_words;
    uint32_t half_word_count;
    bool ok = true;

    if (spray_ms == 0 || zvirt_ms == 0 ||
        !Settings_IsPumpSpeedValid(pump_speed_percent)) {
        return false;
    }

    memset(&record, 0xFF, sizeof(record));
    record.magic = SETTINGS_MAGIC;
    record.version = SETTINGS_VERSION;
    record.length = sizeof(Settings_Record);
    record.reserved = 0U;

    for (uint8_t profile = 0U; profile < APP_SPRAY_PROFILE_COUNT; profile++) {
        for (uint8_t pump = 0U; pump < APP_PUMP_COUNT; pump++) {
            if (!Settings_IsTimeValid(spray_ms[profile][pump])) {
                return false;
            }
            record.spray_ms[profile][pump] = spray_ms[profile][pump];
        }
    }

    for (uint8_t i = 0U; i < APP_ZVIRT_COUNT; i++) {
        if (!Settings_IsZVirtTimeValid(zvirt_ms[i])) {
            return false;
        }
        record.zvirt_ms[i] = zvirt_ms[i];
    }

    record.pump_speed_percent = pump_speed_percent;
    record.checksum = Settings_Checksum(&record);

    HAL_FLASH_Unlock();

    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress = APP_SETTINGS_FLASH_ADDR;
    erase_init.NbPages = 1U;
    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK) {
        ok = false;
    }

    half_words = (const uint16_t *)&record;
    half_word_count = (uint32_t)(sizeof(record) / sizeof(uint16_t));
    for (uint32_t i = 0U; ok && i < half_word_count; i++) {
        ok = Settings_ProgramHalfWord(address, half_words[i]);
        address += sizeof(uint16_t);
    }

    HAL_FLASH_Lock();

    if (!ok) {
        return false;
    }

    return Settings_RecordIsValid((const Settings_Record *)APP_SETTINGS_FLASH_ADDR);
}

bool Settings_SaveSprayMs(const uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT])
{
    uint32_t zvirt_ms[APP_ZVIRT_COUNT];

    for (uint8_t i = 0U; i < APP_ZVIRT_COUNT; i++) {
        zvirt_ms[i] = APP_ZVIRT_TIME_DEFAULT_MS;
    }

    return Settings_SaveAll(spray_ms, zvirt_ms, APP_DEFAULT_PUMP_SPEED_PERCENT);
}
