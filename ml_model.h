/**
 * @file ml_model.h
 * @brief Quản lý mô hình máy học TensorFlow Lite Micro để phát hiện bất thường sức khỏe
 * @author Hồ Xuân Thái
 * @date 2025
 *
 * Mô hình này sử dụng TensorFlow Lite Micro để:
 * - Chuẩn hóa đầu vào (nhịp tim, SpO2, BMI)
 * - Chạy suy diễn trên thiết bị (on-device inference)
 * - Trả về điểm số bất thường (0-1, 0 = bình thường, 1 = bất thường)
 */

#pragma once

/**
 * @struct ModelNormalization
 * @brief Các tham số chuẩn hóa (mean, std) cho đầu vào mô hình
 *
 * Các giá trị này được tính từ dữ liệu huấn luyện
 * Dùng để chuẩn hóa các đầu vào trước khi đưa vào mô hình
 */
struct ModelNormalization
{
  float hr_mean = 79.53374663;   ///< Trung bình nhịp tim
  float hr_std = 11.55286498;    ///< Độ lệch chuẩn nhịp tim
  float spo2_mean = 97.50437243; ///< Trung bình SpO2
  float spo2_std = 1.44259433;   ///< Độ lệch chuẩn SpO2
  float bmi_mean = 25.003625;    ///< Trung bình BMI
  float bmi_std = 6.447143;      ///< Độ lệch chuẩn BMI
};

/**
 * @class MLModel
 * @brief Quản lý mô hình ML TensorFlow Lite Micro
 *
 * Các bước sử dụng:
 * 1. Gọi setup() để khởi tạo mô hình
 * 2. Gọi runInference(hr, spo2, bmi) để chạy suy diễn
 * 3. Nhận về điểm số bất thường (0-1)
 */
class MLModel
{
public:
  /// @brief Constructor
  MLModel();

  /// @brief Khởi tạo TensorFlow Lite interpreter và tải mô hình
  /// Phải được gọi trước khi chạy runInference()
  void setup();

  /// @brief Chạy suy diễn trên đầu vào được chuẩn hóa
  /// @param hr Nhịp tim (BPM)
  /// @param spo2 Độ bão hòa oxy (%)
  /// @param bmi Chỉ số khối cơ thể (BMI) = cân nặng/(chiều cao^2)
  /// @return Điểm số bất thường (0-1, càng cao càng bất thường)
  float runInference(float hr, float spo2, float bmi);

  /// @brief Kiểm tra xem mô hình đã khởi tạo xong chưa
  /// @return true nếu setup() thành công
  bool isInitialized();

  /// @brief Lấy số lần suy diễn đã chạy
  /// @return Số lần runInference() được gọi thành công
  int getInferenceCount();

private:
  ModelNormalization modelNorm; ///< Các tham số chuẩn hóa
  int inference_count;          ///< Đếm số lần suy diễn
  bool initialized;             ///< Cờ: mô hình đã khởi tạo?
};

// Tham chiếu đến dữ liệu mô hình (định nghĩa trong ml_model_data_array.h)
extern const unsigned char g_vital_signs_model_quantized_tflite[];
extern const int g_vital_signs_model_quantized_tflite_len;
