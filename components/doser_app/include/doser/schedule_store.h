#pragma once
// Persistence of the schedules.
//
//  * Configuration (changed by the user): NVS namespace "sched"
//      "cfg"       scheduler parameters (super admin)
//      "locks"     lock times between pumps + manual-dose policy
//      "p0".."p4"  plan per pump (blob version 2; older versions are dropped)
//  * Run state (after every processed slot): EEPROM AT24C32, two slots per
//    pump (A/B) with sequence number and CRC32, behind the dosing records.
//      0x640 + pump * 192 + slot * 96   (used up to 0x9FF)
//    Without EEPROM (or on write error) the state goes to NVS ("r0".."r4").
//    On load the valid record with the highest sequence number wins.

#include "doser/schedule_core.h"
#include "esp_err.h"

namespace doser::schedule_store {

void load_config(sched::Config* c);
esp_err_t save_config(const sched::Config& c);

void load_locks(sched::Locks* l);
esp_err_t save_locks(const sched::Locks& l);

void load_schedule(int pump, sched::PumpSchedule* s);
esp_err_t save_schedule(int pump, const sched::PumpSchedule& s);

// false = no valid record found (s stays unchanged)
bool load_run(int pump, sched::RunState* s);
esp_err_t save_run(int pump, const sched::RunState& s);

}  // namespace doser::schedule_store
