#pragma once

// Second bus I2C (Wire1) pour les capteurs et l'extension GPIO.
// Le bus du tactile GT911 (GPIO19 SDA / GPIO45 SCL) reste dédié à l'écran.
//
// ATTENTION : broches à confirmer au multimètre sur la carte réelle.
// Ce sont deux des trois GPIO des relais d'origine (40, 2, 1).
#define I2C_SENSORS_SDA 40
#define I2C_SENSORS_SCL 2
#define I2C_SENSORS_FREQ 100000

// Adresses I2C attendues
#define ADDR_BME280_A 0x76
#define ADDR_BME280_B 0x77
#define ADDR_ADS1115 0x48
#define ADDR_DS3231 0x68
#define ADDR_PCF8575 0x20
