// Procedural audio: every sound effect and the menu music are synthesised at
// runtime, so the APK contains no audio files at all.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <atomic>

namespace mgh {

enum class Sfx {
    Click, Move, Capture, Check, GameEnd, PaddleHit, WallHit, Goal, Countdown, Go, Eat, Die, Win, Error, Count
};

class IAudio {
public:
    virtual ~IAudio() = default;
    virtual void play(Sfx s, float volume = 1.0f, float pitch = 1.0f) = 0;
    virtual void setSoundEnabled(bool on) = 0;
    virtual void setMusicEnabled(bool on) = 0;
    virtual void setMusicPlaying(bool playing) = 0;  // e.g. off while a game is running
    virtual void pauseAll(bool paused) = 0;          // app lifecycle
};

class SynthAudio : public IAudio {
public:
    SynthAudio();
    ~SynthAudio() override;

    void play(Sfx s, float volume, float pitch) override;
    void setSoundEnabled(bool on) override;
    void setMusicEnabled(bool on) override;
    void setMusicPlaying(bool playing) override;
    void pauseAll(bool paused) override;

private:
    enum class Wave : uint8_t { Sine, Square, Triangle, Noise };
    struct Voice {
        bool active = false;
        Wave wave = Wave::Sine;
        float freq = 440, freqEnd = 440;
        float phase = 0;
        float t = 0, dur = 0.1f;
        float attack = 0.004f;
        float vol = 0.5f;
        float delay = 0;  // seconds before the voice starts
        uint32_t noise = 22222;
        float lp = 0;  // one-pole low-pass state for noise
    };

    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void render(float* out, int frames);
    void addVoice(const Voice& v);
    void ensureDevice();
    void closeDevice();
    void updateDeviceState();
    float musicSample();

    SDL_AudioStream* stream_ = nullptr;
    static constexpr int kRate = 44100;
    std::array<Voice, 24> voices_{};
    bool sound_ = true, music_ = true, musicPlaying_ = true, paused_ = false;
    // Music sequencer state (audio thread).
    double musicTime_ = 0;
    float musicPhase_[3] = {0, 0, 0};
    float musicGain_ = 0;
};

}  // namespace mgh
