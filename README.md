# DC-DC Buck Converter with Soft Starting

## Overview

A hardware-implemented DC-DC synchronous buck converter with a soft-start feature, using a TI C2000 (TMS320F280049C) microcontroller to limit inrush current during startup. Designed, simulated, and validated on custom hardware as part of coursework (EEE320R01: Digital Controller for Power Electronic Applications) at SASTRA Deemed University.

The converter was originally designed and simulated for a 12V → 3.3V target; the fabricated hardware was built and validated at 12V → 5V.

## Key Specifications

- Input Voltage: 12 V
- Output Voltage: 5 V (hardware-validated); 3.3 V design/simulation target
- Switching Frequency: 150 kHz
- Control: Open-loop, with hardware-verified soft-start
- Soft-start duration: 75 ms

## What's in this repo

| File/Folder | Description |
|---|---|
| `report/` | Full project report (PDF) |
| `simulation/` | MATLAB/Simulink simulation files and screenshots |
| `schematic/` | KiCad PCB schematic and layout files |
| `Hardware/` | Real hardware photos and PCB testing images |
| `code/` | TI C2000 firmware — PWM generation and soft-start logic |

## Project Demo

[▶️ Watch Hardware Demo on Google Drive](https://drive.google.com/file/d/1wLX9armRiDO4SZjXrzO_7kywsPe46BNn/view?usp=sharing)

## Tools Used

- MATLAB / Simulink — simulation
- KiCad — PCB design
- TI C2000 (TMS320F280049C) — digital controller
- IR2104 — half-bridge gate driver

## Authors

- Sridarshan B — B.Tech EEE (Smart Grid & EV), SASTRA Deemed University
- Shriram D — B.Tech EEE (Smart Grid & EV), SASTRA Deemed University

Guided by Dr. Santhosh T.K., SAP/EEE/SEEE, SASTRA Deemed University.
