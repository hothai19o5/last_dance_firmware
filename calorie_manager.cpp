#include "calorie_manager.h"

CalorieManager::CalorieManager()
    : lastStepCount_(0), totalCalories_(0.0), lastUpdateMs_(0)
{
}

void CalorieManager::update(uint32_t totalSteps, float currentHR, const UserProfile &profile)
{
    unsigned long now = millis();

    // Calculate calories from new steps
    if (totalSteps > lastStepCount_)
    {
        uint32_t newSteps = totalSteps - lastStepCount_;
        float stepCal = estimateStepCalories(newSteps, profile.weight);
        totalCalories_ += stepCal;
        lastStepCount_ = totalSteps;
    }

    // Calculate calories from HR (if enough time has passed)
    if (lastUpdateMs_ > 0 && (now - lastUpdateMs_) >= 60000) // Every minute
    {
        float durationMin = (now - lastUpdateMs_) / 60000.0;
        if (currentHR > 50 && currentHR < 200) // Valid HR range
        {
            float hrCal = estimateHRCalories(currentHR, durationMin, profile.weight, profile.age, profile.gender);
            totalCalories_ += hrCal;
        }
    }

    lastUpdateMs_ = now;
}

float CalorieManager::getTotalCalories() const
{
    return totalCalories_;
}

void CalorieManager::reset()
{
    totalCalories_ = 0.0;
    lastStepCount_ = 0;
    lastUpdateMs_ = 0;
    Serial.println("[Calorie] Counter reset.");
}

float CalorieManager::estimateStepCalories(uint32_t steps, float weightKg)
{
    // Simple model: ~0.04 kcal per step per kg body weight
    // Average stride: walking burns ~0.035-0.05 kcal/step depending on weight
    float caloriesPerStep = 0.04 * (weightKg / 70.0); // Normalized to 70kg
    return steps * caloriesPerStep;
}

float CalorieManager::estimateHRCalories(float avgHR, float durationMin, float weightKg, int age, int gender)
{
    // Basic calorie burn from HR using simplified formula
    // Calories = ((Age * 0.2017) - (Weight * 0.09036) + (HR * 0.6309) - C) * Duration / 4.184
    // C = 55.0969 for males, 20.4022 for females
    float C = (gender == 1) ? 55.0969 : 20.4022;
    float cal = ((age * 0.2017) - (weightKg * 0.09036) + (avgHR * 0.6309) - C) * durationMin / 4.184;

    // Ensure non-negative
    if (cal < 0)
        cal = 0;

    return cal;
}
