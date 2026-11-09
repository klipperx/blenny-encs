#include "game.h"

#include "roster.h"

#include <algorithm>
#include <esp_random.h>

static bool sameFact(const String &a, const String &b) { return a.equalsIgnoreCase(b); }

static void shuffle3(String opts[3], uint8_t *truth_index) {
  for (int i = 2; i > 0; i--) {
    int j = (int)(esp_random() % (i + 1));
    if (i == j) {
      continue;
    }
    std::swap(opts[i], opts[j]);
    if (*truth_index == i) {
      *truth_index = (uint8_t)j;
    } else if (*truth_index == j) {
      *truth_index = (uint8_t)i;
    }
  }
}

bool gameBuildQuiz(const char *target_id, Quiz *out) {
  const Attendee *target = rosterFind(target_id);
  const Attendee *me = rosterMe();
  if (!target || !me || !out) {
    return false;
  }

  String stolen;
  const auto &people = rosterAll();
  if (people.size() >= 3) {
    for (int tries = 0; tries < 24; tries++) {
      const Attendee &a = people[esp_random() % people.size()];
      if (a.id == me->id || a.id == target->id) {
        continue;
      }
      if (sameFact(a.truth, target->truth)) {
        continue;
      }
      stolen = a.truth;
      break;
    }
  }
  if (!stolen.length()) {
    // Tiny roster fallback: use any other truth, then a second generated fact.
    for (const auto &a : people) {
      if (a.id != me->id && a.id != target->id && !sameFact(a.truth, target->truth)) {
        stolen = a.truth;
        break;
      }
    }
  }

  String generated;
  const auto &facts = rosterFacts();
  for (int tries = 0; tries < 40 && facts.size(); tries++) {
    const String &f = facts[esp_random() % facts.size()];
    if (!sameFact(f, target->truth) && !sameFact(f, stolen)) {
      generated = f;
      break;
    }
  }
  if (!generated.length() && facts.size()) {
    generated = facts[0];
  }
  if (!stolen.length()) {
    for (const auto &f : facts) {
      if (!sameFact(f, target->truth) && !sameFact(f, generated)) {
        stolen = f;
        break;
      }
    }
  }
  if (!stolen.length() || !generated.length()) {
    return false;
  }

  out->target_id = target->id;
  out->target_name = target->name;
  out->options[0] = target->truth;
  out->options[1] = stolen;
  out->options[2] = generated;
  out->truth_index = 0;
  shuffle3(out->options, &out->truth_index);
  return true;
}
