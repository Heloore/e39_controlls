#pragma once

#include <stdint.h>

// Jobs that move glass, the sunroof or the trunk, or deadlock the car.
// The scanner never sends these.
inline bool isDangerousGmJob(uint8_t job) {
  static const uint8_t blockedJobs[] = {
    0x02, 0x05, 0x46, 0x95,                          // trunk
    0x0A, 0x0B, 0x0C, 0x0D,                          // window buttons
    0x41, 0x42, 0x43, 0x44, 0x52, 0x53, 0x54, 0x55,  // windows
    0x7E, 0x7F,                                      // sunroof
    0x49, 0x62,                                      // wipers, washer
    0x97                                             // doors hardlock
  };
  if (job >= 0x78 && job <= 0x7F) return true; // sunroof range, known ids 0x7E/0x7F plus neighbours

  for (uint8_t blocked : blockedJobs) {
    if (blocked == job) return true;
  }
  return false;
}

inline bool isGmJobBlocked(uint8_t job) {
  uint8_t lowNibble = job & 0x0F;
  if (lowNibble == 0x05 || lowNibble == 0x06) return true; // driver seat motor: 15, 16, 25 moved it
  return isDangerousGmJob(job);
}

// Steps through GM "set IO" job ids, one every STEP_MS, skipping blocked ones.
class GmJobScanner {
public:
  static constexpr uint32_t STEP_MS = 4000;

  // useBlocklist = false steps through plain values (e.g. parameters), not job ids.
  void start(uint8_t fromJob, uint8_t toJob, uint32_t nowMs, bool useBlocklist = true, uint32_t stepDelayMs = STEP_MS) {
    nextJob = fromJob;
    lastJob = toJob;
    blocklistEnabled = useBlocklist;
    stepMs = stepDelayMs;
    running = true;
    nextStepMs = nowMs;
    skipBlockedJobs();
  }
  void stop() { running = false; }

  bool isRunning() const { return running; }
  bool isStepDue(uint32_t nowMs) const { return running && (int32_t)(nowMs - nextStepMs) >= 0; }
  uint8_t job() const { return nextJob; }

  void advance(uint32_t nowMs) {
    nextStepMs = nowMs + stepMs;
    if (nextJob >= lastJob) {
      running = false;
      return;
    }
    nextJob++;
    skipBlockedJobs();
  }

private:
  uint8_t nextJob = 0;
  uint8_t lastJob = 0;
  bool running = false;
  uint32_t nextStepMs = 0;
  uint32_t stepMs = STEP_MS;
  bool blocklistEnabled = true;

  void skipBlockedJobs() {
    while (running && blocklistEnabled && isGmJobBlocked(nextJob)) {
      if (nextJob >= lastJob) {
        running = false;
        return;
      }
      nextJob++;
    }
  }
};
