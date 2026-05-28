#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>

#include <webrtc-audio-processing-1/modules/audio_processing/include/audio_processing.h>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

#define BYTES_10_MS 320
#define SAMPLE_RATE_HZ 16000
#define NUM_CHANNELS 1

// ----------------------------------------------------------------------------
// Stable-ABI note
//
// This file is intended to compile with Py_LIMITED_API=0x03090000. Keep it free
// of direct type-object field access or non-stable C API calls.

static PyObject *g_AudioProcessorType = NULL;
static PyObject *g_ProcessedAudioChunkType = NULL;

// ----------------------------------------------------------------------------
// AudioProcessor

typedef struct {
    PyObject_HEAD
    webrtc::AudioProcessing *apm;
    webrtc::AudioProcessing::Config audio_config;
    webrtc::StreamConfig stream_config;
} AudioProcessor;

static void AudioProcessor_dealloc(AudioProcessor *self) {
    delete self->apm;
    self->apm = NULL;

    // PyTypeObject is opaque with Py_LIMITED_API. These instances are
    // fixed-size objects allocated by the default heap-type allocator.
    PyObject_Free(self);
}

static int AudioProcessor_init(AudioProcessor *self, PyObject *args,
                               PyObject *kwds) {
    int auto_gain = 0;
    int noise_suppression = 0;

    static const char *kwlist[] = {"auto_gain", "noise_suppression", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ii", const_cast<char **>(kwlist),
                                     &auto_gain, &noise_suppression)) {
        return -1;
    }

    // Reset if __init__ is called more than once on the same instance.
    delete self->apm;
    self->apm = NULL;

    self->audio_config = webrtc::AudioProcessing::Config();

    // 16 kHz, mono. The third StreamConfig argument matches the original code.
    self->stream_config = webrtc::StreamConfig(SAMPLE_RATE_HZ, NUM_CHANNELS, false);

    self->apm = webrtc::AudioProcessingBuilder().Create();
    if (self->apm == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                        "Failed to create WebRTC AudioProcessing instance");
        return -1;
    }

    self->audio_config.echo_canceller.enabled = false;

    if (auto_gain > 0) {
        self->audio_config.gain_controller1.enabled = true;
        self->audio_config.gain_controller1.target_level_dbfs = auto_gain;
        self->audio_config.gain_controller2.enabled = true;
    }

    self->audio_config.high_pass_filter.enabled = true;
    self->audio_config.voice_detection.enabled = true;

    if (noise_suppression > 0) {
        self->audio_config.noise_suppression.enabled = true;

        if (noise_suppression == 1) {
            self->audio_config.noise_suppression.level =
                webrtc::AudioProcessing::Config::NoiseSuppression::Level::kLow;
        } else if (noise_suppression == 2) {
            self->audio_config.noise_suppression.level =
                webrtc::AudioProcessing::Config::NoiseSuppression::Level::kModerate;
        } else if (noise_suppression == 3) {
            self->audio_config.noise_suppression.level =
                webrtc::AudioProcessing::Config::NoiseSuppression::Level::kHigh;
        } else {
            self->audio_config.noise_suppression.level =
                webrtc::AudioProcessing::Config::NoiseSuppression::Level::kVeryHigh;
        }
    }

    self->apm->ApplyConfig(self->audio_config);

    return 0;
}

// ----------------------------------------------------------------------------
// ProcessedAudioChunk

typedef struct {
    PyObject_HEAD
    PyObject *audio;
    int is_speech;
} ProcessedAudioChunk;

static void ProcessedAudioChunk_dealloc(ProcessedAudioChunk *self) {
    Py_XDECREF(self->audio);
    self->audio = NULL;

    // See AudioProcessor_dealloc for the Py_LIMITED_API rationale.
    PyObject_Free(self);
}

static int ProcessedAudioChunk_init(ProcessedAudioChunk *self, PyObject *args,
                                    PyObject *kwds) {
    // Public construction is allowed but creates an empty/false result. The
    // extension creates populated instances internally with PyObject_CallNoArgs.
    Py_XDECREF(self->audio);
    self->audio = NULL;
    self->is_speech = 0;
    return 0;
}

static PyObject *ProcessedAudioChunk_get_audio(ProcessedAudioChunk *self,
                                               void *closure) {
    if (self->audio == NULL) {
        Py_RETURN_NONE;
    }

    Py_INCREF(self->audio);
    return self->audio;
}

static PyObject *ProcessedAudioChunk_get_is_speech(ProcessedAudioChunk *self,
                                                   void *closure) {
    if (self->is_speech) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyGetSetDef ProcessedAudioChunk_getset[] = {
    {"audio", reinterpret_cast<getter>(ProcessedAudioChunk_get_audio), NULL,
     const_cast<char *>("Processed audio bytes"), NULL},
    {"is_speech", reinterpret_cast<getter>(ProcessedAudioChunk_get_is_speech), NULL,
     const_cast<char *>("True if speech was detected"), NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

// ----------------------------------------------------------------------------
// AudioProcessor.Process10ms

static PyObject *AudioProcessor_process10ms(AudioProcessor *self,
                                            PyObject *audio_obj) {
    if (self->apm == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "AudioProcessor is not initialized");
        return NULL;
    }

    // PyBytes_AsStringAndSize keeps this function ABI-safe. Passing
    // bytearray/memoryview will raise TypeError; callers should pass bytes.
    char *input_bytes = NULL;
    Py_ssize_t input_len = 0;
    if (PyBytes_AsStringAndSize(audio_obj, &input_bytes, &input_len) < 0) {
        return NULL;
    }

    if (input_len != BYTES_10_MS) {
        PyErr_SetString(PyExc_ValueError,
                        "Input buffer must be 320 bytes: 10 ms of 16-bit mono PCM at 16 kHz");
        return NULL;
    }

    PyObject *output_obj = PyBytes_FromStringAndSize(NULL, BYTES_10_MS);
    if (output_obj == NULL) {
        return NULL;
    }

    char *output_bytes = PyBytes_AsString(output_obj);
    if (output_bytes == NULL) {
        Py_DECREF(output_obj);
        return NULL;
    }

    self->apm->ProcessStream(
        reinterpret_cast<const int16_t *>(input_bytes),
        self->stream_config,
        self->stream_config,
        reinterpret_cast<int16_t *>(output_bytes));

    int is_speech = 0;
    auto stats = self->apm->GetStatistics();
    if (stats.voice_detected.has_value()) {
        is_speech = stats.voice_detected.value() ? 1 : 0;
    }

    if (g_ProcessedAudioChunkType == NULL) {
        Py_DECREF(output_obj);
        PyErr_SetString(PyExc_RuntimeError, "ProcessedAudioChunk type is not initialized");
        return NULL;
    }

    PyObject *chunk_obj = PyObject_CallNoArgs(g_ProcessedAudioChunkType);
    if (chunk_obj == NULL) {
        Py_DECREF(output_obj);
        return NULL;
    }

    ProcessedAudioChunk *chunk = reinterpret_cast<ProcessedAudioChunk *>(chunk_obj);
    chunk->audio = output_obj;  // Steals our owned reference.
    chunk->is_speech = is_speech;

    return chunk_obj;
}

static PyMethodDef AudioProcessor_methods[] = {
    {"Process10ms", reinterpret_cast<PyCFunction>(AudioProcessor_process10ms),
     METH_O, const_cast<char *>("Process 10 ms of 16-bit mono PCM audio")},
    {NULL, NULL, 0, NULL},
};

// ----------------------------------------------------------------------------
// Type specs

static PyType_Slot ProcessedAudioChunk_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void *>(ProcessedAudioChunk_dealloc)},
    {Py_tp_init, reinterpret_cast<void *>(ProcessedAudioChunk_init)},
    {Py_tp_getset, reinterpret_cast<void *>(ProcessedAudioChunk_getset)},
    {Py_tp_new, reinterpret_cast<void *>(PyType_GenericNew)},
    {Py_tp_doc, const_cast<char *>(
        "ProcessedAudioChunk\n\n"
        "Result of processing audio. Properties: audio, is_speech.")},
    {0, NULL},
};

static PyType_Spec ProcessedAudioChunk_spec = {
    "webrtc_noise_gain_cpp.ProcessedAudioChunk",
    sizeof(ProcessedAudioChunk),
    0,
    Py_TPFLAGS_DEFAULT,
    ProcessedAudioChunk_slots,
};

static PyType_Slot AudioProcessor_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void *>(AudioProcessor_dealloc)},
    {Py_tp_init, reinterpret_cast<void *>(AudioProcessor_init)},
    {Py_tp_methods, reinterpret_cast<void *>(AudioProcessor_methods)},
    {Py_tp_new, reinterpret_cast<void *>(PyType_GenericNew)},
    {Py_tp_doc, const_cast<char *>(
        "AudioProcessor(auto_gain=0, noise_suppression=0)\n\n"
        "Process 10 ms chunks of 16-bit mono PCM audio with WebRTC noise "
        "suppression, high-pass filtering, voice detection, and optional gain.")},
    {0, NULL},
};

static PyType_Spec AudioProcessor_spec = {
    "webrtc_noise_gain_cpp.AudioProcessor",
    sizeof(AudioProcessor),
    0,
    Py_TPFLAGS_DEFAULT,
    AudioProcessor_slots,
};

// ----------------------------------------------------------------------------
// Module lifecycle

static void webrtc_noise_gain_free(void *module) {
    Py_CLEAR(g_AudioProcessorType);
    Py_CLEAR(g_ProcessedAudioChunkType);
}

static PyModuleDef webrtc_noise_gain_module = {
    PyModuleDef_HEAD_INIT,
    "webrtc_noise_gain_cpp",
    "WebRTC noise suppression and auto gain module",
    -1,
    NULL,
    NULL,
    NULL,
    NULL,
    webrtc_noise_gain_free,
};

PyMODINIT_FUNC PyInit_webrtc_noise_gain_cpp(void) {
    PyObject *module = PyModule_Create(&webrtc_noise_gain_module);
    if (module == NULL) {
        return NULL;
    }

    PyObject *processed_type = PyType_FromSpec(&ProcessedAudioChunk_spec);
    if (processed_type == NULL) {
        Py_DECREF(module);
        return NULL;
    }

    g_ProcessedAudioChunkType = processed_type;

    Py_INCREF(processed_type);
    if (PyModule_AddObject(module, "ProcessedAudioChunk", processed_type) < 0) {
        Py_DECREF(processed_type);
        Py_DECREF(module);
        return NULL;
    }

    PyObject *processor_type = PyType_FromSpec(&AudioProcessor_spec);
    if (processor_type == NULL) {
        Py_DECREF(module);
        return NULL;
    }

    g_AudioProcessorType = processor_type;

    Py_INCREF(processor_type);
    if (PyModule_AddObject(module, "AudioProcessor", processor_type) < 0) {
        Py_DECREF(processor_type);
        Py_DECREF(module);
        return NULL;
    }

#ifdef VERSION_INFO
    if (PyModule_AddStringConstant(module, "__version__",
                                   MACRO_STRINGIFY(VERSION_INFO)) < 0) {
        Py_DECREF(module);
        return NULL;
    }
#else
    if (PyModule_AddStringConstant(module, "__version__", "dev") < 0) {
        Py_DECREF(module);
        return NULL;
    }
#endif

    return module;
}
