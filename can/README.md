# CAN contract

> **MOCK(M1).** The message table below is a draft. IDs, layouts and rates become real in M1 (`tools/dbc2c.py` generates `firmware/common/can_msgs.{h,c}` from `fleetbus.dbc`).

| ID | Message | Sender | Rate | Purpose |
|---|---|---|---|---|
| 0x100 | Heartbeat | all nodes | 10 Hz (TARGET) | node id, proto version, firmware version, uptime, state |
| 0x200 | BatteryStatus | Battery | 10 Hz (TARGET) | pack mV, mA, temperature, SoC |
| 0x201 | BodyStatus | Body | 10 Hz (TARGET) | lights, servo, limp-home flag |
| 0x300 | FaultEvent | any | on event | fault code and severity |
| 0x400–0x402 | OtaCtrl / OtaData / OtaAck | Gateway / Node | during OTA | block transfer |
| 0x500 | VehicleObd | Telematics | 5 Hz (TARGET) | read-only mirror of OBD-II PIDs |
