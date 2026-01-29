#ifndef CONFIG_H
#define CONFIG_H

#define PIN_MIX_SENSOR 6 // Bể pha trộn

// ===== VALVES (6 van điều khiển) =====
#define PIN_WATER_TO_GARDEN_VALVE 7 // V1: Nước → Vườn
#define PIN_MIX_TO_GARDEN_VALVE 8   // V2: Mix → Vườn
#define PIN_WATER_TO_MIX_VALVE 15   // V3: Nước → Mix
#define PIN_MIX_TO_CHEM_VALVE 16    // V4: Mix → Chem
#define PIN_CHEM_TO_PUMP_VALVE 17   // V5: Chem → Pump
#define PIN_PUMP_TO_MIX_VALVE 18    // V6: Pump → Mix

// ===== PUMP (1 máy bơm tuần hoàn) =====
#define PIN_CIRCULATION_PUMP 9 // Pump khuấy

// ===== TANK CAPACITY =====
#define WATER_TANK_MAX 1000.0f // 1000L
#define CHEM_TANK_MAX 100.0f   // 100L
#define MIX_TANK_MAX 500.0f    // 500L

#endif