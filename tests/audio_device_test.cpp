// End-to-end check of the platform audio device: the device thread must
// pull the mixer so a voice plays to completion. Uses ALSA's "null" sink so
// it runs without sound hardware; skips if libasound isn't installed.

#include <ion/audio/Audio.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

int main() {
    setenv("ION_AUDIO_DEVICE", "null", 1);
    ion::Audio audio;
    if (!audio.initialize()) {
        std::printf("audio_device_test skipped (no ALSA)\n");
        return 0;
    }
    int failures = 0;
    if (!audio.hasDevice()) {
        std::printf("FAIL: initialize() succeeded without a device\n");
        failures++;
    }

    ion::Sound tone = ion::makeSound(std::vector<float>(4800, 0.25f), 1, 48000);
    ion::VoiceId voice = audio.play(tone);
    auto start = std::chrono::steady_clock::now();
    while (audio.mixer().isPlaying(voice) &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (audio.mixer().isPlaying(voice)) {
        std::printf("FAIL: device never pulled the mixer\n");
        failures++;
    }

    // Restarting and shutting down repeatedly must not hang or leak threads.
    for (int i = 0; i < 3; i++) {
        audio.shutdown();
        if (!audio.initialize()) {
            std::printf("FAIL: re-initialize %d\n", i);
            failures++;
        }
    }
    audio.shutdown();
    if (audio.hasDevice()) {
        std::printf("FAIL: device still running after shutdown\n");
        failures++;
    }

    if (failures == 0) {
        std::printf("audio_device_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
