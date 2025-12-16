/**
 * @file ml_model.cpp
 * @brief Triển khai mô hình ML TensorFlow Lite Micro
 *
 * Quá trình:
 * 1. Tải mô hình từ bộ nhớ (ml_model_data_array.h)
 * 2. Tạo TensorFlow Lite interpreter
 * 3. Cấp phát bộ nhớ tensor arena
 * 4. Chạy suy diễn với dữ liệu được chuẩn hóa
 */

#include "ml_model.h"
#include "ml_model_data_array.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include <Arduino.h>

namespace
{
    const tflite::Model *model = nullptr;            ///< Con trỏ đến mô hình TFLite
    tflite::MicroInterpreter *interpreter = nullptr; ///< Interpreter TFLite
    TfLiteTensor *model_input = nullptr;             ///< Tensor đầu vào
    TfLiteTensor *model_output = nullptr;            ///< Tensor đầu ra

    constexpr int kTensorArenaSize = 4 * 1024; ///< Kích thước bộ nhớ tensor arena (4 KB)
    uint8_t tensor_arena[kTensorArenaSize];    ///< Bộ nhớ cho tensor
}

/**
 * @brief Constructor - khởi tạo các biến
 */
MLModel::MLModel() : initialized(false)
{
}

/**
 * @brief Khởi tạo TensorFlow Lite interpreter và mô hình
 *
 * Quá trình:
 * 1. Tải mô hình từ bộ nhớ PROGMEM (flash)
 * 2. Kiểm tra schema version
 * 3. Khởi tạo MicroMutableOpResolver với các ops cần thiết
 * 4. Tạo interpreter
 * 5. Cấp phát bộ nhớ tensor
 * 6. Lấy con trỏ đến tensor đầu vào/ra
 *
 * Lỗi có thể xảy ra:
 * - Schema version không khớp
 * - AllocateTensors() thất bại (bộ nhớ đủ không?)
 */
void MLModel::setup()
{
    Serial.println("Setting up TFLite...");

    // Tải mô hình từ bộ nhớ
    model = tflite::GetModel(g_vital_signs_model_quantized_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        Serial.println("Model schema mismatch!");
        return;
    }

    // Tạo resolver với các operations cần thiết
    // <8> = cho phép tối đa 8 operations khác nhau
    static tflite::MicroMutableOpResolver<8> micro_op_resolver;
    micro_op_resolver.AddFullyConnected(); // Lớp fully-connected (Dense)
    micro_op_resolver.AddLogistic();       // Hàm sigmoid activation
    micro_op_resolver.AddRelu();           // ReLU activation
    micro_op_resolver.AddReshape();        // Reshape tensor
    micro_op_resolver.AddQuantize();       // Quantization
    micro_op_resolver.AddDequantize();     // Dequantization
    micro_op_resolver.AddSoftmax();        // Softmax
    micro_op_resolver.AddAdd();            // Phép cộng

    // Tạo static interpreter
    static tflite::MicroInterpreter static_interpreter(
        model, micro_op_resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    // Cấp phát bộ nhớ cho các tensor
    if (interpreter->AllocateTensors() != kTfLiteOk)
    {
        Serial.println("AllocateTensors() failed");
        return;
    }

    // Lấy con trỏ đến tensor đầu vào/ra
    model_input = interpreter->input(0);
    model_output = interpreter->output(0);

    initialized = true;
    Serial.println("TFLite setup done.");
}

/**
 * @brief Chạy suy diễn trên dữ liệu sức khỏe
 *
 * Quy trình:
 * 1. Chuẩn hóa đầu vào: (giá trị - mean) / std
 * 2. Đổ dữ liệu vào tensor đầu vào
 * 3. Gọi interpreter->Invoke() để chạy suy diễn
 * 4. Đọc kết quả từ tensor đầu ra
 * 5. Trả về điểm số (0-1)
 *
 * @param hr Nhịp tim (BPM)
 * @param spo2 Độ bão hòa oxy (%)
 * @param bmi Chỉ số khối cơ thể (Weight in kg / (Height in m)^2)
 * @return Điểm số bất thường (0-1)
 */
float MLModel::runInference(float hr, float spo2, float bmi)
{
    if (!initialized || model_input == nullptr)
    {
        Serial.println("TFLite not initialized.");
        return 0.0;
    }

    // === Chuẩn hóa các đầu vào ===
    // Công thức chuẩn hóa: x_norm = (x - mean) / std
    float hr_norm = (hr - modelNorm.hr_mean) / modelNorm.hr_std;
    float spo2_norm = (spo2 - modelNorm.spo2_mean) / modelNorm.spo2_std;
    float body_temp_norm = 0; ///< body_temp = body_temp_mean
    float systolic_bp_norm = 0;
    float diastolic_bp_norm = 0;
    float bmi_norm = (bmi - modelNorm.bmi_mean) / modelNorm.bmi_std;

    // === Đổ dữ liệu vào tensor đầu vào ===
    if (model_input->type == kTfLiteFloat32)
    {
        model_input->data.f[0] = hr_norm;
        model_input->data.f[1] = spo2_norm;
        model_input->data.f[2] = body_temp_norm;
        model_input->data.f[3] = systolic_bp_norm;
        model_input->data.f[4] = diastolic_bp_norm;
        model_input->data.f[5] = bmi_norm;
    }
    else
    {
        Serial.println("Quantized model - implement dequantization");
        return 0.0;
    }

    // === Chạy suy diễn ===
    if (interpreter->Invoke() != kTfLiteOk)
    {
        Serial.println("Invoke failed!");
        return 0.0;
    }

    // === Đọc kết quả từ tensor đầu ra ===
    float score = 0.0;
    if (model_output->type == kTfLiteFloat32)
    {
        score = model_output->data.f[0]; // Lấy điểm đầu tiên
    }

    Serial.printf("[ML] Inference: Score=%.4f\n", score);

    return score;
}

/**
 * @brief Kiểm tra xem mô hình đã khởi tạo xong chưa
 * @return true nếu setup() thành công
 */
bool MLModel::isInitialized()
{
    return initialized;
}
