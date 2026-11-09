#pragma once

#include <Arduino.h>

bool touchBegin();
bool touchPoll(int *x, int *y, bool *pressed);
bool touchOk();
const char *touchI2c();
