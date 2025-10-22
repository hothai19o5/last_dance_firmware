#pragma once

// Model normalization parameters
struct ModelNormalization
{
  float hr_mean = 79.53374663;
  float hr_std = 11.55286498;
  float spo2_mean = 97.50437243;
  float spo2_std = 1.44259433;
  float body_temperature_mean = 36.74835291;
  float body_temperature_std = 0.43328918;
  float age_mean = 53.44627537;
  float age_std = 20.78674961;
  float weight_mean = 74.99641903;
  float weight_std = 14.4714659;
  float height_mean = 1.75003102;
  float height_std = 0.14455348;
};

class MLModel
{
public:
  MLModel();
  void setup();
  float runInference(float hr, float spo2, float body_temperature,
                     int age, float weight, float height, int gender);
  bool isInitialized();
  int getInferenceCount();

private:
  ModelNormalization modelNorm;
  int inference_count;
  bool initialized;
};

// Model data reference
extern const unsigned char g_vital_signs_model_quantized_tflite[];
extern const int g_vital_signs_model_quantized_tflite_len;
