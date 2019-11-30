#ifndef SOUND_BASE_H
#define SOUND_BASE_H

#include "../3rdp/libstbvorbis.h"

namespace grynca {

    enum {
        SND_TP_OGG,
        SND_TP_WAV
    };

    typedef struct {
        int data_offset;
        int bitdepth;
        int samplerate;
        int channels;
        int length;
    } Wav;

    struct Stream {
        Stream() : type_id(IID8){}

        u8 type_id;
        void* data;           // data from asset manager, do not alloc/free them
        union {
            struct {
                u8 channels;
                u16 bitdepth;
                u32 idx;
                u32 samplerate;
                u32 length;
            } wav;
            struct {
                stb_vorbis* vorbis;
            } ogg;
        };
    };

    typedef struct {
        u32 type;
        Stream* udata;
        const char* msg;
        i16* buffer;
        u32 length;
    } cm_Event;

    typedef void (*cm_EventHandler)(cm_Event* e);

    struct Sound {
        Sound() 
            : sound_id(IID32), length(IID32), sample_rate(IID32), type(IID8), channels(IID8), bitdepth(IID16), udataSize(IID32), udata_offset(0), udata(NULL) {}

        void clear();

        u32 sound_id;
        u32 length;
        u32 sample_rate;
        u8 type;
        u8 channels;
        u16 bitdepth;
        u32 udataSize;
        u32 udata_offset;
        void* udata;
    };

    class SoundInfo {
    public:
        static bool fillSoundInfo(Sound* snd);

        static bool fillOggSoundInfo(Sound* snd);
        static bool fillWavSoundInfo(Sound* snd);
    };

}

#endif //SOUND_BASE_H

#if !defined(SOUND_BASE_IMPL) && defined(GENG_GAME_IMPL)
#define SOUND_BASE_IMPL
#include "sound_base.cpp"
#endif //THE_GAME_IMPL