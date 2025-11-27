/**
 * @file calorie_manager.cpp
 * @brief Triển khai quản lý tính toán lượng calo
 */

#include "calorie_manager.h"

/**
 * @brief Constructor - khởi tạo các biến thành viên
 */
CalorieManager::CalorieManager()
    : lastStepCount_(0), totalCalories_(0.0), lastUpdateMs_(0)
{
}

/**
 * @brief Cập nhật tính toán calo tiêu thụ
 *
 * Quá trình:
 * 1. Tính calo từ bước chân mới (so với lần cập nhật trước)
 * 2. Mỗi phút, tính calo từ nhịp tim và thêm vào tổng
 *
 * @param totalSteps Tổng số bước từ khi khởi động
 * @param currentHR Nhịp tim hiện tại (BPM)
 * @param profile Hồ sơ người dùng (cân nặng, tuổi, giới tính)
 */
void CalorieManager::update(uint32_t totalSteps, float currentHR, const UserProfile &profile)
{
    unsigned long now = millis();

    // === Phần 1: Tính calo từ bước chân mới ===
    if (totalSteps > lastStepCount_)
    {
        // Chỉ tính calo cho các bước MỚI
        uint32_t newSteps = totalSteps - lastStepCount_;
        float stepCal = estimateStepCalories(newSteps, profile.weight);
        totalCalories_ += stepCal;
        lastStepCount_ = totalSteps;
    }

    // === Phần 2: Tính calo từ nhịp tim (mỗi phút) ===
    // Chỉ cập nhật mỗi 60 giây để tránh tính quá lần
    if (lastUpdateMs_ > 0 && (now - lastUpdateMs_) >= 60000) // 60000 ms = 1 phút
    {
        float durationMin = (now - lastUpdateMs_) / 60000.0;

        // Kiểm tra HR có hợp lệ không (50-200 BPM)
        if (currentHR > 50 && currentHR < 200)
        {
            float hrCal = estimateHRCalories(currentHR, durationMin, profile.weight, profile.age, profile.gender);
            totalCalories_ += hrCal;
        }
    }

    lastUpdateMs_ = now;
}

/**
 * @brief Lấy tổng calo tiêu thụ
 * @return Tổng calo tính bằng kcal
 */
float CalorieManager::getTotalCalories() const
{
    return totalCalories_;
}

/**
 * @brief Reset bộ đếm calo (ví dụ: khi bắt đầu ngày mới)
 */
void CalorieManager::reset()
{
    totalCalories_ = 0.0;
    lastStepCount_ = 0;
    lastUpdateMs_ = 0;
    Serial.println("[Calorie] Counter reset.");
}

/**
 * @brief Ước tính calo tiêu thụ từ số lượng bước chân
 *
 * Công thức: Calo = Bước × 0.04 × (Cân nặng / 70)
 *
 * Giải thích:
 * - Một bước đi tiêu thụ khoảng 0.04 kcal ở cơ thể 70kg
 * - Cân nặng càng lớn thì tiêu thụ calo càng nhiều
 *
 * @param steps Số bước
 * @param weightKg Cân nặng (kg)
 * @return Lượng calo (kcal)
 */
float CalorieManager::estimateStepCalories(uint32_t steps, float weightKg)
{
    // Mô hình đơn giản: ~0.04 kcal mỗi bước ở cơ thể 70kg
    // Trung bình chiều dài bước: đi bộ tiêu thụ ~0.035-0.05 kcal/bước tùy cân nặng
    float caloriesPerStep = 0.04 * (weightKg / 70.0); // Chuẩn hóa theo cân nặng 70kg
    return steps * caloriesPerStep;
}

/**
 * @brief Ước tính calo tiêu thụ từ hoạt động tim (cardio)
 *
 * Sử dụng công thức Karvonen sửa đổi:
 * Calo = ((Age×0.2017 - Weight×0.09036 + HR×0.6309 - C) × Phút) / 4.184
 *
 * Trong đó:
 * - C = 55.0969 (nam), 20.4022 (nữ)
 * - Age = tuổi (năm)
 * - Weight = cân nặng (kg)
 * - HR = nhịp tim (BPM)
 * - Phút = thời lượng tính bằng phút
 *
 * @param avgHR Nhịp tim trung bình (BPM)
 * @param durationMin Thời lượng (phút)
 * @param weightKg Cân nặng (kg)
 * @param age Tuổi (năm)
 * @param gender Giới tính (1=nam, 0=nữ)
 * @return Lượng calo (kcal)
 */
float CalorieManager::estimateHRCalories(float avgHR, float durationMin, float weightKg, int age, int gender)
{
    // Công thức Karvonen sửa đổi
    // C = 55.0969 cho nam, 20.4022 cho nữ
    float C = (gender == 1) ? 55.0969 : 20.4022;

    // Tính toán calo theo công thức
    float cal = ((age * 0.2017) - (weightKg * 0.09036) + (avgHR * 0.6309) - C) * durationMin / 4.184;

    // Đảm bảo giá trị không âm (nếu kết quả âm, set = 0)
    if (cal < 0)
        cal = 0;

    return cal;
}
