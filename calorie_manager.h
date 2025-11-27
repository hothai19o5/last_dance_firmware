/**
 * @file calorie_manager.h
 * @brief Quản lý tính toán lượng calo tiêu thụ hàng ngày
 * @author Ho Xuan Thai
 * @date 2025
 *
 * Chức năng:
 * - Ước tính calo từ số lượng bước đi
 * - Ước tính calo từ nhịp tim (cường độ hoạt động tim)
 * - Cộng dồn tổng calo tiêu thụ
 * - Reset bộ đếm calo hàng ngày
 */

#pragma once
#include <Arduino.h>
#include "max30102_manager.h"

/**
 * @class CalorieManager
 * @brief Quản lý tính toán lượng calo tiêu thụ
 *
 * Sử dụng hai phương pháp để ước tính calo:
 * 1. Calo từ bước chân: ~0.04 kcal/bước/kg (tùy thuộc trọng lượng)
 * 2. Calo từ nhịp tim: dựa trên công thức Karvonen sửa đổi
 */
class CalorieManager
{
public:
    /// @brief Constructor - khởi tạo biến
    CalorieManager();

    /// @brief Cập nhật calo tiêu thụ dựa trên bước chân và nhịp tim
    /// @param totalSteps Tổng số bước từ khi khởi động
    /// @param currentHR Nhịp tim hiện tại (BPM)
    /// @param profile Hồ sơ người dùng (cân nặng, tuổi, giới tính)
    void update(uint32_t totalSteps, float currentHR, const UserProfile &profile);

    /// @brief Lấy tổng calo tiêu thụ tính từ lần khởi động gần nhất
    /// @return Tổng calo tính bằng kcal
    float getTotalCalories() const;

    /// @brief Reset bộ đếm calo (ví dụ: khi bắt đầu ngày mới)
    void reset();

private:
    uint32_t lastStepCount_;     ///< Số bước lần đọc trước (để tính Delta)
    float totalCalories_;        ///< Tổng calo tiêu thụ tích lũy
    unsigned long lastUpdateMs_; ///< Thời điểm cập nhật calo từ HR lần trước (ms)

    /// @brief Ước tính calo từ số lượng bước chân
    /// @param steps Số lượng bước
    /// @param weightKg Cân nặng tính bằng kg
    /// @return Lượng calo ước tính (kcal)
    float estimateStepCalories(uint32_t steps, float weightKg);

    /// @brief Ước tính calo từ hoạt động tim trong một khoảng thời gian
    /// @param avgHR Nhịp tim trung bình (BPM)
    /// @param durationMin Thời lượng tính bằng phút
    /// @param weightKg Cân nặng tính bằng kg
    /// @param age Tuổi (năm)
    /// @param gender Giới tính (1=nam, 0=nữ)
    /// @return Lượng calo ước tính (kcal)
    float estimateHRCalories(float avgHR, float durationMin, float weightKg, int age, int gender);
};
