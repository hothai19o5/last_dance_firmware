#include "ml_model.h"
#include "ml_model_data_array.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include <Arduino.h>

namespace
{
    const tflite::Model *model = nullptr;
    tflite::MicroInterpreter *interpreter = nullptr;
    TfLiteTensor *model_input = nullptr;
    TfLiteTensor *model_output = nullptr;

    constexpr int kTensorArenaSize = 4 * 1024;
    uint8_t tensor_arena[kTensorArenaSize];
}

MLModel::MLModel() : inference_count(0), initialized(false)
{
}

void MLModel::setup()
{
    Serial.println("Setting up TFLite...");

    model = tflite::GetModel(g_vital_signs_model_quantized_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        Serial.println("Model schema mismatch!");
        return;
    }

    static tflite::MicroMutableOpResolver<8> micro_op_resolver;
    micro_op_resolver.AddFullyConnected();
    micro_op_resolver.AddLogistic();
    micro_op_resolver.AddRelu();
    micro_op_resolver.AddReshape();
    micro_op_resolver.AddQuantize();
    micro_op_resolver.AddDequantize();
    micro_op_resolver.AddSoftmax();
    micro_op_resolver.AddAdd();

    static tflite::MicroInterpreter static_interpreter(
        model, micro_op_resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk)
    {
        Serial.println("AllocateTensors() failed");
        return;
    }

    model_input = interpreter->input(0);
    model_output = interpreter->output(0);

    initialized = true;
    Serial.println("TFLite setup done.");
}

float MLModel::runInference(float hr, float spo2, float body_temperature,
                            int age, float weight, float height, int gender)
{
    if (!initialized || model_input == nullptr)
    {
        Serial.println("TFLite not initialized.");
        return 0.0;
    }

    // Normalize inputs
    float hr_norm = (hr - modelNorm.hr_mean) / modelNorm.hr_std;
    float spo2_norm = (spo2 - modelNorm.spo2_mean) / modelNorm.spo2_std;
    float temp_norm = (body_temperature - modelNorm.body_temperature_mean) / modelNorm.body_temperature_std;
    float age_norm = (age - modelNorm.age_mean) / modelNorm.age_std;
    float weight_norm = (weight - modelNorm.weight_mean) / modelNorm.weight_std;
    float height_norm = (height - modelNorm.height_mean) / modelNorm.height_std;

    // Fill input tensor
    if (model_input->type == kTfLiteFloat32)
    {
        model_input->data.f[0] = hr_norm;
        model_input->data.f[1] = spo2_norm;
        model_input->data.f[2] = temp_norm;
        model_input->data.f[3] = age_norm;
        model_input->data.f[4] = weight_norm;
        model_input->data.f[5] = height_norm;
        model_input->data.f[6] = (float)gender;
    }
    else
    {
        Serial.println("Quantized model - implement dequantization");
        return 0.0;
    }

    // Run inference
    if (interpreter->Invoke() != kTfLiteOk)
    {
        Serial.println("Invoke failed!");
        return 0.0;
    }

    // Read output
    float score = 0.0;
    if (model_output->type == kTfLiteFloat32)
    {
        score = model_output->data.f[0];
    }

    inference_count++;
    Serial.printf("[ML] Inference #%d: Score=%.4f\n", inference_count, score);

    return score;
}

bool MLModel::isInitialized()
{
    return initialized;
}

int MLModel::getInferenceCount()
{
    return inference_count;
}
