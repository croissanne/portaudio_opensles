Portaudio implementation for android using opensles.

Building
----
To build portaudio with opensles an android NDK is needed to crosscompile it, version r9b has been used during development.

TODOs
----
Input:
Only playback is supported at this point.

Misc
----
Audio fast track:
Opensles on android has an audio_output_fast_flag which will be automatically set when you open a device with the right parameters. When this flag is set latency will be reduced dramatically between opensles and the android HAL. The samplerate needed for the fast track flag can be queried using AudioManager's getProperty(PROPERTY_OUTPUT_SAMPLE_RATE).

Latency:
Default latencies and precise stream latencies are hard to calculate since these will depend on device, android version, and whether or not the audio fast track flag has been set. At the moment this implementation only takes into account the user buffers and the opensles buffers. 

Buffer sizes:
When using portaudio opensles in an android application it is recommended to query the AudioManager's getProperty(PROPERTY_OUTPUT_FRAMES_PER_BUFFER). Using this buffer size or a multiple of this buffer size is recommended. The implementation does not have any lower bound on the framesPerBuffer argument in Pa_Openstream. This way newer devices can take advantage of improving audio latency on android.
