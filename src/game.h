#pragma once

#include <Arduino.h>

struct Quiz {
  String options[3];
  uint8_t truth_index = 0;
  String target_id;
  String target_name;
};

bool gameBuildQuiz(const char *target_id, Quiz *out);
