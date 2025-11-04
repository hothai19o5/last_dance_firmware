#pragma once

// Model normalization parameters
struct ModelNormalization
{
  float hr_mean = 79.53374663;
  float hr_std = 11.55286498;
  float spo2_mean = 97.50437243;
  float spo2_std = 1.44259433;
  float bmi_mean = 25.003625;
  float bmi_std = 6.447143;
};

class MLModel
{
public:
  MLModel();
  void setup();
  float runInference(float hr, float spo2, float bmi);
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
