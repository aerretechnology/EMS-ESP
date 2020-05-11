/*
 * EMS-ESP - https://github.com/proddy/EMS-ESP
 * Copyright 2019  Paul Derbyshire
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "solar.h"

// MAKE_PSTR_WORD(solar)
MAKE_PSTR(kwh, "kWh")
MAKE_PSTR(wh, "Wh")

namespace emsesp {

REGISTER_FACTORY(Solar, EMSdevice::DeviceType::SOLAR);
MAKE_PSTR(logger_name, "solar")
uuid::log::Logger Solar::logger_{F_(logger_name), uuid::log::Facility::CONSOLE};

Solar::Solar(uint8_t device_type, uint8_t device_id, uint8_t product_id, const std::string & version, const std::string & name, uint8_t flags, uint8_t brand)
    : EMSdevice(device_type, device_id, product_id, version, name, flags, brand) {
    DEBUG_LOG(F("Registering new Solar module with device ID 0x%02X"), device_id);

    // telegram handlers
    register_telegram_type(0x0097, F("SM10Monitor"), true, std::bind(&Solar::process_SM10Monitor, this, _1));
    register_telegram_type(0x0362, F("SM100Monitor"), true, std::bind(&Solar::process_SM100Monitor, this, _1));
    register_telegram_type(0x0364, F("SM100Status"), false, std::bind(&Solar::process_SM100Status, this, _1));
    register_telegram_type(0x036A, F("SM100Status2"), false, std::bind(&Solar::process_SM100Status2, this, _1));
    register_telegram_type(0x038E, F("SM100Energy"), false, std::bind(&Solar::process_SM100Energy, this, _1));
    register_telegram_type(0x0003, F("ISM1StatusMessage"), true, std::bind(&Solar::process_ISM1StatusMessage, this, _1));
    register_telegram_type(0x0001, F("ISM1Set"), false, std::bind(&Solar::process_ISM1Set, this, _1));

    // MQTT callbacks
    // register_mqtt_topic("cmd", std::bind(&Solar::cmd, this, _1));
}

// context submenu
void Solar::add_context_menu() {
}

// display all values into the shell console
void Solar::show_values(uuid::console::Shell & shell) {
    EMSdevice::show_values(shell); // always call this to show header

    char buffer[10]; // used for formatting

    print_value(shell, F("Collector temperature (TS1)"), F_(degrees), Helpers::render_value(buffer, collectorTemp_, 10));
    print_value(shell, F("Bottom temperature (TS2)"), F_(degrees), Helpers::render_value(buffer, bottomTemp_, 10));
    print_value(shell, F("Bottom temperature (TS5)"), F_(degrees), Helpers::render_value(buffer, bottomTemp2_, 10));
    print_value(shell, F("Pump modulation"), F_(percent), Helpers::render_value(buffer, pumpModulation_, 1));
    print_value(shell, F("Valve (VS2) status"), Helpers::render_value(buffer, valveStatus_, EMS_VALUE_BOOL));
    print_value(shell, F("Pump (PS1) active"), Helpers::render_value(buffer, pump_, EMS_VALUE_BOOL));

    if (pumpWorkMin_ != EMS_VALUE_ULONG_NOTSET) {
        shell.printfln(F("  Pump working time: %d days %d hours %d minutes"), pumpWorkMin_ / 1440, (pumpWorkMin_ % 1440) / 60, pumpWorkMin_ % 60);
    }

    print_value(shell, F("Energy last hour"), F_(wh), Helpers::render_value(buffer, energyLastHour_, 10));
    print_value(shell, F("Energy today"), F_(wh), Helpers::render_value(buffer, energyToday_, 0)); // no division
    print_value(shell, F("Energy total"), F_(kwh), Helpers::render_value(buffer, energyTotal_, 10));

    shell.println();
}

// publish values via MQTT
void Solar::publish_values() {
    DynamicJsonDocument doc(EMSESP_MAX_JSON_SIZE_MEDIUM);

    char s[10]; // for formatting strings

    if (collectorTemp_ != EMS_VALUE_SHORT_NOTSET) {
        doc["collectortemp"] = (float)collectorTemp_ / 10;
    }
    if (bottomTemp_ != EMS_VALUE_SHORT_NOTSET) {
        doc["bottomtemp"] = (float)bottomTemp_ / 10;
    }
    if (bottomTemp2_ != EMS_VALUE_SHORT_NOTSET) {
        doc["bottomtemp2"] = (float)bottomTemp2_ / 10;
    }
    if (pumpModulation_ != EMS_VALUE_INT_NOTSET) {
        doc["pumpmodulation"] = pumpModulation_;
    }
    if (pump_ != EMS_VALUE_BOOL_NOTSET) {
        doc["pump"] = Helpers::render_value(s, pump_, EMS_VALUE_BOOL);
    }
    if (valveStatus_ != EMS_VALUE_BOOL_NOTSET) {
        doc["valvestatus"] = Helpers::render_value(s, valveStatus_, EMS_VALUE_BOOL);
    }
    if (pumpWorkMin_ != EMS_VALUE_ULONG_NOTSET) {
        doc["pumpWorkMin"] = (float)pumpWorkMin_;
    }
    if (energyLastHour_ != EMS_VALUE_ULONG_NOTSET) {
        doc["energylasthour"] = (float)energyLastHour_ / 10;
    }
    if (energyToday_ != EMS_VALUE_ULONG_NOTSET) {
        doc["energytoday"] = energyToday_;
    }
    if (energyTotal_ != EMS_VALUE_ULONG_NOTSET) {
        doc["energytotal"] = (float)energyTotal_ / 10;
    }

#ifdef EMSESP_DEBUG
    DEBUG_LOG(F("[DEBUG] Performing a solar module publish"));
#endif

    Mqtt::publish("sm_data", doc);
}

// check to see if values have been updated
bool Solar::updated_values() {
    return false;
}

// add console commands
void Solar::console_commands() {
}

// SM10Monitor - type 0x97
void Solar::process_SM10Monitor(std::shared_ptr<const Telegram> telegram) {
    telegram->read_value(collectorTemp_, 2);  // collector temp from SM10, is *10
    telegram->read_value(bottomTemp_, 5);     // bottom temp from SM10, is *10
    telegram->read_value(pumpModulation_, 4); // modulation solar pump
    telegram->read_value(pump_, 7, 1);
}

/*
 * SM100Monitor - type 0x0162 EMS+ - for SM100 and SM200
 * e.g. B0 0B FF 00 02 62 00 44 02 7A 80 00 80 00 80 00 80 00 80 00 80 00 00 7C 80 00 80 00 80 00 80
 * e.g, 30 00 FF 00 02 62 01 AC
 *      30 00 FF 18 02 62 80 00
 *      30 00 FF 00 02 62 01 A1 - for bottom temps
 * bytes 0+1 = TS1 Temperature sensor for collector
 * bytes 2+3 = TS2 Temperature sensor bottom cylinder 1
 * bytes 16+17 = TS5 Temperature sensor bottom cylinder 2
 */
void Solar::process_SM100Monitor(std::shared_ptr<const Telegram> telegram) {
    telegram->read_value(collectorTemp_, 0); // is *10
    telegram->read_value(bottomTemp_, 2);    // is *10
    telegram->read_value(bottomTemp2_, 16);  // is *10
}

/*
 * SM100Status - type 0x0264 EMS+ for pump modulation - for SM100 and SM200
 * e.g. 30 00 FF 09 02 64 64 = 100%
 *      30 00 FF 09 02 64 1E = 30%
 */
void Solar::process_SM100Status(std::shared_ptr<const Telegram> telegram) {
    telegram->read_value(pumpModulation_, 9);
}

/*
 * SM100Status2 - type 0x026A EMS+ for pump on/off at offset 0x0A - for SM100 and SM200
 * e.g. B0 00 FF 00 02 6A 03 03 03 03 01 03 03 03 03 03 01 03 
 * byte 4 = VS2 3-way valve for cylinder 2 : test=01, on=04 and off=03
 * byte 10 = PS1 Solar circuit pump for collector array 1: test=01, on=04 and off=03
 */
void Solar::process_SM100Status2(std::shared_ptr<const Telegram> telegram) {
    telegram->read_value(valveStatus_, 4, 2); // on if bit 2 set
    telegram->read_value(pump_, 10, 2);       // on if bit 2 set
}

/*
 * SM100Energy - type 0x028E EMS+ for energy readings
 * e.g. 30 00 FF 00 02 8E 00 00 00 00 00 00 06 C5 00 00 76 35
 */
void Solar::process_SM100Energy(std::shared_ptr<const Telegram> telegram) {
    telegram->read_value(energyLastHour_, 0); // last hour / 10 in Wh
    telegram->read_value(energyToday_, 4);    //  todays in Wh
    telegram->read_value(energyTotal_, 8);    //  total / 10 in kWh
}

/*
 * Junkers ISM1 Solar Module - type 0x0003 EMS+ for energy readings
 *  e.g. B0 00 FF 00 00 03 32 00 00 00 00 13 00 D6 00 00 00 FB D0 F0
 */
void Solar::process_ISM1StatusMessage(std::shared_ptr<const Telegram> telegram) {
    telegram->read_value(collectorTemp_, 4);  // Collector Temperature
    telegram->read_value(bottomTemp_, 6);     // Temperature Bottom of Solar Boiler
    telegram->read_value(energyLastHour_, 2); // Solar Energy produced in last hour - is * 10 and handled in ems-esp.cpp
    telegram->read_value(pump_, 8, 0);        // Solar pump on (1) or off (0)
    telegram->read_value(pumpWorkMin_, 10);
}

/*
 * Junkers ISM1 Solar Module - type 0x0001 EMS+ for setting values
 * e.g. 90 30 FF 06 00 01 50
 */
void Solar::process_ISM1Set(std::shared_ptr<const Telegram> telegram) {
    telegram->read_value(setpoint_maxBottomTemp_, 6);
}

} // namespace emsesp