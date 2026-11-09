#pragma once

#include <Arduino.h>

// TM1637 2-digit boards on the scoreboard carrier.
// HOME:  GPIO1 CLK, GPIO2 DIO
// VISITOR: GPIO3 CLK, GPIO10 DIO
void scoreboardBegin();
void scoreboardSet(uint8_t home, uint8_t visitor);
void scoreboardOff();
