// Converts a console's native-rate int32 stereo mix to 48 kHz int16 output: an optional low-pass (the console's
// output filter), DC blocking, cubic resampling with dynamic rate control, volume and soft limiting.
#pragma once
#include <cstdint>
#include <vector>

class AudioMixer {
public:
    // cutoff_hz: the low-pass corner (0 = none); gain: the console's mix to the output's scale, times the volume.
    void configure(double in_rate, double out_rate, double cutoff_hz, float gain);
    void reset();
    // `adjust` > 1 produces slightly more output samples (used to refill the device queue).
    void process(const int32_t* in, int frames, double adjust, std::vector<int16_t>& out);

private:
    double in_rate_ = 53267.0, out_rate_ = 48000.0;
    float  gain_ = 0.8f;
    float  lp_coef_ = 0.0f;       // one-pole low-pass coefficient (0 = off)
    float  lp_state_[2] = {0, 0};
    float  dc_x_[2] = {0, 0}, dc_y_[2] = {0, 0};
    float  hist_[4][2] = {};
    double frac_ = 0.0;
};
