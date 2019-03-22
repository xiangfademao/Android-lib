////////////////////////////////////////////////////////////////////////////////
///
/// Example Interface class for SoundTouch native compilation
///
/// Author        : Copyright (c) Olli Parviainen
/// Author e-mail : oparviai 'at' iki.fi
/// WWW           : http://www.surina.net
///
////////////////////////////////////////////////////////////////////////////////

#include <jni.h>
#include <android/log.h>
#include <stdexcept>
#include <string>

using namespace std;

#include "../../../include/SoundTouch.h"
#include "../source/SoundStretch/WavFile.h"

#define LOGV(...)   __android_log_print((int)ANDROID_LOG_INFO, "SOUNDTOUCH", __VA_ARGS__)
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

// String for keeping possible c++ exception error messages. Notice that this isn't
// thread-safe but it's expected that exceptions are special situations that won't
// occur in several threads in parallel.
static string _errMsg = "";


#define DLL_PUBLIC __attribute__ ((visibility ("default")))
#define BUFF_SIZE 4096


using namespace soundtouch;


// Set error message to return
static void _setErrmsg(const char *msg) {
    _errMsg = msg;
}


#ifdef _OPENMP

#include <pthread.h>
extern pthread_key_t gomp_tls_key;
static void * _p_gomp_tls = NULL;

/// Function to initialize threading for OpenMP.
///
/// This is a workaround for bug in Android NDK v10 regarding OpenMP: OpenMP works only if
/// called from the Android App main thread because in the main thread the gomp_tls storage is
/// properly set, however, Android does not properly initialize gomp_tls storage for other threads.
/// Thus if OpenMP routines are invoked from some other thread than the main thread,
/// the OpenMP routine will crash the application due to NULL pointer access on uninitialized storage.
///
/// This workaround stores the gomp_tls storage from main thread, and copies to other threads.
/// In order this to work, the Application main thread needws to call at least "getVersionString"
/// routine.
static int _init_threading(bool warn)
{
    void *ptr = pthread_getspecific(gomp_tls_key);
    LOGV("JNI thread-specific TLS storage %ld", (long)ptr);
    if (ptr == NULL)
    {
        LOGV("JNI set missing TLS storage to %ld", (long)_p_gomp_tls);
        pthread_setspecific(gomp_tls_key, _p_gomp_tls);
    }
    else
    {
        LOGV("JNI store this TLS storage");
        _p_gomp_tls = ptr;
    }
    // Where critical, show warning if storage still not properly initialized
    if ((warn) && (_p_gomp_tls == NULL))
    {
        _setErrmsg("Error - OpenMP threading not properly initialized: Call SoundTouch.getVersionString() from the App main thread!");
        return -1;
    }
    return 0;
}

#else

static int _init_threading(bool warn) {
    // do nothing if not OpenMP build
    return 0;
}

#endif


// Processes the sound file
static void _processFile(SoundTouch *pSoundTouch, const char *inFileName, const char *outFileName) {
    int nSamples;
    int nChannels;
    int buffSizeSamples;
    SAMPLETYPE sampleBuffer[BUFF_SIZE];

    // open input file
    WavInFile inFile(inFileName);
    int sampleRate = inFile.getSampleRate();
    int bits = inFile.getNumBits();
    nChannels = inFile.getNumChannels();

    // create output file
    WavOutFile outFile(outFileName, sampleRate, bits, nChannels);

    pSoundTouch->setSampleRate(sampleRate);
    pSoundTouch->setChannels(nChannels);

    assert(nChannels > 0);
    buffSizeSamples = BUFF_SIZE / nChannels;

    // Process samples read from the input file
    while (inFile.eof() == 0) {
        int num;

        // Read a chunk of samples from the input file
        //从输入文件中读取一大块样本
        num = inFile.read(sampleBuffer, BUFF_SIZE);
        nSamples = num / nChannels;
        // Feed the samples into SoundTouch processor
        //将样本送入SoundTouch处理器
        LOGV("_processFile nSamples %d  sampleBuffer%f", nSamples, sampleBuffer[0]);
        pSoundTouch->putSamples(sampleBuffer, nSamples);
        // Read ready samples from SoundTouch processor & write them output file.
        // NOTES:
        // - 'receiveSamples' doesn't necessarily return any samples at all
        //   during some rounds!
        // - On the other hand, during some round 'receiveSamples' may have more
        //   ready samples than would fit into 'sampleBuffer', and for this reason
        //   the 'receiveSamples' call is iterated for as many times as it
        //   outputs samples.
        do {
            nSamples = pSoundTouch->receiveSamples(sampleBuffer, buffSizeSamples);
            LOGV("_processFile receiveSamples %d", nSamples);
            outFile.write(sampleBuffer, nSamples * nChannels);
        } while (nSamples != 0);
    }

    // Now the input file is processed, yet 'flush' few last samples that are
    // hiding in the SoundTouch's internal processing pipeline.
    //现在处理输入文件，然后“刷新”几个最后的样本
    //隐藏在SoundTouch的内部处理管道中。
    pSoundTouch->flush();
    do {
        nSamples = pSoundTouch->receiveSamples(sampleBuffer, buffSizeSamples);
        LOGV("_processFile receiveSamples %d", nSamples);
        outFile.write(sampleBuffer, nSamples * nChannels);
    } while (nSamples != 0);
}

// dummy helper-function
static inline int _swap32(int &dwData) {
    // do nothing
    return dwData;
}

// dummy helper-function
static inline short _swap16(short &wData) {
    // do nothing
    return wData;
}

/// Convert from float to integer and saturate
inline int saturate(float fvalue, float minval, float maxval) {
    if (fvalue > maxval) {
        fvalue = maxval;
    } else if (fvalue < minval) {
        fvalue = minval;
    }
    return (int) fvalue;
}

static void
receiveSamples(JNIEnv *env, jobject object, float *buffer, int numElems, int bytesPerSample) {
    int numBytes;
    if (numElems == 0) return;
    numBytes = numElems * bytesPerSample;
    char *temp = new char[(numBytes + 24) & -8];
    switch (bytesPerSample) {
        case 1: {
            unsigned char *temp2 = (unsigned char *) temp;
            for (int i = 0; i < numElems; i++) {
                temp2[i] = (unsigned char) saturate(buffer[i] * 128.0f + 128.0f, 0.0f, 255.0f);
            }
            break;
        }

        case 2: {
            short *temp2 = (short *) temp;
            for (int i = 0; i < numElems; i++) {
                short value = (short) saturate(buffer[i] * 32768.0f, -32768.0f, 32767.0f);
                temp2[i] = _swap16(value);
            }
            break;
        }

        case 3: {
            char *temp2 = temp;
            for (int i = 0; i < numElems; i++) {
                int value = saturate(buffer[i] * 8388608.0f, -8388608.0f, 8388607.0f);
                *((int *) temp2) = _swap32(value);
                temp2 += 3;
            }
            break;
        }

        case 4: {
            int *temp2 = (int *) temp;
            for (int i = 0; i < numElems; i++) {
                int value = saturate(buffer[i] * 2147483648.0f, -2147483648.0f, 2147483647.0f);
                temp2[i] = _swap32(value);
            }
            break;
        }
        default:
            assert(false);
    }
    jclass clazz = env->GetObjectClass(object); //其中obj是要引用的对象，类型是jobject
    jmethodID methodId = env->GetMethodID(clazz, "receiveSamples", "([B)V");//获取JAVA方法的ID
    jbyteArray j_sampleBuffer = env->NewByteArray(numBytes);
    env->SetByteArrayRegion(j_sampleBuffer, 0, numBytes, (jbyte *) temp);
    env->CallVoidMethod(object, methodId, j_sampleBuffer);
}

static void process_frame(JNIEnv *env, SoundTouch *pSoundTouch, jobject object,
                          SAMPLETYPE *sampleBuffer,
                          jsize bufferSize,
                          int sampleRate,
                          int nChannels, int bytesPerSample) {
    int nSamples = bufferSize / nChannels;
    //指定采样率
    pSoundTouch->setSampleRate(sampleRate);
    //指定声道数
    pSoundTouch->setChannels(nChannels);
    int buffSizeSamples = BUFF_SIZE / nChannels;
    //根据sampleBuffer计算出采样数量
    //将样本送入SoundTouch处理器
    LOGV("process_frame nSamples %d  sampleBuffer%f", nSamples, sampleBuffer[0]);
    pSoundTouch->putSamples(sampleBuffer, nSamples);
    do {
        nSamples = pSoundTouch->receiveSamples(sampleBuffer, buffSizeSamples);
        LOGV("process_frame receiveSamples %d", nSamples);
        receiveSamples(env, object, sampleBuffer, nSamples * nChannels, bytesPerSample);
    } while (nSamples != 0);
}

extern "C" DLL_PUBLIC jstring
Java_net_surina_soundtouch_SoundTouch_getVersionString(JNIEnv *env, jobject thiz) {
    const char *verStr;

    LOGV("JNI call SoundTouch.getVersionString");

    // Call example SoundTouch routine
    verStr = SoundTouch::getVersionString();

    /// gomp_tls storage bug workaround - see comments in _init_threading() function!
    _init_threading(false);

    int threads = 0;
#pragma omp parallel
    {
#pragma omp atomic
        threads++;
    }
    LOGV("JNI thread count %d", threads);

    // return version as string
    return env->NewStringUTF(verStr);
}



extern "C" DLL_PUBLIC jlong
Java_net_surina_soundtouch_SoundTouch_newInstance(JNIEnv *env, jobject thiz) {
    return (jlong) (new SoundTouch());
}


extern "C" DLL_PUBLIC void
Java_net_surina_soundtouch_SoundTouch_deleteInstance(JNIEnv *env, jobject thiz, jlong handle) {
    SoundTouch *ptr = (SoundTouch *) handle;
    delete ptr;
}


extern "C" DLL_PUBLIC void
Java_net_surina_soundtouch_SoundTouch_setTempo(JNIEnv *env, jobject thiz, jlong handle,
                                               jfloat tempo) {
    SoundTouch *ptr = (SoundTouch *) handle;
    ptr->setTempo(tempo);
}


extern "C" DLL_PUBLIC void
Java_net_surina_soundtouch_SoundTouch_setPitchSemiTones(JNIEnv *env, jobject thiz, jlong handle,
                                                        jfloat pitch) {
    SoundTouch *ptr = (SoundTouch *) handle;
    ptr->setPitchSemiTones(pitch);
}


extern "C" DLL_PUBLIC void
Java_net_surina_soundtouch_SoundTouch_setSpeed(JNIEnv *env, jobject thiz, jlong handle,
                                               jfloat speed) {
    SoundTouch *ptr = (SoundTouch *) handle;
    ptr->setRate(speed);
}


extern "C" DLL_PUBLIC jstring
Java_net_surina_soundtouch_SoundTouch_getErrorString(JNIEnv *env, jobject thiz) {
    jstring result = env->NewStringUTF(_errMsg.c_str());
    _errMsg.clear();

    return result;
}


extern "C" DLL_PUBLIC int
Java_net_surina_soundtouch_SoundTouch_processFile(JNIEnv *env, jobject thiz, jlong handle,
                                                  jstring jinputFile, jstring joutputFile) {
    SoundTouch *ptr = (SoundTouch *) handle;

    const char *inputFile = env->GetStringUTFChars(jinputFile, 0);
    const char *outputFile = env->GetStringUTFChars(joutputFile, 0);

    LOGV("JNI process file %s", inputFile);

    /// gomp_tls storage bug workaround - see comments in _init_threading() function!
    if (_init_threading(true)) return -1;

    try {
        _processFile(ptr, inputFile, outputFile);
    }
    catch (const runtime_error &e) {
        const char *err = e.what();
        // An exception occurred during processing, return the error message
        LOGV("JNI exception in SoundTouch::processFile: %s", err);
        _setErrmsg(err);
        return -1;
    }


    env->ReleaseStringUTFChars(jinputFile, inputFile);
    env->ReleaseStringUTFChars(joutputFile, outputFile);

    return 0;
}


extern "C" DLL_PUBLIC int
Java_net_surina_soundtouch_SoundTouch_processFrame(JNIEnv *env, jobject object, jlong handle,
                                                   jbyteArray sampleBuffer,
                                                   jint sampleRate,
                                                   jint nChannels) {
    SAMPLETYPE buffer[BUFF_SIZE];
    int bytesPerSample = 2;
    SoundTouch *ptr = (SoundTouch *) handle;
    /// gomp_tls storage bug workaround - see comments in _init_threading() function!
    if (_init_threading(true)) return -1;
    if (sampleBuffer == NULL) {
        ptr->flush();
        int nSamples;
        int buffSizeSamples = BUFF_SIZE / nChannels;
        do {
            nSamples = ptr->receiveSamples(buffer, buffSizeSamples);
            LOGV("process_frame receiveSamples %d", nSamples);
            receiveSamples(env, object, buffer, nSamples * nChannels, bytesPerSample);
        } while (nSamples != 0);
        return 0;
    }
    jbyte *byteBuffer = env->GetByteArrayElements(sampleBuffer, 0);
    int numBytes = env->GetArrayLength(sampleBuffer);

    // read raw data into temporary buffer
    char *temp = (char *) byteBuffer;
    int numElems = numBytes / bytesPerSample;

    // swap byte ordert & convert to float, depending on sample format
    switch (bytesPerSample) {
        case 1: {
            unsigned char *temp2 = (unsigned char *) temp;
            double conv = 1.0 / 128.0;
            for (int i = 0; i < numElems; i++) {
                buffer[i] = (float) (temp2[i] * conv - 1.0);
            }
            break;
        }

        case 2: {
            short *temp2 = (short *) temp;
            double conv = 1.0 / 32768.0;
            for (int i = 0; i < numElems; i++) {
                short value = temp2[i];
                buffer[i] = (float) (_swap16(value) * conv);
            }
            break;
        }

        case 3: {
            char *temp2 = temp;
            double conv = 1.0 / 8388608.0;
            for (int i = 0; i < numElems; i++) {
                int value = *((int *) temp2);
                value = _swap32(value) & 0x00ffffff;             // take 24 bits
                value |= (value & 0x00800000) ? 0xff000000 : 0;  // extend minus sign bits
                buffer[i] = (float) (value * conv);
                temp2 += 3;
            }
            break;
        }

        case 4: {
            int *temp2 = (int *) temp;
            double conv = 1.0 / 2147483648.0;
            assert(sizeof(int) == 4);
            for (int i = 0; i < numElems; i++) {
                int value = temp2[i];
                buffer[i] = (float) (_swap32(value) * conv);
            }
            break;
        }
    }
    process_frame(env, ptr, object, buffer, numElems, sampleRate,
                  nChannels, bytesPerSample);
    return 0;
}



