#include "Audio.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mgh {

namespace {
constexpr float kTwoPi = 6.28318530718f;
float noteHz(int midi) { return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f); }
}  // namespace

SynthAudio::SynthAudio() = default;
SynthAudio::~SynthAudio() { closeDevice(); }

void SynthAudio::ensureDevice() {
    if (stream_) return;
    SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SynthAudio::callback, this);
    if (stream_) SDL_ResumeAudioStreamDevice(stream_);
}

void SynthAudio::closeDevice() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

// The device only exists while something can be heard: no CPU/battery use otherwise.
void SynthAudio::updateDeviceState() {
    const bool needed = !paused_ && (sound_ || (music_ && musicPlaying_));
    if (needed) {
        ensureDevice();
        if (stream_) SDL_ResumeAudioStreamDevice(stream_);
    } else if (stream_) {
        if (paused_) SDL_PauseAudioStreamDevice(stream_);
        else closeDevice();
    }
}

void SynthAudio::setSoundEnabled(bool on) {
    sound_ = on;
    updateDeviceState();
}
void SynthAudio::setMusicEnabled(bool on) {
    if (stream_) SDL_LockAudioStream(stream_);
    music_ = on;
    if (stream_) SDL_UnlockAudioStream(stream_);
    updateDeviceState();
}
void SynthAudio::setMusicPlaying(bool playing) {
    if (stream_) SDL_LockAudioStream(stream_);
    musicPlaying_ = playing;
    if (stream_) SDL_UnlockAudioStream(stream_);
    updateDeviceState();
}
void SynthAudio::pauseAll(bool paused) {
    paused_ = paused;
    updateDeviceState();
}

void SynthAudio::addVoice(const Voice& v) {
    for (auto& slot : voices_) {
        if (!slot.active) {
            slot = v;
            slot.active = true;
            return;
        }
    }
    // Steal the oldest voice.
    Voice* oldest = &voices_[0];
    for (auto& slot : voices_)
        if (slot.t > oldest->t) oldest = &slot;
    *oldest = v;
    oldest->active = true;
}

void SynthAudio::play(Sfx s, float volume, float pitch) {
    if (!sound_ || paused_) return;
    ensureDevice();
    if (!stream_) return;
    std::vector<Voice> vs;
    auto tone = [&](Wave w, float f0, float f1, float dur, float vol, float delay = 0, float attack = 0.004f) {
        Voice v;
        v.wave = w;
        v.freq = f0 * pitch;
        v.freqEnd = f1 * pitch;
        v.dur = dur;
        v.vol = vol * volume;
        v.delay = delay;
        v.attack = attack;
        vs.push_back(v);
    };
    switch (s) {
        case Sfx::Click: tone(Wave::Sine, 900, 700, 0.045f, 0.25f); break;
        case Sfx::Move:
            tone(Wave::Noise, 2200, 2200, 0.05f, 0.25f);
            tone(Wave::Sine, 220, 150, 0.08f, 0.45f);
            break;
        case Sfx::Capture:
            tone(Wave::Noise, 3000, 3000, 0.06f, 0.3f);
            tone(Wave::Sine, 260, 120, 0.12f, 0.55f);
            tone(Wave::Sine, 180, 100, 0.1f, 0.35f, 0.05f);
            break;
        case Sfx::Check:
            tone(Wave::Triangle, 660, 660, 0.1f, 0.4f);
            tone(Wave::Triangle, 880, 880, 0.16f, 0.4f, 0.09f);
            break;
        case Sfx::GameEnd:
        case Sfx::Win:
            for (int i = 0; i < 4; ++i) {
                static const int kArp[4] = {0, 4, 7, 12};
                const float f = noteHz(72 + kArp[i]);
                tone(Wave::Triangle, f, f, i == 3 ? 0.45f : 0.14f, 0.38f, i * 0.11f);
            }
            break;
        case Sfx::PaddleHit: tone(Wave::Square, 330, 260, 0.06f, 0.18f); tone(Wave::Noise, 4000, 4000, 0.03f, 0.2f); break;
        case Sfx::WallHit: tone(Wave::Sine, 190, 150, 0.06f, 0.35f); break;
        case Sfx::Goal:
            for (int i = 0; i < 3; ++i) tone(Wave::Square, noteHz(67 + i * 5), noteHz(67 + i * 5), 0.12f, 0.16f, i * 0.09f);
            tone(Wave::Noise, 1500, 1500, 0.35f, 0.12f, 0.0f, 0.05f);
            break;
        case Sfx::Countdown: tone(Wave::Sine, 600, 600, 0.12f, 0.35f); break;
        case Sfx::Go: tone(Wave::Sine, 1200, 1200, 0.25f, 0.35f); break;
        case Sfx::Eat: tone(Wave::Sine, 520, 880, 0.08f, 0.35f); break;
        case Sfx::Die:
            tone(Wave::Square, 420, 90, 0.45f, 0.18f);
            tone(Wave::Noise, 800, 800, 0.25f, 0.15f);
            break;
        case Sfx::Error: tone(Wave::Square, 200, 180, 0.12f, 0.15f); break;
        case Sfx::Count: break;
    }
    SDL_LockAudioStream(stream_);
    for (auto& v : vs) addVoice(v);
    SDL_UnlockAudioStream(stream_);
}

void SDLCALL SynthAudio::callback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<SynthAudio*>(user);
    int frames = additional / static_cast<int>(sizeof(float));
    float buf[512];
    while (frames > 0) {
        int n = std::min(frames, 512);
        self->render(buf, n);
        SDL_PutAudioStreamData(stream, buf, n * static_cast<int>(sizeof(float)));
        frames -= n;
    }
}

float SynthAudio::musicSample() {
    // A calm generative arpeggio (A minor pentatonic) with a soft bass.
    const double beat = 60.0 / 92.0 / 2.0;  // eighth notes at 92 BPM
    static const int prog[4][3] = {{57, 60, 64}, {53, 57, 60}, {48, 52, 55}, {55, 59, 62}};
    static const int pattern[8] = {0, 1, 2, 1, 0, 2, 1, 2};
    const double t = musicTime_;
    const int step = static_cast<int>(t / beat);
    const int bar = (step / 8) % 4;
    const float within = static_cast<float>(std::fmod(t, beat) / beat);
    const int note = prog[bar][pattern[step % 8]] + 12;
    const float env = std::exp(-within * 3.5f) * std::min(1.0f, within * 40.0f);
    const float f = noteHz(note);
    musicPhase_[0] += f / kRate;
    if (musicPhase_[0] > 1) musicPhase_[0] -= 1;
    float tri = 4.0f * std::fabs(musicPhase_[0] - 0.5f) - 1.0f;
    const float fb = noteHz(prog[bar][0] - 12);
    musicPhase_[1] += fb / kRate;
    if (musicPhase_[1] > 1) musicPhase_[1] -= 1;
    float bass = std::sin(musicPhase_[1] * kTwoPi);
    return tri * env * 0.07f + bass * 0.05f;
}

void SynthAudio::render(float* out, int frames) {
    const float dt = 1.0f / kRate;
    const bool musicOn = music_ && musicPlaying_;
    for (int i = 0; i < frames; ++i) {
        float s = 0;
        for (auto& v : voices_) {
            if (!v.active) continue;
            if (v.delay > 0) {
                v.delay -= dt;
                continue;
            }
            float u = v.t / v.dur;
            if (u >= 1) {
                v.active = false;
                continue;
            }
            float f = v.freq + (v.freqEnd - v.freq) * u;
            v.phase += f * dt;
            if (v.phase > 1) v.phase -= std::floor(v.phase);
            float x = 0;
            switch (v.wave) {
                case Wave::Sine: x = std::sin(v.phase * kTwoPi); break;
                case Wave::Square: x = v.phase < 0.5f ? 0.6f : -0.6f; break;
                case Wave::Triangle: x = 4.0f * std::fabs(v.phase - 0.5f) - 1.0f; break;
                case Wave::Noise: {
                    v.noise ^= v.noise << 13;
                    v.noise ^= v.noise >> 17;
                    v.noise ^= v.noise << 5;
                    float n = (v.noise / 4294967295.0f) * 2.0f - 1.0f;
                    float a = std::min(1.0f, f / kRate * kTwoPi);
                    v.lp += (n - v.lp) * a;
                    x = v.lp * 1.5f;
                    break;
                }
            }
            float env = std::min(1.0f, v.t / v.attack) * (1.0f - u) * (1.0f - u);
            s += x * env * v.vol;
            v.t += dt;
        }
        // Fade music in/out smoothly.
        float target = musicOn ? 1.0f : 0.0f;
        musicGain_ += (target - musicGain_) * 0.0005f;
        if (musicGain_ > 0.001f) {
            s += musicSample() * musicGain_;
            musicTime_ += dt;
        }
        out[i] = std::tanh(s * 0.9f);  // soft clip
    }
}

}  // namespace mgh
