#pragma once

#include <cmath>
#include <cstdint>

namespace niteraid::audio_internals {

// Sampled square-wave approximation, not a full PIT/speaker circuit model.
class PcSpeakerOscillator {
public:
    void program(std::uint16_t divisor)
    {
        // 2156:0231 skips PIT writes when the divisor is unchanged. Repeated
        // service words extend a tone; they must not restart its carrier.
        if (divisor != divisor_) {
            divisor_ = divisor;
            phase_ = 0.0f;
        }
    }

    float sample()
    {
        if (divisor_ == 0) return 0.0f;
        const float output = phase_ < 0.5f ? 1.0f : -1.0f;
        phase_ += (1'193'182.0f / static_cast<float>(divisor_)) / 44100.0f;
        phase_ -= std::floor(phase_);
        return output;
    }

private:
    std::uint16_t divisor_ = 0;
    float phase_ = 0.0f;
};

}  // namespace niteraid::audio_internals
