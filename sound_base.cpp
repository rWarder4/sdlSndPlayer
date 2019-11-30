#include "sound_base.h"
#include "grynca_common.h"

namespace grynca {

    static int check_header(void* data, int size, char* str, int offset) {
        int len = (int)strlen(str);
        return (size >= offset + len) && !memcmp((char*)data + offset, str, len);
    }

    static char* find_subchunk(char* data, int len, char* id, int* size) {
        /* TODO : Error handling on malformed wav file */
        int idlen = (int)strlen(id);        
        char* p = data + 12;
    next:
        *size = *((u32*)(p + 4));
        if (memcmp(p, id, idlen)) {
            p += 8 + *size;
            if (p > data + len) return NULL;
            goto next;
        }
        return p + 8;
    }


    static bool read_wav(Wav * w, void* data, int len) {
        int sz;
        char* p = (char*)data;
        memset(w, 0, sizeof(*w));

        /* Check header */
        if (memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) {
            PERR("read_wav - bad wav header");
            return false;
        }
        /* Find fmt subchunk */
        p = find_subchunk((char*)data, len, "fmt", &sz);
        if (!p) {
            PERR("read_wav - no fmt subchunk");
            return false;
        }

        /* Load fmt info */
        const u16 format = *((u16*)(p));
        const u16 channels = *((u16*)(p + 2));
        const u32 samplerate = *((u32*)(p + 4));
        const u16 bitdepth = *((u16*)(p + 14));
        if (format != 1) {
            PERR("read_wav - unsupported format");
            return false;
        }
        if (channels == 0 || samplerate == 0 || bitdepth == 0) {
            PERR("read_wav - bad format");
            return false;
        }

        /* Find data subchunk */
        p = find_subchunk((char*)data, len, "data", &sz);
        if (!p) {
            PERR("read_wav - no data subchunk");
            return false;
        }

        /* Init struct */
        w->data_offset = len - sz;
        w->samplerate = samplerate;
        w->channels = channels;
        w->length = (sz / (bitdepth / 8)) / channels;
        w->bitdepth = bitdepth;
        /* Done */
        return true;
    }

    void Sound::clear() {
        sound_id = IID32; 
        length = IID32; 
        sample_rate = IID32; 
        type = IID8; 
        channels = IID8; 
        bitdepth = IID16; 
        udataSize = IID32; 
        udata_offset = 0; 
        udata = NULL;
    }


    bool SoundInfo::fillSoundInfo(Sound* snd) {
        switch(snd->type) {
            case SND_TP_OGG: {
                return fillOggSoundInfo(snd);
            }break;
            case SND_TP_WAV: {
                return fillWavSoundInfo(snd);
            }break;
        }
        NEVER_GET_HERE("Unknown sound type.\n");
        return false;
    }

    bool SoundInfo::fillOggSoundInfo(Sound* snd) {
        if (check_header(snd->udata, snd->udataSize, "OggS", 0)) {
            int err;
            stb_vorbis* ogg = stb_vorbis_open_memory((const unsigned char*)snd->udata, snd->udataSize, &err, NULL);
            if (!ogg) {
                PERR("ogg_init - invalid ogg data");
                return false;
            }

            stb_vorbis_info ogginfo = stb_vorbis_get_info(ogg);

            snd->sample_rate = ogginfo.sample_rate;
            snd->length = stb_vorbis_stream_length_in_samples(ogg);
            snd->channels = (u8)ogginfo.channels;
            snd->bitdepth = IID16;

            stb_vorbis_close(ogg);

            return true;
        }
        return false;
    }


    bool SoundInfo::fillWavSoundInfo(Sound* snd) {
        if (check_header(snd->udata, snd->udataSize, "WAVE", 8)) {
            Wav wav;
            if (!read_wav(&wav, snd->udata, snd->udataSize)) {
                return false;
            }

            if (wav.channels > 2 || (wav.bitdepth != 16 && wav.bitdepth != 8)) {
                PERR("wav init- unsupported wav format");
                return false;
            }

            snd->channels = (u8)wav.channels;
            snd->bitdepth = (u16)wav.bitdepth;
            snd->sample_rate = wav.samplerate;
            snd->length = wav.length;
            snd->udata_offset = wav.data_offset;

            return true;
        }
        return false;
    }

}
