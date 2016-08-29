/*
 * $Id$
 * Portable Audio I/O Library skeleton implementation
 * demonstrates how to use the common functions to implement support
 * for a host API
 *
 * Based on the Open Source API proposed by Ross Bencina
 * Copyright (c) 1999-2002 Ross Bencina, Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however, 
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also 
 * requested that these non-binding requests be included along with the 
 * license above.
 */

/** @file
 @ingroup common_src

 @brief opensles implementation of support for a host API.

 @note IMPLEMENT ME comments are used to indicate functionality
 which much be customised for each implementation.
*/


#include <string.h> /* strlen() */
#include <pthread.h>
#include <errno.h> /* ETIMEDOUT */
#include <assert.h>
#include <math.h> /* floor */
#include <time.h>

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "pa_util.h"
#include "pa_allocation.h"
#include "pa_hostapi.h"
#include "pa_stream.h"
#include "pa_cpuload.h"
#include "pa_process.h"
#include "pa_unix_util.h"
#include "pa_debugprint.h"

#define APPNAME "PORTAUDIO_OPENSL"
#define NUMBER_OF_ENGINE_OPTIONS 2
#define NUMBER_OF_BUFFERS 2

#define ENSURE(expr, errorText) \
    do { \
        PaError err; \
        if( UNLIKELY( (err = (expr)) < paNoError ) ) \
        { \
            PaUtil_DebugPrint(( "Expression '" #expr "' failed in '" __FILE__ "', line: " STRINGIZE( __LINE__ ) "\n" )); \
            PaUtil_SetLastHostErrorInfo( paInDevelopment, err, errorText ); \
            result = err; \
            goto error; \
        } \
    } while (0);

/* prototypes for functions declared in this file */
#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

PaError PaOpenSLES_Initialize( PaUtilHostApiRepresentation **hostApi, PaHostApiIndex index );

#ifdef __cplusplus
}
#endif /* __cplusplus */


static void Terminate( struct PaUtilHostApiRepresentation *hostApi );
static PaError IsFormatSupported( struct PaUtilHostApiRepresentation *hostApi,
                                  const PaStreamParameters *inputParameters,
                                  const PaStreamParameters *outputParameters,
                                  double sampleRate );
static PaError OpenStream( struct PaUtilHostApiRepresentation *hostApi,
                           PaStream** s,
                           const PaStreamParameters *inputParameters,
                           const PaStreamParameters *outputParameters,
                           double sampleRate,
                           unsigned long framesPerBuffer,
                           PaStreamFlags streamFlags,
                           PaStreamCallback *streamCallback,
                           void *userData );
static PaError CloseStream( PaStream* stream );
static PaError StartStream( PaStream *stream );
static PaError StopStream( PaStream *stream );
static PaError AbortStream( PaStream *stream );
static PaError IsStreamStopped( PaStream *s );
static PaError IsStreamActive( PaStream *stream );
static PaTime GetStreamTime( PaStream *stream );
static double GetStreamCpuLoad( PaStream* stream );
static PaError ReadStream( PaStream* stream, void *buffer, unsigned long frames );
static PaError WriteStream( PaStream* stream, const void *buffer, unsigned long frames );
static signed long GetStreamReadAvailable( PaStream* stream );
static signed long GetStreamWriteAvailable( PaStream* stream );



/* PaSkeletonHostApiRepresentation - host api datastructure specific to this implementation */

typedef struct
{
    PaUtilHostApiRepresentation inheritedHostApiRep;
    PaUtilStreamInterface callbackStreamInterface;
    PaUtilStreamInterface blockingStreamInterface;

    PaUtilAllocationGroup *allocations;

    SLObjectItf sl;
    SLEngineItf slEngineItf;

}
PaOpenslHostApiRepresentation;

PaError Opensles_InitializeEngine(PaOpenslHostApiRepresentation *openslHostApi)
{
    SLresult slResult;
    PaError result = paNoError;
    const SLEngineOption engineOption[] = {{ SL_ENGINEOPTION_THREADSAFE, SL_BOOLEAN_TRUE }};
    slResult = slCreateEngine( &openslHostApi->sl , 1, engineOption, 0, NULL, NULL);
    result = slResult == SL_RESULT_SUCCESS ? paNoError : paUnanticipatedHostError;
    slResult = (*openslHostApi->sl)->Realize( openslHostApi->sl, SL_BOOLEAN_FALSE );
    result = slResult == SL_RESULT_SUCCESS ? paNoError : paUnanticipatedHostError;
    slResult = (*openslHostApi->sl)->GetInterface( openslHostApi->sl, SL_IID_ENGINE, (void *) &openslHostApi->slEngineItf );
    result = slResult == SL_RESULT_SUCCESS ? paNoError : paUnanticipatedHostError;
    return result;
}

/* expects samplerate to be in milliHertz */
static PaError IsOutputSampleRateSupported(PaOpenslHostApiRepresentation *openslHostApi, double sampleRate)
{
    SLresult slResult;
    SLObjectItf audioPlayer;
    SLObjectItf outputMixObject;

    (*openslHostApi->slEngineItf)->CreateOutputMix( openslHostApi->slEngineItf, &outputMixObject, 0, NULL, NULL );
    (*outputMixObject)->Realize( outputMixObject, SL_BOOLEAN_FALSE );

    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, outputMixObject };
    SLDataSink audioSnk = { &loc_outmix, NULL };
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    SLDataFormat_PCM  format_pcm = { SL_DATAFORMAT_PCM, 1, sampleRate,
                                     SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                                     SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN };
    SLDataSource audioSrc = { &loc_bufq, &format_pcm };

    slResult = (*openslHostApi->slEngineItf)->CreateAudioPlayer( openslHostApi->slEngineItf, &audioPlayer, &audioSrc, &audioSnk, 0, NULL, NULL );
    (*audioPlayer)->Destroy( audioPlayer );
    (*outputMixObject)->Destroy( outputMixObject );
    return slResult == SL_RESULT_SUCCESS ? paNoError : paInvalidSampleRate;
}

static PaError IsOutputChannelCountSupported(PaOpenslHostApiRepresentation *openslHostApi, SLuint32 numOfChannels)
{
    if ( numOfChannels > 2 || numOfChannels == 0 )
        return paInvalidChannelCount;

    SLresult slResult;
    SLObjectItf audioPlayer;
    SLObjectItf outputMixObject;
    const SLuint32 channelMasks[] = { SL_SPEAKER_FRONT_CENTER, SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT };

    (*openslHostApi->slEngineItf)->CreateOutputMix( openslHostApi->slEngineItf, &outputMixObject, 0, NULL, NULL );
    (*outputMixObject)->Realize( outputMixObject, SL_BOOLEAN_FALSE );

    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, outputMixObject };
    SLDataSink audioSnk = { &loc_outmix, NULL };
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    SLDataFormat_PCM  format_pcm = { SL_DATAFORMAT_PCM, numOfChannels, SL_SAMPLINGRATE_16,
                                     SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                                     channelMasks[numOfChannels - 1], SL_BYTEORDER_LITTLEENDIAN };
    SLDataSource audioSrc = { &loc_bufq, &format_pcm };

    slResult = (*openslHostApi->slEngineItf)->CreateAudioPlayer( openslHostApi->slEngineItf, &audioPlayer, &audioSrc, &audioSnk, 0, NULL, NULL );
    (*audioPlayer)->Destroy( audioPlayer );
    (*outputMixObject)->Destroy( outputMixObject );
    return slResult == SL_RESULT_SUCCESS ? paNoError : paInvalidSampleRate;
}

PaError PaOpenSLES_Initialize( PaUtilHostApiRepresentation **hostApi, PaHostApiIndex hostApiIndex )
{
    PaError result = paNoError;
    int i, deviceCount;
    PaOpenslHostApiRepresentation *openslHostApi;
    PaDeviceInfo *deviceInfoArray;

    openslHostApi = (PaOpenslHostApiRepresentation*)PaUtil_AllocateMemory( sizeof(PaOpenslHostApiRepresentation) );
    if( !openslHostApi )
    {
        result = paInsufficientMemory;
        goto error;
    }

    openslHostApi->allocations = PaUtil_CreateAllocationGroup();
    if( !openslHostApi->allocations )
    {
        result = paInsufficientMemory;
        goto error;
    }

    *hostApi = &openslHostApi->inheritedHostApiRep;
    (*hostApi)->info.structVersion = 1;
    (*hostApi)->info.type = paInDevelopment;
    (*hostApi)->info.name = "android opensl es";
    (*hostApi)->info.defaultOutputDevice = 0;
    (*hostApi)->info.defaultInputDevice = paNoDevice;
    (*hostApi)->info.deviceCount = 1;
    deviceCount = 1;

    if( deviceCount > 0 )
    {
        (*hostApi)->deviceInfos = (PaDeviceInfo**)PaUtil_GroupAllocateMemory(
                openslHostApi->allocations, sizeof(PaDeviceInfo*) * deviceCount );
        if( !(*hostApi)->deviceInfos )
        {
            result = paInsufficientMemory;
            goto error;
        }

        /* allocate all device info structs in a contiguous block */
        deviceInfoArray = (PaDeviceInfo*)PaUtil_GroupAllocateMemory(
                openslHostApi->allocations, sizeof(PaDeviceInfo) * deviceCount );
        if( !deviceInfoArray )
        {
            result = paInsufficientMemory;
            goto error;
        }

        ENSURE( Opensles_InitializeEngine(openslHostApi), "Initializing engine failed" );

        for( i=0; i < deviceCount; ++i )
        {
            PaDeviceInfo *deviceInfo = &deviceInfoArray[i];
            deviceInfo->structVersion = 2;
            deviceInfo->hostApi = hostApiIndex;
            /* android selects it's own device, so we'll just expose a default device */
            deviceInfo->name = "default";

            const SLuint32 outputChannels[] = { 2, 1 };
            const SLuint32 numOutputChannels = 2;
            deviceInfo->maxOutputChannels = 0;
            deviceInfo->maxInputChannels = 0; /* only doing output */
            for (i = 0; i < numOutputChannels; ++i)
            {
                if (IsOutputChannelCountSupported(openslHostApi, outputChannels[i]) == paNoError)
                {
                    deviceInfo->maxOutputChannels = outputChannels[i];
                    break;
                }
            }

            /* check samplerates in order of preference */
            const SLuint32 sampleRates[] = { SL_SAMPLINGRATE_44_1, SL_SAMPLINGRATE_48, SL_SAMPLINGRATE_32, SL_SAMPLINGRATE_24 };
            const SLuint32 numberOfSampleRates = 4;
            deviceInfo->defaultSampleRate = 0;
            for ( i = 0; i < numberOfSampleRates; ++i ) 
            {
                if ( IsOutputSampleRateSupported(openslHostApi, sampleRates[i]) == paNoError )
                {
                    /* opensl defines sampling rates in milliHertz, so we divide by 1000 */
                    deviceInfo->defaultSampleRate = sampleRates[i] / 1000;
                    break;
                }
            }
            if ( deviceInfo->defaultSampleRate == 0 )
                goto error;

            /* these are arbitrary values that worked for nexus2013/kitkat,
               opensl goes through the android HAL and latency depends on device and android version */
            deviceInfo->defaultLowInputLatency = 0.0;
            deviceInfo->defaultLowOutputLatency = (double) 512 / deviceInfo->defaultSampleRate;
            deviceInfo->defaultHighInputLatency = 0.0;  /* IMPLEMENT ME */
            deviceInfo->defaultHighOutputLatency = (double) 1024 / deviceInfo->defaultSampleRate;  /* IMPLEMENT ME */  
            
            (*hostApi)->deviceInfos[i] = deviceInfo;
            ++(*hostApi)->info.deviceCount;
        }
    }

    (*hostApi)->Terminate = Terminate;
    (*hostApi)->OpenStream = OpenStream;
    (*hostApi)->IsFormatSupported = IsFormatSupported;

    PaUtil_InitializeStreamInterface( &openslHostApi->callbackStreamInterface, CloseStream, StartStream,
                                      StopStream, AbortStream, IsStreamStopped, IsStreamActive,
                                      GetStreamTime, GetStreamCpuLoad,
                                      PaUtil_DummyRead, PaUtil_DummyWrite,
                                      PaUtil_DummyGetReadAvailable, PaUtil_DummyGetWriteAvailable );

    PaUtil_InitializeStreamInterface( &openslHostApi->blockingStreamInterface, CloseStream, StartStream,
                                      StopStream, AbortStream, IsStreamStopped, IsStreamActive,
                                      GetStreamTime, PaUtil_DummyGetCpuLoad,
                                      ReadStream, WriteStream, GetStreamReadAvailable, GetStreamWriteAvailable );

    return result;

error:
    if( openslHostApi )
    {
        if( openslHostApi->allocations )
        {
            PaUtil_FreeAllAllocations( openslHostApi->allocations );
            PaUtil_DestroyAllocationGroup( openslHostApi->allocations );
        }
                
        PaUtil_FreeMemory( openslHostApi );
    }
    return result;
}

static void Terminate( struct PaUtilHostApiRepresentation *hostApi )
{
    PaOpenslHostApiRepresentation *openslHostApi = (PaOpenslHostApiRepresentation*)hostApi;

    if ( openslHostApi->sl )
    {
        (*openslHostApi->sl)->Destroy(openslHostApi->sl);
    }

    if( openslHostApi->allocations )
    {
        PaUtil_FreeAllAllocations( openslHostApi->allocations );
        PaUtil_DestroyAllocationGroup( openslHostApi->allocations );
    }

    PaUtil_FreeMemory( openslHostApi );
}

static PaError IsFormatSupported( struct PaUtilHostApiRepresentation *hostApi,
                                  const PaStreamParameters *inputParameters,
                                  const PaStreamParameters *outputParameters,
                                  double sampleRate )
{
    int inputChannelCount, outputChannelCount;
    PaSampleFormat inputSampleFormat, outputSampleFormat;
    PaOpenslHostApiRepresentation *openslHostApi = (PaOpenslHostApiRepresentation*) hostApi;
    
    if( inputParameters )
    {
        inputChannelCount = inputParameters->channelCount;
        inputSampleFormat = inputParameters->sampleFormat;

        /* all standard sample formats are supported by the buffer adapter,
            this implementation doesn't support any custom sample formats */
        if( inputSampleFormat & paCustomFormat )
            return paSampleFormatNotSupported;
            
        /* unless alternate device specification is supported, reject the use of
            paUseHostApiSpecificDeviceSpecification */

        if( inputParameters->device == paUseHostApiSpecificDeviceSpecification )
            return paInvalidDevice;

        /* check that input device can support inputChannelCount */
        if( inputChannelCount > hostApi->deviceInfos[ inputParameters->device ]->maxInputChannels )
            return paInvalidChannelCount;

        /* validate inputStreamInfo */
        if( inputParameters->hostApiSpecificStreamInfo )
            return paIncompatibleHostApiSpecificStreamInfo; /* this implementation doesn't use custom stream info */
    }
    else
    {
        inputChannelCount = 0;
    }

    if( outputParameters )
    {
        outputChannelCount = outputParameters->channelCount;
        outputSampleFormat = outputParameters->sampleFormat;

        /* all standard sample formats are supported by the buffer adapter,
            this implementation doesn't support any custom sample formats */

        if( outputSampleFormat & paCustomFormat )
            return paSampleFormatNotSupported;

        /* unless alternate device specification is supported, reject the use of
            paUseHostApiSpecificDeviceSpecification */
        if( outputParameters->device == paUseHostApiSpecificDeviceSpecification )
            return paInvalidDevice;


        /* check that output device can support outputChannelCount */
        if( outputChannelCount > hostApi->deviceInfos[ outputParameters->device ]->maxOutputChannels )
            return paInvalidChannelCount;

        /* validate outputStreamInfo */
        if( outputParameters->hostApiSpecificStreamInfo ) 
            return paIncompatibleHostApiSpecificStreamInfo;
    }
    else
    {
        outputChannelCount = 0;
    }

    if ( IsOutputSampleRateSupported( openslHostApi, sampleRate * 1000 ) != paNoError )
    {
        return paInvalidSampleRate;
    }

    return paFormatIsSupported;
}

typedef struct OpenslesStream
{
    PaUtilStreamRepresentation streamRepresentation;
    PaUtilCpuLoadMeasurer cpuLoadMeasurer;
    PaUtilBufferProcessor bufferProcessor;

    SLObjectItf audioPlayer;
    SLPlayItf playerItf;
    SLPrefetchStatusItf prefetchStatusItf;
    SLAndroidSimpleBufferQueueItf outputBufferQueueItf;
    SLVolumeItf volumeItf;

    SLObjectItf outputMixObject;

    SLboolean isBlocking;
    SLboolean isStopped;
    SLboolean isActive;
    SLboolean doStop;
    SLboolean doAbort;
    int callbackResult;
    pthread_mutex_t mtx;
    pthread_cond_t cond;

    PaStreamCallbackFlags cbFlags;
    void   *outputBuffers[NUMBER_OF_BUFFERS];
    int currentBuffer;
    unsigned long framesPerHostCallback;
    unsigned bytesPerFrame;
}
OpenslesStream;

static PaError InitializeOutputStream(PaOpenslHostApiRepresentation *openslHostApi, OpenslesStream *stream, double sampleRate);
static void OpenslOutputStreamCallback(SLAndroidSimpleBufferQueueItf outpuBufferQueueItf, void *userData);
static void PrefetchStatusCallback(SLPrefetchStatusItf prefetchStatusItf, void *userData, SLuint32 event);

static PaError WaitCondition( OpenslesStream *stream )
{
    PaError result = paNoError;
    int err = 0;
    PaTime pt = PaUtil_GetTime();
    struct timespec ts;
    ts.tv_sec = (time_t) floor( pt + 10 * 60 );
    ts.tv_nsec = (long) ((pt - floor( pt )) * 1000000000);
    err = pthread_cond_timedwait( &stream->cond, &stream->mtx, &ts );
    if ( err == ETIMEDOUT )
    {
        result = paTimedOut;
        goto error;
    }
    if ( err )
    {
        result =  paInternalError;
        goto error;
    }
error:
    return result;
}

static PaError OpenStream( struct PaUtilHostApiRepresentation *hostApi,
                           PaStream** s,
                           const PaStreamParameters *inputParameters,
                           const PaStreamParameters *outputParameters,
                           double sampleRate,
                           unsigned long framesPerBuffer,
                           PaStreamFlags streamFlags,
                           PaStreamCallback *streamCallback,
                           void *userData )
{
    PaError result = paNoError;
    PaOpenslHostApiRepresentation *openslHostApi = (PaOpenslHostApiRepresentation*)hostApi;
    OpenslesStream *stream = 0;
    unsigned long framesPerHostBuffer; /* these may not be equivalent for all implementations */
    int inputChannelCount, outputChannelCount;
    PaSampleFormat inputSampleFormat, outputSampleFormat;
    PaSampleFormat hostInputSampleFormat, hostOutputSampleFormat;

    if( inputParameters )
    {
        inputChannelCount = inputParameters->channelCount;
        inputSampleFormat = inputParameters->sampleFormat;

        /* unless alternate device specification is supported, reject the use of
            paUseHostApiSpecificDeviceSpecification */

        if( inputParameters->device == paUseHostApiSpecificDeviceSpecification )
            return paInvalidDevice;

        /* check that input device can support inputChannelCount */
        if( inputChannelCount > hostApi->deviceInfos[ inputParameters->device ]->maxInputChannels )
            return paInvalidChannelCount;

        /* validate inputStreamInfo */
        if( inputParameters->hostApiSpecificStreamInfo )
            return paIncompatibleHostApiSpecificStreamInfo; /* this implementation doesn't use custom stream info */

        hostInputSampleFormat = PaUtil_SelectClosestAvailableFormat( paInt16, inputSampleFormat );
    }
    else
    {
        inputChannelCount = 0;
        inputSampleFormat = hostInputSampleFormat = paInt16; /* Surpress 'uninitialised var' warnings. */
    }

    if( outputParameters )
    {
        outputChannelCount = outputParameters->channelCount;
        outputSampleFormat = outputParameters->sampleFormat;
        
        /* unless alternate device specification is supported, reject the use of
            paUseHostApiSpecificDeviceSpecification */

        if( outputParameters->device == paUseHostApiSpecificDeviceSpecification )
            return paInvalidDevice;

        /* check that output device can support inputChannelCount */
        if( outputChannelCount > hostApi->deviceInfos[ outputParameters->device ]->maxOutputChannels )
            return paInvalidChannelCount;

        /* validate outputStreamInfo */
        if( outputParameters->hostApiSpecificStreamInfo )
            return paIncompatibleHostApiSpecificStreamInfo; /* this implementation doesn't use custom stream info */

        if ( IsOutputSampleRateSupported( openslHostApi, sampleRate * 1000 ) != paNoError )
            return paInvalidSampleRate;

        hostOutputSampleFormat = PaUtil_SelectClosestAvailableFormat( paInt16, outputSampleFormat );    

    }
    else
    {
        outputChannelCount = 0;
        outputSampleFormat = hostOutputSampleFormat = paInt16; /* Surpress 'uninitialized var' warnings. */
    }

    /* validate platform specific flags */
    if( (streamFlags & paPlatformSpecificFlags) != 0 )
        return paInvalidFlag; /* unexpected platform specific flag */

    if (framesPerBuffer == paFramesPerBufferUnspecified)
    {
        framesPerHostBuffer = (unsigned long) ( outputParameters->suggestedLatency * sampleRate);
    }
    else
    {
        framesPerHostBuffer = framesPerBuffer;
    }
    stream = (OpenslesStream*)PaUtil_AllocateMemory( sizeof(OpenslesStream) );

    if( !stream )
    {
        result = paInsufficientMemory;
        goto error;
    }

    if( streamCallback )
    {
        PaUtil_InitializeStreamRepresentation( &stream->streamRepresentation,
                                               &openslHostApi->callbackStreamInterface, streamCallback, userData );
    }
    else
    {
        PaUtil_InitializeStreamRepresentation( &stream->streamRepresentation,
                                               &openslHostApi->blockingStreamInterface, streamCallback, userData );
    }

    PaUtil_InitializeCpuLoadMeasurer( &stream->cpuLoadMeasurer, sampleRate );

    result =  PaUtil_InitializeBufferProcessor( &stream->bufferProcessor,
              inputChannelCount, inputSampleFormat, hostInputSampleFormat,
              outputChannelCount, outputSampleFormat, hostOutputSampleFormat,
              sampleRate, streamFlags, framesPerBuffer,
              framesPerHostBuffer, paUtilFixedHostBufferSize,
              streamCallback, userData );
    if( result != paNoError )
        goto error;

    stream->streamRepresentation.streamInfo.sampleRate = sampleRate;
    stream->isBlocking = streamCallback ? SL_BOOLEAN_FALSE : SL_BOOLEAN_TRUE;
    /* tested on nexus 2013 with android 4.4.4, if stream is blocking 1024 was the minimum to make it run */
    stream->framesPerHostCallback = stream->isBlocking ? PA_MAX( framesPerHostBuffer, 1024 ) : framesPerHostBuffer;

    if ( inputChannelCount > 0 )
        stream->streamRepresentation.streamInfo.inputLatency =
            ((PaTime)PaUtil_GetBufferProcessorInputLatencyFrames(&stream->bufferProcessor) + stream->framesPerHostCallback) / sampleRate;
    if ( outputChannelCount > 0 )
        stream->streamRepresentation.streamInfo.outputLatency =
            ((PaTime)PaUtil_GetBufferProcessorOutputLatencyFrames(&stream->bufferProcessor) + stream->framesPerHostCallback) / sampleRate;

    ENSURE( InitializeOutputStream( openslHostApi, stream, sampleRate ), "Initializing outputstream failed" );

    *s = (PaStream*)stream;
    
    stream->isStopped = SL_BOOLEAN_TRUE;
    stream->isActive = SL_BOOLEAN_FALSE;
    
    return result;
    
error:
    if( stream )
        PaUtil_FreeMemory( stream );
    return result;
}

static PaError InitializeOutputStream(PaOpenslHostApiRepresentation *openslHostApi, OpenslesStream *stream, double sampleRate)
{
    PaError result = paNoError;
    SLresult slResult;
    int i;
    //    int numberOfBuffers = stream->isBlocking ? NUMBER_OF_BUFFERS * 2 : NUMBER_OF_BUFFERS;
    const SLuint32 channelMasks[] = { SL_SPEAKER_FRONT_CENTER, SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT };
    SLDataLocator_AndroidSimpleBufferQueue outputSourceBufferQueue = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, NUMBER_OF_BUFFERS };
    SLDataFormat_PCM  format_pcm = { SL_DATAFORMAT_PCM, stream->bufferProcessor.outputChannelCount, sampleRate * 1000.0, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16, channelMasks[stream->bufferProcessor.outputChannelCount - 1], SL_BYTEORDER_LITTLEENDIAN };
    SLDataSource audioSrc = { &outputSourceBufferQueue, &format_pcm };
    
    (*openslHostApi->slEngineItf)->CreateOutputMix( openslHostApi->slEngineItf, &(stream->outputMixObject), 0, NULL, NULL );
    (*stream->outputMixObject)->Realize( stream->outputMixObject, SL_BOOLEAN_FALSE );
    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, stream->outputMixObject };
    SLDataSink audioSnk = { &loc_outmix, &format_pcm };
    
    if ( !stream->isBlocking )
    {
        const SLInterfaceID ids[] = { SL_IID_BUFFERQUEUE, SL_IID_PREFETCHSTATUS, SL_IID_VOLUME };
        const SLboolean req[] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
        const unsigned interfaceCount = 3;
        slResult = (*openslHostApi->slEngineItf)->CreateAudioPlayer(openslHostApi->slEngineItf, &stream->audioPlayer, &audioSrc, &audioSnk, interfaceCount, ids, req);
    }
    else
    {
        const SLInterfaceID ids[] = { SL_IID_BUFFERQUEUE, SL_IID_VOLUME };
        const SLboolean req[] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
        const unsigned interfaceCount = 2;
        slResult = (*openslHostApi->slEngineItf)->CreateAudioPlayer( openslHostApi->slEngineItf, &stream->audioPlayer, &audioSrc, &audioSnk, interfaceCount, ids, req );
    }
    if ( slResult != SL_RESULT_SUCCESS )
    {
        result = paUnanticipatedHostError;
        goto error;
    }
    slResult = (*stream->audioPlayer)->Realize( stream->audioPlayer, SL_BOOLEAN_FALSE );
    if ( slResult != SL_RESULT_SUCCESS )
    {
        result = paUnanticipatedHostError;
        goto error;
    }

    (*stream->audioPlayer)->GetInterface( stream->audioPlayer, SL_IID_PLAY, &stream->playerItf );
    (*stream->audioPlayer)->GetInterface( stream->audioPlayer, SL_IID_BUFFERQUEUE, &stream->outputBufferQueueItf );
    (*stream->audioPlayer)->GetInterface( stream->audioPlayer, SL_IID_VOLUME, &stream->volumeItf );

    stream->bytesPerFrame = sizeof(SLint16);
    for (i = 0; i < NUMBER_OF_BUFFERS; ++i)
    {
        stream->outputBuffers[i] = (void*) PaUtil_AllocateMemory( stream->framesPerHostCallback * stream->bytesPerFrame );
        if ( !stream->outputBuffers[i] )
        {
            result = paInsufficientMemory;
            goto error;
        }
    }
    stream->currentBuffer = 0;
    stream->cbFlags = 0;

    if ( !stream->isBlocking )
    {
        (*stream->audioPlayer)->GetInterface( stream->audioPlayer, SL_IID_PREFETCHSTATUS, &stream->prefetchStatusItf );
        (*stream->prefetchStatusItf)->SetCallbackEventsMask( stream->prefetchStatusItf, SL_PREFETCHEVENT_STATUSCHANGE | SL_PREFETCHEVENT_FILLLEVELCHANGE );
        (*stream->prefetchStatusItf)->SetFillUpdatePeriod( stream->prefetchStatusItf, 200 );
        (*stream->prefetchStatusItf)->RegisterCallback( stream->prefetchStatusItf, PrefetchStatusCallback, (void*) stream );
        (*stream->outputBufferQueueItf)->RegisterCallback( stream->outputBufferQueueItf, OpenslOutputStreamCallback, (void*) stream );
        pthread_mutex_init( &stream->mtx, NULL );
        pthread_cond_init( &stream->cond, NULL );
    }
error:
    return result;
}

static void OpenslOutputStreamCallback(SLAndroidSimpleBufferQueueItf outpuBufferQueueItf, void *userData)
{
    OpenslesStream *stream = (OpenslesStream*) userData;
    PaStreamCallbackTimeInfo timeInfo = {0,0,0};
    unsigned long framesProcessed = 0;
    struct timespec timeSpec;

    clock_gettime( CLOCK_REALTIME, &timeSpec );
    timeInfo.currentTime = (PaTime) (timeSpec.tv_sec + (timeSpec.tv_nsec / 1000000000.0)); // (PaTime) time(NULL);
    timeInfo.outputBufferDacTime = (PaTime) (stream->framesPerHostCallback / stream->streamRepresentation.streamInfo.sampleRate + timeInfo.currentTime);

    /* check if StopStream or AbortStream was called */
    if ( stream->doStop )
        stream->callbackResult = paComplete;
    else if ( stream->doAbort )
        stream->callbackResult = paAbort;

    PaUtil_BeginCpuLoadMeasurement( &stream->cpuLoadMeasurer );
    PaUtil_BeginBufferProcessing( &stream->bufferProcessor, &timeInfo, stream->cbFlags );
    PaUtil_SetOutputFrameCount( &stream->bufferProcessor, 0 );
    PaUtil_SetInterleavedOutputChannels( &stream->bufferProcessor, 0, (void*) stream->outputBuffers[stream->currentBuffer], 0 );

    /* continue processing user buffers if cbresult is pacontinue or if cbresult is  pacomplete and buffers aren't empty yet  */
    if ( stream->callbackResult == paContinue || ( stream->callbackResult == paComplete && !PaUtil_IsBufferProcessorOutputEmpty( &stream->bufferProcessor )) )
        framesProcessed = PaUtil_EndBufferProcessing( &stream->bufferProcessor, &stream->callbackResult );

    if ( framesProcessed  > 0 ) /* enqueue a buffer only when there are frames to be processed, this will be 0 when paComplete + empty buffers or paAbort */
    {
        (*stream->outputBufferQueueItf)->Enqueue( stream->outputBufferQueueItf, (void*) stream->outputBuffers[stream->currentBuffer], framesProcessed * stream->bytesPerFrame );
        stream->currentBuffer = (stream->currentBuffer + 1) % NUMBER_OF_BUFFERS;
    }
    else
    {
        pthread_mutex_lock( &stream->mtx );
        pthread_cond_signal( &stream->cond );
        pthread_mutex_unlock( &stream->mtx );
    }

    PaUtil_EndCpuLoadMeasurement( &stream->cpuLoadMeasurer, framesProcessed);
}

static void PrefetchStatusCallback(SLPrefetchStatusItf prefetchStatusItf, void *userData, SLuint32 event)
{
    SLuint32 prefetchStatus = 2;
    OpenslesStream *stream = (OpenslesStream*) userData;
    (*stream->prefetchStatusItf)->GetPrefetchStatus(stream->prefetchStatusItf, &prefetchStatus);
    if ( event & SL_PREFETCHEVENT_STATUSCHANGE && prefetchStatus == SL_PREFETCHSTATUS_UNDERFLOW )
    {
        stream->cbFlags = paOutputUnderflow;
    } 
    else if ( event & SL_PREFETCHEVENT_STATUSCHANGE && prefetchStatus == SL_PREFETCHSTATUS_OVERFLOW ) 
    {
        stream->cbFlags = paOutputOverflow;
    }
}

static PaError CloseStream( PaStream* s )
{
    PaError result = paNoError;
    OpenslesStream *stream = (OpenslesStream*)s;

    if ( !stream->isBlocking )
    {
        pthread_cond_destroy( &stream->cond );
        pthread_mutex_destroy( &stream->mtx );
    }

    (*stream->audioPlayer)->Destroy( stream->audioPlayer );
    (*stream->outputMixObject)->Destroy( stream->outputMixObject );

    PaUtil_TerminateBufferProcessor( &stream->bufferProcessor );
    PaUtil_TerminateStreamRepresentation( &stream->streamRepresentation );
    PaUtil_FreeMemory( stream );
    return result;
}


static PaError StartStream( PaStream *s )
{
    SLresult slResult;
    PaError result = paNoError;
    OpenslesStream *stream = (OpenslesStream*)s;
    int i;

    PaUtil_ResetBufferProcessor( &stream->bufferProcessor );

    (*stream->volumeItf)->SetVolumeLevel( stream->volumeItf, -300 );
    stream->isStopped = SL_BOOLEAN_FALSE;
    stream->isActive = SL_BOOLEAN_TRUE;
    stream->doStop = SL_BOOLEAN_FALSE;
    stream->doAbort = SL_BOOLEAN_FALSE;

    if ( !stream->isBlocking ) {
        stream->callbackResult = paContinue;
    }
    SLint16 zeroBuffer[stream->framesPerHostCallback];
    memset( zeroBuffer, 0, stream->framesPerHostCallback * stream->bytesPerFrame );
    for ( i = 0; i < NUMBER_OF_BUFFERS; ++i )
        slResult = (*stream->outputBufferQueueItf)->Enqueue( stream->outputBufferQueueItf, (void*) zeroBuffer, stream->framesPerHostCallback * stream->bytesPerFrame );

    slResult = (*stream->playerItf)->SetPlayState( stream->playerItf, SL_PLAYSTATE_PLAYING );
    result = slResult == SL_RESULT_SUCCESS ? paNoError : paUnanticipatedHostError;

    return result;
}

static PaError StopStream( PaStream *s )
{
    PaError result = paNoError;
    OpenslesStream *stream = (OpenslesStream*)s;
    SLAndroidSimpleBufferQueueState state;

    if ( stream->isBlocking )
    {
        do {
            (*stream->outputBufferQueueItf)->GetState( stream->outputBufferQueueItf, &state);
        } while ( state.count > 0 );
    }
    else
    {
        stream->doStop = SL_BOOLEAN_TRUE;
        pthread_mutex_lock( &stream->mtx );
        WaitCondition( stream ); /* cb thread sends a signal when 0 frames were processed */
        pthread_mutex_unlock( &stream->mtx );
    }

    (*stream->playerItf)->SetPlayState( stream->playerItf, SL_PLAYSTATE_STOPPED );
    stream->isActive = SL_BOOLEAN_FALSE;
    stream->isStopped = SL_BOOLEAN_TRUE;
    if( stream->streamRepresentation.streamFinishedCallback != NULL )
        stream->streamRepresentation.streamFinishedCallback( stream->streamRepresentation.userData );

    return result;
}

static PaError AbortStream( PaStream *s )
{
    PaError result = paNoError;
    OpenslesStream *stream = (OpenslesStream*)s;

    (*stream->playerItf)->SetPlayState( stream->playerItf, SL_PLAYSTATE_STOPPED );
    stream->isActive = SL_BOOLEAN_FALSE;
    stream->isStopped = SL_BOOLEAN_TRUE;

    if( stream->streamRepresentation.streamFinishedCallback != NULL )
        stream->streamRepresentation.streamFinishedCallback( stream->streamRepresentation.userData );
    return result;
}


static PaError IsStreamStopped( PaStream *s )
{
    OpenslesStream *stream = (OpenslesStream*)s;
    return stream->isStopped;
}


static PaError IsStreamActive( PaStream *s )
{
    OpenslesStream *stream = (OpenslesStream*)s;
    return stream->isActive;
}


static PaTime GetStreamTime( PaStream *s )
{
    return PaUtil_GetTime();
}


static double GetStreamCpuLoad( PaStream* s )
{
    OpenslesStream *stream = (OpenslesStream*)s;

    return PaUtil_GetCpuLoad( &stream->cpuLoadMeasurer );
}


/*
    As separate stream interfaces are used for blocking and callback
    streams, the following functions can be guaranteed to only be called
    for blocking streams.
*/

static PaError ReadStream( PaStream* s,
                           void *buffer,
                           unsigned long frames )
{
    OpenslesStream *stream = (OpenslesStream*)s;

    /* suppress unused variable warnings */
    (void) buffer;
    (void) frames;
    (void) stream;
    
    /* IMPLEMENT ME, see portaudio.h for required behavior*/

    return paNoError;
}

static PaError WriteStream( PaStream* s,
                            const void *buffer,
                            unsigned long frames )
{
    OpenslesStream *stream = (OpenslesStream*)s;
    const void *userBuffer = buffer;

    while ( frames > 0 )
    {
        if ( GetStreamWriteAvailable( stream ) > 0 ) /* we have available frames */
        {
            unsigned long framesToWrite = PA_MIN( stream->framesPerHostCallback, frames );
            PaUtil_SetOutputFrameCount( &stream->bufferProcessor, framesToWrite );
            PaUtil_SetInterleavedOutputChannels( &stream->bufferProcessor, 0, stream->outputBuffers[stream->currentBuffer], 0 );
            PaUtil_CopyOutput( &stream->bufferProcessor, &userBuffer, framesToWrite);

            (*stream->outputBufferQueueItf)->Enqueue( stream->outputBufferQueueItf, stream->outputBuffers[stream->currentBuffer], framesToWrite * stream->bytesPerFrame );
            stream->currentBuffer = (stream->currentBuffer + 1) % NUMBER_OF_BUFFERS;

            frames -= framesToWrite;
        }
        else /* Bufferqueue is full and there are frames to write, so we wait */
        {
            SLAndroidSimpleBufferQueueState state;
            do {
                (*stream->outputBufferQueueItf)->GetState( stream->outputBufferQueueItf, &state );
            } while ( state.count >= NUMBER_OF_BUFFERS ); /* there are as much buffers in the queue as there are buffers in total, wait until there is at least one free */
        }
    }
    return paNoError;
}

static signed long GetStreamReadAvailable( PaStream* s )
{
    OpenslesStream *stream = (OpenslesStream*)s;

    /* suppress unused variable warnings */
    (void) stream;
    
    /* IMPLEMENT ME, see portaudio.h for required behavior*/

    return 0;
}

static signed long GetStreamWriteAvailable( PaStream* s )
{
    OpenslesStream *stream = (OpenslesStream*)s;
    SLAndroidSimpleBufferQueueState state;

    (*stream->outputBufferQueueItf)->GetState( stream->outputBufferQueueItf, &state );

    return stream->framesPerHostCallback * ( NUMBER_OF_BUFFERS - state.count );
}




