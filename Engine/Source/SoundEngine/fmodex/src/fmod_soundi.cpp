#include "fmod_settings.h"

#include "fmod_async.h"
#include "fmod_codeci.h"
#include "fmod_localcriticalsection.h"
#include "fmod_soundi.h"
#include "fmod_sound_sample.h"
#include "fmod_sound_stream.h"
#include "fmod_systemi.h"
#include "fmod_stringw.h"

namespace FMOD
{


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT SoundI::validate(Sound *sound, SoundI **soundi)
{
    if (!soundi)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!sound)
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    *soundi = (SoundI *)sound;

    #if 0
    {
        SystemI *system = SAFE_CAST(SystemI, gGlobal->gSystemHead->getNext());
        while (system != gGlobal->gSystemHead)
        {
            LinkedListNode *currentsound = SAFE_CAST(SoundI, system->mSoundListHead.getNext());

            while (currentsound != &system->mSoundListHead)
            {
                if (currentsound == *soundi || ((*soundi)->mSubSoundParent && currentsound == (*soundi)->mSubSoundParent))
                {
                    return FMOD_OK;
                }

                currentsound = currentsound->getNext();
            }

            system = SAFE_CAST(SystemI, system->getNext());
        }

        return FMOD_ERR_INVALID_HANDLE;
    }
    #endif

    return FMOD_OK;
}

#ifndef __FMOD_EVENT

/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 52faac93 manual
// OFFSET: 0x008E1ED0
SoundI::SoundI()
{
    mMode                    = FMOD_DEFAULT;
    mFormat                  = FMOD_SOUND_FORMAT_NONE;
    mName                    = 0;
    mChannels                = 1;
    mMinDistance             = 1.0f;
    mMaxDistance             = 10000.0f;
    mConeInsideAngle         = 360.0f;
    mConeOutsideAngle        = 360.0f;
    mConeOutsideVolume       = 1.0f;
                             
    mDefaultVolume           = 1.0f;
    mDefaultFrequency        = DEFAULT_FREQUENCY;
    mDefaultPan              = 0.0f;
    mDefaultPriority         = FMOD_CHANNEL_DEFAULTPRIORITY;
    mFrequencyVariation      = 0;
    mVolumeVariation         = 0;
    mPanVariation            = 0;
    mLoopStart               = 0;
    mLoopLength              = 0;
    mLoopCount               = -1;
    mUserData                = 0;
    mMemoryUsed              = 0;
    mCodec                   = 0;
    mSubSound                = 0;
    mSubSoundParent          = 0;
#ifdef FMOD_SUPPORT_SENTENCING
    mSubSoundList            = 0;
#endif
    mNumSubSounds            = 0;
    mNumActiveSubSounds      = 0;
    mAsyncData               = 0;
    mNumSyncPoints           = 0;
    mOpenState               = FMOD_OPENSTATE_READY;
    mSubSampleParent         = this;     /* unless made a sample with subsamples where it will point to a higher sound */
    mRolloffPoint            = 0;
    mNumRolloffPoints        = 0;
    mSyncPointHead           = 0;
    mPostReadCallback        = 0;
    mPostSetPositionCallback = 0;
    mPostCallbackSound       = 0;
    mFlags                   = 0;

    mSoundGroupNode.initNode();
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 3faa35d2 auto
// OFFSET: 0x008E4590
FMOD_RESULT SoundI::loadSubSound(int index, FMOD_MODE mode)
{
    FMOD_RESULT result;
    Sample *sample = 0;
    FMOD_CODEC_WAVEFORMAT waveformat;

    if (!mNumSubSounds)
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (index < 0 || index >= mNumSubSounds)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    result = mCodec->mDescription.getwaveformat(mCodec, index, &waveformat);
    if (result != FMOD_OK)
    {
        return result;
    }

    result = mSystem->createSample(mode, &waveformat, &sample);
    if (result != FMOD_OK)
    {
        return result;
    }

    sample->mType = mType;
    sample->mCodec = mCodec;

    if (mCodec->mDescription.soundcreate)
    {
        SoundI *s = SAFE_CAST(SoundI, sample);

        result = mCodec->mDescription.soundcreate(mCodec, index, (FMOD_SOUND *)s);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

    mCodec->reset();

    result = mCodec->setPosition(index, 0, FMOD_TIMEUNIT_PCM);
    if (result != FMOD_OK)
    {
        return result;
    }

    if (mPostSetPositionCallback)
    {
        mPostSetPositionCallback((FMOD_SOUND *)this, index, 0, FMOD_TIMEUNIT_PCM);
    }

    if (!(mode & FMOD_OPENONLY))
    {
        result = sample->read(0, sample->mLength, 0);
        if (result != FMOD_OK && result != FMOD_ERR_FILE_EOF)
        {
            return result;
        }
    }

    result = setSubSound(index, sample);
    if (result != FMOD_OK)
    {
        return result;
    }

    sample->setPositionInternal(0);

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: eb78ec38 manual
// OFFSET: 0x008E3100
FMOD_RESULT SoundI::read(unsigned int offset, unsigned int numsamples, unsigned int *read)
{
    FMOD_RESULT result;
    unsigned int offbytes, numbytes, readbytes, chunksize;
    FMOD_CODEC_WAVEFORMAT waveformat;

    mPosition = offset;

    if (mMode & FMOD_CREATECOMPRESSEDSAMPLE)
    {
        offbytes = offset;
        numbytes = numsamples;
    }
    else
    {
        getBytesFromSamples(offset, &offbytes);
        getBytesFromSamples(numsamples, &numbytes);
    }

    readbytes = 0;
    if (read)
    {
        *read = 0;
    }

    result = mCodec->mDescription.getwaveformat(mCodec, mCodec->mSubSoundIndex, &waveformat);
    if (result != FMOD_OK)
    {
        return result;
    }

    chunksize = SOUND_READCHUNKSIZE;
    if (mMode & FMOD_CREATESTREAM)
    {
        if (mCodec->mBlockAlign)
        {
            chunksize /= mCodec->mBlockAlign;
            chunksize *= mCodec->mBlockAlign;
            if (!chunksize)
            {
                chunksize = mCodec->mBlockAlign;
            }
        }
    }
    else
    {
        if (waveformat.blockalign)
        {
            chunksize /= waveformat.blockalign;
            chunksize *= waveformat.blockalign;
            if (!chunksize)
            {
                chunksize = waveformat.blockalign;
            }
        }
    }

    while (numbytes)
    {
        FMOD_RESULT     result2;
        int             size = numbytes > chunksize ? chunksize : numbytes;
        unsigned int    bytesreadtotal = 0;
        void           *ptr1, *ptr2;
        unsigned int    len1, len2;

        result = lock(offbytes, size, &ptr1, &ptr2, &len1, &len2);
        if (result != FMOD_OK)
        {
            return result;
        }

        if (!len1 && !len2)
        {
            unlock(ptr1, ptr2, len1, len2);
            return FMOD_ERR_FILE_BAD;
        }

        if (ptr1 && len1)
        {
            unsigned int bytesread = 0;

            result = readData(ptr1, len1, &bytesread);
            if ((result != FMOD_OK) && (result != FMOD_ERR_FILE_EOF))
            {
                unlock(ptr1, ptr2, len1, len2);
                return result;
            }
            bytesreadtotal += bytesread;
        }

        if (ptr2 && len2)
        {
            unsigned int bytesread = 0;

            result = readData(ptr2, len2, &bytesread);
            if ((result != FMOD_OK) && (result != FMOD_ERR_FILE_EOF))
            {
                unlock(ptr1, ptr2, len1, len2);
                return result;
            }
            bytesreadtotal += bytesread;
        }

        if (result == FMOD_ERR_FILE_EOF)
        {
            numbytes = bytesreadtotal;
        }

        result2 = unlock(ptr1, ptr2, len1, len2);
        if (result2 != FMOD_OK)
        {
            return result2;
        }

        numbytes  -= bytesreadtotal;
        offbytes  += bytesreadtotal;
        readbytes += bytesreadtotal;
    }

    if (read)
    {
        getSamplesFromBytes(readbytes, read);
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: a98022ce auto
// OFFSET: 0x008E10A0
FMOD_RESULT SoundI::seek(int subsound, unsigned int position)
{
    FMOD_RESULT result;
    unsigned int newpos;
    
    if (!mCodec->mFile)
    {
        return FMOD_ERR_FILE_COULDNOTSEEK;
    }

    result = mCodec->setPosition(subsound, position, FMOD_TIMEUNIT_PCM);

    newpos = position;
    if (mCodec->getPosition(&newpos, FMOD_TIMEUNIT_PCM) == FMOD_OK)
    {
        position = newpos;
    }

    mPosition = newpos;

    if (mPostSetPositionCallback)
    {
        mPostSetPositionCallback((FMOD_SOUND *)this, subsound, newpos, FMOD_TIMEUNIT_PCM);
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 6ed3298c manual
// OFFSET: 0x008E3700
FMOD_RESULT SoundI::clear(unsigned int offset, unsigned int numsamples)
{
    FMOD_RESULT result;
    unsigned int offbytes, numbytes;
    void *ptr1, *ptr2;
    unsigned int len1, len2;

    if (!numsamples)
    {
        return FMOD_OK;
    }

    getBytesFromSamples(offset, &offbytes);
    getBytesFromSamples(numsamples, &numbytes);

    while (numbytes)
    {
        int size = numbytes > SOUND_READCHUNKSIZE ? SOUND_READCHUNKSIZE : numbytes, bytesreadtotal = 0;

        result = lock(offbytes, size, &ptr1, &ptr2, &len1, &len2);
        if (result != FMOD_OK)
        {
            return result;
        }

        if (ptr1 && len1)
        {
            FMOD_memset(ptr1, 0, len1);
            bytesreadtotal += len1;
        }
        if (ptr2 && len2)
        {
            FMOD_memset(ptr2, 0, len2);
            bytesreadtotal += len2;
        }

        result = unlock(ptr1, ptr2, len1, len2);
        if (result != FMOD_OK)
        {
            return result;
        }

        numbytes  -= bytesreadtotal;
        offbytes  += bytesreadtotal;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 7b791f76 manual
// OFFSET: 0x008E1FC0
FMOD_RESULT SoundI::release(bool freethis)
{
    FMOD_RESULT              result;
    Codec                   *oldcodec;
    AsyncData               *oldasyncdata;
    ChannelStream           *oldstreamchannel;
    FMOD_OS_CRITICALSECTION *readdatacrit = mSystem->mReadDataCrit;
    bool                     readdatacritentered = false;

    while ((mOpenState != FMOD_OPENSTATE_READY && mOpenState != FMOD_OPENSTATE_ERROR) || (mFlags & FMOD_SOUND_FLAG_DONOTRELEASE))
    {
        FMOD_OS_Time_Sleep(2);
    }

    if (mCodec)
    {
        if (mCodec->mFile)
        {
            mCodec->mFile->cancel();
        }
    }

    if (mSystem)
    {
        result = mSystem->stopSound(this);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

    if (isStream() && (mFlags & FMOD_SOUND_FLAG_READDATA) && !(mFlags & FMOD_SOUND_FLAG_PLAYED))
    {
        FMOD_OS_CriticalSection_Enter(readdatacrit);
        readdatacritentered = true;
    }

    if (mSyncPointHead)
    {
        while (mSyncPointHead->getNext() != mSyncPointHead)
        {
            deleteSyncPoint((FMOD_SYNCPOINT *)mSyncPointHead->getNext());
        }

        FMOD_Memory_Free(mSyncPointHead);
        mSyncPointHead = 0;
    }

    oldcodec = mCodec;
    oldasyncdata = mAsyncData;
    oldstreamchannel = 0;

    #ifdef FMOD_SUPPORT_STREAMING
    if (isStream() && mCodec && (!mSubSoundParent || mSubSoundParent == this || (mSubSoundParent && mCodec != mSubSoundParent->mCodec)))
    {
        Stream *stream = SAFE_CAST(Stream, this);

        if (stream->mChannel)
        {
            stream->mChannel->mFinished = true;
        }

        if (!(mSystem->mFlags & FMOD_INIT_STREAM_FROM_UPDATE) && !stream->mStreamNode.isEmpty())
        {
            while (!(stream->mFlags & FMOD_SOUND_FLAG_FINISHED))
            {
                FMOD_OS_Time_Sleep(2);
            }
        }

        FMOD_OS_CriticalSection_Enter(mSystem->mStreamListCrit);
        {
            stream->mStreamNode.removeNode();
            stream->mStreamNode.setData(0);
        }
        FMOD_OS_CriticalSection_Leave(mSystem->mStreamListCrit);

        oldstreamchannel = stream->mChannel;

        if (stream->mSample)
        {
            stream->mSample->mCodec = 0;
            stream->mSample->release();
            stream->mSample = 0;
        }
    }
    #endif

    if (mNumSubSounds && mSubSound)
    {
        int count;

        if (mNumActiveSubSounds)
        {
            for (count = 0; count < mNumSubSounds; count++)
            {
                if (mSubSound[count])
                {
                    if (mSubSound[count]->mCodec == oldcodec)
                    {
                        mSubSound[count]->mCodec = 0;
                    }
                    if (mSubSound[count]->mAsyncData == oldasyncdata)
                    {
                        mSubSound[count]->mAsyncData = 0;
                    }

                    #ifdef FMOD_SUPPORT_STREAMING
                    if (mSubSound[count]->isStream())
                    {
                        Stream *stream = SAFE_CAST(Stream, mSubSound[count]);

                        if (stream->mChannel == oldstreamchannel)
                        {
                            stream->mChannel = 0;
                        }
                    }
                    #endif

                    if (mSubSound[count]->mSubSoundShared)
                    {
                        int count2;

                        for (count2 = count + 1; count2 < mNumSubSounds; count2++)
                        {
                            if (mSubSound[count2] == mSubSound[count])
                            {
                                mSubSound[count2] = 0;
                            }
                        }
                    }

                    mSubSound[count]->release();
                    mSubSound[count] = 0;
                }
            }
        }

        FMOD_Memory_Free(mSubSound);
        mSubSound = 0;

        if (mSubSoundShared)
        {
            FMOD_Memory_Free(mSubSoundShared);
            mSubSoundShared = 0;
        }
    }

    if (mCodec)
    {
        if (!mSubSoundParent || mSubSoundParent == this || (mSubSoundParent && mCodec != mSubSoundParent->mCodec))
        {
            mCodec->release();
            mCodec = 0;
        }
    }

    #ifdef FMOD_SUPPORT_STREAMING
    if (isStream())
    {
        Stream *stream = SAFE_CAST(Stream, this);
        Stream *parentstream = SAFE_CAST(Stream, stream->mSubSoundParent);

        if (parentstream && parentstream->mChannel == stream->mChannel)
        {
            stream->mChannel = 0;
        }
        if (stream->mChannel)
        {
            FMOD_Memory_Free(stream->mChannel);
            stream->mChannel = 0;
        }
    }
    #endif

    if (mAsyncData)
    {
        if (!mSubSoundParent || mSubSoundParent->mAsyncData != mAsyncData)
        {
            FMOD_Memory_Free(mAsyncData);
        }
        mAsyncData = 0;
    }

    if (mSubSoundParent)
    {
        int count;

        for (count = 0; count < mSubSoundParent->mNumSubSounds; count++)
        {
            if (mSubSoundParent->mSubSound && mSubSoundParent->mSubSound[count] == this)
            {
                mSubSoundParent->setSubSound(count, 0);
                break;
            }
        }
    }

#ifdef FMOD_SUPPORT_SENTENCING
    if (mSubSoundList)
    {
        FMOD_Memory_Free(mSubSoundList);
        mSubSoundList = 0;
    }
#endif

    if (mName)
    {
        FMOD_Memory_Free(mName);
        mName = 0;
    }

    FMOD_OS_CriticalSection_Enter(mSystem->gSoundListCrit);
    {
        removeNode();
        mSoundGroupNode.removeNode();
    }
    FMOD_OS_CriticalSection_Leave(mSystem->gSoundListCrit);

    if (freethis && !mSubSoundShared)
    {
        FMOD_Memory_Free(this);
    }

    if (readdatacritentered)
    {
        FMOD_OS_CriticalSection_Leave(readdatacrit);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 69a7acc6 auto
// OFFSET: 0x008E1110
bool SoundI::readyForRelease()
{
    if (mCodec && !mCodec->readyForRelease())
    {
        return false;
    }

    if (isStream())
    {
        return (mFlags & FMOD_SOUND_FLAG_FINISHED) ? true : false;
    }

    return true;
}


// VERIFY: 1ef88b11 auto
// OFFSET: 0x008E1140
FMOD_RESULT SoundI::getSystemObject(System **system)
{
    if (!system)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *system = (System *)mSystem;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 285b601e auto
// OFFSET: 0x008E1170
FMOD_RESULT SoundI::lock(unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
    return FMOD_ERR_BADCOMMAND;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: c432ee50 auto
// OFFSET: 0x008E1180
FMOD_RESULT SoundI::unlock(void *ptr1, void *ptr2, unsigned int len1, unsigned int len2)
{
    return FMOD_ERR_BADCOMMAND;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: c2313840 manual
// OFFSET: 0x008E1190
FMOD_RESULT SoundI::setDefaults(float frequency, float volume, float pan, int priority)
{
    if (volume > 1)
    {
        volume = 1;
    }
    if (volume < 0)
    {
        volume = 0;
    }
    if (pan < -1)
    {
        pan = -1;
    }
    if (pan > 1)
    {
        pan = 1;
    }
    if (priority < 0)
    {
        priority = 0;
    }
    if (priority > 256)
    {
        priority = 256;
    }

    mDefaultFrequency = frequency;
    mDefaultVolume    = volume;
    mDefaultPan       = pan;
    mDefaultPriority  = priority;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 1db94e29 auto
// OFFSET: 0x008E1210
FMOD_RESULT SoundI::getDefaults(float *frequency, float *volume, float *pan, int *priority)
{
    if (frequency)
    {
        *frequency = mDefaultFrequency;
    }
    if (volume)
    {
        *volume = mDefaultVolume;
    }
    if (pan)
    {
        *pan = mDefaultPan;
    }
    if (priority)
    {
        *priority = mDefaultPriority;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 9dbfc2da auto
// OFFSET: 0x008E1250
FMOD_RESULT SoundI::setVariations(float frequencyvar, float volumevar, float panvar)
{
    if (frequencyvar >= 0)
    {
        mFrequencyVariation = frequencyvar;
    }
    if (volumevar >= 0)
    {
        mVolumeVariation = volumevar;
    }
    if (panvar >= 0)
    {
        mPanVariation = panvar;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 1b5760f2 auto
// OFFSET: 0x008E12A0
FMOD_RESULT SoundI::getVariations(float *frequencyvar, float *volumevar, float *panvar)
{
	if (frequencyvar) 
    {
        *frequencyvar = mFrequencyVariation;
    }

	if (volumevar)
    {
        *volumevar = mVolumeVariation;
    }

	if (panvar)
    {
        *panvar = mPanVariation;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: abbd7b44 auto
// OFFSET: 0x008E12D0
FMOD_RESULT SoundI::set3DMinMaxDistance(float min, float max)
{
    if (min < 0 || max < 0 || max < min)
    {
        FLOG((FMOD_DEBUG_LEVEL_LOG, __FILE__, __LINE__, "SoundI::set3DMinMaxDistance", "ERROR! Invalid parameter passed in.  min = %.02f = max %.02f\n", min, max));
        return FMOD_ERR_INVALID_PARAM;
    }

    mMinDistance = min;
    mMaxDistance = max;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 57957d67 auto
// OFFSET: 0x008E1320
FMOD_RESULT SoundI::get3DMinMaxDistance(float *min, float *max)
{
    if (min)
    {
        *min = mMinDistance;
    }

    if (max)
    {
        *max = mMaxDistance;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]
    FMOD_OK

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: f8735e53 auto
// OFFSET: 0x008E1350
FMOD_RESULT SoundI::set3DConeSettings(float insideconeangle, float outsideconeangle, float outsidevolume)
{
    if (insideconeangle < 0)
    {
        insideconeangle = 0;
    }
    if (outsideconeangle < 0)
    {
        outsideconeangle = 0;
    }
    if (insideconeangle > 360)
    {
        insideconeangle = 360;
    }
    if (outsideconeangle > 360)
    {
        outsideconeangle = 360;
    }
    if (outsidevolume < 0)
    {
        outsidevolume = 0;
    }
    if (outsidevolume > 1)
    {
        outsidevolume = 1;
    }

    mConeInsideAngle = insideconeangle;
    mConeOutsideAngle = outsideconeangle;
    mConeOutsideVolume = outsidevolume;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]
    FMOD_OK

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 11f71946 auto
// OFFSET: 0x008E13E0
FMOD_RESULT SoundI::get3DConeSettings(float *insideconeangle, float *outsideconeangle, float *outsidevolume)
{
    if (insideconeangle)
    {
        *insideconeangle = mConeInsideAngle;
    }

    if (outsideconeangle)
    {
        *outsideconeangle = mConeOutsideAngle;
    }

    if (outsidevolume)
    {
        *outsidevolume = mConeOutsideVolume;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 01ebf242 manual
// OFFSET: 0x008E1410
FMOD_RESULT  SoundI::set3DCustomRolloff(FMOD_VECTOR *points, int numpoints)
{
    if (numpoints < 0)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (points)
    {
        int count;

        for (count = 1; count < numpoints; count++)
        {
            if (points[count].x <= points[count-1].x)
            {
                return FMOD_ERR_INVALID_PARAM;
            }
            if (points[count].y < 0 || points[count].y > 1)
            {
                return FMOD_ERR_INVALID_PARAM;
            }
        }
    }

    mRolloffPoint = points;
    mNumRolloffPoints = numpoints;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: c877355a auto
// OFFSET: 0x008E14A0
FMOD_RESULT SoundI::get3DCustomRolloff(FMOD_VECTOR **points, int *numpoints)
{
    if (points)
    {
        *points = mRolloffPoint;
    }

    if (numpoints)
    {
        *numpoints = mNumRolloffPoints;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 2855c95f manual
// OFFSET: 0x008E14D0
FMOD_RESULT SoundI::setSubSound(int index, SoundI *subsound)
{
    int count;
    SoundI *oldsubsound;
    bool blockstream = false;

    if (index < 0 || index >= mNumSubSounds)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (subsound && subsound->mSubSoundParent)
    {
        return FMOD_ERR_SUBSOUND_ALLOCATED;
    }

#ifdef FMOD_SUPPORT_STREAMING
    if (isStream())
    {
        SoundI *soundi = this;

        if (soundi->mSubSoundShared)
        {
            soundi = soundi->mSubSoundShared;
        }
        else if (soundi->mSubSound)
        {
            soundi = soundi->mSubSound[soundi->mSubSoundIndex];
        }

        if (soundi && soundi->mFlags & FMOD_SOUND_FLAG_PLAYED && !(soundi->mFlags & FMOD_SOUND_FLAG_FINISHED))
        {
            blockstream = true;
            FMOD_OS_CriticalSection_Enter(mSystem->mStreamUpdateCrit);
        }
    }
#endif

    oldsubsound = mSubSound[index];

    if (subsound)
    {
        if (!subsound->isStream())
        {
            Sample *sample = (Sample *)subsound;

            for (count = 0; count < sample->mNumSubSamples; count++)
            {
                sample->mSubSample[count]->mCodec = mCodec;
                if (oldsubsound)
                {
                    Sample *oldsample = (Sample *)oldsubsound;

                    sample->mSubSample[count]->mSubSampleParent = oldsample->mSubSampleParent;
                }
            }
        }

        subsound->mSubSoundIndex   = index;
        subsound->mSubSoundParent  = this;
    }

    mSubSound[index] = subsound;

    if (oldsubsound)
    {
        oldsubsound->mSubSoundParent = 0;

        if (!subsound)
        {
            mNumActiveSubSounds--;
        }
    }
    else
    {
        if (subsound)
        {
            mNumActiveSubSounds++;
        }
    }

#ifdef FMOD_SUPPORT_SENTENCING
    if (mSubSoundListNum)
    {
        mLength = 0;

        for (count = 0; count < mSubSoundListNum; count++)
        {
            if (mSubSound[mSubSoundList[count]])
            {
                mLength += mSubSound[mSubSoundList[count]]->mLength;
            }
        }
    }
#endif

    mLoopStart  = 0;
    mLoopLength = mLength;

#ifdef FMOD_SUPPORT_STREAMING
    if (isStream())
    {
        if (subsound)
        {
            mFlags |= FMOD_SOUND_FLAG_WANTSTOFLUSH;
        }

        if (blockstream)
        {
            FMOD_OS_CriticalSection_Leave(mSystem->mStreamUpdateCrit);
        }
    }
#endif

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: d1d25fde auto
// OFFSET: 0x008E3A60
FMOD_RESULT SoundI::getSubSound(int index, SoundI **subsound)
{
    if (!subsound)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *subsound = 0;

    if (index < 0 || index >= mNumSubSounds)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *subsound = mSubSound[index];

#ifdef FMOD_SUPPORT_NONBLOCKING
    if (mSubSound[index] && mMode & FMOD_NONBLOCKING && mSubSound[index]->isStream())
    {
        if (mSubSound[index]->mOpenState == FMOD_OPENSTATE_SEEKING)
        {
            return FMOD_ERR_NOTREADY;
        }
    }
#endif

    if (mSubSound[index] && mSubSound[index]->mSubSoundShared)
    {
        mSubSound[index]->updateSubSound(index, false);
    }

#if defined(FMOD_SUPPORT_STREAMING) && defined(FMOD_SUPPORT_NONBLOCKING)
    if (mSubSound[index] && mMode & FMOD_NONBLOCKING && mSubSound[index]->isStream())
    {
        FMOD_RESULT result;
        Stream *stream = SAFE_CAST(Stream, *subsound);
        Stream *streamParent = SAFE_CAST(Stream, stream->mSubSoundParent);

        if ((index == stream->mSubSoundParent->mSubSoundIndex && (stream->mPosition == streamParent->mInitialPosition + stream->mSample->mLength || (stream->mLength < stream->mSample->mLength && stream->mPosition == streamParent->mInitialPosition + stream->mLength))) && !(stream->mFlags & FMOD_SOUND_FLAG_WANTSTOFLUSH))
        {
            return FMOD_OK;
        }

        stream->mOpenState = FMOD_OPENSTATE_SEEKING;
        mOpenState         = FMOD_OPENSTATE_SEEKING;

        result = AsyncThread::getAsyncThread(stream);
        if (result != FMOD_OK)
        {
            return result;
        }

        stream->mAsyncData->mNode.setData(stream);

        FMOD_OS_CriticalSection_Enter(stream->mAsyncData->mThread->mCrit);
        {
            stream->mAsyncData->mNode.addBefore(&stream->mAsyncData->mThread->mHead);
        }
        FMOD_OS_CriticalSection_Leave(stream->mAsyncData->mThread->mCrit);

        stream->mAsyncData->mThread->mThread.wakeupThread();
    }
#endif

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 43c301f5 manual
// OFFSET: 0x008E1690
FMOD_RESULT SoundI::setSubSoundSentence(int *subsoundlist, int numsubsounds)
{
#ifdef FMOD_SUPPORT_SENTENCING
    int count;

    if (!mNumSubSounds)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (subsoundlist && !numsubsounds)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!isStream())
    {
        return FMOD_ERR_FORMAT;
    }

    if (subsoundlist)
    {
        for (count = 0; count < numsubsounds; count++)
        {
            int index = subsoundlist[count];

            if (index < 0 || index >= mNumSubSounds)
            {
                return FMOD_ERR_INVALID_PARAM;
            }
            if (!mSubSound[index])
            {
                return FMOD_ERR_INVALID_PARAM;
            }
            if (mSubSound[index]->isStream() != isStream())
            {
                return FMOD_ERR_FORMAT;
            }
            if (isStream())
            {
                if (mFormat != mSubSound[index]->mFormat || mChannels != mSubSound[index]->mChannels)
                {
                    return FMOD_ERR_FORMAT;
                }
            }
        }
    }

    if (mSubSoundList)
    {
        FMOD_Memory_Free(mSubSoundList);
    }

    mSubSoundListNum = numsubsounds;

    if (mSubSoundListNum)
    {
        mSubSoundList = (int *)FMOD_Memory_Calloc(mSubSoundListNum * sizeof(int));
        if (!mSubSoundList)
        {
            return FMOD_ERR_MEMORY;
        }

        mLength = 0;

        for (count = 0; count < mSubSoundListNum; count++)
        {
            if (subsoundlist)
            {
                mSubSoundList[count] = subsoundlist[count];
            }
            else
            {
                mSubSoundList[count] = count;
            }

            if (mSubSound[mSubSoundList[count]])
            {
                mLength += mSubSound[mSubSoundList[count]]->mLength;
            }
        }
    }

    mLoopStart           = 0;
    mLoopLength          = mLength;
    mSubSoundListCurrent = 0;

#ifdef FMOD_SUPPORT_STREAMING
    if (isStream())
    {
        SoundI *subsound = mSubSound[mSubSoundList[mSubSoundListCurrent]];

        if (subsound)
        {
            Stream *stream = SAFE_CAST(Stream, this);

            stream->mSample->mCodec = subsound->mCodec;
            mFlags |= FMOD_SOUND_FLAG_WANTSTOFLUSH;
        }
    }
#endif

    return FMOD_OK;
#else
    return FMOD_ERR_UNSUPPORTED;
#endif
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 87805cb4 auto
// OFFSET: 0x008E1890
FMOD_RESULT SoundI::getName(char *name, int namelen)
{
    if (!name)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (namelen > FMOD_STRING_MAXNAMELEN)
    {
        namelen = FMOD_STRING_MAXNAMELEN;
    }

    if (mName)
    {
        FMOD_strncpy(name, mName, namelen);
    }
    else
    {
        FMOD_strncpy(name, "(null)", namelen);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 46567540 auto
// OFFSET: 0x008E3C30
FMOD_RESULT SoundI::getLength(unsigned int *length, FMOD_TIMEUNIT lengthtype)
{
    if (!length)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

#ifdef FMOD_SUPPORT_SENTENCING
    if (lengthtype == FMOD_TIMEUNIT_SENTENCE)
    {
        *length = mSubSoundListNum;
    }
    else if (lengthtype == FMOD_TIMEUNIT_SENTENCE_MS)
    {
        if (!mSubSoundList)
        {
            return FMOD_ERR_INVALID_PARAM;
        }

        if (mSubSoundShared)
        {
            FMOD_RESULT result;
            FMOD_CODEC_WAVEFORMAT waveformat;

            result = mCodec->mDescription.getwaveformat(mCodec, mSubSoundList[mSubSoundListCurrent], &waveformat);
            if (result != FMOD_OK)
            {
                return result;
            }

            *length = (unsigned int)((FMOD_UINT64)waveformat.lengthpcm * 1000 / (FMOD_UINT64)mDefaultFrequency);
        }
        else
        {
            SoundI *subsound = mSubSound[mSubSoundList[mSubSoundListCurrent]];

            if (!subsound)
            {
                return FMOD_ERR_INVALID_PARAM;
            }

            *length = (unsigned int)((FMOD_UINT64)subsound->mLength * 1000 / (FMOD_UINT64)mDefaultFrequency);
        }
    }
    else
#endif
    if (lengthtype == FMOD_TIMEUNIT_PCM)
    {
        *length = mLength;
    }
    else if (lengthtype == FMOD_TIMEUNIT_MS)
    {
        if (!mDefaultFrequency)
        {
            *length = (unsigned int)-1;
        }
        else if (mLength == 0xFFFFFFFF)
        {
            *length = (unsigned int)-1;
        }
        else
        {
            *length = (unsigned int)((FMOD_UINT64)mLength * 1000 / (FMOD_UINT64)mDefaultFrequency);
        }
    }
    else if (lengthtype == FMOD_TIMEUNIT_PCMBYTES)
    {
        if (mLength == 0xFFFFFFFF)
        {
            *length = (unsigned int)-1;
        }
        else if (mMode & FMOD_CREATECOMPRESSEDSAMPLE)
        {
            *length = mLength * sizeof(signed short) * mChannels;
        }
        else
        {
            getBytesFromSamples(mLength, length);
        }
    }
    else
    {
        if (!mCodec)
        {
            return FMOD_ERR_INVALID_PARAM;
        }
        return mCodec->getLength(length, lengthtype);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 879e33fc auto
// OFFSET: 0x008E2500
FMOD_RESULT SoundI::getFormat(FMOD_SOUND_TYPE *type, FMOD_SOUND_FORMAT *format, int *channels, int *bits)
{
    if (type)
    {
        *type = mType;
    }

    if (format)
    {
        *format = mFormat;
    }

    if (channels)
    {
        *channels = mChannels;
    }

    if (bits)
    {
        SoundI::getBitsFromFormat(bits);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
FMOD_RESULT SoundI::getNumSubSounds(int *numsubsounds)
{
    if (!numsubsounds)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *numsubsounds = mNumSubSounds;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 8d8039fb auto
// OFFSET: 0x008E1920
FMOD_RESULT SoundI::getNumTags(int *numtags, int *numtagsupdated)
{
    if (!numtags && !numtagsupdated)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (numtags)
    {
        *numtags = 0;
    }
    if (numtagsupdated)
    {
        *numtagsupdated = 0;
    }

    if (mCodec && mCodec->mMetadata)
    {
        return mCodec->mMetadata->getNumTags(numtags, numtagsupdated);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 63c355f3 auto
// OFFSET: 0x008E1970
FMOD_RESULT SoundI::getTag(const char *name, int index, FMOD_TAG *tag)
{
    if (!tag)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mCodec || !mCodec->mMetadata)
    {
        return FMOD_ERR_TAGNOTFOUND;
    }

    return mCodec->mMetadata->getTag(name, index, tag);
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: cba9369d auto
// OFFSET: 0x008E19C0
FMOD_RESULT SoundI::getOpenState(FMOD_OPENSTATE *openstate, unsigned int *percentbuffered, bool *starving)
{
    if (openstate)
    {
        *openstate = mOpenState;

        if (mOpenState == FMOD_OPENSTATE_READY && (mFlags & FMOD_SOUND_FLAG_DONOTRELEASE))
        {
            *openstate = FMOD_OPENSTATE_BUFFERING;
        }
    }

    if (percentbuffered)
    {
        if (mCodec && mCodec->mFile && (mOpenState == FMOD_OPENSTATE_BUFFERING || mOpenState == FMOD_OPENSTATE_READY))
        {
            mCodec->mFile->isBusy(0, percentbuffered);
        }
        else
        {
            *percentbuffered = 0;
        }
    }

    if (starving)
    {
        *starving = false;

        if (mCodec && mCodec->mFile)
        {
            mCodec->mFile->isStarving(starving);
        }
    }

    return mAsyncData ? mAsyncData->mResult : FMOD_OK;
}



/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: d3be65fe manual
// OFFSET: 0x008E3FA0
FMOD_RESULT SoundI::readData(void *buffer, unsigned int numbytes, unsigned int *read)
{
    FMOD_RESULT           result;
    unsigned int          offbytes, readbytes, scale, position = 0, chunksize, blockalign;
    int                   bits;
    FMOD_UINT_NATIVE      threadid;
    FMOD_CODEC_WAVEFORMAT waveformat;

    if (!mCodec)
    {
        return FMOD_ERR_PLUGIN_MISSING;
    }

    if (!buffer)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    result = getBitsFromFormat(&bits);
    if (result != FMOD_OK)
    {
        return result;
    }

    LocalCriticalSection crit(mSystem->mReadDataCrit);

    if (!mCodec->mFile)
    {
        return FMOD_ERR_UNSUPPORTED;
    }

    FMOD_OS_Thread_GetCurrentID(&threadid);
    if (threadid != mSystem->mMainThreadID)
    {
        mFlags |= (FMOD_SOUND_FLAG_DONOTRELEASE | FMOD_SOUND_FLAG_READDATA);
        crit.enter();
    }

    result = mCodec->mDescription.getwaveformat(mCodec, mCodec->mSubSoundIndex, &waveformat);
    if (result != FMOD_OK)
    {
        mFlags &= ~FMOD_SOUND_FLAG_DONOTRELEASE;
        return result;
    }

    offbytes  = 0;
    readbytes = 0;
    scale = 1;

    if (mChannels != waveformat.channels && mChannels == 1)
    {
        scale = waveformat.channels;
    }

    chunksize = SOUND_READCHUNKSIZE;
    blockalign = 1;

    if (mMode & FMOD_CREATESTREAM)
    {
        if (mCodec->mBlockAlign)
        {
            blockalign = mCodec->mBlockAlign;
        }
    }
    else
    {
        if (waveformat.blockalign)
        {
            blockalign = waveformat.blockalign;
        }
    }

    chunksize /= blockalign;
    chunksize *= blockalign;
    if (!chunksize)
    {
        chunksize = blockalign;
    }

    while (numbytes)
    {
        unsigned int    size = numbytes > (chunksize / scale) ? (chunksize / scale) : numbytes;
        unsigned int    bytesreadtotal = 0, bytesread = 0;
        void           *ptr;

        ptr = (char *)buffer + offbytes;

        if (mMode & FMOD_CREATECOMPRESSEDSAMPLE)
        {
            result = mCodec->mFile->read(ptr, 1, size, &bytesread);
            if (result != FMOD_OK && result != FMOD_ERR_FILE_EOF)
            {
                mFlags &= ~FMOD_SOUND_FLAG_DONOTRELEASE;
                return result;
            }
        }
        else
        {
            result = mCodec->read(ptr, size, &bytesread);
            if (result != FMOD_OK && result != FMOD_ERR_FILE_EOF)
            {
                mFlags &= ~FMOD_SOUND_FLAG_DONOTRELEASE;
                return result;
            }
        }

        if (mPostReadCallback && bytesread)
        {
            mPostReadCallback(mPostCallbackSound, ptr, bytesread);
        }

        bytesreadtotal += (bytesread / scale);

        if (result == FMOD_ERR_FILE_EOF)
        {
            numbytes = bytesreadtotal;
        }

        numbytes  -= bytesreadtotal;
        offbytes  += bytesreadtotal;
        readbytes += bytesreadtotal;

        getSamplesFromBytes(bytesreadtotal, &position);

        mPosition += position;
        if (mPosition > mLength)
        {
            mPosition = mLength;
        }
    }

    if (read)
    {
        *read = readbytes;
    }

    mFlags &= ~FMOD_SOUND_FLAG_DONOTRELEASE;

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: bbf5773b auto
// OFFSET: 0x008E1A60
FMOD_RESULT SoundI::seekData(unsigned int position)
{
    if (mCodec)
    {
        mCodec->reset();
    }

    return seek(mSubSoundIndex, position);
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: fd13a68d manual
// OFFSET: 0x008E1AC0
FMOD_RESULT SoundI::setSoundGroup(SoundGroupI *soundgroup)
{
    mSoundGroup = soundgroup;
    if (!mSoundGroup)
    {
        mSoundGroup = mSystem->mSoundGroup;
    }

    FMOD_OS_CriticalSection_Enter(mSystem->gSoundListCrit);
    {
        mSoundGroupNode.removeNode();
        mSoundGroupNode.addBefore(&mSoundGroup->mSoundHead);
        mSoundGroupNode.setData(this);

        // CoA64: relinked under the lock (the original does it after Leave); the NONBLOCKING thread races updateSoundGroups.
        mSoundGroup->removeNode();
        mSoundGroup->addAfter(&mSystem->mSoundGroupUsedHead);
    }
    FMOD_OS_CriticalSection_Leave(mSystem->gSoundListCrit);

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: cbb4e9c8 auto
// OFFSET: 0x008E1BA0
FMOD_RESULT SoundI::getSoundGroup(SoundGroupI **soundgroup)
{
    if (!soundgroup)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *soundgroup = mSoundGroup;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 517d9c96 auto
// OFFSET: 0x008E1BD0
FMOD_RESULT SoundI::getNumSyncPoints(int *numsyncpoints)
{
    if (!numsyncpoints)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *numsyncpoints = mNumSyncPoints;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: dcdd0332 auto
// OFFSET: 0x008E1C00
FMOD_RESULT SoundI::getSyncPoint(int index, FMOD_SYNCPOINT **point)
{
    SyncPoint *current;
    int count;

    if (index < 0 || index >= mNumSyncPoints || !point)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    current = SAFE_CAST(SyncPoint, mSyncPointHead->getNext());
    for (count = 0; count < index; count++)
    {
        current = SAFE_CAST(SyncPoint, current->getNext());
    }

    *point = (FMOD_SYNCPOINT *)current;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 75872feb manual
// OFFSET: 0x008E25B0
FMOD_RESULT SoundI::getSyncPointInfo(FMOD_SYNCPOINT *point, char *name, int namelen, unsigned int *offset, FMOD_TIMEUNIT offsettype)
{
    SyncPoint *spoint = (SyncPoint *)point;

    if (!name && !offset)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (name)
    {
        FMOD_strncpy(name, spoint->mName, namelen);
    }

    if (offset)
    {
        if (offsettype == FMOD_TIMEUNIT_PCM)
        {
            *offset = spoint->mOffset;
        }
        else if (offsettype == FMOD_TIMEUNIT_PCMBYTES)
        {
            SoundI::getBytesFromSamples(spoint->mOffset, offset, mChannels, mFormat);
        }
        else if (offsettype == FMOD_TIMEUNIT_MS)
        {
            *offset = (unsigned int)((float)spoint->mOffset / mDefaultFrequency * 1000.0f);
        }
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 01da5d32 manual
// OFFSET: 0x008E4300
FMOD_RESULT SoundI::addSyncPoint(unsigned int offset, FMOD_TIMEUNIT offsettype, const char *name, FMOD_SYNCPOINT **syncpoint)
{
    unsigned int pcm = offset;
    SyncPoint *point;

    if (!mSyncPointHead)
    {
        mSyncPointHead = FMOD_Object_Alloc(SyncPoint);
        if (!mSyncPointHead)
        {
            return FMOD_ERR_MEMORY;
        }

        mSyncPointHead->mOffset = (unsigned int)-1;
        FMOD_strcpy(mSyncPointHead->mName, "head");
    }

    point = FMOD_Object_Alloc(SyncPoint);
    if (!point)
    {
        return FMOD_ERR_MEMORY;
    }

    if (syncpoint)
    {
        *syncpoint = (FMOD_SYNCPOINT *)point;
    }

    if (offsettype == FMOD_TIMEUNIT_PCM)
    {
        pcm = offset;
    }
    else if (offsettype == FMOD_TIMEUNIT_PCMBYTES)
    {
        SoundI::getSamplesFromBytes(offset, &pcm, mChannels, mFormat);
    }
    else if (offsettype == FMOD_TIMEUNIT_MS)
    {
        pcm = (unsigned int)((float)offset * mDefaultFrequency * 0.001f);
    }
    else
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    FMOD_strncpy(point->mName, name, FMOD_STRING_MAXNAMELEN);
    point->mOffset = pcm;
    point->addAt(mSyncPointHead, pcm);

    mNumSyncPoints++;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: bae2a669 auto
// OFFSET: 0x008E1C40
FMOD_RESULT SoundI::deleteSyncPoint(FMOD_SYNCPOINT *point)
{
    SyncPoint *spoint = (SyncPoint *)point;

    if (!point)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    spoint->removeNode();

    FMOD_Memory_Free(point);

    mNumSyncPoints--;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 8c5c1a0e auto
// OFFSET: 0x008E1CB0
FMOD_RESULT SoundI::setMode(FMOD_MODE mode)
{
    if (mode & (FMOD_LOOP_OFF | FMOD_LOOP_NORMAL | FMOD_LOOP_BIDI))
    {
        mMode &= ~(FMOD_LOOP_OFF | FMOD_LOOP_NORMAL | FMOD_LOOP_BIDI);

        if (mode & FMOD_LOOP_OFF)
        {
            mMode |= FMOD_LOOP_OFF;
        }
        else
        {
            if (mode & FMOD_LOOP_NORMAL)
            {
                mMode |= FMOD_LOOP_NORMAL;
            }
            else if (mode & FMOD_LOOP_BIDI)
            {
                mMode |= FMOD_LOOP_BIDI;
            }

#ifdef FMOD_SUPPORT_STREAMING
            if (isStream())
            {
                mFlags &= ~FMOD_SOUND_FLAG_FINISHED;
            }
#endif
        }
    }

    if (mode & FMOD_3D_HEADRELATIVE)
    {
        mMode &= ~FMOD_3D_WORLDRELATIVE;
        mMode |=  FMOD_3D_HEADRELATIVE;
    }
    else if (mode & FMOD_3D_WORLDRELATIVE)
    {
        mMode &= ~FMOD_3D_HEADRELATIVE;
        mMode |=  FMOD_3D_WORLDRELATIVE;
    }

    if (mode & FMOD_3D_LOGROLLOFF)
    {
        mMode &= ~FMOD_3D_LINEARROLLOFF;
        mMode &= ~FMOD_3D_CUSTOMROLLOFF;
        mMode |=  FMOD_3D_LOGROLLOFF;
    }
    else if (mode & FMOD_3D_LINEARROLLOFF)
    {
        mMode &= ~FMOD_3D_LOGROLLOFF;
        mMode &= ~FMOD_3D_CUSTOMROLLOFF;
        mMode |=  FMOD_3D_LINEARROLLOFF;
    }
    else if (mode & FMOD_3D_CUSTOMROLLOFF)
    {
        mMode &= ~FMOD_3D_LOGROLLOFF;
        mMode &= ~FMOD_3D_LINEARROLLOFF;
        mMode |=  FMOD_3D_CUSTOMROLLOFF;
    }

    if (mode & FMOD_3D_IGNOREGEOMETRY)
    {
        mMode |= FMOD_3D_IGNOREGEOMETRY;
    }
    else
    {
        mMode &= ~FMOD_3D_IGNOREGEOMETRY;
    }

    if (mode & FMOD_VIRTUAL_PLAYFROMSTART)
    {
        mMode |= FMOD_VIRTUAL_PLAYFROMSTART;
    }
    else
    {
        mMode &= ~FMOD_VIRTUAL_PLAYFROMSTART;
    }

    if (!(mMode & FMOD_HARDWARE))
    {
        if (mode & FMOD_2D)
        {
            mMode &= ~FMOD_3D;
            mMode |= FMOD_2D;
        }
        else if (mode & FMOD_3D)
        {
            mMode &= ~FMOD_2D;
            mMode |= FMOD_3D;
        }
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: a4637be6 auto
// OFFSET: 0x008E1DE0
FMOD_RESULT SoundI::getMode(FMOD_MODE *mode)
{
    if (!mode)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *mode = mMode;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: c07083e7 manual
// OFFSET: 0x008E1E00
FMOD_RESULT SoundI::setLoopCount(int loopcount)
{
    FMOD_RESULT result;

    if (loopcount)
    {
        result = setMode(FMOD_LOOP_NORMAL);
    }
    else
    {
        result = setMode(FMOD_LOOP_OFF);
    }
    if (result != FMOD_OK)
    {
        return result;
    }

    mLoopCount = loopcount;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: a33f588b auto
// OFFSET: 0x008E1E40
FMOD_RESULT SoundI::getLoopCount(int *loopcount)
{
    if (!loopcount)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *loopcount = mLoopCount;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: da5212f7 manual
// OFFSET: 0x008E2810
FMOD_RESULT SoundI::setLoopPoints(unsigned int loopstart, FMOD_TIMEUNIT loopstarttype, unsigned int loopend, FMOD_TIMEUNIT loopendtype)
{
    unsigned int loopstartpcm = 0, loopendpcm = 0;

    if (loopstarttype != FMOD_TIMEUNIT_MS && loopstarttype != FMOD_TIMEUNIT_PCM && loopstarttype != FMOD_TIMEUNIT_PCMBYTES)
    {
        return FMOD_ERR_FORMAT;
    }
    if (loopendtype != FMOD_TIMEUNIT_MS && loopendtype != FMOD_TIMEUNIT_PCM && loopendtype != FMOD_TIMEUNIT_PCMBYTES)
    {
        return FMOD_ERR_FORMAT;
    }

    if (loopstarttype == FMOD_TIMEUNIT_PCM)
    {
        loopstartpcm = loopstart;
    }
    else if (loopstarttype == FMOD_TIMEUNIT_PCMBYTES)
    {
        SoundI::getSamplesFromBytes(loopstart, &loopstartpcm, mChannels, mFormat);
    }
    else if (loopstarttype == FMOD_TIMEUNIT_MS)
    {
        loopstartpcm = (unsigned int)((float)loopstart * mDefaultFrequency * 0.001f);
    }

    if (loopendtype == FMOD_TIMEUNIT_PCM)
    {
        loopendpcm = loopend;
    }
    else if (loopendtype == FMOD_TIMEUNIT_PCMBYTES)
    {
        SoundI::getSamplesFromBytes(loopend, &loopendpcm, mChannels, mFormat);
    }
    else if (loopendtype == FMOD_TIMEUNIT_MS)
    {
        loopendpcm = (unsigned int)((float)loopend * mDefaultFrequency * 0.001f);
    }

    if (loopstartpcm >= mLength)
    {
        loopstartpcm = 0;
    }
    if (loopendpcm >= mLength)
    {
        loopendpcm = mLength - 1;
    }
    if (!loopendpcm)
    {
        loopendpcm = mLength - 1;
    }

    if (loopstartpcm >= loopendpcm)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    mLoopLength = loopendpcm - loopstartpcm + 1;
    mLoopStart = loopstartpcm;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: 23a13c6f manual
// OFFSET: 0x008E2BB0
FMOD_RESULT SoundI::getLoopPoints(unsigned int *loopstart, FMOD_TIMEUNIT loopstarttype, unsigned int *loopend, FMOD_TIMEUNIT loopendtype)
{
    if ((loopstarttype != FMOD_TIMEUNIT_MS && loopstarttype != FMOD_TIMEUNIT_PCM && loopstarttype != FMOD_TIMEUNIT_PCMBYTES) || 
        (loopendtype   != FMOD_TIMEUNIT_MS && loopendtype   != FMOD_TIMEUNIT_PCM && loopendtype   != FMOD_TIMEUNIT_PCMBYTES))
    {
        return FMOD_ERR_FORMAT;
    }

    if (loopstart)
    {
        if (loopstarttype == FMOD_TIMEUNIT_PCM)
        {
            *loopstart = mLoopStart;
        }
        else if (loopstarttype == FMOD_TIMEUNIT_PCMBYTES)
        {
            SoundI::getBytesFromSamples(mLoopStart, loopstart, mChannels, mFormat);
        }
        else if (loopstarttype == FMOD_TIMEUNIT_MS)
        {
            *loopstart = (unsigned int)((float)mLoopStart / mDefaultFrequency * 1000.0f);
        }
    }

    if (loopend)
    {
        unsigned int lend = mLoopStart + mLoopLength - 1;

        if (loopendtype == FMOD_TIMEUNIT_PCM)
        {
            *loopend = lend;
        }
        else if (loopendtype == FMOD_TIMEUNIT_PCMBYTES)
        {
            SoundI::getBytesFromSamples(lend, loopend, mChannels, mFormat);
        }
        else if (loopendtype == FMOD_TIMEUNIT_MS)
        {
            *loopend = (unsigned int)((float)lend / mDefaultFrequency * 1000.0f);
        }
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
FMOD_RESULT SoundI::setPosition(unsigned int pcm)
{
    mPosition = pcm;
    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
FMOD_RESULT SoundI::setPositionInternal(unsigned int pcm)
{
    mPosition = pcm;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
FMOD_RESULT SoundI::getPosition(unsigned int *pcm)
{
    if (!pcm)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *pcm = mPosition;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: c09aa4af auto
// OFFSET: 0x008E1E80
FMOD_RESULT SoundI::setUserData(void *userdata)
{
    mUserData = userdata;
    
    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: fc6e17ba auto
// OFFSET: 0x008E1EA0
FMOD_RESULT SoundI::getUserData(void **userdata)
{
    if (!userdata)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *userdata = mUserData;
    
    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/
// VERIFY: de4f6be1 auto
// OFFSET: 0x008E2FA0
FMOD_RESULT SoundI::updateSubSound(int index, bool updateinfoonly)
{
    FMOD_RESULT result;
    FMOD_CODEC_WAVEFORMAT waveformat;
    FMOD_OS_CRITICALSECTION *crit = mSystem->mStreamUpdateCrit;
    bool critentered = false;
    FMOD_UINT_NATIVE threadid;

    FMOD_OS_Thread_GetCurrentID(&threadid);
    if (threadid == mSystem->mMainThreadID && !updateinfoonly)
    {
        mSubSoundIndex = index;
        mSystem->stopSound(this);

        if (mMode & FMOD_NONBLOCKING && isStream())
        {
            return FMOD_OK;
        }
    }

#ifdef FMOD_SUPPORT_STREAMING
    if (isStream() && (mFlags & FMOD_SOUND_FLAG_PLAYED))
    {
        Stream *stream = (Stream *)this;

        if (!(mFlags & FMOD_SOUND_FLAG_FINISHED))
        {
            FMOD_OS_CriticalSection_Enter(crit);
            critentered = true;
        }

        stream->mChannel->mFinished = false;
    }
#endif

    mFlags &= ~FMOD_SOUND_FLAG_FINISHED;
    mSubSoundIndex = index;

    result = mCodec->mDescription.getwaveformat(mCodec, index, &waveformat);
    if (result != FMOD_OK)
    {
        if (critentered)
        {
            FMOD_OS_CriticalSection_Leave(crit);
        }
        return result;
    }

    if (mName)
    {
        FMOD_strcpy(mName,  waveformat.name);
    }

    mChannels           = waveformat.channels;
    mDefaultFrequency   = (float)waveformat.frequency;
    mFormat             = waveformat.format;
    mDefaultChannelMask = waveformat.channelmask;
    mLength             = waveformat.lengthpcm;
    mLoopStart          = waveformat.loopstart;
    mLoopLength         = waveformat.loopend - waveformat.loopstart + 1;

    setLoopPoints(waveformat.loopstart, FMOD_TIMEUNIT_PCM, waveformat.loopend, FMOD_TIMEUNIT_PCM);

    if (critentered)
    {
        FMOD_OS_CriticalSection_Leave(crit);
    }

    return FMOD_OK;
}


/*
[API]
[
	[DESCRIPTION]
    Used for 2 purposes.<br>
    One use is for user created sounds when FMOD_OPENUSER is specified when creating the sound.<br>
    The other use is to 'piggyback' on FMOD's read functions when opening a normal sound, therefore the callee can read (rip) or even write back new PCM data while FMOD is opening the sound.

	[PARAMETERS]
    'sound'     Pointer to the sound.  C++ users see remarks.
    'data'          Pointer to raw PCM data that the user can either read or write to.
    'datalen'       Length of the data in bytes.

	[RETURN_VALUE]

	[REMARKS]
    <u>C++ Users</u>.  Cast <b>FMOD_SOUND *</b> to <b>FMOD::Sound *</b> inside the callback and use as normal.<br>
    <br>
    The format of the sound can be retrieved with Sound::getFormat from this callback.  This will allow the user to determine what type of pointer to use if they are not sure what format the sound is.<br>
    If the callback is used for the purpose of 'piggybacking' normal FMOD sound loads, then you do not have to do anything at all, and it can be treated as purely informational.  The return value is also ignored.

    [PLATFORMS]
    Win32, Win64, Linux, Linux64, Macintosh, Xbox360, PlayStation 2, PlayStation Portable, PlayStation 3, Wii, Solaris, iPhone

	[SEE_ALSO]
    Sound::getFormat
    FMOD_SOUND_PCMSETPOSCALLBACK
    System::createSound
    System::createStream
    FMOD_CREATESOUNDEXINFO
]
*/
/*
FMOD_RESULT F_CALLBACK FMOD_SOUND_PCMREADCALLBACK(FMOD_SOUND *sound, void *data, unsigned int datalen)
{
#if 0
	 // here purely for documentation purposes.
#endif
}
*/


/*
[API]
[
	[DESCRIPTION]
    Callback for when the caller calls a seeking function such as Channel::setTime or Channel::setPosition.<br>
    If the sound is a user created sound, this can be used to seek within the user's resource.<br>

	[PARAMETERS]
    'sound'         Pointer to the sound.  C++ users see remarks.
    'subsound'      In a multi subsound type sound (ie fsb/dls/cdda), this will contain the index into the list of sounds.
    'position'      Position to seek to that has been requested.  This value will be of format FMOD_TIMEUNIT and must be parsed to determine what it is.  Generally FMOD_TIMEUNIT_PCM will be the most common format.
    'postype'       Position type that the user wanted to seek with.  If the sound is a user create sound and the seek type is unsupported return FMOD_ERR_FORMAT.

	[RETURN_VALUE]

	[REMARKS]
    <u>C++ Users</u>.  Cast <b>FMOD_SOUND *</b> to <b>FMOD::Sound *</b> inside the callback and use as normal.<br>
    <br>
    If the callback is used for the purpose of 'piggybacking' normal FMOD sound loads, then you do not have to do anything at all, and it can be treated as purely informational.  The return value is also ignored.

    [PLATFORMS]
    Win32, Win64, Linux, Linux64, Macintosh, Xbox360, PlayStation 2, PlayStation Portable, PlayStation 3, Wii, Solaris, iPhone

	[SEE_ALSO]
    FMOD_SOUND_PCMREADCALLBACK
    System::createSound
    System::createStream
    FMOD_CREATESOUNDEXINFO
]
*/
/*
FMOD_RESULT F_CALLBACK FMOD_SOUND_PCMSETPOSCALLBACK(FMOD_SOUND *sound, int subsound, unsigned int position, FMOD_TIMEUNIT postype)
{
#if 0
	 // here purely for documentation purposes.
#endif
}
*/


/*
[API]
[
	[DESCRIPTION]
    Callback to be called when a sound has finished loading.

	[PARAMETERS]
    'sound'     Pointer to the sound.  C++ users see remarks.
    'result'    Error code. FMOD_OK if sound was created successfully, or an error code otherwise.

	[RETURN_VALUE]

	[REMARKS]
    <u>C++ Users</u>.  Cast <b>FMOD_SOUND *</b> to <b>FMOD::Sound *</b> inside the callback and use as normal.<br>
    <br>
    Return code currently ignored.

    [PLATFORMS]
    Win32, Win64, Linux, Linux64, Macintosh, Xbox360, PlayStation 2, PlayStation Portable, PlayStation 3, Wii, Solaris, iPhone

	[SEE_ALSO]
    System::createSound
    FMOD_CREATESOUNDEXINFO
]
*/
/*
FMOD_RESULT F_CALLBACK FMOD_SOUND_NONBLOCKCALLBACK(FMOD_SOUND *sound, FMOD_RESULT result)
{
#if 0
	 // here purely for documentation purposes.
#endif
}
*/



/*
[
	[DESCRIPTION]

	[PARAMETERS]
 
	[RETURN_VALUE]

	[REMARKS]

    [PLATFORMS]
    
	[SEE_ALSO]
]
*/


#endif

}
