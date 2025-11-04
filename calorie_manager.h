#pragma once
#include <Arduino.h>
#include "sensor_manager.h"

class CalorieManager
{
public:
    CalorieManager();
    // Update with latest step count and HR to compute calories
    void update(uint32_t totalSteps, float currentHR, const UserProfile &profile);
    // Get total calories burned since last reset
    float getTotalCalories() const;
    // Reset calorie counter (e.g., at midnight)
    void reset();

private:
    uint32_t lastStepCount_;
    float totalCalories_;
    unsigned long lastUpdateMs_;

    // Estimate calories from steps (simple model)
    float estimateStepCalories(uint32_t steps, float weightKg);
    // Estimate calories from HR over time (basic cardio model)
    float estimateHRCalories(float avgHR, float durationMin, float weightKg, int age, int gender);
};
