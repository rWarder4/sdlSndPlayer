#ifndef SOUND_PLAYER_H
#define SOUND_PLAYER_H

#include "sound_base.h"
#include "../graphics2D/assets.h"

namespace grynca {

#define MIXER_BUFFER_SIZE (512)
#define BASE_AUDIO_FREQUENCY 44100
    
    enum {
        CM_STATE_STOPPED,
        CM_STATE_PLAYING,
        CM_STATE_PAUSED
    };

    enum {
        CM_EVENT_LOCK,
        CM_EVENT_UNLOCK,
        CM_EVENT_DESTROY,
        CM_EVENT_SAMPLES,
        CM_EVENT_REWIND
    };

    struct SoundConfig {
        bool loop;
        double gain;
        u32 sample_rate;
    };


    // FW
    class SoundInstance;
    class SoundPlayer;

    class SoundManager : public Manager<SoundInstance> {
    public:
        void init();
        SoundInstance* getSound(const Sound* snd, const SoundConfig& config);

        Bits& accPlayingSounds() { return playing_sounds_; }

        void clear();

    protected:
        friend class SoundPlayer;
        void stopInstance_(SoundInstance* inst);

    private:
        SoundInstance* tryReuseSound_(u32 sound_id);

        Bits playing_sounds_;
    };

    class SoundInstance : public Item<SoundManager> {
    public:
        SoundInstance(Manager* mgr, Index id) : Base(mgr, id), state(CM_STATE_STOPPED) {}
        ~SoundInstance() {}

        void init(const Sound* snd, u32 mixer_sample_rate, bool looped);
        bool oggInit(const Sound* snd);
        void wavInit(const Sound* snd);


    private:
        friend class SoundPlayer;
        friend class SoundManager;

        void recalc_source_gains();
        void set_gain(double gain);
        void set_pan(double pan);
        void set_pitch(double pitch, u32 mixer_sample_rate);

        i16 buffer[MIXER_BUFFER_SIZE];
        cm_EventHandler handler;
        u32 sample_rate;                    /* Stream's native sample_rate */
        u32 length;                         /* Stream's length in frames */
        u32 end;                            /* End index for the current play-through */
        u32 state;                          /* Current state (playing|paused|stopped) */
        u64 position;                       /* Current playhead position */
        u32 lgain, rgain;
        u32 rate;
        u32 nextfill;
        u32 sound_id;
        u8 loop;
        u8 rewind;
        Stream stream;
        double gain;
        double pan;

        REFLECTED();
    };
    REFLECTION_FW(SoundInstance);

    /// ////////////////////// ///
    //  ------- PLAYER -------  //
    /// ////////////////////// ///

    class SoundPlayer {
    public:
        SoundPlayer(AssetsManager* assets);
        ~SoundPlayer();

        bool init();
        void startDevice();
        void pauseDevice();
        void deinit();
        void clear();

        SoundInstance* play(u32 snd_id, bool looped = false, double gain = 1.0);
        void stop(Index& index);

        void clearSoundInstances();

        void setMasterGain(double gain);

        const SoundManager* getSoundManager() const { return &snd_instances_; }
        SoundManager& accSoundManager() { return snd_instances_; }
    private:
        static void audioCallback_(void* ctx, Uint8* stream, int len);
        static void fillSourceBuffer_(SoundInstance* src, u32 offset, u32 length);

        void fillNextSoundSamplesRec_(i16* dst, u32 len);
        void addSoundSourceToBuffer_(SoundInstance* src, u32 len);
        void rewindSource_(SoundInstance* src);

        SDL_AudioDeviceID device_id;
        AssetsManager* assets_;

        SoundManager snd_instances_;

        volatile u32 inst_lock_;
        // mixer
        i32 buffer_[MIXER_BUFFER_SIZE];
        u32 samplerate_;
        i32 gain_;
    };
}
#endif /*SOUND_PLAYER_H*/

#if !defined(SOUND_PLAYER_IMPL) && defined(GENG_GAME_IMPL)
#define SOUND_PLAYER_IMPL
#include "sound_player.cpp"
#endif //SOUND_PLAYER_IMPL