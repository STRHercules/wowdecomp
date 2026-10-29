#include "fmod_settings.h"

#include "fmod_3d.h"
#include "fmod_channeli.h"
#include "fmod_channel_real.h"
#include "fmod_channel_stream.h"
#include "fmod_channelgroupi.h"
#include "fmod_codeci.h"
#include "fmod_dspi.h"
#include "fmod_dsp_filter.h"
#include "fmod_listener.h"
#include "fmod_output.h"
#include "fmod_output_emulated.h"
#include "fmod_reverbi.h"
#include "fmod_soundi.h"
#include "fmod_soundgroupi.h"
#include "fmod_sound_sample.h"
#include "fmod_speakermap.h"
#include "fmod_speakerlevels_pool.h"
#include "fmod_syncpoint.h"
#include "fmod_systemi.h"

namespace FMOD
{


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

    Channel handle are composed like so :

    S = system id       = SystemI::mId
    I = channel index   = index into SystemI::mChannel
    C = reference count

    SSSSIIII IIIIIIII CCCCCCCC CCCCCCCC

	[SEE_ALSO]
]
*/
// VERIFY: 4ed18a39 auto
// OFFSET: 0x008DCD60
FMOD_RESULT ChannelI::validate(Channel *channel, ChannelI **channeli)
{
    unsigned int    handle   = (unsigned int)((FMOD_UINT_NATIVE)channel);
    unsigned int    sysindex = (handle >> SYSTEMID_SHIFT)  & SYSTEMID_MASK;
    unsigned int    index    = (handle >> CHANINDEX_SHIFT) & CHANINDEX_MASK;
    ChannelI       *tmpchanneli;
    SystemI        *sys;
    FMOD_RESULT     result;

    if (!channeli)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *channeli = 0;

    result = SystemI::getInstance(sysindex, &sys);
    if (result != FMOD_OK)
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!sys->mChannel)
    {
        return FMOD_ERR_UNINITIALIZED;
    }

    if ((int)index >= sys->mNumChannels)
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    tmpchanneli = &sys->mChannel[index];

    if (handle && tmpchanneli->mHandleCurrent != handle)
    {
        if (tmpchanneli->mHandleCurrent - handle >= 2)
        {
            return FMOD_ERR_CHANNEL_STOLEN;
        }
        return FMOD_ERR_INVALID_HANDLE;
    }

    *channeli = tmpchanneli;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: e908bb3d manual
// OFFSET: 0x008DCE10
FMOD_RESULT ChannelI::returnToFreeList()
{
    SystemI *systemi;

    systemi = SAFE_CAST(SystemI, mSystem);
    if (!systemi)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    mSortedListNode.removeNode();
    removeNode();
    addAfter(&systemi->mChannelFreeListHead);

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: f03e1f56 auto
// OFFSET: 0x008DF300
FMOD_RESULT ChannelI::setDefaults()
{
    float realfrequency, realvolume, realpan;
    int defaultpriority, channels;
    float defaultfrequency;
    float defaultvolume;
    float defaultpan;
    float frequencyvariation;
    float volumevariation;
    float panvariation;
    unsigned int channelmask;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mRealChannel[0]->mSound)
    {
        SoundI *soundi = SAFE_CAST(SoundI, mRealChannel[0]->mSound);

        defaultpriority     = soundi->mDefaultPriority;
        defaultfrequency    = soundi->mDefaultFrequency;
        defaultvolume       = soundi->mDefaultVolume;
        defaultpan          = soundi->mDefaultPan;
        frequencyvariation  = soundi->mFrequencyVariation;
        volumevariation     = soundi->mVolumeVariation;
        panvariation        = soundi->mPanVariation;
        channels            = soundi->mChannels;
        channelmask         = soundi->mDefaultChannelMask;
    }
    else if (mRealChannel[0]->mDSP)
    {
        DSPI *dspi = SAFE_CAST(DSPI, mRealChannel[0]->mDSP);

        defaultpriority     = dspi->mDefaultPriority;
        defaultfrequency    = dspi->mDefaultFrequency;
        defaultvolume       = dspi->mDefaultVolume;
        defaultpan          = dspi->mDefaultPan;
        frequencyvariation  = 0;
        volumevariation     = 0;
        panvariation        = 0;
        channels            = 0;
        channelmask         = 0;
    }
    else
    {
        return FMOD_ERR_INTERNAL;
    }

    mPriority = defaultpriority;

	mDirectOcclusion        = 0.0f;
	mDirectOcclusionTarget  = 0.0f;
	mReverbOcclusion        = 0.0f;
	mReverbOcclusionTarget  = 0.0f;

    realfrequency = defaultfrequency;
    realvolume    = defaultvolume;
    realpan       = defaultpan;

    if (frequencyvariation > 0)
    {
        float randval = ((float)(FMOD_RAND() % 32768) / 16384.0f) - 1.0f;

        realfrequency += frequencyvariation * randval;
    }
    if (volumevariation > 0)
    {
        float randval = ((float)(FMOD_RAND() % 32768) / 16384.0f) - 1.0f;

        realvolume += volumevariation * randval;
    }
    if (panvariation > 0)
    {
        float randval = ((float)(FMOD_RAND() % 32768) / 8192.0f) - 2.0f;

        realpan += panvariation * randval;
    }

    setFrequency(realfrequency);
	setVolume(realvolume);

    if (channelmask & SPEAKER_WAVEFORMAT_MASK)
    {
        int speaker, incount;
        unsigned int maskbit = 1;

        incount = 0;
        for (speaker = 0; speaker < mSystem->mMaxOutputChannels; speaker++)
        {
            if (channelmask & maskbit)
            {
                float clevels[DSP_MAXLEVELS_IN];

                FMOD_memset(clevels, 0, DSP_MAXLEVELS_IN * sizeof(float));
                clevels[incount] = 1.0f;

                setSpeakerLevels((FMOD_SPEAKER)speaker, clevels, channels);

                incount++;
                if (incount >= channels)
                {
                    break;
                }
            }
            maskbit <<= 1;
        }
    }
    else
    {
	    setPan(realpan);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 7f7ea101 manual
// OFFSET: 0x008DCE80
FMOD_RESULT ChannelI::referenceStamp(bool newstamp)
{
    unsigned int sysindex, index, refcount;

    sysindex = (mHandleCurrent >> SYSTEMID_SHIFT)  & SYSTEMID_MASK;
    index    = (mHandleCurrent >> CHANINDEX_SHIFT) & CHANINDEX_MASK;
    refcount = ((newstamp ? mHandleCurrent : mHandleOriginal) >> REFCOUNT_SHIFT) & REFCOUNT_MASK;

    refcount++;
    if (refcount > REFCOUNT_MASK)
    {
        refcount = 1;
    }

    mHandleCurrent = (sysindex << SYSTEMID_SHIFT) | ((index << CHANINDEX_SHIFT) & (CHANINDEX_MASK << CHANINDEX_SHIFT)) | (refcount << REFCOUNT_SHIFT);

    if (newstamp)
    {
        mHandleOriginal = mHandleCurrent;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: edb55259 manual
// OFFSET: 0x008E05E0
FMOD_RESULT ChannelI::updatePosition()
{
    unsigned int oldlistposition;
    float audibility;
    bool forcevirtual = false;
    float realdirecttransmission, realreverbtransmission;

    if (!mSystem)
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    getAudibility(&audibility);

    realdirecttransmission = (1.0f - mDirectOcclusion) * mChannelGroup->mRealDirectOcclusionVolume;
    realreverbtransmission = (1.0f - mReverbOcclusion) * mChannelGroup->mRealReverbOcclusionVolume;

    if (audibility <= mSystem->mAdvancedSettings.vol0virtualvol && mSystem->mFlags & FMOD_INIT_VOL0_BECOMES_VIRTUAL)
    {
        forcevirtual = true;
    }
    if (realdirecttransmission == 0.0f && realreverbtransmission > 0.0f)
    {
        forcevirtual = false;
    }

    forceVirtual(forcevirtual);

    oldlistposition = mListPosition;

    mListPosition  = mPriority * (FMOD_MAXAUDIBIILITY + 1);
    mListPosition += (FMOD_MAXAUDIBIILITY - (int)(audibility * FMOD_MAXAUDIBIILITY));

    if (mListPosition != oldlistposition)
    {
        mSortedListNode.removeNode();
        mSortedListNode.addAt(&mSystem->mChannelSortedListHead, mListPosition);
        mSortedListNode.setData(this);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: f5eb5c0f manual
// OFFSET: 0x008E0460
FMOD_RESULT ChannelI::forceVirtual(bool force)
{
    FMOD_RESULT result;

    if (force)
    {
        bool isvirtual;

        if (mFlags & CHANNELI_FLAG_FORCEVIRTUAL)
        {
            return FMOD_OK;
        }

        result = isVirtual(&isvirtual);
        if (result != FMOD_OK)
        {
            return result;
        }

        mFlags |= CHANNELI_FLAG_FORCEVIRTUAL;

        if (!isvirtual && mRealChannel[0])
        {
            FMOD_CHANNEL_INFO realinfo;
            ChannelReal *emuchannelreal;
            ChannelGroupI *channelgroup;

            result = mSystem->mEmulated->getFreeChannel(mRealChannel[0]->mMode, &emuchannelreal, 1, 1, 0);
            if (result != FMOD_OK)
            {
                return result;
            }

            channelgroup = mChannelGroup;

            getChannelInfo(&realinfo);

            stopEx(false, false, false, true, false, true, false);

            mNumRealChannels = 1;
            mRealChannel[0] = emuchannelreal;

            if (realinfo.mSound)
            {
                play(realinfo.mSound->mSubSampleParent, true, false, false);
                setChannelGroupInternal(channelgroup, true);
                setChannelInfo(&realinfo);
                setPaused(realinfo.mPaused);
            }
            else if (realinfo.mDSP)
            {
                play(realinfo.mDSP, true, false, false);
                setChannelGroupInternal(channelgroup, true);
                setChannelInfo(&realinfo);
                setPaused(realinfo.mPaused);
            }
        }
    }
    else
    {
        if (mFlags & CHANNELI_FLAG_FORCEVIRTUAL)
        {
            mFlags &= ~CHANNELI_FLAG_FORCEVIRTUAL;
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

	[SEE_ALSO]
]
*/
// VERIFY: 9c5e7a56 manual
// OFFSET: 0x008DFE20
FMOD_RESULT ChannelI::getChannelInfo(FMOD_CHANNEL_INFO *info)
{
    DSPI *dsphead;

    info->mRealChannel      = mRealChannel[0];
    info->mAddDSP           = 0;
    info->mLevels           = mLevels;
    info->mSound            = 0;
    info->mDSP              = 0;

    getMode            (&info->mMode);
    getPosition        (&info->mPCM, FMOD_TIMEUNIT_PCM);
    getLoopPoints      (&info->mLoopStart, FMOD_TIMEUNIT_PCM, &info->mLoopEnd, FMOD_TIMEUNIT_PCM);
    getCurrentSound    (&info->mSound);
    if (!info->mSound)
    {
        getCurrentDSP(&info->mDSP);
    }
    getLoopCount       (&info->mLoopCount);
    getMute            (&info->mMute);
    getPaused          (&info->mPaused);
    getDelay           (&info->mDelayStart, &info->mDelayEnd);
    getReverbProperties(&info->mReverbProperties);

    getDSPHead(&dsphead);
    if (dsphead && mFlags & CHANNELI_FLAG_USEDADDDSP)
    {
        if (dsphead->getInput(0, &info->mAddDSP) == FMOD_OK && info->mAddDSP && !(info->mAddDSP->mFlags & FMOD_DSP_FLAG_USEDADDDSP))
        {
            info->mAddDSP = 0;
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

	[SEE_ALSO]
]
*/
// VERIFY: b939b069 manual
// OFFSET: 0x008DFFC0
FMOD_RESULT ChannelI::setChannelInfo(FMOD_CHANNEL_INFO *info)
{
    int count;

    setMode(info->mMode);
    setVolume(mVolume);
    setFrequency(mFrequency);

    if (mLastPanMode == FMOD_CHANNEL_PANMODE_PAN)
    {
        setPan(mPan, true);
    }
    else if (mLastPanMode == FMOD_CHANNEL_PANMODE_SPEAKERMIX)
    {
        setSpeakerMix(mSpeakerFL,  mSpeakerFR,  mSpeakerC,  mSpeakerLFE,  mSpeakerBL,  mSpeakerBR,  mSpeakerSL,  mSpeakerSR, true);
    }
    else if (mLastPanMode == FMOD_CHANNEL_PANMODE_SPEAKERLEVELS)
    {
        mLevels = info->mLevels;

        if (mLevels)
        {
            for (count = 0; count < mSystem->mMaxOutputChannels; count++)
            {
                setSpeakerLevels((FMOD_SPEAKER)count, &mLevels[count * mSystem->mMaxOutputChannels], mSystem->mMaxInputChannels, true);
            }
        }
    }

    set3DAttributes    (&mPosition3D, &mVelocity3D);
    setDelay           (info->mDelayStart, info->mDelayEnd);
    setPosition        (info->mPCM, FMOD_TIMEUNIT_PCM);
    setLoopPoints      (info->mLoopStart, FMOD_TIMEUNIT_PCM, info->mLoopEnd, FMOD_TIMEUNIT_PCM);
    setLoopCount       (info->mLoopCount);
    setMute            (info->mMute);
    setReverbProperties(&info->mReverbProperties);

    if (info->mAddDSP)
    {
        addDSPChain(info->mAddDSP);
    }

    if (mCallback[FMOD_CHANNEL_CALLBACKTYPE_VIRTUALVOICE])
    {
        bool isvirtual;

        isVirtual(&isvirtual);

        mCallback[FMOD_CHANNEL_CALLBACKTYPE_VIRTUALVOICE]((FMOD_CHANNEL *)((FMOD_UINT_NATIVE)mHandleCurrent), FMOD_CHANNEL_CALLBACKTYPE_VIRTUALVOICE, mCallbackCommand[FMOD_CHANNEL_CALLBACKTYPE_VIRTUALVOICE], isvirtual ? 1 : 0, 0);
    }

    update(0);

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 85910fe1 auto
// OFFSET: 0x008DCEE0
FMOD_RESULT ChannelI::getRealChannel(ChannelReal **realchan, int *subchannels)
{
#ifdef FMOD_SUPPORT_STREAMING
    if (mRealChannel[0]->isStream())
    {
        ChannelStream *channelstream = SAFE_CAST(ChannelStream, mRealChannel[0]);

        if (realchan)
        {
            int count;

            for (count = 0; count < channelstream->mNumRealChannels; count++)
            {
                realchan[count] = channelstream->mRealChannel[count];
            }
        }
        if (subchannels)
        {
            *subchannels = channelstream->mNumRealChannels;
        }
    }
    else
#endif
    {
        if (realchan)
        {
            int count;

            for (count = 0; count < mNumRealChannels; count++)
            {
                realchan[count] = mRealChannel[count];
            }
        }
        if (subchannels)
        {
            *subchannels = mNumRealChannels;
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

	[SEE_ALSO]
]
*/
// VERIFY: 3f9477d7 manual
// OFFSET: 0x008DCF80
FMOD_RESULT ChannelI::calcVolumeAndPitchFor3D(int delta)
{
    float pitch3d = 1.0f, vol3d = 1.0f, conevol = 1.0f;
    int count;

    for (count = 0; count < mNumRealChannels; count++)
    {
        if (!(mRealChannel[count]->mMode & FMOD_2D))
        {
            FMOD_RESULT      result;
            int              count2, numlisteners;
            float            smallestdistance;

            result = mSystem->get3DNumListeners(&numlisteners);
            if (result != FMOD_OK)
            {
                return result;
            }

            smallestdistance = 1000000000.0f;

            for (count2 = 0; count2 < numlisteners; count2++)
            {
                FMOD_VECTOR  currdiff;
                float        distance;
                Listener    *listener;
                float        dopplerscale, distancescale, rolloffscale;

                result = mSystem->getListenerObject(count2, &listener);
                if (result != FMOD_OK)
                {
                    return result;
                }

                if (!(mFlags & CHANNELI_FLAG_MOVED) && !listener->mMoved)
                {
                    return FMOD_OK;
                }

                result = mSystem->get3DSettings(&dopplerscale, &distancescale, &rolloffscale);
                if (result != FMOD_OK)
                {
                    return result;
                }

                dopplerscale *= m3DDopplerLevel;

                if (mRealChannel[count]->mMode & FMOD_3D_HEADRELATIVE)
                {
                    currdiff = mPosition3D;
                }
                else
                {
                    FMOD_Vector_Subtract(&mPosition3D, &listener->mPosition, &currdiff);
                }

                if (mSystem->mFlags & FMOD_INIT_3D_RIGHTHANDED)
                {
                    currdiff.z = -currdiff.z;
                }

                distance = (float)FMOD_Vector_GetLengthFast(&currdiff);

                if (distance < smallestdistance)
                {
                    mDistance = smallestdistance = distance;

                    if (mSystem->mRolloffCallback)
                    {
                        vol3d = mSystem->mRolloffCallback((FMOD_CHANNEL *)((FMOD_UINT_NATIVE)mRealChannel[count]->mParent->mHandleCurrent), distance);
                    }
                    else if (mRealChannel[count]->mMode & FMOD_3D_CUSTOMROLLOFF)
                    {
                        if (mRolloffPoint && mNumRolloffPoints)
                        {
                            if (distance >= mRolloffPoint[mNumRolloffPoints - 1].x)
                            {
                                vol3d = mRolloffPoint[mNumRolloffPoints - 1].y;
                            }
                            else
                            {
                                int count3;

                                for (count3 = 1; count3 < mNumRolloffPoints; count3++)
                                {
                                    if (distance >= mRolloffPoint[count3-1].x && distance < mRolloffPoint[count3].x)
                                    {
                                        float frac = (distance - mRolloffPoint[count3-1].x) / (mRolloffPoint[count3].x - mRolloffPoint[count3-1].x);

                                        vol3d = (frac * mRolloffPoint[count3].y) + ((1.0f - frac) * mRolloffPoint[count3-1].y);
                                        break;
                                    }
                                }
                            }
                        }
                        else
                        {
                            vol3d = 1.0f;
                        }
                    }
                    else
                    {
                        if (distance >= mMaxDistance)
                        {
                            distance = mMaxDistance;
                        }
                        if (distance < mMinDistance)
                        {
                            distance = mMinDistance;
                        }

                        if (mRealChannel[count]->mMode & FMOD_3D_LINEARROLLOFF)
                        {
                            float range;

                            range = mMaxDistance - mMinDistance;
                            if (range <= 0)
                            {
                                vol3d = 1.0f;
                            }
                            else
                            {
                                vol3d = (mMaxDistance - distance) / range;
                            }
                        }
                        else
                        {
                            if ((distance > mMinDistance) && (rolloffscale != 1.0f))
                            {
                                distance = rolloffscale * (distance - mMinDistance) + mMinDistance;
                            }

                            if (distance < .000001f)
                            {
                                distance = .000001f;
                            }

                            vol3d = mMinDistance / distance;
                        }
                    }

                    if (vol3d < 0.0f)
                    {
                        vol3d = 0.0f;
                    }
                    if (vol3d > 1.0f)
                    {
                        vol3d = 1.0f;
                    }

                    if (mConeOutsideAngle < 360.0f || mConeInsideAngle < 360.0f)
                    {
                        float angle;

                        if (mDistance <= 0)
                        {
                            angle = 0;
                        }
                        else
                        {
                            float scale = 1.0f / mDistance;
                            float cosangle;

                            FMOD_Vector_Scale(&currdiff, scale, &currdiff);

                            cosangle = -FMOD_Vector_DotProduct(&currdiff, &mConeOrientation);
                            cosangle = cosangle < -1.0f ? -1.0f : cosangle > 1.0f ? 1.0f : cosangle;

                            angle = (1.0f - cosangle) * 180.0f;
                        }

                        if (angle < mConeInsideAngle)
                        {
                            conevol = 1.0f;
                        }
                        else if (angle < mConeOutsideAngle)
                        {
                            float f;

                            f = (angle - mConeInsideAngle) / (mConeOutsideAngle - mConeInsideAngle);

                            conevol = (1.0f - f) + (mConeOutsideVolume * f);
                        }
                        else
                        {
                            conevol = mConeOutsideVolume;
                        }
                    }
                }

                #define SPEED_OF_SOUND 340.0f

                if ((dopplerscale > 0) && (numlisteners == 1))
                {
                    FMOD_VECTOR lastdiff, lastpos, tmp;
                    float       lastdistance;
                    float       delta_sec = (delta <= 0 || delta > 1000) ? 1.0f : delta / 1000.0f;

                    FMOD_Vector_Scale(&mVelocity3D, delta_sec, &tmp);
                    FMOD_Vector_Subtract(&mPosition3D, &tmp, &lastpos);

                    if (mRealChannel[count]->mMode & FMOD_3D_HEADRELATIVE)
                    {
                        lastdiff = lastpos;
                    }
                    else
                    {
                        FMOD_VECTOR listenerlastpos;

                        FMOD_Vector_Scale(&listener->mVelocity, delta_sec, &tmp);
                        FMOD_Vector_Subtract(&listener->mPosition, &tmp, &listenerlastpos);
                        FMOD_Vector_Subtract(&lastpos, &listenerlastpos, &lastdiff);
                    }

                    lastdistance = (float)FMOD_Vector_GetLengthFast(&lastdiff);

                    pitch3d  = distancescale * SPEED_OF_SOUND;
                    pitch3d -= ((mDistance - lastdistance) / delta_sec * dopplerscale);
                    pitch3d /= (distancescale * SPEED_OF_SOUND);
                }

                if (pitch3d < .000001f)
                {
                    pitch3d = .000001f;
                }
            }
        }
    }

    mVolume3D     = vol3d;
    mConeVolume3D = conevol;
    mPitch3D      = pitch3d;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: f8da4d48 manual
// OFFSET: 0x008DF530
ChannelI::ChannelI()
{
    init();
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 55ba9d37 manual
// OFFSET: 0x008DF570
ChannelI::ChannelI(int index, SystemI *system)
{
    init();

    mSystem         = system;
    mIndex          = index;
    mHandleCurrent  = (mSystem->mIndex << SYSTEMID_SHIFT) | ((mIndex << CHANINDEX_SHIFT) & (CHANINDEX_MASK << CHANINDEX_SHIFT)) | (1 << REFCOUNT_SHIFT);
    mHandleOriginal = mHandleCurrent;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: d98a99cf manual
// OFFSET: 0x008DD530
FMOD_RESULT ChannelI::init()
{
    int count;

    mSystem               = 0;
    mHandleCurrent        = 0;
    mHandleOriginal       = 0;

    for (count = 0; count < FMOD_CHANNEL_CALLBACKTYPE_MAX; count++)
    {
        mCallback[count]        = 0;
        mCallbackCommand[count] = 0;
    }

    for (count = 0; count < FMOD_CHANNEL_MAXINPUTCHANNELS; count++)
    {
        mRealChannel[count] = 0;
        mInputMix[count]    = 1.0f;
    }

    mNumRealChannels  = 1;
    mPriority         = FMOD_CHANNEL_DEFAULTPRIORITY;
    mListPosition     = (unsigned int)-1;
    mIndex            = 0;

    mVolume           = 1.0f;
    mFadeVolume       = 1.0f;
    mFadeTarget       = 1.0f;
    mFrequency        = DEFAULT_FREQUENCY;
    mPan              = 0.0f;
    mSpeakerFL        = 1.0f;
    mSpeakerFR        = 1.0f;
    mSpeakerC         = 1.0f;
    mSpeakerLFE       = 1.0f;
    mSpeakerBL        = 1.0f;
    mSpeakerBR        = 1.0f;
    mSpeakerSL        = 1.0f;
    mSpeakerSR        = 1.0f;
    mLevels           = 0;

    mVolume3D              = 1.0f;
    mPitch3D               = 1.0f;
    mConeVolume3D          = 1.0f;
    mDirectOcclusionVolume = 1.0f;
    mReverbDryVolume       = 1.0f;
    mPosition3D.x          = 0;
    mPosition3D.y          = 0;
    mPosition3D.z          = 0;
    mVelocity3D.x          = 0;
    mVelocity3D.y          = 0;
    mVelocity3D.z          = 0;
    mMinDistance           = 1.0f;
    mMaxDistance           = 1000000000.0f;
    mRolloffPoint          = 0;
    mNumRolloffPoints      = 0;

    mFlags            = 0;

    mChannelGroupNode.initNode();

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getSystemObject(System **system)
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

	[SEE_ALSO]
]
*/
// VERIFY: 4be9f2ab manual
// OFFSET: 0x008E0200
FMOD_RESULT ChannelI::play(SoundI *sound, bool paused, bool reset, bool startmuted)
{
    FMOD_RESULT result;

    if (!sound)
    {
        return FMOD_ERR_INVALID_PARAM;
    }
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    result = alloc(sound, reset);
    if (result != FMOD_OK)
    {
        return result;
    }

	result = setPaused(true);
    if (result != FMOD_OK)
    {
        return result;
    }

    if (startmuted)
    {
        mFadeVolume = 0.0f;
        mFadeTarget = 0.0f;
    }
    else
    {
        mFadeVolume = 1.0f;
        mFadeTarget = 1.0f;
    }

    if (reset)
    {
        result = setDefaults();
        if (result != FMOD_OK)
        {
            return result;
        }

        result = setPosition(0, FMOD_TIMEUNIT_PCM);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

	result = start();
    if (result != FMOD_OK)
    {
        return result;
    }

    sound->mNumAudible++;

    if (sound->mSoundGroup && mSystem)
    {
        // CoA64: locked, see SoundI::setSoundGroup.
        FMOD_OS_CriticalSection_Enter(SystemI::gSoundListCrit);
        sound->mSoundGroup->removeNode();
        sound->mSoundGroup->addAfter(&mSystem->mSoundGroupUsedHead);
        FMOD_OS_CriticalSection_Leave(SystemI::gSoundListCrit);
    }

    if (reset)
    {
        FMOD_MODE mode;

        sound->getMode(&mode);

        if (mode & FMOD_3D)
        {
	        FMOD_VECTOR vel = { 0, 0, 0 };

            result = set3DAttributes(&mSystem->mListener[0].mPosition, &vel);
            if (result != FMOD_OK)
            {
                return result;
            }

            result = update(0);
            if (result != FMOD_OK)
            {
                return result;
            }
        }
    }

    if (sound->mSyncPointHead && sound->mNumSyncPoints)
    {
        mSyncPointCurrent = sound->mSyncPointHead;
    }

    if (!paused)
    {
        result = setPaused(false);
        if (result != FMOD_OK)
        {
            return result;
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

	[SEE_ALSO]
]
*/
// VERIFY: 6fbf3be5 auto
// OFFSET: 0x008E0360
FMOD_RESULT ChannelI::play(DSPI *dsp, bool paused, bool reset, bool startmuted)
{
    FMOD_RESULT result;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    result = alloc(dsp, reset);
    if (result != FMOD_OK)
    {
        return result;
    }

	result = setPaused(true);
    if (result != FMOD_OK)
    {
        return result;
    }

    if (startmuted)
    {
        mFadeVolume = 0.0f;
        mFadeTarget = 0.0f;
    }
    else
    {
        mFadeVolume = 1.0f;
        mFadeTarget = 1.0f;
    }

    if (reset)
    {
        result = setDefaults();
        if (result != FMOD_OK)
        {
            return result;
        }

        result = setPosition(0, FMOD_TIMEUNIT_PCM);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

	result = start();
    if (result != FMOD_OK)
    {
        return result;
    }

    if (reset)
    {
        FMOD_MODE mode;

        getMode(&mode);

        if (mode & FMOD_3D)
        {
	        FMOD_VECTOR vel = { 0, 0, 0 };

            result = set3DAttributes(&mSystem->mListener[0].mPosition, &vel);
            if (result != FMOD_OK)
            {
                return result;
            }

            result = update(0);
            if (result != FMOD_OK)
            {
                return result;
            }
        }
    }

    if (!paused)
    {
        result = setPaused(false);
        if (result != FMOD_OK)
        {
            return result;
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

	[SEE_ALSO]
]
*/
// VERIFY: b723577c auto
// OFFSET: 0x008DD670
FMOD_RESULT ChannelI::alloc(SoundI *sound, bool reset)
{
    FMOD_RESULT result;
    int count;

    if (reset)
    {
        mSyncPointCurrent = 0;

        if (mSystem)
        {
            mChannelGroup = mSystem->mChannelGroup;
        }

        mFlags             &= ~CHANNELI_FLAG_REALMUTE;
        mFlags             &= ~CHANNELI_FLAG_PAUSED;
        mFlags             &= ~CHANNELI_FLAG_MUTED;
        mFlags             &= ~CHANNELI_FLAG_FORCEVIRTUAL;

        for (count = 0; count < FMOD_CHANNEL_MAXINPUTCHANNELS; count++)
        {
            mInputMix[count] = 1.0f;
        }

        mVolume3D              = 1.0f;
        mFlags                &= ~CHANNELI_FLAG_MOVED;
        mConeVolume3D          = 1.0f;
        mPitch3D               = 1.0f;
        mDirectOcclusionVolume = 1.0f;
        mReverbDryVolume       = 1.0f;
        mMinDistance           = sound->mMinDistance;
        mMaxDistance           = sound->mMaxDistance;
        mDistance              = 0;
        mConeInsideAngle       = sound->mConeInsideAngle;
        mConeOutsideAngle      = sound->mConeOutsideAngle;
        mConeOutsideVolume     = sound->mConeOutsideVolume;
        mConeOrientation.x     = 0.0f;
        mConeOrientation.y     = 0.0f;
        mConeOrientation.z     = 1.0f;
        mRolloffPoint          = sound->mRolloffPoint;
        mNumRolloffPoints      = sound->mNumRolloffPoints;
        m3DPanLevel            = 1.0f;
        m3DDopplerLevel        = 1.0f;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        if (!mRealChannel[count])
        {
            return FMOD_ERR_INVALID_HANDLE;
        }

#ifdef FMOD_SUPPORT_STREAMING
        if (sound->isStream())
        {
            mRealChannel[count]->mSound = sound;
        }
        else
#endif
        {
            Sample *sample = SAFE_CAST(Sample, sound);

            mRealChannel[count]->mSound = mNumRealChannels > 1 ? sample->mSubSample[count] : sound;
        }

        mRealChannel[count]->mSubChannelIndex     = count;
        mRealChannel[count]->mDSP                 = 0;
        mRealChannel[count]->mMode                = sound->mMode;
        mRealChannel[count]->mLoopStart           = sound->mLoopStart;
        mRealChannel[count]->mLoopLength          = sound->mLoopLength;
        mRealChannel[count]->mLoopCount           = sound->mLoopCount;
        mRealChannel[count]->mLength              = sound->mLength;
        mRealChannel[count]->mDelayStart          = 0;
        mRealChannel[count]->mDelayEnd            = 0;
        mRealChannel[count]->mParent              = this;

        result = mRealChannel[count]->alloc();
        if (result != FMOD_OK)
        {
            return result;
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

	[SEE_ALSO]
]
*/
// VERIFY: 129db19f manual
// OFFSET: 0x008DD870
FMOD_RESULT ChannelI::alloc(DSPI *dsp, bool reset)
{
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        mRealChannel[count]->mSubChannelIndex = count;
        mRealChannel[count]->mSound           = 0;
        mRealChannel[count]->mDSP             = dsp;
        mRealChannel[count]->mLoopStart       = 0;
        mRealChannel[count]->mLoopLength      = (unsigned int)-1;
        mRealChannel[count]->mLoopCount       = -1;
        mRealChannel[count]->mMode            = FMOD_2D | FMOD_SOFTWARE;
        mRealChannel[count]->mDelayStart      = 0;
        mRealChannel[count]->mDelayEnd        = 0;
        mRealChannel[count]->mParent          = this;
    }

    if (reset)
    {
        mFlags             &= ~CHANNELI_FLAG_REALMUTE;
        mFlags             &= ~CHANNELI_FLAG_PAUSED;
        mFlags             &= ~CHANNELI_FLAG_MUTED;
        mFlags             &= ~CHANNELI_FLAG_FORCEVIRTUAL;

        for (count = 0; count < FMOD_CHANNEL_MAXINPUTCHANNELS; count++)
        {
            mInputMix[count] = 1.0f;
        }

        mVolume3D              = 1.0f;
        mFlags                &= ~CHANNELI_FLAG_MOVED;
        mConeVolume3D          = 1.0f;
        mPitch3D               = 1.0f;
        mRolloffPoint          = 0;
        mDirectOcclusionVolume = 1.0f;
        mNumRolloffPoints      = 0;
        mReverbDryVolume       = 1.0f;
        mMinDistance           = 1.0f;
        mMaxDistance           = 10000.0f;
        mDistance              = 0;
        mConeInsideAngle       = 360.0f;
        mConeOutsideAngle      = 360.0f;
        mConeOutsideVolume     = 1.0f;
        mConeOrientation.z     = 1.0f;
        mConeOrientation.x     = 0.0f;
        mConeOrientation.y     = 0.0f;
        m3DPanLevel            = 1.0f;
        m3DDopplerLevel        = 1.0f;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result;

        result = mRealChannel[count]->alloc(dsp);
        if (result != FMOD_OK)
        {
            return result;
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

	[SEE_ALSO]
]
*/
// VERIFY: 8ff75b94 auto
// OFFSET: 0x008DDA10
FMOD_RESULT ChannelI::start()
{
    FMOD_RESULT result;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        result = mRealChannel[count]->start();
        if (result != FMOD_OK)
        {
            return result;
        }

        mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_STOPPED;
        mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_ALLOCATED;
        mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_ENDDELAY;
        mRealChannel[count]->mFlags |= CHANNELREAL_FLAG_PLAYING;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 6c83883c manual
// OFFSET: 0x008DF5E0
FMOD_RESULT ChannelI::updateSyncPoints(bool seeking)
{
    FMOD_RESULT  result;
    SoundI      *sound = mRealChannel[0]->mSound;
    SoundI      *parent;
    SyncPoint   *head, *point;
    unsigned int position;
    int          index;

    if (!sound)
    {
        return FMOD_OK;
    }

    if (!mSyncPointCurrent)
    {
        if (sound->mSyncPointHead && sound->mNumSyncPoints)
        {
            mSyncPointCurrent = sound->mSyncPointHead;
        }
        if (!mSyncPointCurrent)
        {
            return FMOD_OK;
        }
    }

    parent = sound->mSubSampleParent;

    result = getPosition(&position, FMOD_TIMEUNIT_PCM);
    if (result != FMOD_OK)
    {
        return FMOD_OK;
    }

    head = parent->mSyncPointHead;

    if (mFrequency > 0)
    {
        index = 0;
        if (!head)
        {
            return FMOD_OK;
        }
        point = SAFE_CAST(SyncPoint, head->getNext());
    }
    else
    {
        index = parent->mNumSyncPoints - 1;
        if (!head)
        {
            return FMOD_OK;
        }
        point = SAFE_CAST(SyncPoint, head->getPrev());
    }

    if (!point)
    {
        return FMOD_OK;
    }

    if (seeking)
    {
        while ((mFrequency > 0 && position >= point->mOffset) || (mFrequency < 0 && position < point->mOffset))
        {
            if (mFrequency > 0)
            {
                if (point->mOffset >= position)
                {
                    return FMOD_OK;
                }

                mSyncPointCurrent = point;
                point = SAFE_CAST(SyncPoint, point->getNext());

                index++;
                if (index >= parent->mNumSyncPoints)
                {
                    return FMOD_OK;
                }
            }
            else
            {
                if (point->mOffset <= position)
                {
                    return FMOD_OK;
                }

                index--;
                mSyncPointCurrent = point;
                point = SAFE_CAST(SyncPoint, point->getPrev());

                if (index < 0)
                {
                    return FMOD_OK;
                }
            }
        }
    }
    else
    {
        if ((mFrequency > 0 && (int)position < (int)mSyncPointCurrent->mOffset) || (mFrequency < 0 && position > mSyncPointCurrent->mOffset))
        {
            mSyncPointCurrent = head;
        }

        while ((mFrequency > 0 && position >= point->mOffset) || (mFrequency < 0 && position < point->mOffset))
        {
            unsigned int current = mSyncPointCurrent->mOffset;

            if (mFrequency > 0)
            {
                if (((int)point->mOffset > (int)current && point->mOffset <= position) || ((int)point->mOffset < (int)current && point->mOffset >= position))
                {
                    if (mCallback[FMOD_CHANNEL_CALLBACKTYPE_SYNCPOINT])
                    {
                        mCallback[FMOD_CHANNEL_CALLBACKTYPE_SYNCPOINT]((FMOD_CHANNEL *)((FMOD_UINT_NATIVE)mHandleCurrent), FMOD_CHANNEL_CALLBACKTYPE_SYNCPOINT, mCallbackCommand[FMOD_CHANNEL_CALLBACKTYPE_SYNCPOINT], index, 0);
                    }

                    mSyncPointCurrent = point;
                    return FMOD_OK;
                }

                point = SAFE_CAST(SyncPoint, point->getNext());

                index++;
                if (index >= parent->mNumSyncPoints)
                {
                    return FMOD_OK;
                }
            }
            else
            {
                if ((point->mOffset > current && point->mOffset <= position) || (point->mOffset < current && point->mOffset >= position))
                {
                    if (mCallback[FMOD_CHANNEL_CALLBACKTYPE_SYNCPOINT])
                    {
                        mCallback[FMOD_CHANNEL_CALLBACKTYPE_SYNCPOINT]((FMOD_CHANNEL *)((FMOD_UINT_NATIVE)mHandleCurrent), FMOD_CHANNEL_CALLBACKTYPE_SYNCPOINT, mCallbackCommand[FMOD_CHANNEL_CALLBACKTYPE_SYNCPOINT], index, 0);
                    }

                    mSyncPointCurrent = point;
                    return FMOD_OK;
                }

                index--;
                point = SAFE_CAST(SyncPoint, point->getPrev());

                if (index < 0)
                {
                    return FMOD_OK;
                }
            }
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

	[SEE_ALSO]
]
*/
// VERIFY: 0a5f49c9 manual
// OFFSET: 0x008E0850
FMOD_RESULT ChannelI::update(int delta)
{
    FMOD_RESULT result;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        ChannelReal *realchannel = mRealChannel[count];
        bool playingpaused = (realchannel->mFlags & CHANNELREAL_FLAG_PLAYING) && (realchannel->mFlags & CHANNELREAL_FLAG_PAUSED);

        if ((realchannel->mMode & FMOD_3D) && !(realchannel->mMode & FMOD_3D_IGNOREGEOMETRY))
        {
            if (((mFlags & CHANNELI_FLAG_MOVED) || mSystem->mListener[0].mMoved || mSystem->mGeometryMgr.mMoved || playingpaused) && mSystem->mGeometryList)
            {
                FMOD_VECTOR channelpos;
                float gain, lingain;

                if (realchannel->mMode & FMOD_3D_HEADRELATIVE)
                {
                    FMOD_Vector_Add(&mPosition3D, &mSystem->mListener[0].mPosition, &channelpos);
                }
                else
                {
                    FMOD_Vector_Copy(&mPosition3D, &channelpos);
                }

                mSystem->mGeometryMgr.lineTestAll(&mSystem->mListener[0].mPosition, &channelpos, &mDirectOcclusionTarget, &mReverbOcclusionTarget);

                calculate3DReverbGain(&mSystem->mReverb3D, &channelpos, &gain);
                mSystem->mReverb3D.getPresenceGain(mIndex, &lingain);
                if (gain != lingain)
                {
                    mSystem->mReverb3D.setPresenceGain(mIndex, gain);
                }

                ReverbI* reverb_c;
                for (reverb_c = SAFE_CAST(ReverbI, mSystem->mReverb3DHead.getNext()); reverb_c != &mSystem->mReverb3DHead; reverb_c = SAFE_CAST(ReverbI, reverb_c->getNext()))
                {
                    if (reverb_c->getMode() == FMOD_REVERB_PHYSICAL)
                    {
                        float reverbgain, reverblingain;

                        calculate3DReverbGain(reverb_c, &channelpos, &reverbgain);
                        reverb_c->getPresenceGain(mIndex, &reverblingain);
                        if (reverbgain != reverblingain)
                        {
                            reverb_c->setPresenceGain(mIndex, reverbgain);
                        }
                    }
                }
            }

            if ((mDirectOcclusionTarget != mDirectOcclusion || mReverbOcclusionTarget != mReverbOcclusion) && delta)
            {
                if (playingpaused)
                {
                    mDirectOcclusion = mDirectOcclusionTarget;
                }
                else if (mDirectOcclusion < mDirectOcclusionTarget)
                {
                    mDirectOcclusion += delta * 0.002f;
                    if (mDirectOcclusion > mDirectOcclusionTarget)
                    {
                        mDirectOcclusion = mDirectOcclusionTarget;
                    }
                }
                else if (mDirectOcclusion > mDirectOcclusionTarget)
                {
                    mDirectOcclusion -= delta * 0.002f;
                    if (mDirectOcclusion < mDirectOcclusionTarget)
                    {
                        mDirectOcclusion = mDirectOcclusionTarget;
                    }
                }

                if (playingpaused)
                {
                    mReverbOcclusion = mReverbOcclusionTarget;
                }
                if (mReverbOcclusion < mReverbOcclusionTarget)
                {
                    mReverbOcclusion += delta * 0.002f;
                    if (mReverbOcclusion > mReverbOcclusionTarget)
                    {
                        mReverbOcclusion = mReverbOcclusionTarget;
                    }
                }
                else if (mReverbOcclusion > mReverbOcclusionTarget)
                {
                    mReverbOcclusion -= delta * 0.002f;
                    if (mReverbOcclusion < mReverbOcclusionTarget)
                    {
                        mReverbOcclusion = mReverbOcclusionTarget;
                    }
                }

                set3DOcclusionInternal(mDirectOcclusion, mReverbOcclusion, false);
            }
        }
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        ChannelReal *realchannel = mRealChannel[count];

        if (realchannel->mDelayEnd && realchannel->mFlags & CHANNELREAL_FLAG_ENDDELAY)
        {
            if (realchannel->mDelayEnd > (unsigned int)delta)
            {
                realchannel->mDelayEnd -= delta;
            }
            else
            {
                realchannel->mDelayEnd = 0;
                realchannel->mFlags &= ~CHANNELREAL_FLAG_ENDDELAY;
            }
        }
    }

    result = calcVolumeAndPitchFor3D(delta);
    if (result != FMOD_OK)
    {
        return result;
    }

	for (count = 0; count < mNumRealChannels; count++)
    {
        result = mRealChannel[count]->set2DFreqVolumePanFor3D();
        if (result != FMOD_OK)
        {
            return result;
        }
    }

    result = updateSyncPoints(false);
    if (result != FMOD_OK)
    {
        return result;
    }

    if (!(mFlags & CHANNELI_FLAG_JUSTWENTVIRTUAL))
    {
	    for (count = 0; count < mNumRealChannels; count++)
        {
            result = mRealChannel[count]->update(delta);
            if (result != FMOD_OK)
            {
                return result;
            }
        }
    }

    if ((mFlags & CHANNELI_FLAG_MOVED) || mSystem->mListener[0].mMoved)
    {
        result = updatePosition();
        if (result != FMOD_OK)
        {
            return result;
        }

        mFlags &= ~CHANNELI_FLAG_MOVED;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: c9f5766c auto
// OFFSET: 0x008DDA70
FMOD_RESULT ChannelI::calculate3DReverbGain(ReverbI *reverb, FMOD_VECTOR *channelpos, float *gain)
{
    float direct_o, reverb_o;
    FMOD_VECTOR reverbpos;

    if (!gain)
    {
        return FMOD_OK;
    }

    if (reverb == &(mSystem->mReverb3D))
    {
        *gain = 1.0f;
    }
    else
    {
        reverb->calculateDistanceGain(channelpos, gain, 0);
    }

    if (*gain > 0.0f)
    {
        reverb->get3DAttributes(&reverbpos, 0, 0);
        mSystem->mGeometryMgr.lineTestAll(channelpos, &reverbpos, &direct_o, &reverb_o);

        *gain *= (1.0f - reverb_o);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 884198ae auto
// OFFSET: 0x008E0440
FMOD_RESULT ChannelI::stop()
{
    return stopEx(true, true, true, true, true, true, false);
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 8c769b1c manual
// OFFSET: 0x008DF810
FMOD_RESULT ChannelI::stopEx(bool refstamp, bool updatelist, bool resetcallbacks, bool resetstate, bool callendcallback, bool resetchannelgroup, bool force)
{
    FMOD_RESULT result;
    int count;
    ChannelReal *oldrealchannel[FMOD_CHANNEL_MAXREALSUBCHANNELS];

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mRealChannel[0]->mFlags & CHANNELREAL_FLAG_STOPPED)
    {
        return FMOD_OK;
    }

    if (resetstate)
    {
        for (count = 0; count < mNumRealChannels; count++)
        {
            mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_IN_USE;
            mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_ALLOCATED;
            mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_PLAYING;
            mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_PAUSED;
            mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_PAUSEDFORSETPOS;
            mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_ENDDELAY;
            mRealChannel[count]->mFlags |=  CHANNELREAL_FLAG_STOPPED;
        }
    }

    if (resetchannelgroup)
    {
        result = setChannelGroupInternal(0, false);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

	for (count = 0; count < mNumRealChannels; count++)
    {
        result = mRealChannel[count]->stop(true, resetstate, force);
        if (result != FMOD_OK)
        {
            return result;
        }

        if (mRealChannel[count]->mSound)
        {
            mRealChannel[count]->mSound->mNumAudible--;
        }
    }

    if (updatelist)
    {
        result = returnToFreeList();
        if (result != FMOD_OK)
        {
            return result;
        }

        mListPosition = (unsigned int)-1;
    }

    mFlags &= ~(CHANNELI_FLAG_USEDINPUTMIX | CHANNELI_FLAG_USEDADDDSP | CHANNELI_FLAG_JUSTWENTVIRTUAL);

	for (count = 0; count < mNumRealChannels; count++)
    {
        oldrealchannel[count] = mRealChannel[count];
    }

    if (callendcallback && mCallback[FMOD_CHANNEL_CALLBACKTYPE_END])
    {
        mCallback[FMOD_CHANNEL_CALLBACKTYPE_END]((FMOD_CHANNEL *)((FMOD_UINT_NATIVE)mHandleCurrent), FMOD_CHANNEL_CALLBACKTYPE_END, mCallbackCommand[FMOD_CHANNEL_CALLBACKTYPE_END], 0, 0);
    }

	for (count = 0; count < mNumRealChannels; count++)
    {
        if (!(mRealChannel[count]->mFlags & CHANNELREAL_FLAG_PLAYING) || oldrealchannel[count] != mRealChannel[count])
        {
            bool crit = false;

            if (oldrealchannel[count]->mSound && oldrealchannel[count]->mSound->isStream())
            {
                crit = true;
                FMOD_OS_CriticalSection_Enter(mSystem->mStreamRealchanCrit);
            }

            oldrealchannel[count]->mSound  = 0;
            oldrealchannel[count]->mDSP    = 0;
            oldrealchannel[count]->mParent = 0;

            if (crit)
            {
                FMOD_OS_CriticalSection_Leave(mSystem->mStreamRealchanCrit);
            }
        }
    }

    if (mListPosition == (unsigned int)-1)
    {
	    for (count = 0; count < mNumRealChannels; count++)
        {
            mRealChannel[count] = 0;
        }

        if (resetcallbacks)
        {
            for (count = 0; count < FMOD_CHANNEL_CALLBACKTYPE_MAX; count++)
            {
                mCallback[count]        = 0;
                mCallbackCommand[count] = 0;
            }

            mSyncPointCurrent = 0;
        }

        if (refstamp)
        {
            referenceStamp();
        }
    }

    if (mLevels)
    {
        mSystem->mSpeakerLevelsPool.free(mLevels);
        mLevels = 0;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 936aa7f2 manual
// OFFSET: 0x008DDB00
FMOD_RESULT ChannelI::setPaused(bool paused)
{
    FMOD_RESULT result = FMOD_OK;
    int count;
	ChannelGroupI *current;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (paused)
    {
        mFlags |= CHANNELI_FLAG_PAUSED;
    }
    else
    {
        mFlags &= ~CHANNELI_FLAG_PAUSED;
    }

	current = mChannelGroup;
	do
	{
		if (current->mPaused)
		{
			paused = true;
			break;
		}

		current = current->mParent;
	} while (current);

	for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->setPaused(paused);
        if (result == FMOD_OK)
        {
            result = result2;
        }

	    if (paused)
	    {
            mRealChannel[count]->mFlags |= CHANNELREAL_FLAG_PAUSED;
	    }
	    else
	    {
            mRealChannel[count]->mFlags &= ~CHANNELREAL_FLAG_PAUSED;
	    }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getPaused(bool *paused)
{
    if (!paused)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    *paused = mFlags & CHANNELI_FLAG_PAUSED ? true : false;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: afb24016 manual
// OFFSET: 0x008DDB90
FMOD_RESULT ChannelI::setVolume(float volume, bool forceupdatepos)
{
    FMOD_RESULT   result = FMOD_OK;
    int           count;
    bool          updatepos = false;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (volume < 0)
    {
        volume = 0;
    }
    if (volume > 1.0f)
    {
        volume = 1.0f;
    }

    if (mVolume != volume)
    {
        updatepos = true;
    }

    mVolume = volume;

    if (mFlags & CHANNELI_FLAG_REALMUTE)
    {
        volume = 0;
    }

    if (mNumRealChannels > 1 && mLevels)
    {
        for (count = 0; count < mNumRealChannels; count++)
        {
            result = mRealChannel[count]->updateSpeakerLevels(volume);
        }
    }
    else
    {
        for (count = 0; count < mNumRealChannels; count++)
        {
            result = mRealChannel[count]->setVolume(volume);
        }
    }

    if (updatepos || forceupdatepos)
    {
        return updatePosition();
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 353d8189 auto
// OFFSET: 0x008DDC90
FMOD_RESULT ChannelI::getVolume(float *volume)
{
    if (!volume)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    *volume = mVolume;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: c08bac45 manual
// OFFSET: 0x008DDCC0
FMOD_RESULT ChannelI::setFrequency(float frequency)
{
    FMOD_RESULT result = FMOD_OK;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (frequency < mRealChannel[0]->mMinFrequency)
    {
        frequency = mRealChannel[0]->mMinFrequency;
    }
    if (frequency > mRealChannel[0]->mMaxFrequency)
    {
        frequency = mRealChannel[0]->mMaxFrequency;
    }

    mFrequency = frequency;

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->setFrequency(mFrequency);
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 21647fa1 auto
// OFFSET: 0x008DDD60
FMOD_RESULT ChannelI::getFrequency(float *frequency)
{
    if (!frequency)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    *frequency = mFrequency;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: bd83d946 auto
// OFFSET: 0x008DDD90
FMOD_RESULT ChannelI::setPan(float pan, bool calldriver)
{
    FMOD_RESULT result = FMOD_OK;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (pan < -1.0f)
    {
        pan = -1.0f;
    }
    if (pan > 1.0f)
    {
        pan = 1.0f;
    }

    mPan = pan;
    mLastPanMode = FMOD_CHANNEL_PANMODE_PAN;

    if (mRealChannel[0]->mMode & FMOD_3D)
    {
        return FMOD_OK;
    }
    else if (calldriver)
    {
        int count;

        for (count = 0; count < mNumRealChannels; count++)
        {
            FMOD_RESULT result2;

            if (mNumRealChannels > 1)
            {
                if (mNumRealChannels == 2 || (mRealChannel[0]->mSound && mRealChannel[0]->mSound->mDefaultChannelMask == SPEAKER_ALLSTEREO))
                {
                    if (!(count & 1))
                    {
                        pan = -1;
                    }
                    else
                    {
                        pan = 1;
                    }
                }
            }

            result2 = mRealChannel[count]->setPan(pan);
            if (result == FMOD_OK)
            {
                result = result2;
            }
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getPan(float *pan)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mRealChannel[0]->mMode & FMOD_3D)
    {
        return FMOD_ERR_NEEDS2D;
    }

    if (!pan)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *pan = mPan;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: c91bf4a0 auto
// OFFSET: 0x008DDEA0
FMOD_RESULT ChannelI::setDelay(unsigned int startdelay, unsigned int enddelay)
{
    FMOD_RESULT result = FMOD_OK;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->setDelay(startdelay, enddelay);
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getDelay(unsigned int *startdelay, unsigned int *enddelay)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (startdelay)
    {
        *startdelay = mRealChannel[0]->mDelayStart;
    }
    if (enddelay)
    {
        *enddelay = mRealChannel[0]->mDelayEnd;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 5860909c auto
// OFFSET: 0x008DDF00
FMOD_RESULT ChannelI::setSpeakerMix(float frontleft, float frontright, float center, float lfe, float backleft, float backright, float sideleft, float sideright, bool calldriver)
{
    FMOD_RESULT result = FMOD_OK;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    mSpeakerFL  = frontleft  < 0 ? 0 : frontleft  > 5.0f ? 5.0f : frontleft ;
    mSpeakerFR  = frontright < 0 ? 0 : frontright > 5.0f ? 5.0f : frontright;
    mSpeakerC   = center     < 0 ? 0 : center     > 5.0f ? 5.0f : center    ;
    mSpeakerLFE = lfe        < 0 ? 0 : lfe        > 5.0f ? 5.0f : lfe;
    mSpeakerBL  = backleft   < 0 ? 0 : backleft   > 5.0f ? 5.0f : backleft  ;
    mSpeakerBR  = backright  < 0 ? 0 : backright  > 5.0f ? 5.0f : backright ;
    mSpeakerSL  = sideleft   < 0 ? 0 : sideleft   > 5.0f ? 5.0f : sideleft  ;
    mSpeakerSR  = sideright  < 0 ? 0 : sideright  > 5.0f ? 5.0f : sideright ;

    mLastPanMode = FMOD_CHANNEL_PANMODE_SPEAKERMIX;

    if (mRealChannel[0]->mMode & FMOD_3D)
    {
        return FMOD_OK;
    }
    else if (calldriver)
    {
        int count;

        for (count = 0; count < mNumRealChannels; count++)
        {
            FMOD_RESULT result2;

            result2 = mRealChannel[count]->setSpeakerMix(mSpeakerFL, mSpeakerFR, mSpeakerC, mSpeakerLFE, mSpeakerBL, mSpeakerBR, mSpeakerSL, mSpeakerSR);
            if (result == FMOD_OK)
            {
                result = result2;
            }
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getSpeakerMix(float *frontleft, float *frontright, float *center, float *lfe, float *backleft, float *backright, float *sideleft, float *sideright)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (frontleft)
    {
        *frontleft = mSpeakerFL;
    }
    if (frontright)
    {
        *frontright = mSpeakerFR;
    }
    if (center)
    {
        *center = mSpeakerC;
    }
    if (lfe)
    {
        *lfe = mSpeakerLFE;
    }
    if (backleft)
    {
        *backleft = mSpeakerBL;
    }
    if (backright)
    {
        *backright = mSpeakerBR;
    }
    if (sideleft)
    {
        *sideleft = mSpeakerSL;
    }
    if (sideright)
    {
        *sideright = mSpeakerSR;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 111d3274 manual
// OFFSET: 0x008DE100
FMOD_RESULT ChannelI::setSpeakerLevels(FMOD_SPEAKER speaker, float *levels, int numlevels, bool calldriver)
{
    FMOD_RESULT result = FMOD_OK;
    int count;
    float actuallevels[DSP_MAXLEVELS_IN];

    if (!levels)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (numlevels > mSystem->mMaxInputChannels)
    {
        return FMOD_ERR_TOOMANYCHANNELS;
    }

    if (speaker < 0 || speaker > FMOD_SPEAKER_SIDE_RIGHT)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    switch (mSystem->mSpeakerMode)
    {
        case FMOD_SPEAKERMODE_MONO:
        {
            if (speaker != FMOD_SPEAKER_MONO)
            {
                return FMOD_ERR_INVALID_SPEAKER;
            }
            break;
        }
        case FMOD_SPEAKERMODE_STEREO:
        {
            if (speaker != FMOD_SPEAKER_FRONT_LEFT && speaker != FMOD_SPEAKER_FRONT_RIGHT)
            {
                return FMOD_ERR_INVALID_SPEAKER;
            }
            break;
        }
        case FMOD_SPEAKERMODE_QUAD:
        {
            if (speaker != FMOD_SPEAKER_FRONT_LEFT && speaker != FMOD_SPEAKER_FRONT_RIGHT &&
                speaker != FMOD_SPEAKER_BACK_LEFT  && speaker != FMOD_SPEAKER_BACK_RIGHT)
            {
                return FMOD_ERR_INVALID_SPEAKER;
            }
            if (speaker == FMOD_SPEAKER_BACK_LEFT)
            {
                speaker = (FMOD_SPEAKER)2;
            }
            if (speaker == FMOD_SPEAKER_BACK_RIGHT)
            {
                speaker = (FMOD_SPEAKER)3;
            }
            break;
        }
        case FMOD_SPEAKERMODE_SURROUND:
        {
            if (speaker != FMOD_SPEAKER_FRONT_LEFT   && speaker != FMOD_SPEAKER_FRONT_RIGHT &&
                speaker != FMOD_SPEAKER_BACK_LEFT    && speaker != FMOD_SPEAKER_BACK_RIGHT &&
                speaker != FMOD_SPEAKER_FRONT_CENTER)
            {
                return FMOD_ERR_INVALID_SPEAKER;
            }
            break;
        }
        case FMOD_SPEAKERMODE_PROLOGIC:
        case FMOD_SPEAKERMODE_5POINT1:
        {
            if (speaker != FMOD_SPEAKER_FRONT_LEFT   && speaker != FMOD_SPEAKER_FRONT_RIGHT &&
                speaker != FMOD_SPEAKER_BACK_LEFT    && speaker != FMOD_SPEAKER_BACK_RIGHT &&
                speaker != FMOD_SPEAKER_FRONT_CENTER && speaker != FMOD_SPEAKER_LOW_FREQUENCY)
            {
                return FMOD_ERR_INVALID_SPEAKER;
            }
            break;
        }
        default:
        {
            break;
        }
    };

    FMOD_memset(actuallevels, 0, DSP_MAXLEVELS_IN * sizeof(float));

    if (!mLevels)
    {
        mSystem->mSpeakerLevelsPool.alloc(&mLevels);
        if (!mLevels)
        {
            return FMOD_ERR_MEMORY;
        }
    }

    for (count = 0; count < numlevels; count++)
    {
        float volume = levels[count];

        if (volume < 0)
        {
            volume = 0;
        }
        if (volume > 1.0f)
        {
            volume = 1.0f;
        }

        mLevels[(speaker * mSystem->mMaxInputChannels) + count] = volume;
        actuallevels[count] = volume;
    }

    mLastPanMode = FMOD_CHANNEL_PANMODE_SPEAKERLEVELS;

    if (mRealChannel[0]->mMode & FMOD_3D)
    {
        return FMOD_OK;
    }
    else if (calldriver)
    {
        for (count = 0; count < mNumRealChannels; count++)
        {
            FMOD_RESULT result2;

            result2 = mRealChannel[count]->setSpeakerLevels(speaker, actuallevels, numlevels);
            if (result == FMOD_OK)
            {
                result = result2;
            }
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 8fe91578 manual
// OFFSET: 0x008DE460
FMOD_RESULT ChannelI::getSpeakerLevels(FMOD_SPEAKER speaker, float *levels, int numlevels)
{
    int count;

    if (!levels || !numlevels)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (speaker < 0 || speaker > FMOD_SPEAKER_SIDE_RIGHT)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mLevels)
    {
        for (count = 0; count < numlevels; count++)
        {
            levels[count] = mLevels[count + (speaker * mSystem->mMaxInputChannels)];
        }
    }
    else
    {
        for (count = 0; count < numlevels; count++)
        {
            levels[count] = 0;
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

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::setInputChannelMix(float *levels, int numlevels)
{
    int count, diff;

    if (numlevels > FMOD_CHANNEL_MAXINPUTCHANNELS)
    {
        return FMOD_ERR_TOOMANYCHANNELS;
    }

    if (!levels)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    diff = 0;
    for (count = 0; count < numlevels; count++)
    {
        if (mInputMix[count] != levels[count])
        {
            diff++;
        }

        mInputMix[count] = levels[count];
    }

    if (!diff && (mFlags & CHANNELI_FLAG_USEDINPUTMIX))
    {
        return FMOD_OK;
    }

    mFlags |= CHANNELI_FLAG_USEDINPUTMIX;

    setVolume(mVolume);

    if (mLastPanMode == FMOD_CHANNEL_PANMODE_PAN)
    {
        setPan(mPan, true);
    }
    else if (mLastPanMode == FMOD_CHANNEL_PANMODE_SPEAKERMIX)
    {
        setSpeakerMix(mSpeakerFL,  mSpeakerFR,  mSpeakerC,  mSpeakerLFE,  mSpeakerBL,  mSpeakerBR,  mSpeakerSL,  mSpeakerSR, true);
    }
    else if (mLastPanMode == FMOD_CHANNEL_PANMODE_SPEAKERLEVELS)
    {
        if (mLevels)
        {
            for (count = 0; count < mSystem->mMaxOutputChannels; count++)
            {
                setSpeakerLevels((FMOD_SPEAKER)count, &mLevels[count * mSystem->mMaxInputChannels], mSystem->mMaxInputChannels, true);
            }
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

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getInputChannelMix(float *levels, int numlevels)
{
    int count;

    if (numlevels > FMOD_CHANNEL_MAXINPUTCHANNELS)
    {
        return FMOD_ERR_TOOMANYCHANNELS;
    }

    if (!levels)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    for (count = 0; count < numlevels; count++)
    {
        levels[count] = mInputMix[count];
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 655af493 manual
// OFFSET: 0x008DE5A0
FMOD_RESULT ChannelI::setMute(bool mute)
{
    FMOD_RESULT result = FMOD_OK;
    ChannelGroupI *current;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mute)
    {
        mFlags |= CHANNELI_FLAG_MUTED;
    }
    else
    {
        mFlags &= ~CHANNELI_FLAG_MUTED;
    }

	current = mChannelGroup;
	do
	{
		if (current->mMute)
		{
			mute = true;
			break;
		}

		current = current->mParent;
	} while (current);

	if (!mute)
    {
        mFlags &= ~CHANNELI_FLAG_REALMUTE;

        return setVolume(mVolume);
    }

    mFlags |= CHANNELI_FLAG_REALMUTE;

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->setVolume(0);
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

	return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getMute(bool *mute)
{
    if (!mute)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (mFlags & CHANNELI_FLAG_MUTED)
    {
        *mute = true;
    }
    else
    {
        *mute = false;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: f309c318 auto
// OFFSET: 0x008E0700
FMOD_RESULT ChannelI::setPriority(int priority)
{
    if (priority < 0 || priority > FMOD_MAXPRIORITIES)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    mPriority = priority;

    return updatePosition();
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 53f13530 auto
// OFFSET: 0x008DE660
FMOD_RESULT ChannelI::getPriority(int *priority)
{
    if (!priority)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *priority = mPriority;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 521665cc manual
// OFFSET: 0x008DE680
FMOD_RESULT ChannelI::set3DAttributes(const FMOD_VECTOR *pos, const FMOD_VECTOR *vel)
{
    FMOD_RESULT result = FMOD_OK;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (pos)
    {
        if (mPosition3D.x != pos->x ||
            mPosition3D.y != pos->y ||
            mPosition3D.z != pos->z)
        {
            mFlags |= CHANNELI_FLAG_MOVED;
        }

        mPosition3D = *pos;
    }

    if (vel)
    {
        if (mVelocity3D.x != vel->x ||
            mVelocity3D.y != vel->y ||
            mVelocity3D.z != vel->z)
        {
            mFlags |= CHANNELI_FLAG_MOVED;
        }

		mVelocity3D = *vel;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_OK;
    }

    if (mRealChannel[0]->mFlags & CHANNELREAL_FLAG_PAUSED)
    {
        update(0);
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->set3DAttributes();
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 8d410c66 auto
// OFFSET: 0x008DE7B0
FMOD_RESULT ChannelI::get3DAttributes(FMOD_VECTOR *pos, FMOD_VECTOR *vel)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (pos)
    {
        pos->x = mPosition3D.x;
        pos->y = mPosition3D.y;
        pos->z = mPosition3D.z;
    }

    if (vel)
    {
        vel->x = mVelocity3D.x;
        vel->y = mVelocity3D.y;
        vel->z = mVelocity3D.z;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: a33cab83 auto
// OFFSET: 0x008E0CC0
FMOD_RESULT ChannelI::set3DMinMaxDistance(float mindistance, float maxdistance)
{
    FMOD_RESULT result = FMOD_OK;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    FMOD_MODE mode = mRealChannel[0]->mMode;

    if (!(mode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

	if (mindistance < 0 || maxdistance < 0 || maxdistance < mindistance)
	{
		return FMOD_ERR_INVALID_PARAM;
	}

    if (mMinDistance == mindistance && mMaxDistance == maxdistance)
    {
        return FMOD_OK;
    }

    mMinDistance = mindistance;
    mMaxDistance = maxdistance;

    if (mode & (FMOD_3D_LOGROLLOFF | FMOD_3D_LINEARROLLOFF | FMOD_3D_CUSTOMROLLOFF) || mSystem->mRolloffCallback)
    {
        mFlags |= CHANNELI_FLAG_MOVED;

        result = update(0);
        if (result != FMOD_OK)
        {
            return result;
        }

        result = setVolume(mVolume);
        if (result != FMOD_OK)
        {
            return result;
        }

        return FMOD_OK;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->set3DMinMaxDistance();
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

    mFlags |= CHANNELI_FLAG_MOVED;

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: a8aec6c8 auto
// OFFSET: 0x008DE820
FMOD_RESULT ChannelI::get3DMinMaxDistance(float *mindistance, float *maxdistance)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (mindistance)
    {
        *mindistance = mMinDistance;
    }
    if (maxdistance)
    {
        *maxdistance = mMaxDistance;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 99fb2c3f manual
// OFFSET: 0x008DE870
FMOD_RESULT ChannelI::set3DConeSettings(float insideconeangle, float outsideconeangle, float outsidevolume)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (insideconeangle > outsideconeangle)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (outsidevolume > 1.0f)
    {
        outsidevolume = 1.0f;
    }
    if (outsidevolume < 0.0f)
    {
        outsidevolume = 0.0f;
    }

    mFlags             |= CHANNELI_FLAG_MOVED;
    mConeInsideAngle    = insideconeangle;
    mConeOutsideAngle   = outsideconeangle;
    mConeOutsideVolume  = outsidevolume;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::get3DConeSettings(float *insideconeangle, float *outsideconeangle, float *outsidevolume)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

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

	[SEE_ALSO]
]
*/
// VERIFY: f15f124f auto
// OFFSET: 0x008DE8F0
FMOD_RESULT ChannelI::set3DConeOrientation(FMOD_VECTOR *orientation)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (!orientation)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    mConeOrientation = *orientation;
    mFlags |= CHANNELI_FLAG_MOVED;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::get3DConeOrientation(FMOD_VECTOR *orientation)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (orientation)
    {
        *orientation = mConeOrientation;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::set3DCustomRolloff(FMOD_VECTOR *points, int numpoints)
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
    mFlags |= CHANNELI_FLAG_MOVED;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::get3DCustomRolloff(FMOD_VECTOR **points, int *numpoints)
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

	[SEE_ALSO]
]
*/
// VERIFY: c66be9ef manual
// OFFSET: 0x008E0730
FMOD_RESULT ChannelI::set3DOcclusionInternal(float direct, float reverb, bool resettarget)
{
    FMOD_RESULT result = FMOD_OK;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (direct < 0)
    {
        direct = 0;
    }
    if (reverb < 0)
    {
        reverb = 0;
    }
    if (direct > 1)
    {
        direct = 1;
    }
    if (reverb > 1)
    {
        reverb = 1;
    }

    mDirectOcclusion = direct;
    mReverbOcclusion = reverb;

    if (resettarget)
    {
        mDirectOcclusionTarget = mDirectOcclusion;
        mReverbOcclusionTarget = mReverbOcclusion;
    }

    mDirectOcclusionVolume = 1.0f - direct;

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->set3DOcclusion(direct, reverb);
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

    return updatePosition();
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::set3DOcclusion(float direct, float reverb)
{
    return set3DOcclusionInternal(direct, reverb, true);
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::get3DOcclusion(float *directOcclusion, float *reverbOcclusion)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (directOcclusion)
    {
        *directOcclusion = mDirectOcclusion;
    }
    if (reverbOcclusion)
    {
        *reverbOcclusion = mReverbOcclusion;
    }

	return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::set3DSpread(float angle)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (angle < 0.0f || angle > 360.0f)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    mSpread = angle;

	return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::get3DSpread(float *angle)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (!angle)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *angle = mSpread;

	return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: eb2bd054 auto
// OFFSET: 0x008E0E00
FMOD_RESULT ChannelI::set3DPanLevel(float level)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (mRealChannel[0]->mMode & FMOD_HARDWARE)
    {
        return FMOD_ERR_NEEDSSOFTWARE;
    }

    if (level < 0.0f || level > 1.0f)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    m3DPanLevel = level;

    if (mRealChannel[0]->mFlags & CHANNELREAL_FLAG_PAUSED && m3DPanLevel < 1.0f)
    {
        return update(0);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 384571c7 auto
// OFFSET: 0x008DE950
FMOD_RESULT ChannelI::get3DPanLevel(float *level)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (!level)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *level = m3DPanLevel;

	return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::set3DDopplerLevel(float level)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (level < 0.0f || level > 5.0f)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    m3DDopplerLevel = level;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::get3DDopplerLevel(float *level)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (!(mRealChannel[0]->mMode & FMOD_3D))
    {
        return FMOD_ERR_NEEDS3D;
    }

    if (!level)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *level = m3DDopplerLevel;

	return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: b711343a auto
// OFFSET: 0x008DE990
FMOD_RESULT ChannelI::setReverbProperties(const FMOD_REVERB_CHANNELPROPERTIES *prop)
{
    FMOD_RESULT result = FMOD_OK;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->setReverbProperties(prop);
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: fff11f42 auto
// OFFSET: 0x008DE9F0
FMOD_RESULT ChannelI::getReverbProperties(FMOD_REVERB_CHANNELPROPERTIES *prop)
{
    FMOD_RESULT result = FMOD_OK;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->getReverbProperties(prop);
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 158c71ec manual
// OFFSET: 0x008DEA50
FMOD_RESULT ChannelI::setChannelGroupInternal(ChannelGroupI *channelgroup, bool resetattributes)
{
    FMOD_RESULT result = FMOD_OK;
    ChannelGroupI *oldchannelgroup = mChannelGroup;
    int count;

    if (mChannelGroup)
    {
        if (mChannelGroupNode.isEmpty())
        {
            return FMOD_OK;
        }

        mChannelGroup->mNumChannels--;
        mChannelGroupNode.removeNode();
    }

    if (!channelgroup)
    {
        channelgroup = mSystem->mChannelGroup;
    }

    mChannelGroup = channelgroup;

    mChannelGroupNode.addAfter(&mChannelGroup->mChannelHead);
    mChannelGroupNode.setData(this);
    mChannelGroup->mNumChannels++;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (resetattributes)
    {
        float levels[DSP_MAXLEVELS_OUT][DSP_MAXLEVELS_IN];

        if (mLastPanMode == FMOD_CHANNEL_PANMODE_SPEAKERLEVELS)
        {
            for (count = 0; count < mSystem->mMaxOutputChannels; count++)
            {
                getSpeakerLevels((FMOD_SPEAKER)count, levels[count], mSystem->mMaxInputChannels);
            }
        }

        for (count = 0; count < mNumRealChannels; count++)
        {
            FMOD_RESULT result2;

            result2 = mRealChannel[count]->moveChannelGroup(oldchannelgroup, mChannelGroup);
            if (result == FMOD_OK)
            {
                result = result2;
            }
        }

        if (mChannelGroup->mMute)
        {
            setMute(mChannelGroup->mMute);
        }
        if (mChannelGroup->mPaused)
        {
            setPaused(mChannelGroup->mPaused);
        }

        setVolume(mVolume);

        if (mRealChannel[0]->mMode & FMOD_3D)
        {
            if (mRealChannel[0]->mFlags & CHANNELREAL_FLAG_PAUSED)
            {
                update(0);
            }
        }
        else
        {
            if (mLastPanMode == FMOD_CHANNEL_PANMODE_PAN)
            {
                setPan(mPan);
            }
            else if (mLastPanMode == FMOD_CHANNEL_PANMODE_SPEAKERMIX)
            {
                setSpeakerMix(mSpeakerFL, mSpeakerFR, mSpeakerC, mSpeakerLFE, mSpeakerBL, mSpeakerBR, mSpeakerSL, mSpeakerSR);
            }
            else if (mLastPanMode == FMOD_CHANNEL_PANMODE_SPEAKERLEVELS)
            {
                for (count = 0; count < mSystem->mMaxOutputChannels; count++)
                {
                    setSpeakerLevels((FMOD_SPEAKER)count, levels[count], mSystem->mMaxInputChannels);
                }
            }
        }

        setFrequency(mFrequency);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: c6439ad7 auto
// OFFSET: 0x008DECC0
FMOD_RESULT ChannelI::setChannelGroup(ChannelGroupI *channelgroup)
{
    return setChannelGroupInternal(channelgroup, true);
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getChannelGroup(ChannelGroupI **channelgroup)
{
    if (!channelgroup)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *channelgroup = mChannelGroup;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 6a5f51a8 manual
// OFFSET: 0x008DECE0
FMOD_RESULT ChannelI::isPlaying(bool *isplaying)
{
    FMOD_RESULT result;
    int count;

    if (!isplaying)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *isplaying = false;

    if (mHandleCurrent != mHandleOriginal || !mRealChannel)
    {
        return FMOD_ERR_CHANNEL_STOLEN;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mRealChannel[0]->mFlags & CHANNELREAL_FLAG_STOPPED)
    {
        *isplaying = false;
        return FMOD_OK;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        bool isplayingtemp;

        result = mRealChannel[count]->isPlaying(&isplayingtemp);
        if (result != FMOD_OK)
        {
            return result;
        }

        if (isplayingtemp)
        {
            *isplaying = true;
            return FMOD_OK;
        }
    }

    if (!*isplaying)
    {
        mListPosition = (unsigned int)-1;

        if (mSortedListNode.getData())
        {
            mSortedListNode.removeNode();
            mSortedListNode.addBefore(&mSystem->mChannelSortedListHead);
            mSortedListNode.setData(this);
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

	[SEE_ALSO]
]
*/
// VERIFY: 8d58d807 manual
// OFFSET: 0x008DEDE0
FMOD_RESULT ChannelI::isVirtual(bool *isvirtual)
{
    if (!isvirtual)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        *isvirtual = false;
        return FMOD_ERR_INVALID_HANDLE;
    }

    return mRealChannel[0]->isVirtual(isvirtual);
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 2af0fe73 auto
// OFFSET: 0x008DEE20
FMOD_RESULT ChannelI::getAudibility(float *audibility)
{
    if (!audibility)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mRealChannel[0]->mMode & FMOD_3D)
    {
        *audibility = mChannelGroup->mRealDirectOcclusionVolume * mChannelGroup->mRealVolume * mConeVolume3D * mVolume3D * mReverbDryVolume * mDirectOcclusionVolume * mVolume;
    }
    else
    {
        *audibility = mChannelGroup->mRealVolume * mVolume;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: ba6aefc6 auto
// OFFSET: 0x008DEE90
FMOD_RESULT ChannelI::getFadeVolume(float *fadevolume)
{
    if (!fadevolume)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    *fadevolume = mFadeVolume;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: fb9b957d auto
// OFFSET: 0x008DEEC0
FMOD_RESULT ChannelI::getCurrentSound(SoundI **sound)
{
    if (!sound)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        *sound = 0;
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mRealChannel[0]->mSound)
    {
        *sound = mRealChannel[0]->mSound->mSubSampleParent;
    }
    else
    {
        *sound = 0;
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 83132577 auto
// OFFSET: 0x008DEF10
FMOD_RESULT ChannelI::getCurrentDSP(DSPI **dsp)
{
    if (!dsp)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        *dsp = 0;
        return FMOD_ERR_INVALID_HANDLE;
    }

    *dsp = mRealChannel[0]->mDSP;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getSpectrum(float *spectrumarray, int numvalues, int channel, FMOD_DSP_FFT_WINDOW windowtype)
{
#ifdef FMOD_SUPPORT_GETSPECTRUM
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mNumRealChannels > 1)
    {
        if (channel >= mNumRealChannels || channel < 0)
        {
            return FMOD_ERR_INVALID_PARAM;
        }

        return mRealChannel[channel]->getSpectrum(spectrumarray, numvalues, 0, windowtype);
    }
    else
    {
        return mRealChannel[0]->getSpectrum(spectrumarray, numvalues, channel, windowtype);
    }
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

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getWaveData(float *wavearray, int numvalues, int channel)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mNumRealChannels > 1)
    {
        if (channel >= mNumRealChannels || channel < 0)
        {
            return FMOD_ERR_INVALID_PARAM;
        }

        return mRealChannel[channel]->getWaveData(wavearray, numvalues, 0);
    }
    else
    {
        return mRealChannel[0]->getWaveData(wavearray, numvalues, channel);
    }
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getIndex(int *index)
{
    if (!index)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *index = mIndex;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 77494d19 auto
// OFFSET: 0x008DEF40
FMOD_RESULT ChannelI::setCallback(FMOD_CHANNEL_CALLBACKTYPE type, FMOD_CHANNEL_CALLBACK callback, int command)
{
    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (type < 0 || type >= FMOD_CHANNEL_CALLBACKTYPE_MAX)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    mCallback[type]        = callback;
    mCallbackCommand[type] = command;

    if (type == FMOD_CHANNEL_CALLBACKTYPE_VIRTUALVOICE && mCallback[FMOD_CHANNEL_CALLBACKTYPE_VIRTUALVOICE])
    {
        FMOD_RESULT result;
        bool isvirtual = false;

        result = isVirtual(&isvirtual);
        if (result != FMOD_OK)
        {
            return result;
        }

        mCallback[FMOD_CHANNEL_CALLBACKTYPE_VIRTUALVOICE]((FMOD_CHANNEL *)((FMOD_UINT_NATIVE)mHandleCurrent), FMOD_CHANNEL_CALLBACKTYPE_VIRTUALVOICE, mCallbackCommand[FMOD_CHANNEL_CALLBACKTYPE_VIRTUALVOICE], isvirtual ? 1 : 0, 0);
    }

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 959f1972 manual
// OFFSET: 0x008DFA80
FMOD_RESULT ChannelI::setPosition(unsigned int position, FMOD_TIMEUNIT postype)
{
    FMOD_RESULT result = FMOD_OK;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if (mRealChannel[0]->mSound)
    {
        SoundI *soundi = mRealChannel[0]->mSound->mSubSampleParent;
        unsigned int   length;

#ifdef FMOD_SUPPORT_SENTENCING
        if (postype == FMOD_TIMEUNIT_SENTENCE_MS ||
            postype == FMOD_TIMEUNIT_SENTENCE_PCM ||
            postype == FMOD_TIMEUNIT_SENTENCE_PCMBYTES ||
            postype == FMOD_TIMEUNIT_SENTENCE_SUBSOUND)
        {
            unsigned int  sentenceid, subsound;

            if (!soundi->mSubSoundList)
            {
                return FMOD_ERR_INVALID_PARAM;
            }

            if (postype == FMOD_TIMEUNIT_SENTENCE_SUBSOUND)
            {
                if (position >= (unsigned int)soundi->mSubSoundListNum)
                {
                    return FMOD_ERR_INVALID_PARAM;
                }

                sentenceid = position;
                postype    = FMOD_TIMEUNIT_PCM;
                position   = 0;
            }
            else
            {
                result = getPosition(&sentenceid, FMOD_TIMEUNIT_SENTENCE);
                if (result != FMOD_OK)
                {
                    return result;
                }
            }

            subsound = soundi->mSubSoundList[sentenceid];

            if (postype == FMOD_TIMEUNIT_SENTENCE_MS)
            {
                postype = FMOD_TIMEUNIT_MS;
            }
            else if (postype == FMOD_TIMEUNIT_SENTENCE_PCM)
            {
                postype = FMOD_TIMEUNIT_PCM;
            }
            else if (postype == FMOD_TIMEUNIT_SENTENCE_PCMBYTES)
            {
                postype = FMOD_TIMEUNIT_PCMBYTES;
            }

            if (soundi->mSubSoundShared)
            {
                FMOD_CODEC_WAVEFORMAT waveformat;

                soundi->mCodec->mDescription.getwaveformat(soundi->mCodec, subsound, &waveformat);

                length = waveformat.lengthpcm;

                if (postype == FMOD_TIMEUNIT_MS)
                {
                    position = (unsigned int)((float)waveformat.frequency * position / 1000.0f);
                }
                else if (postype == FMOD_TIMEUNIT_PCMBYTES)
                {
                    SoundI::getSamplesFromBytes(position, &position, waveformat.channels, waveformat.format);
                }

                postype = FMOD_TIMEUNIT_PCM;
            }
            else
            {
                result = soundi->mSubSound[subsound]->getLength(&length, postype);
                if (result != FMOD_OK)
                {
                    return result;
                }
            }

            if (position >= length)
            {
                return FMOD_ERR_INVALID_PARAM;
            }

            {
                unsigned int count;

                for (count = 0; count < sentenceid; count++)
                {
                    if (soundi->mSubSoundShared)
                    {
                        FMOD_CODEC_WAVEFORMAT waveformat;

                        soundi->mCodec->mDescription.getwaveformat(soundi->mCodec, soundi->mSubSoundList[count], &waveformat);

                        length = waveformat.lengthpcm;
                    }
                    else
                    {
                        soundi->mSubSound[soundi->mSubSoundList[count]]->getLength(&length, postype);
                    }

                    position += length;
                }
            }
        }
        else
#endif
        {
            result = soundi->getLength(&length, postype);
            if (result != FMOD_OK)
            {
                return result;
            }

            if (position >= length)
            {
                return FMOD_ERR_INVALID_PARAM;
            }
        }
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        result = mRealChannel[count]->setPosition(position, postype);
        if (result != FMOD_OK && result != FMOD_ERR_INVALID_PARAM)
        {
            return result;
        }
    }

    return updateSyncPoints(true);
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: be82e40e manual
// OFFSET: 0x008DEFD0
FMOD_RESULT ChannelI::getPosition(unsigned int *position, FMOD_TIMEUNIT postype)
{
    if (!position)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    return mRealChannel[0]->getPosition(position, postype);
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getDSPHead(DSPI **dsp)
{
    if (!dsp)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    return mRealChannel[0]->getDSPHead(dsp);
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: bba6748a auto
// OFFSET: 0x008DF010
FMOD_RESULT ChannelI::addDSP(DSPI *dsp)
{
    FMOD_RESULT result;
    DSPI *dsphead, *inputdsp;
    int numinputs;

    if (!dsp)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    result = getDSPHead(&dsphead);
    if (result != FMOD_OK)
    {
        return result;
    }

    result = dsphead->getNumInputs(&numinputs);
    if (result != FMOD_OK)
    {
        return result;
    }
    if (numinputs > 1)
    {
        return FMOD_ERR_DSP_TOOMANYCONNECTIONS;
    }

    if (dsp->mFlags & FMOD_DSP_FLAG_USEDADDDSP)
    {
        result = dsp->remove();
    }
    else
    {
        result = dsp->disconnectFrom(0);
    }
    if (result != FMOD_OK)
    {
        return result;
    }

    result = dsp->getNumInputs(&numinputs);
    if (result != FMOD_OK)
    {
        return result;
    }
    if (numinputs > 0)
    {
        return FMOD_ERR_DSP_CONNECTION;
    }

    if (dsphead->getInput(0, &inputdsp) != FMOD_OK)
    {
        inputdsp = 0;
    }
    else
    {
        result = dsphead->disconnectFrom(inputdsp);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

    result = dsphead->addInput(dsp);
    if (result != FMOD_OK)
    {
        return result;
    }

    if (inputdsp)
    {
        result = dsp->addInput(inputdsp);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

    dsp->reset();
    dsp->mFlags |= FMOD_DSP_FLAG_ACTIVE | FMOD_DSP_FLAG_USEDADDDSP;

    mFlags |= CHANNELI_FLAG_USEDADDDSP;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 77f20ef0 auto
// OFFSET: 0x008DF140
FMOD_RESULT ChannelI::addDSPChain(DSPI *dsp)
{
    FMOD_RESULT result;
    DSPI *dsphead, *inputdsp;
    int numinputs;

    if (!dsp)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    result = getDSPHead(&dsphead);
    if (result != FMOD_OK)
    {
        return result;
    }

    result = dsphead->getNumInputs(&numinputs);
    if (result != FMOD_OK)
    {
        return result;
    }
    if (numinputs > 1)
    {
        return FMOD_ERR_DSP_TOOMANYCONNECTIONS;
    }

    if (!(dsp->mFlags & FMOD_DSP_FLAG_USEDADDDSP))
    {
        return FMOD_ERR_DSP_CONNECTION;
    }

    if (dsphead->getInput(0, &inputdsp) != FMOD_OK)
    {
        inputdsp = 0;
    }
    else
    {
        result = dsphead->disconnectFrom(inputdsp);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

    result = dsphead->addInput(dsp);
    if (result != FMOD_OK)
    {
        return result;
    }

    if (inputdsp)
    {
        DSPI *last = dsp, *next;

        if (dsp->getInput(0, &next) == FMOD_OK)
        {
            do
            {
                if (!(next->mFlags & FMOD_DSP_FLAG_USEDADDDSP))
                {
                    break;
                }

                last = next;
            } while (next->getInput(0, &next) == FMOD_OK);
        }

        result = last->addInput(inputdsp);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

    dsp->reset();
    dsp->mFlags |= FMOD_DSP_FLAG_ACTIVE | FMOD_DSP_FLAG_USEDADDDSP;

    mFlags |= CHANNELI_FLAG_USEDADDDSP;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 96baf45e auto
// OFFSET: 0x008E0E90
FMOD_RESULT ChannelI::setMode(FMOD_MODE mode)
{
    FMOD_RESULT result;
    int count;
    FMOD_MODE oldmode;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    oldmode = mRealChannel[0]->mMode;

    for (count = 0; count < mNumRealChannels; count++)
    {
        result = mRealChannel[count]->setMode(mode);
        if (result != FMOD_OK)
        {
            return result;
        }
    }

    if (mRealChannel[0]->mMode & FMOD_SOFTWARE)
    {
        if (!(oldmode & FMOD_2D) && mode & FMOD_2D)
        {
            unsigned int channelmask = 0;
            int channels = 0;

            result = setVolume(mVolume);
            if (result != FMOD_OK)
            {
                return result;
            }

            if (mRealChannel[0]->mSound)
            {
                SoundI *soundi = SAFE_CAST(SoundI, mRealChannel[0]->mSound);

                channelmask = soundi->mDefaultChannelMask;
                channels = soundi->mChannels;
            }

            if (channelmask & SPEAKER_WAVEFORMAT_MASK)
            {
                int speaker, incount;
                unsigned int maskbit = 1;

                incount = 0;
                for (speaker = 0; speaker < mSystem->mMaxOutputChannels; speaker++)
                {
                    if (channelmask & maskbit)
                    {
                        float clevels[DSP_MAXLEVELS_IN];

                        FMOD_memset(clevels, 0, DSP_MAXLEVELS_IN * sizeof(float));
                        clevels[incount] = 1.0f;

                        setSpeakerLevels((FMOD_SPEAKER)speaker, clevels, channels);

                        incount++;
                        if (incount >= channels)
                        {
                            break;
                        }
                    }
                    maskbit <<= 1;
                }
            }
            else
            {
                setPan(mPan, true);
            }
        }
        else if (!(oldmode & FMOD_3D) && mode & FMOD_3D)
        {
            FMOD_VECTOR pos = mPosition3D;

            mPosition3D.x += 1.0f;

            result = set3DAttributes(&pos, &mVelocity3D);
            if (result != FMOD_OK)
            {
                return result;
            }
        }
    }
    else
    {
        if ((mode & FMOD_3D) && (mode & (FMOD_3D_LOGROLLOFF | FMOD_3D_LINEARROLLOFF | FMOD_3D_CUSTOMROLLOFF) || mSystem->mRolloffCallback))
        {
            result = set3DAttributes(&mPosition3D, &mVelocity3D);
            if (result != FMOD_OK)
            {
                return result;
            }

            mFlags |= CHANNELI_FLAG_MOVED;

            result = update(0);
            if (result != FMOD_OK)
            {
                return result;
            }

            result = setVolume(mVolume);
            if (result != FMOD_OK)
            {
                return result;
            }
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

	[SEE_ALSO]
]
*/
// VERIFY: 2ded5c1d auto
// OFFSET: 0x008DF270
FMOD_RESULT ChannelI::getMode(FMOD_MODE *mode)
{
    if (!mode)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    *mode = mRealChannel[0]->mMode;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
// VERIFY: 8e4e6caa auto
// OFFSET: 0x008DF2A0
FMOD_RESULT ChannelI::setLoopCount(int loopcount)
{
    FMOD_RESULT result = FMOD_OK;
    int count;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->setLoopCount(loopcount);
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getLoopCount(int *loopcount)
{
    if (!loopcount)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    *loopcount = mRealChannel[0]->mLoopCount;

    return FMOD_OK;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::setLoopPoints(unsigned int loopstart, FMOD_TIMEUNIT loopstarttype, unsigned int loopend, FMOD_TIMEUNIT loopendtype)
{
    FMOD_RESULT result = FMOD_OK;
    unsigned int loopstartpcm = 0, loopendpcm = 0, looplengthpcm = 0;
    int count;
    SoundI *soundi;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if ((loopstarttype != FMOD_TIMEUNIT_MS && loopstarttype != FMOD_TIMEUNIT_PCM && loopstarttype != FMOD_TIMEUNIT_PCMBYTES) ||
        (loopendtype   != FMOD_TIMEUNIT_MS && loopendtype   != FMOD_TIMEUNIT_PCM && loopendtype   != FMOD_TIMEUNIT_PCMBYTES))
    {
        return FMOD_ERR_FORMAT;
    }

    soundi = SAFE_CAST(SoundI, mRealChannel[0]->mSound);
    if (!soundi)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    soundi = soundi->mSubSampleParent;

    if (loopstarttype == FMOD_TIMEUNIT_PCM)
    {
        loopstartpcm = loopstart;
    }
    else if (loopstarttype == FMOD_TIMEUNIT_PCMBYTES)
    {
        SoundI::getSamplesFromBytes(loopstart, &loopstartpcm, soundi->mChannels, soundi->mFormat);
    }
    else if (loopstarttype == FMOD_TIMEUNIT_MS)
    {
        loopstartpcm = (unsigned int)((float)loopstart / 1000.0f * soundi->mDefaultFrequency);
    }

    if (loopendtype == FMOD_TIMEUNIT_PCM)
    {
        loopendpcm = loopend;
    }
    else if (loopendtype == FMOD_TIMEUNIT_PCMBYTES)
    {
        SoundI::getSamplesFromBytes(loopend, &loopendpcm, soundi->mChannels, soundi->mFormat);
    }
    else if (loopendtype == FMOD_TIMEUNIT_MS)
    {
        loopendpcm = (unsigned int)((float)loopend / 1000.0f * soundi->mDefaultFrequency);
    }

    if (loopstartpcm >= loopendpcm)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    looplengthpcm = loopendpcm - loopstartpcm + 1;

    for (count = 0; count < mNumRealChannels; count++)
    {
        FMOD_RESULT result2;

        result2 = mRealChannel[count]->setLoopPoints(loopstartpcm, looplengthpcm);
        if (result == FMOD_OK)
        {
            result = result2;
        }
    }

    return result;
}


/*
[
	[DESCRIPTION]

	[PARAMETERS]

	[RETURN_VALUE]

	[REMARKS]

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getLoopPoints(unsigned int *loopstart, FMOD_TIMEUNIT loopstarttype, unsigned int *loopend, FMOD_TIMEUNIT loopendtype)
{
    SoundI *soundi;

    if (!mRealChannel[0])
    {
        return FMOD_ERR_INVALID_HANDLE;
    }

    if ((loopstarttype != FMOD_TIMEUNIT_MS && loopstarttype != FMOD_TIMEUNIT_PCM && loopstarttype != FMOD_TIMEUNIT_PCMBYTES) ||
        (loopendtype  != FMOD_TIMEUNIT_MS && loopendtype  != FMOD_TIMEUNIT_PCM && loopendtype  != FMOD_TIMEUNIT_PCMBYTES))
    {
        return FMOD_ERR_FORMAT;
    }

    soundi = SAFE_CAST(SoundI, mRealChannel[0]->mSound);
    if (!soundi)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    soundi = soundi->mSubSampleParent;

    if (loopstart)
    {
        if (loopstarttype == FMOD_TIMEUNIT_PCM)
        {
            *loopstart = mRealChannel[0]->mLoopStart;
        }
        else if (loopstarttype == FMOD_TIMEUNIT_PCMBYTES)
        {
            SoundI::getBytesFromSamples(mRealChannel[0]->mLoopStart, loopstart, soundi->mChannels, soundi->mFormat);
        }
        else if (loopstarttype == FMOD_TIMEUNIT_MS)
        {
            *loopstart = (unsigned int)((float)mRealChannel[0]->mLoopStart * 1000.0f / soundi->mDefaultFrequency);
        }
    }

    if (loopend)
    {
        unsigned int lend = mRealChannel[0]->mLoopStart + mRealChannel[0]->mLoopLength - 1;

        if (loopendtype == FMOD_TIMEUNIT_PCM)
        {
            *loopend = lend;
        }
        else if (loopendtype == FMOD_TIMEUNIT_PCMBYTES)
        {
            SoundI::getBytesFromSamples(lend, loopend, soundi->mChannels, soundi->mFormat);
        }
        else if (loopendtype == FMOD_TIMEUNIT_MS)
        {
            *loopend = (unsigned int)((float)lend * 1000.0f / soundi->mDefaultFrequency);
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

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::setUserData(void *userdata)
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

	[SEE_ALSO]
]
*/
FMOD_RESULT ChannelI::getUserData(void **userdata)
{
    if (!userdata)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *userdata = mUserData;

    return FMOD_OK;
}


}
