#include "sound_player.h"

namespace grynca {

#define FX_BITS           (12)
#define FX_UNIT           (1 << FX_BITS)
#define FX_MASK           (FX_UNIT - 1)
#define FX_FROM_FLOAT(f)  ((f) * FX_UNIT)
#define FX_LERP(a, b, p)  ((a) + ((((b) - (a)) * (p)) >> FX_BITS))
#define MIXER_BUFFER_MASK       (MIXER_BUFFER_SIZE - 1)

    void SoundPlayer::fillSourceBuffer_(SoundInstance* src, u32 offset, u32 length) {
        cm_Event e;
        e.type = CM_EVENT_SAMPLES;
        e.udata = &src->stream;
        e.buffer = src->buffer + offset;
        e.length = length;
        src->handler(&e);
    }

    void SoundInstance::recalc_source_gains() {
        double l, r;
        double pan_backup = pan;
        l = gain * (pan_backup <= 0. ? 1. : 1. - pan_backup);
        r = gain * (pan_backup >= 0. ? 1. : 1. + pan_backup);
        lgain = (u32)FX_FROM_FLOAT(l);
        rgain = (u32)FX_FROM_FLOAT(r);
    }

    /*============================================================================
    ** Wav stream
    **============================================================================*/
#define WAV_PROCESS_LOOP(X) \
  while (n--) {             \
    X                       \
    dst += 2;               \
    s->wav.idx++;               \
  }

    static void wav_handler(cm_Event * e) {
        Stream* s = (Stream*)e->udata;
        u32 n;

        switch (e->type) {
            case CM_EVENT_DESTROY: {
                free(s->data);
                free(s);
            }break;
            case CM_EVENT_SAMPLES: {
                i16* dst = e->buffer;
                u32 len = e->length / 2;
            fill:
                n = min(len, s->wav.length - s->wav.idx);
                len -= n;
                if (s->wav.bitdepth == 16 && s->wav.channels == 1) {
                    WAV_PROCESS_LOOP({
                      dst[0] = dst[1] = ((i16*)s->data)[s->wav.idx];
                        });
                }
                else if (s->wav.bitdepth == 16 && s->wav.channels == 2) {
                    WAV_PROCESS_LOOP({
                      u32 x = s->wav.idx * 2;
                      dst[0] = ((i16*)s->data)[x];
                      dst[1] = ((i16*)s->data)[x + 1];
                        });
                }
                else if (s->wav.bitdepth == 8 && s->wav.channels == 1) {
                    WAV_PROCESS_LOOP({
                      dst[0] = dst[1] = (((u8*)s->data)[s->wav.idx] - 128) << 8;
                        });
                }
                else if (s->wav.bitdepth == 8 && s->wav.channels == 2) {
                    WAV_PROCESS_LOOP({
                      u32 x = s->wav.idx * 2;
                      dst[0] = (((u8*)s->data)[x] - 128) << 8;
                      dst[1] = (((u8*)s->data)[x + 1] - 128) << 8;
                        });
                }
                /* Loop back and continue filling buffer if we didn't fill the buffer */
                if (len > 0) {
                    s->wav.idx = 0;
                    goto fill;
                }
            }break;
            case CM_EVENT_REWIND: {
                s->wav.idx = 0;
            }break;
        }
    }

    /*============================================================================
    ** Ogg stream
    **============================================================================*/
    static void ogg_handler(cm_Event * e) {
        int n, len;
        Stream* s = (Stream*)e->udata;
        i16* buf;

        switch (e->type) {
            case CM_EVENT_DESTROY: {
                stb_vorbis_close(s->ogg.vorbis);
                free(s->data);
                free(s);
            }break;
            case CM_EVENT_SAMPLES: {
                len = e->length;
                buf = e->buffer;
            fill:
                n = stb_vorbis_get_samples_short_interleaved(s->ogg.vorbis, 2, buf, len);
                n *= 2;
                // rewind and fill remaining buffer if we reached the end of the ogg before filling it
                if (len != n) {
                    stb_vorbis_seek_start(s->ogg.vorbis);
                    buf += n;
                    len -= n;
                    goto fill;
                }
            }break;
            case CM_EVENT_REWIND: {
                stb_vorbis_seek_start(s->ogg.vorbis);
            }break;
        }
    }


    /// Reflection
    REFLECTION_BEGIN_TID(SoundInstance);
        REF_BASE_ITEM();
    REFLECTION_END();

    void SoundManager::init() {
        initItemType(tidSoundInstance);
    }


    SoundInstance* SoundManager::getSound(const Sound* snd, const SoundConfig& config) {
        SoundInstance* sinst = tryReuseSound_(snd->sound_id);
        if (sinst) {
            sinst->rewind = 1;
            sinst->loop = config.loop;
            sinst->set_gain(config.gain);
            sinst->state = CM_STATE_PLAYING;
        }
        else {
            sinst = addItem();
            sinst->init(snd, config.sample_rate, config.loop);
            switch (snd->type) {
                case SND_TP_OGG: {
                    if (!sinst->oggInit(snd)) {
                        removeItem(sinst->getIndex());
                        return NULL;
                    }
                }break;
                case SND_TP_WAV: {
                    sinst->wavInit(snd);
                }break;
            }
        }

        u32 inst_pos = sinst->getIndex().index;
        playing_sounds_.set(inst_pos);
        return sinst;
    }

    void SoundManager::clear() {
        playing_sounds_.clear();

        Manager::clear();
    }

    void SoundManager::stopInstance_(SoundInstance* inst) {
        inst->state = CM_STATE_STOPPED;
        playing_sounds_.reset(inst->getIndex().index);
    }

    SoundInstance* SoundManager::tryReuseSound_(u32 sound_id) {
        LOOP_UNSET_BITS(playing_sounds_, it) {
            SoundInstance* sinst = accItemAtPos(it.getPos());
            if (sinst->sound_id == sound_id) {
                return sinst;
            }
        }
        return NULL;
    }

    /// ///////////////////////////// ///
    //  ------- SoundInstance -------  //
    /// ///////////////////////////// ///
    void SoundInstance::init(const Sound* snd, u32 mixer_sample_rate, bool looped) {
        length = snd->length;
        sample_rate = snd->sample_rate;
        sound_id = snd->sound_id;
        set_gain(1);
        set_pan(0);
        set_pitch(1, mixer_sample_rate);
        state = CM_STATE_STOPPED;
        rewind = 1;
        loop = looped;
    }

    bool SoundInstance::oggInit(const Sound* snd) {
        int err;
        stream.data = snd->udata;
        stream.ogg.vorbis = stb_vorbis_open_memory((unsigned char*)snd->udata, snd->udataSize, &err, NULL);
        if (!stream.ogg.vorbis) {
            PERR("SoundInstance::oggInit - invalid ogg data.\n");
            return false;
        }
        handler = ogg_handler;
        stream.type_id = snd->type;
        return true;
    }

    void SoundInstance::wavInit(const Sound* snd) {
        stream.data = (u8*)snd->udata + snd->udata_offset;
        stream.wav.idx = 0;
        stream.wav.bitdepth = snd->bitdepth;
        stream.wav.channels = snd->channels;
        stream.wav.samplerate = snd->sample_rate;
        stream.wav.length = snd->length;
        handler = wav_handler;
    }

    void SoundInstance::set_gain(double g) {
        gain = g;
        recalc_source_gains();
    }

    void SoundInstance::set_pan(double new_pan) {
        pan = clampToRange(new_pan, -1.0, 1.0);
        recalc_source_gains();
    }

    void SoundInstance::set_pitch(double pitch, u32 mixer_sample_rate) {
        double new_rate;
        if (pitch > 0.) {
            new_rate = sample_rate / (double)mixer_sample_rate * pitch;
        }
        else {
            new_rate = 0.001;
        }
        rate = (u32)FX_FROM_FLOAT(new_rate);
    }


    /// ///////////////////////////// ///
    //  -------- SoundPlayer --------  //
    /// ///////////////////////////// ///
    SoundPlayer::SoundPlayer(AssetsManager* assets)
        : device_id(IID32), assets_(assets), inst_lock_(0) {}

    SoundPlayer::~SoundPlayer() {
        clear();
    }

    bool SoundPlayer::init() {
        i32 rslt = CALL_SDL(SDL_WasInit(SDL_INIT_AUDIO));
        if (rslt == 0) {
            rslt = CALL_SDL(SDL_InitSubSystem(SDL_INIT_AUDIO));
            if (rslt == -1) {
                PERR("ERROR: [SoundPlayer::init] Could not initialize Audio Subsystem.\n");
                return false;
            }
        }
        SDL_AudioSpec spec;
        spec.freq = (int)BASE_AUDIO_FREQUENCY;
        spec.samples = MIXER_BUFFER_SIZE;
        spec.format = AUDIO_S16;
        spec.channels = 2;
        spec.callback = audioCallback_;
        spec.userdata = this;
        spec.silence = 0;

        SDL_AudioSpec obtained;
        device_id = CALL_SDL(SDL_OpenAudioDevice(NULL, 0, &spec, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE));
        if (device_id == 0) {
            device_id = IID32;
            const char* error = CALL_SDL(SDL_GetError());
            PERR("SoundPlayer::play(): Could not play sound: %s\n", error);
            return false;
        }

        // mixer init
        snd_instances_.init();
        samplerate_ = obtained.freq;
        gain_ = FX_UNIT;

        // must be called as last item in init
        CALL_SDL(SDL_PauseAudioDevice(device_id, 0));

        return true;
    }

    void SoundPlayer::startDevice() {
        ASSERT(device_id != IID32);
        CALL_SDL(SDL_PauseAudioDevice(device_id, 0));
    }

    void SoundPlayer::pauseDevice() {
        ASSERT(device_id != IID32);
        CALL_SDL(SDL_PauseAudioDevice(device_id, 1));
    }

    void SoundPlayer::deinit() {
        ASSERT(device_id != IID32);
        // lock to wait if callback running
        CALL_SDL(SDL_LockAudioDevice(device_id));
        CALL_SDL(SDL_CloseAudioDevice(device_id));
        // unlock cannot be called, because the device_id is not valid after close and lock has been freed
    }

    void SoundPlayer::clear() {
        pauseDevice();
        clearSoundInstances();
    }

    SoundInstance* SoundPlayer::play(u32 snd_id, bool looped, double gain) {
        const Sound* snd = assets_->accSound(snd_id);
        const SoundConfig snd_cfg = { looped, gain, samplerate_ };
        atomicSpinLock(&inst_lock_);
        SoundInstance* snd_inst = snd_instances_.getSound(snd, snd_cfg);
        snd_inst->state = CM_STATE_PLAYING;
        atomicSpinUnlock(&inst_lock_);
        return snd_inst;
    }

    void SoundPlayer::stop(Index &index) {
        if(snd_instances_.isValidIndex(index)) {
            atomicSpinLock(&inst_lock_);
            SoundInstance* inst = snd_instances_.accItem(index);
            snd_instances_.stopInstance_(inst);
            atomicSpinUnlock(&inst_lock_);
        }
    }

    void SoundPlayer::clearSoundInstances() {
        // turn all of them off first because they are used in callback
        for (u32 i = 0; i < snd_instances_.getSize(); ++i) {
            SoundInstance* snd_inst = snd_instances_.tryAccItemAtPos(i);
            if(snd_inst != NULL)
                snd_inst->state = CM_STATE_STOPPED;
        }

        atomicSpinLock(&inst_lock_);
        for (u32 i = 0; i < snd_instances_.getSize(); ++i) {
            SoundInstance* snd_inst = snd_instances_.tryAccItemAtPos(i);
            if (snd_inst != NULL && snd_inst->stream.type_id == SND_TP_OGG) {
                stb_vorbis_close(snd_inst->stream.ogg.vorbis);
            }
        }
        snd_instances_.clear();
        atomicSpinUnlock(&inst_lock_);
    }

    void SoundPlayer::setMasterGain(double gain) {
        gain_ = (i32)FX_FROM_FLOAT(gain);
    }


    void SoundPlayer::rewindSource_(SoundInstance * src) {
        cm_Event e;
        e.type = CM_EVENT_REWIND;
        e.udata = &src->stream;
        src->handler(&e);
        src->position = 0;
        src->rewind = 0;
        src->end = src->length;
        src->nextfill = 0;
    }


    void SoundPlayer::addSoundSourceToBuffer_(SoundInstance* src, u32 len) {
        i32* dst = buffer_;

        if (src->state != CM_STATE_PLAYING) {
            return;
        }
        if (src->rewind) {
            rewindSource_(src);
        }

        while (len > 0) {
            u32 frame = (u32)(src->position >> FX_BITS);

            if (frame + 3 >= src->nextfill) {
                fillSourceBuffer_(src, (src->nextfill * 2) & MIXER_BUFFER_MASK, MIXER_BUFFER_SIZE / 2);
                src->nextfill += MIXER_BUFFER_SIZE / 4;
            }

            if (frame >= src->end) {
                src->end = frame + src->length;
                if (!src->loop) {
                    snd_instances_.stopInstance_(src);
                    break;
                }
            }

            u32 n = min(src->nextfill - 2, src->end) - frame;
            u32 count = (n << FX_BITS) / src->rate;
            count = max(count, (u32)1);
            count = min(count, len / 2);
            len -= count * 2;

            if (src->rate == FX_UNIT) {
                n = frame * 2;
                for (u32 i = 0; i < count; i++) {
                    dst[0] += (src->buffer[(n)& MIXER_BUFFER_MASK] * src->lgain) >> FX_BITS;
                    dst[1] += (src->buffer[(n + 1) & MIXER_BUFFER_MASK] * src->rgain) >> FX_BITS;
                    n += 2;
                    dst += 2;
                }
                src->position += count * FX_UNIT;

            }
            else {
                // Add audio to buffer -- interpolated
                for (u32 i = 0; i < count; i++) {
                    n = (u32)((src->position >> FX_BITS) * 2);
                    u32 p = src->position & FX_MASK;
                    u32 a = src->buffer[(n)& MIXER_BUFFER_MASK];
                    u32 b = src->buffer[(n + 2) & MIXER_BUFFER_MASK];
                    dst[0] += (FX_LERP(a, b, p) * src->lgain) >> FX_BITS;
                    n++;
                    a = src->buffer[(n)& MIXER_BUFFER_MASK];
                    b = src->buffer[(n + 2) & MIXER_BUFFER_MASK];
                    dst[1] += (FX_LERP(a, b, p) * src->rgain) >> FX_BITS;
                    src->position += src->rate;
                    dst += 2;
                }
            }
        }
    }


    void SoundPlayer::audioCallback_(void* ctx, Uint8* stream, int len) {
    // static
        SoundPlayer* sndPlayer = (SoundPlayer*)ctx;
        sndPlayer->fillNextSoundSamplesRec_((i16*)stream, len / 2);
    }

    void SoundPlayer::fillNextSoundSamplesRec_(i16 * dst, u32 len) {
        while (len > MIXER_BUFFER_SIZE) {
            fillNextSoundSamplesRec_(dst, MIXER_BUFFER_SIZE);
            dst += MIXER_BUFFER_SIZE;
            len -= MIXER_BUFFER_SIZE;
        }

        memset(buffer_, 0, len * sizeof(buffer_[0]));

        // loop over all active sources
        atomicSpinLock(&inst_lock_);
        LOOP_SET_BITS(snd_instances_.playing_sounds_, it) {
            SoundInstance* s = snd_instances_.accItemAtPos(it.getPos());
            addSoundSourceToBuffer_(s, len);
        }
        atomicSpinUnlock(&inst_lock_);

        // copy internal buffer to destination and clamp
        for (u32 i = 0; i < len; i++) {
            int x = (buffer_[i] * gain_) >> FX_BITS;
            dst[i] = (i16)(clampToRange(x, -32768, 32767));
        }
    }
}

#undef FX_BITS
#undef FX_UNIT
#undef FX_MASK
#undef FX_FROM_FLOAT
#undef FX_LERP
#undef MIXER_BUFFER_MASK